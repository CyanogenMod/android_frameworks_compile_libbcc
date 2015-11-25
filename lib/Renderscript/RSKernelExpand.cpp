/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bcc/Assert.h"
#include "bcc/Renderscript/RSTransforms.h"

#include <cstdlib>
#include <functional>
#include <unordered_set>

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include "bcc/Config/Config.h"
#include "bcc/Support/Log.h"

#include "bcinfo/MetadataExtractor.h"

#ifndef __DISABLE_ASSERTS
// Only used in bccAssert()
const int kNumExpandedForeachParams = 4;
const int kNumExpandedReduceParams = 3;
const int kNumExpandedReduceNewAccumulatorParams = 4;
#endif

const char kRenderScriptTBAARootName[] = "RenderScript Distinct TBAA";
const char kRenderScriptTBAANodeName[] = "RenderScript TBAA";

using namespace bcc;

namespace {

static const bool gEnableRsTbaa = true;

/* RSKernelExpandPass - This pass operates on functions that are able
 * to be called via rsForEach(), "foreach_<NAME>", or
 * "reduce_<NAME>". We create an inner loop for the function to be
 * invoked over the appropriate data cells of the input/output
 * allocations (adjusting other relevant parameters as we go). We
 * support doing this for any forEach or reduce style compute
 * kernels. The new function name is the original function name
 * followed by ".expand". Note that we still generate code for the
 * original function.
 */
class RSKernelExpandPass : public llvm::ModulePass {
public:
  static char ID;

private:
  static const size_t RS_KERNEL_INPUT_LIMIT = 8; // see frameworks/base/libs/rs/cpu_ref/rsCpuCoreRuntime.h

  typedef std::unordered_set<llvm::Function *> FunctionSet;

  enum RsLaunchDimensionsField {
    RsLaunchDimensionsFieldX,
    RsLaunchDimensionsFieldY,
    RsLaunchDimensionsFieldZ,
    RsLaunchDimensionsFieldLod,
    RsLaunchDimensionsFieldFace,
    RsLaunchDimensionsFieldArray,

    RsLaunchDimensionsFieldCount
  };

  enum RsExpandKernelDriverInfoPfxField {
    RsExpandKernelDriverInfoPfxFieldInPtr,
    RsExpandKernelDriverInfoPfxFieldInStride,
    RsExpandKernelDriverInfoPfxFieldInLen,
    RsExpandKernelDriverInfoPfxFieldOutPtr,
    RsExpandKernelDriverInfoPfxFieldOutStride,
    RsExpandKernelDriverInfoPfxFieldOutLen,
    RsExpandKernelDriverInfoPfxFieldDim,
    RsExpandKernelDriverInfoPfxFieldCurrent,
    RsExpandKernelDriverInfoPfxFieldUsr,
    RsExpandKernelDriverInfoPfxFieldUsLenr,

    RsExpandKernelDriverInfoPfxFieldCount
  };

  llvm::Module *Module;
  llvm::LLVMContext *Context;

  /*
   * Pointers to LLVM type information for the the function signatures
   * for expanded functions. These must be re-calculated for each module
   * the pass is run on.
   */
  llvm::FunctionType *ExpandedForEachType, *ExpandedReduceType;
  llvm::Type *RsExpandKernelDriverInfoPfxTy;

  uint32_t mExportForEachCount;
  const char **mExportForEachNameList;
  const uint32_t *mExportForEachSignatureList;

  uint32_t mExportReduceCount;
  const char **mExportReduceNameList;

  // Turns on optimization of allocation stride values.
  bool mEnableStepOpt;

  uint32_t getRootSignature(llvm::Function *Function) {
    const llvm::NamedMDNode *ExportForEachMetadata =
        Module->getNamedMetadata("#rs_export_foreach");

    if (!ExportForEachMetadata) {
      llvm::SmallVector<llvm::Type*, 8> RootArgTys;
      for (llvm::Function::arg_iterator B = Function->arg_begin(),
                                        E = Function->arg_end();
           B != E;
           ++B) {
        RootArgTys.push_back(B->getType());
      }

      // For pre-ICS bitcode, we may not have signature information. In that
      // case, we use the size of the RootArgTys to select the number of
      // arguments.
      return (1 << RootArgTys.size()) - 1;
    }

    if (ExportForEachMetadata->getNumOperands() == 0) {
      return 0;
    }

    bccAssert(ExportForEachMetadata->getNumOperands() > 0);

    // We only handle the case for legacy root() functions here, so this is
    // hard-coded to look at only the first such function.
    llvm::MDNode *SigNode = ExportForEachMetadata->getOperand(0);
    if (SigNode != nullptr && SigNode->getNumOperands() == 1) {
      llvm::Metadata *SigMD = SigNode->getOperand(0);
      if (llvm::MDString *SigS = llvm::dyn_cast<llvm::MDString>(SigMD)) {
        llvm::StringRef SigString = SigS->getString();
        uint32_t Signature = 0;
        if (SigString.getAsInteger(10, Signature)) {
          ALOGE("Non-integer signature value '%s'", SigString.str().c_str());
          return 0;
        }
        return Signature;
      }
    }

    return 0;
  }

  bool isStepOptSupported(llvm::Type *AllocType) {

    llvm::PointerType *PT = llvm::dyn_cast<llvm::PointerType>(AllocType);
    llvm::Type *VoidPtrTy = llvm::Type::getInt8PtrTy(*Context);

    if (mEnableStepOpt) {
      return false;
    }

    if (AllocType == VoidPtrTy) {
      return false;
    }

    if (!PT) {
      return false;
    }

    // remaining conditions are 64-bit only
    if (VoidPtrTy->getPrimitiveSizeInBits() == 32) {
      return true;
    }

    // coerce suggests an upconverted struct type, which we can't support
    if (AllocType->getStructName().find("coerce") != llvm::StringRef::npos) {
      return false;
    }

    // 2xi64 and i128 suggest an upconverted struct type, which are also unsupported
    llvm::Type *V2xi64Ty = llvm::VectorType::get(llvm::Type::getInt64Ty(*Context), 2);
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(*Context, 128);
    if (AllocType == V2xi64Ty || AllocType == Int128Ty) {
      return false;
    }

    return true;
  }

  // Get the actual value we should use to step through an allocation.
  //
  // Normally the value we use to step through an allocation is given to us by
  // the driver. However, for certain primitive data types, we can derive an
  // integer constant for the step value. We use this integer constant whenever
  // possible to allow further compiler optimizations to take place.
  //
  // DL - Target Data size/layout information.
  // T - Type of allocation (should be a pointer).
  // OrigStep - Original step increment (root.expand() input from driver).
  llvm::Value *getStepValue(llvm::DataLayout *DL, llvm::Type *AllocType,
                            llvm::Value *OrigStep) {
    bccAssert(DL);
    bccAssert(AllocType);
    bccAssert(OrigStep);
    llvm::PointerType *PT = llvm::dyn_cast<llvm::PointerType>(AllocType);
    if (isStepOptSupported(AllocType)) {
      llvm::Type *ET = PT->getElementType();
      uint64_t ETSize = DL->getTypeAllocSize(ET);
      llvm::Type *Int32Ty = llvm::Type::getInt32Ty(*Context);
      return llvm::ConstantInt::get(Int32Ty, ETSize);
    } else {
      return OrigStep;
    }
  }

  /// Builds the types required by the pass for the given context.
  void buildTypes(void) {
    // Create the RsLaunchDimensionsTy and RsExpandKernelDriverInfoPfxTy structs.

    llvm::Type *Int8Ty                   = llvm::Type::getInt8Ty(*Context);
    llvm::Type *Int8PtrTy                = Int8Ty->getPointerTo();
    llvm::Type *Int8PtrArrayInputLimitTy = llvm::ArrayType::get(Int8PtrTy, RS_KERNEL_INPUT_LIMIT);
    llvm::Type *Int32Ty                  = llvm::Type::getInt32Ty(*Context);
    llvm::Type *Int32ArrayInputLimitTy   = llvm::ArrayType::get(Int32Ty, RS_KERNEL_INPUT_LIMIT);
    llvm::Type *VoidPtrTy                = llvm::Type::getInt8PtrTy(*Context);
    llvm::Type *Int32Array4Ty            = llvm::ArrayType::get(Int32Ty, 4);

    /* Defined in frameworks/base/libs/rs/cpu_ref/rsCpuCore.h:
     *
     * struct RsLaunchDimensions {
     *   uint32_t x;
     *   uint32_t y;
     *   uint32_t z;
     *   uint32_t lod;
     *   uint32_t face;
     *   uint32_t array[4];
     * };
     */
    llvm::SmallVector<llvm::Type*, RsLaunchDimensionsFieldCount> RsLaunchDimensionsTypes;
    RsLaunchDimensionsTypes.push_back(Int32Ty);       // uint32_t x
    RsLaunchDimensionsTypes.push_back(Int32Ty);       // uint32_t y
    RsLaunchDimensionsTypes.push_back(Int32Ty);       // uint32_t z
    RsLaunchDimensionsTypes.push_back(Int32Ty);       // uint32_t lod
    RsLaunchDimensionsTypes.push_back(Int32Ty);       // uint32_t face
    RsLaunchDimensionsTypes.push_back(Int32Array4Ty); // uint32_t array[4]
    llvm::StructType *RsLaunchDimensionsTy =
        llvm::StructType::create(RsLaunchDimensionsTypes, "RsLaunchDimensions");

    /* Defined as the beginning of RsExpandKernelDriverInfo in frameworks/base/libs/rs/cpu_ref/rsCpuCoreRuntime.h:
     *
     * struct RsExpandKernelDriverInfoPfx {
     *     const uint8_t *inPtr[RS_KERNEL_INPUT_LIMIT];
     *     uint32_t inStride[RS_KERNEL_INPUT_LIMIT];
     *     uint32_t inLen;
     *
     *     uint8_t *outPtr[RS_KERNEL_INPUT_LIMIT];
     *     uint32_t outStride[RS_KERNEL_INPUT_LIMIT];
     *     uint32_t outLen;
     *
     *     // Dimension of the launch
     *     RsLaunchDimensions dim;
     *
     *     // The walking iterator of the launch
     *     RsLaunchDimensions current;
     *
     *     const void *usr;
     *     uint32_t usrLen;
     *
     *     // Items below this line are not used by the compiler and can be change in the driver.
     *     // So the compiler must assume there are an unknown number of fields of unknown type
     *     // beginning here.
     * };
     *
     * The name "RsExpandKernelDriverInfoPfx" is known to RSInvariantPass (RSInvariant.cpp).
     */
    llvm::SmallVector<llvm::Type*, RsExpandKernelDriverInfoPfxFieldCount> RsExpandKernelDriverInfoPfxTypes;
    RsExpandKernelDriverInfoPfxTypes.push_back(Int8PtrArrayInputLimitTy); // const uint8_t *inPtr[RS_KERNEL_INPUT_LIMIT]
    RsExpandKernelDriverInfoPfxTypes.push_back(Int32ArrayInputLimitTy);   // uint32_t inStride[RS_KERNEL_INPUT_LIMIT]
    RsExpandKernelDriverInfoPfxTypes.push_back(Int32Ty);                  // uint32_t inLen
    RsExpandKernelDriverInfoPfxTypes.push_back(Int8PtrArrayInputLimitTy); // uint8_t *outPtr[RS_KERNEL_INPUT_LIMIT]
    RsExpandKernelDriverInfoPfxTypes.push_back(Int32ArrayInputLimitTy);   // uint32_t outStride[RS_KERNEL_INPUT_LIMIT]
    RsExpandKernelDriverInfoPfxTypes.push_back(Int32Ty);                  // uint32_t outLen
    RsExpandKernelDriverInfoPfxTypes.push_back(RsLaunchDimensionsTy);     // RsLaunchDimensions dim
    RsExpandKernelDriverInfoPfxTypes.push_back(RsLaunchDimensionsTy);     // RsLaunchDimensions current
    RsExpandKernelDriverInfoPfxTypes.push_back(VoidPtrTy);                // const void *usr
    RsExpandKernelDriverInfoPfxTypes.push_back(Int32Ty);                  // uint32_t usrLen
    RsExpandKernelDriverInfoPfxTy =
        llvm::StructType::create(RsExpandKernelDriverInfoPfxTypes, "RsExpandKernelDriverInfoPfx");

    // Create the function type for expanded kernels.
    llvm::Type *VoidTy = llvm::Type::getVoidTy(*Context);

    llvm::Type *RsExpandKernelDriverInfoPfxPtrTy = RsExpandKernelDriverInfoPfxTy->getPointerTo();
    // void (const RsExpandKernelDriverInfoPfxTy *p, uint32_t x1, uint32_t x2, uint32_t outstep)
    ExpandedForEachType = llvm::FunctionType::get(VoidTy,
        {RsExpandKernelDriverInfoPfxPtrTy, Int32Ty, Int32Ty, Int32Ty}, false);

    // void (void *inBuf, void *outBuf, uint32_t len)
    ExpandedReduceType = llvm::FunctionType::get(VoidTy, {VoidPtrTy, VoidPtrTy, Int32Ty}, false);
  }

  /// @brief Create skeleton of the expanded foreach kernel.
  ///
  /// This creates a function with the following signature:
  ///
  ///   void (const RsForEachStubParamStruct *p, uint32_t x1, uint32_t x2,
  ///         uint32_t outstep)
  ///
  llvm::Function *createEmptyExpandedForEachKernel(llvm::StringRef OldName) {
    llvm::Function *ExpandedFunction =
      llvm::Function::Create(ExpandedForEachType,
                             llvm::GlobalValue::ExternalLinkage,
                             OldName + ".expand", Module);
    bccAssert(ExpandedFunction->arg_size() == kNumExpandedForeachParams);
    llvm::Function::arg_iterator AI = ExpandedFunction->arg_begin();
    (AI++)->setName("p");
    (AI++)->setName("x1");
    (AI++)->setName("x2");
    (AI++)->setName("arg_outstep");
    llvm::BasicBlock *Begin = llvm::BasicBlock::Create(*Context, "Begin",
                                                       ExpandedFunction);
    llvm::IRBuilder<> Builder(Begin);
    Builder.CreateRetVoid();
    return ExpandedFunction;
  }

  // Create skeleton of the expanded reduce kernel.
  //
  // This creates a function with the following signature:
  //
  //   void @func.expand(i8* nocapture %inBuf, i8* nocapture %outBuf, i32 len)
  //
  llvm::Function *createEmptyExpandedReduceKernel(llvm::StringRef OldName) {
    llvm::Function *ExpandedFunction =
      llvm::Function::Create(ExpandedReduceType,
                             llvm::GlobalValue::ExternalLinkage,
                             OldName + ".expand", Module);
    bccAssert(ExpandedFunction->arg_size() == kNumExpandedReduceParams);

    llvm::Function::arg_iterator AI = ExpandedFunction->arg_begin();

    using llvm::Attribute;

    llvm::Argument *InBuf = &(*AI++);
    InBuf->setName("inBuf");
    InBuf->addAttr(llvm::AttributeSet::get(*Context, InBuf->getArgNo() + 1, llvm::makeArrayRef(Attribute::NoCapture)));

    llvm::Argument *OutBuf = &(*AI++);
    OutBuf->setName("outBuf");
    OutBuf->addAttr(llvm::AttributeSet::get(*Context, OutBuf->getArgNo() + 1, llvm::makeArrayRef(Attribute::NoCapture)));

    (AI++)->setName("len");

    llvm::BasicBlock *Begin = llvm::BasicBlock::Create(*Context, "Begin",
                                                       ExpandedFunction);
    llvm::IRBuilder<> Builder(Begin);
    Builder.CreateRetVoid();

    return ExpandedFunction;
  }

  // Create skeleton of a general reduce kernel's expanded accumulator.
  //
  // This creates a function with the following signature:
  //
  //  void @func.expand(%RsExpandKernelDriverInfoPfx* nocapture %p,
  //                    i32 %x1, i32 %x2, accumType* nocapture %accum)
  //
  llvm::Function *createEmptyExpandedReduceNewAccumulator(llvm::StringRef OldName,
                                                          llvm::Type *AccumArgTy) {
    llvm::Type *Int32Ty = llvm::Type::getInt32Ty(*Context);
    llvm::Type *VoidTy = llvm::Type::getVoidTy(*Context);
    llvm::FunctionType *ExpandedReduceNewAccumulatorType =
        llvm::FunctionType::get(VoidTy,
                                {RsExpandKernelDriverInfoPfxTy->getPointerTo(),
                                 Int32Ty, Int32Ty, AccumArgTy}, false);
    llvm::Function *FnExpandedAccumulator =
      llvm::Function::Create(ExpandedReduceNewAccumulatorType,
                             llvm::GlobalValue::ExternalLinkage,
                             OldName + ".expand", Module);
    bccAssert(FnExpandedAccumulator->arg_size() == kNumExpandedReduceNewAccumulatorParams);

    llvm::Function::arg_iterator AI = FnExpandedAccumulator->arg_begin();

    using llvm::Attribute;

    llvm::Argument *Arg_p = &(*AI++);
    Arg_p->setName("p");
    Arg_p->addAttr(llvm::AttributeSet::get(*Context, Arg_p->getArgNo() + 1,
                                           llvm::makeArrayRef(Attribute::NoCapture)));

    llvm::Argument *Arg_x1 = &(*AI++);
    Arg_x1->setName("x1");

    llvm::Argument *Arg_x2 = &(*AI++);
    Arg_x2->setName("x2");

    llvm::Argument *Arg_accum = &(*AI++);
    Arg_accum->setName("accum");
    Arg_accum->addAttr(llvm::AttributeSet::get(*Context, Arg_accum->getArgNo() + 1,
                                               llvm::makeArrayRef(Attribute::NoCapture)));

    llvm::BasicBlock *Begin = llvm::BasicBlock::Create(*Context, "Begin",
                                                       FnExpandedAccumulator);
    llvm::IRBuilder<> Builder(Begin);
    Builder.CreateRetVoid();

    return FnExpandedAccumulator;
  }

  /// @brief Create an empty loop
  ///
  /// Create a loop of the form:
  ///
  /// for (i = LowerBound; i < UpperBound; i++)
  ///   ;
  ///
  /// After the loop has been created, the builder is set such that
  /// instructions can be added to the loop body.
  ///
  /// @param Builder The builder to use to build this loop. The current
  ///                position of the builder is the position the loop
  ///                will be inserted.
  /// @param LowerBound The first value of the loop iterator
  /// @param UpperBound The maximal value of the loop iterator
  /// @param LoopIV A reference that will be set to the loop iterator.
  /// @return The BasicBlock that will be executed after the loop.
  llvm::BasicBlock *createLoop(llvm::IRBuilder<> &Builder,
                               llvm::Value *LowerBound,
                               llvm::Value *UpperBound,
                               llvm::Value **LoopIV) {
    bccAssert(LowerBound->getType() == UpperBound->getType());

    llvm::BasicBlock *CondBB, *AfterBB, *HeaderBB;
    llvm::Value *Cond, *IVNext, *IV, *IVVar;

    CondBB = Builder.GetInsertBlock();
    AfterBB = llvm::SplitBlock(CondBB, Builder.GetInsertPoint(), nullptr, nullptr);
    HeaderBB = llvm::BasicBlock::Create(*Context, "Loop", CondBB->getParent());

    CondBB->getTerminator()->eraseFromParent();
    Builder.SetInsertPoint(CondBB);

    // decltype(LowerBound) *ivvar = alloca(sizeof(int))
    // *ivvar = LowerBound
    IVVar = Builder.CreateAlloca(LowerBound->getType(), nullptr, BCC_INDEX_VAR_NAME);
    Builder.CreateStore(LowerBound, IVVar);

    // if (LowerBound < Upperbound)
    //   goto LoopHeader
    // else
    //   goto AfterBB
    Cond = Builder.CreateICmpULT(LowerBound, UpperBound);
    Builder.CreateCondBr(Cond, HeaderBB, AfterBB);

    // LoopHeader:
    //   iv = *ivvar
    //   <insertion point here>
    //   iv.next = iv + 1
    //   *ivvar = iv.next
    //   if (iv.next < Upperbound)
    //     goto LoopHeader
    //   else
    //     goto AfterBB
    // AfterBB:
    Builder.SetInsertPoint(HeaderBB);
    IV = Builder.CreateLoad(IVVar, "X");
    IVNext = Builder.CreateNUWAdd(IV, Builder.getInt32(1));
    Builder.CreateStore(IVNext, IVVar);
    Cond = Builder.CreateICmpULT(IVNext, UpperBound);
    Builder.CreateCondBr(Cond, HeaderBB, AfterBB);
    AfterBB->setName("Exit");
    Builder.SetInsertPoint(llvm::cast<llvm::Instruction>(IVNext));

    // Record information about this loop.
    *LoopIV = IV;
    return AfterBB;
  }

  // Finish building the outgoing argument list for calling a ForEach-able function.
  //
  // ArgVector - on input, the non-special arguments
  //             on output, the non-special arguments combined with the special arguments
  //               from SpecialArgVector
  // SpecialArgVector - special arguments (from ExpandSpecialArguments())
  // SpecialArgContextIdx - return value of ExpandSpecialArguments()
  //                          (position of context argument in SpecialArgVector)
  // CalleeFunction - the ForEach-able function being called
  // Builder - for inserting code into the caller function
  template<unsigned int ArgVectorLen, unsigned int SpecialArgVectorLen>
  void finishArgList(      llvm::SmallVector<llvm::Value *, ArgVectorLen>        &ArgVector,
                     const llvm::SmallVector<llvm::Value *, SpecialArgVectorLen> &SpecialArgVector,
                     const int SpecialArgContextIdx,
                     const llvm::Function &CalleeFunction,
                     llvm::IRBuilder<> &CallerBuilder) {
    /* The context argument (if any) is a pointer to an opaque user-visible type that differs from
     * the RsExpandKernelDriverInfoPfx type used in the function we are generating (although the
     * two types represent the same thing).  Therefore, we must introduce a pointer cast when
     * generating a call to the kernel function.
     */
    const int ArgContextIdx =
        SpecialArgContextIdx >= 0 ? (ArgVector.size() + SpecialArgContextIdx) : SpecialArgContextIdx;
    ArgVector.append(SpecialArgVector.begin(), SpecialArgVector.end());
    if (ArgContextIdx >= 0) {
      llvm::Type *ContextArgType = nullptr;
      int ArgIdx = ArgContextIdx;
      for (const auto &Arg : CalleeFunction.getArgumentList()) {
        if (!ArgIdx--) {
          ContextArgType = Arg.getType();
          break;
        }
      }
      bccAssert(ContextArgType);
      ArgVector[ArgContextIdx] = CallerBuilder.CreatePointerCast(ArgVector[ArgContextIdx], ContextArgType);
    }
  }

  // GEPHelper() returns a SmallVector of values suitable for passing
  // to IRBuilder::CreateGEP(), and SmallGEPIndices is a typedef for
  // the returned data type. It is sized so that the SmallVector
  // returned by GEPHelper() never needs to do a heap allocation for
  // any list of GEP indices it encounters in the code.
  typedef llvm::SmallVector<llvm::Value *, 3> SmallGEPIndices;

  // Helper for turning a list of constant integer GEP indices into a
  // SmallVector of llvm::Value*. The return value is suitable for
  // passing to a GetElementPtrInst constructor or IRBuilder::CreateGEP().
  //
  // Inputs:
  //   I32Args should be integers which represent the index arguments
  //   to a GEP instruction.
  //
  // Returns:
  //   Returns a SmallVector of ConstantInts.
  SmallGEPIndices GEPHelper(const std::initializer_list<int32_t> I32Args) {
    SmallGEPIndices Out(I32Args.size());
    llvm::IntegerType *I32Ty = llvm::Type::getInt32Ty(*Context);
    std::transform(I32Args.begin(), I32Args.end(), Out.begin(),
                   [I32Ty](int32_t Arg) { return llvm::ConstantInt::get(I32Ty, Arg); });
    return Out;
  }

public:
  RSKernelExpandPass(bool pEnableStepOpt = true)
      : ModulePass(ID), Module(nullptr), Context(nullptr),
        mEnableStepOpt(pEnableStepOpt) {

  }

  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    // This pass does not use any other analysis passes, but it does
    // add/wrap the existing functions in the module (thus altering the CFG).
  }

  // Build contribution to outgoing argument list for calling a
  // ForEach-able function or a general reduction accumulator
  // function, based on the special parameters of that function.
  //
  // Signature - metadata bits for the signature of the callee
  // X, Arg_p - values derived directly from expanded function,
  //            suitable for computing arguments for the callee
  // CalleeArgs - contribution is accumulated here
  // Bump - invoked once for each contributed outgoing argument
  // LoopHeaderInsertionPoint - an Instruction in the loop header, before which
  //                            this function can insert loop-invariant loads
  //
  // Return value is the (zero-based) position of the context (Arg_p)
  // argument in the CalleeArgs vector, or a negative value if the
  // context argument is not placed in the CalleeArgs vector.
  int ExpandSpecialArguments(uint32_t Signature,
                             llvm::Value *X,
                             llvm::Value *Arg_p,
                             llvm::IRBuilder<> &Builder,
                             llvm::SmallVector<llvm::Value*, 8> &CalleeArgs,
                             std::function<void ()> Bump,
                             llvm::Instruction *LoopHeaderInsertionPoint) {

    bccAssert(CalleeArgs.empty());

    int Return = -1;
    if (bcinfo::MetadataExtractor::hasForEachSignatureCtxt(Signature)) {
      CalleeArgs.push_back(Arg_p);
      Bump();
      Return = CalleeArgs.size() - 1;
    }

    if (bcinfo::MetadataExtractor::hasForEachSignatureX(Signature)) {
      CalleeArgs.push_back(X);
      Bump();
    }

    if (bcinfo::MetadataExtractor::hasForEachSignatureY(Signature) ||
        bcinfo::MetadataExtractor::hasForEachSignatureZ(Signature)) {
      bccAssert(LoopHeaderInsertionPoint);

      // Y and Z are loop invariant, so they can be hoisted out of the
      // loop. Set the IRBuilder insertion point to the loop header.
      auto OldInsertionPoint = Builder.saveIP();
      Builder.SetInsertPoint(LoopHeaderInsertionPoint);

      if (bcinfo::MetadataExtractor::hasForEachSignatureY(Signature)) {
        SmallGEPIndices YValueGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldCurrent,
          RsLaunchDimensionsFieldY}));
        llvm::Value *YAddr = Builder.CreateInBoundsGEP(Arg_p, YValueGEP, "Y.gep");
        CalleeArgs.push_back(Builder.CreateLoad(YAddr, "Y"));
        Bump();
      }

      if (bcinfo::MetadataExtractor::hasForEachSignatureZ(Signature)) {
        SmallGEPIndices ZValueGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldCurrent,
          RsLaunchDimensionsFieldZ}));
        llvm::Value *ZAddr = Builder.CreateInBoundsGEP(Arg_p, ZValueGEP, "Z.gep");
        CalleeArgs.push_back(Builder.CreateLoad(ZAddr, "Z"));
        Bump();
      }

      Builder.restoreIP(OldInsertionPoint);
    }

    return Return;
  }

  // Generate loop-invariant input processing setup code for an expanded
  // ForEach-able function or an expanded general reduction accumulator
  // function.
  //
  // LoopHeader - block at the end of which the setup code will be inserted
  // Arg_p - RSKernelDriverInfo pointer passed to the expanded function
  // TBAAPointer - metadata for marking loads of pointer values out of RSKernelDriverInfo
  // ArgIter - iterator pointing to first input of the UNexpanded function
  // NumInputs - number of inputs (NOT number of ARGUMENTS)
  //
  // InBufPtrs[] - this function sets each array element to point to the first
  //               cell of the corresponding input allocation
  // InStructTempSlots[] - this function sets each array element either to nullptr
  //                       or to the result of an alloca (for the case where the
  //                       calling convention dictates that a value must be passed
  //                       by reference, and so we need a stacked temporary to hold
  //                       a copy of that value)
  void ExpandInputsLoopInvariant(llvm::IRBuilder<> &Builder, llvm::BasicBlock *LoopHeader,
                                 llvm::Value *Arg_p,
                                 llvm::MDNode *TBAAPointer,
                                 llvm::Function::arg_iterator ArgIter,
                                 const size_t NumInputs,
                                 llvm::SmallVectorImpl<llvm::Value *> &InBufPtrs,
                                 llvm::SmallVectorImpl<llvm::Value *> &InStructTempSlots) {
    bccAssert(NumInputs <= RS_KERNEL_INPUT_LIMIT);

    // Extract information about input slots. The work done
    // here is loop-invariant, so we can hoist the operations out of the loop.
    auto OldInsertionPoint = Builder.saveIP();
    Builder.SetInsertPoint(LoopHeader->getTerminator());

    for (size_t InputIndex = 0; InputIndex < NumInputs; ++InputIndex, ArgIter++) {
      llvm::Type *InType = ArgIter->getType();

      /*
       * AArch64 calling conventions dictate that structs of sufficient size
       * get passed by pointer instead of passed by value.  This, combined
       * with the fact that we don't allow kernels to operate on pointer
       * data means that if we see a kernel with a pointer parameter we know
       * that it is a struct input that has been promoted.  As such we don't
       * need to convert its type to a pointer.  Later we will need to know
       * to create a temporary copy on the stack, so we save this information
       * in InStructTempSlots.
       */
      if (auto PtrType = llvm::dyn_cast<llvm::PointerType>(InType)) {
        llvm::Type *ElementType = PtrType->getElementType();
        InStructTempSlots.push_back(Builder.CreateAlloca(ElementType, nullptr,
                                                         "input_struct_slot"));
      } else {
        InType = InType->getPointerTo();
        InStructTempSlots.push_back(nullptr);
      }

      SmallGEPIndices InBufPtrGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldInPtr,
                                             static_cast<int32_t>(InputIndex)}));
      llvm::Value    *InBufPtrAddr = Builder.CreateInBoundsGEP(Arg_p, InBufPtrGEP, "input_buf.gep");
      llvm::LoadInst *InBufPtr = Builder.CreateLoad(InBufPtrAddr, "input_buf");
      llvm::Value    *CastInBufPtr = Builder.CreatePointerCast(InBufPtr, InType, "casted_in");

      if (gEnableRsTbaa) {
        InBufPtr->setMetadata("tbaa", TBAAPointer);
      }

      InBufPtrs.push_back(CastInBufPtr);
    }

    Builder.restoreIP(OldInsertionPoint);
  }

  // Generate loop-varying input processing code for an expanded ForEach-able function
  // or an expanded general reduction accumulator function.  Also, for the call to the
  // UNexpanded function, collect the portion of the argument list corresponding to the
  // inputs.
  //
  // Arg_x1 - first X coordinate to be processed by the expanded function
  // TBAAAllocation - metadata for marking loads of input values out of allocations
  // NumInputs -- number of inputs (NOT number of ARGUMENTS)
  // InBufPtrs[] - this function consumes the information produced by ExpandInputsLoopInvariant()
  // InStructTempSlots[] - this function consumes the information produced by ExpandInputsLoopInvariant()
  // IndVar - value of loop induction variable (X coordinate) for a given loop iteration
  //
  // RootArgs - this function sets this to the list of outgoing argument values corresponding
  //            to the inputs
  void ExpandInputsBody(llvm::IRBuilder<> &Builder,
                        llvm::Value *Arg_x1,
                        llvm::MDNode *TBAAAllocation,
                        const size_t NumInputs,
                        const llvm::SmallVectorImpl<llvm::Value *> &InBufPtrs,
                        const llvm::SmallVectorImpl<llvm::Value *> &InStructTempSlots,
                        llvm::Value *IndVar,
                        llvm::SmallVectorImpl<llvm::Value *> &RootArgs) {
    llvm::Value *Offset = Builder.CreateSub(IndVar, Arg_x1);

    for (size_t Index = 0; Index < NumInputs; ++Index) {
      llvm::Value *InPtr = Builder.CreateInBoundsGEP(InBufPtrs[Index], Offset);
      llvm::Value *Input;

      llvm::LoadInst *InputLoad = Builder.CreateLoad(InPtr, "input");

      if (gEnableRsTbaa) {
        InputLoad->setMetadata("tbaa", TBAAAllocation);
      }

      if (llvm::Value *TemporarySlot = InStructTempSlots[Index]) {
        // Pass a pointer to a temporary on the stack, rather than
        // passing a pointer to the original value. We do not want
        // the kernel to potentially modify the input data.

        // Note: don't annotate with TBAA, since the kernel might
        // have its own TBAA annotations for the pointer argument.
        Builder.CreateStore(InputLoad, TemporarySlot);
        Input = TemporarySlot;
      } else {
        Input = InputLoad;
      }

      RootArgs.push_back(Input);
    }
  }

  /* Performs the actual optimization on a selected function. On success, the
   * Module will contain a new function of the name "<NAME>.expand" that
   * invokes <NAME>() in a loop with the appropriate parameters.
   */
  bool ExpandOldStyleForEach(llvm::Function *Function, uint32_t Signature) {
    ALOGV("Expanding ForEach-able Function %s",
          Function->getName().str().c_str());

    if (!Signature) {
      Signature = getRootSignature(Function);
      if (!Signature) {
        // We couldn't determine how to expand this function based on its
        // function signature.
        return false;
      }
    }

    llvm::DataLayout DL(Module);

    llvm::Function *ExpandedFunction =
      createEmptyExpandedForEachKernel(Function->getName());

    /*
     * Extract the expanded function's parameters.  It is guaranteed by
     * createEmptyExpandedForEachKernel that there will be four parameters.
     */

    bccAssert(ExpandedFunction->arg_size() == kNumExpandedForeachParams);

    llvm::Function::arg_iterator ExpandedFunctionArgIter =
      ExpandedFunction->arg_begin();

    llvm::Value *Arg_p       = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_x1      = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_x2      = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_outstep = &*(ExpandedFunctionArgIter);

    llvm::Value *InStep  = nullptr;
    llvm::Value *OutStep = nullptr;

    // Construct the actual function body.
    llvm::IRBuilder<> Builder(ExpandedFunction->getEntryBlock().begin());

    // Collect and construct the arguments for the kernel().
    // Note that we load any loop-invariant arguments before entering the Loop.
    llvm::Function::arg_iterator FunctionArgIter = Function->arg_begin();

    llvm::Type  *InTy      = nullptr;
    llvm::Value *InBufPtr = nullptr;
    if (bcinfo::MetadataExtractor::hasForEachSignatureIn(Signature)) {
      SmallGEPIndices InStepGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldInStride, 0}));
      llvm::LoadInst *InStepArg  = Builder.CreateLoad(
        Builder.CreateInBoundsGEP(Arg_p, InStepGEP, "instep_addr.gep"), "instep_addr");

      InTy = (FunctionArgIter++)->getType();
      InStep = getStepValue(&DL, InTy, InStepArg);

      InStep->setName("instep");

      SmallGEPIndices InputAddrGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldInPtr, 0}));
      InBufPtr = Builder.CreateLoad(
        Builder.CreateInBoundsGEP(Arg_p, InputAddrGEP, "input_buf.gep"), "input_buf");
    }

    llvm::Type *OutTy = nullptr;
    llvm::Value *OutBasePtr = nullptr;
    if (bcinfo::MetadataExtractor::hasForEachSignatureOut(Signature)) {
      OutTy = (FunctionArgIter++)->getType();
      OutStep = getStepValue(&DL, OutTy, Arg_outstep);
      OutStep->setName("outstep");
      SmallGEPIndices OutBaseGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldOutPtr, 0}));
      OutBasePtr = Builder.CreateLoad(Builder.CreateInBoundsGEP(Arg_p, OutBaseGEP, "out_buf.gep"));
    }

    llvm::Value *UsrData = nullptr;
    if (bcinfo::MetadataExtractor::hasForEachSignatureUsrData(Signature)) {
      llvm::Type *UsrDataTy = (FunctionArgIter++)->getType();
      llvm::Value *UsrDataPointerAddr = Builder.CreateStructGEP(nullptr, Arg_p, RsExpandKernelDriverInfoPfxFieldUsr);
      UsrData = Builder.CreatePointerCast(Builder.CreateLoad(UsrDataPointerAddr), UsrDataTy);
      UsrData->setName("UsrData");
    }

    llvm::BasicBlock *LoopHeader = Builder.GetInsertBlock();
    llvm::Value *IV;
    createLoop(Builder, Arg_x1, Arg_x2, &IV);

    llvm::SmallVector<llvm::Value*, 8> CalleeArgs;
    const int CalleeArgsContextIdx = ExpandSpecialArguments(Signature, IV, Arg_p, Builder, CalleeArgs,
                                                            [&FunctionArgIter]() { FunctionArgIter++; },
                                                            LoopHeader->getTerminator());

    bccAssert(FunctionArgIter == Function->arg_end());

    // Populate the actual call to kernel().
    llvm::SmallVector<llvm::Value*, 8> RootArgs;

    llvm::Value *InPtr  = nullptr;
    llvm::Value *OutPtr = nullptr;

    // Calculate the current input and output pointers
    //
    // We always calculate the input/output pointers with a GEP operating on i8
    // values and only cast at the very end to OutTy. This is because the step
    // between two values is given in bytes.
    //
    // TODO: We could further optimize the output by using a GEP operation of
    // type 'OutTy' in cases where the element type of the allocation allows.
    if (OutBasePtr) {
      llvm::Value *OutOffset = Builder.CreateSub(IV, Arg_x1);
      OutOffset = Builder.CreateMul(OutOffset, OutStep);
      OutPtr = Builder.CreateInBoundsGEP(OutBasePtr, OutOffset);
      OutPtr = Builder.CreatePointerCast(OutPtr, OutTy);
    }

    if (InBufPtr) {
      llvm::Value *InOffset = Builder.CreateSub(IV, Arg_x1);
      InOffset = Builder.CreateMul(InOffset, InStep);
      InPtr = Builder.CreateInBoundsGEP(InBufPtr, InOffset);
      InPtr = Builder.CreatePointerCast(InPtr, InTy);
    }

    if (InPtr) {
      RootArgs.push_back(InPtr);
    }

    if (OutPtr) {
      RootArgs.push_back(OutPtr);
    }

    if (UsrData) {
      RootArgs.push_back(UsrData);
    }

    finishArgList(RootArgs, CalleeArgs, CalleeArgsContextIdx, *Function, Builder);

    Builder.CreateCall(Function, RootArgs);

    return true;
  }

  /* Expand a pass-by-value foreach kernel.
   */
  bool ExpandForEach(llvm::Function *Function, uint32_t Signature) {
    bccAssert(bcinfo::MetadataExtractor::hasForEachSignatureKernel(Signature));
    ALOGV("Expanding kernel Function %s", Function->getName().str().c_str());

    // TODO: Refactor this to share functionality with ExpandOldStyleForEach.
    llvm::DataLayout DL(Module);

    llvm::Function *ExpandedFunction =
      createEmptyExpandedForEachKernel(Function->getName());

    /*
     * Extract the expanded function's parameters.  It is guaranteed by
     * createEmptyExpandedForEachKernel that there will be four parameters.
     */

    bccAssert(ExpandedFunction->arg_size() == kNumExpandedForeachParams);

    llvm::Function::arg_iterator ExpandedFunctionArgIter =
      ExpandedFunction->arg_begin();

    llvm::Value *Arg_p       = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_x1      = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_x2      = &*(ExpandedFunctionArgIter++);
    // Arg_outstep is not used by expanded new-style forEach kernels.

    // Construct the actual function body.
    llvm::IRBuilder<> Builder(ExpandedFunction->getEntryBlock().begin());

    // Create TBAA meta-data.
    llvm::MDNode *TBAARenderScriptDistinct, *TBAARenderScript,
                 *TBAAAllocation, *TBAAPointer;
    llvm::MDBuilder MDHelper(*Context);

    TBAARenderScriptDistinct =
      MDHelper.createTBAARoot(kRenderScriptTBAARootName);
    TBAARenderScript = MDHelper.createTBAANode(kRenderScriptTBAANodeName,
        TBAARenderScriptDistinct);
    TBAAAllocation = MDHelper.createTBAAScalarTypeNode("allocation",
                                                       TBAARenderScript);
    TBAAAllocation = MDHelper.createTBAAStructTagNode(TBAAAllocation,
                                                      TBAAAllocation, 0);
    TBAAPointer = MDHelper.createTBAAScalarTypeNode("pointer",
                                                    TBAARenderScript);
    TBAAPointer = MDHelper.createTBAAStructTagNode(TBAAPointer, TBAAPointer, 0);

    /*
     * Collect and construct the arguments for the kernel().
     *
     * Note that we load any loop-invariant arguments before entering the Loop.
     */
    size_t NumRemainingInputs = Function->arg_size();

    // No usrData parameter on kernels.
    bccAssert(
        !bcinfo::MetadataExtractor::hasForEachSignatureUsrData(Signature));

    llvm::Function::arg_iterator ArgIter = Function->arg_begin();

    // Check the return type
    llvm::Type     *OutTy            = nullptr;
    llvm::LoadInst *OutBasePtr       = nullptr;
    llvm::Value    *CastedOutBasePtr = nullptr;

    bool PassOutByPointer = false;

    if (bcinfo::MetadataExtractor::hasForEachSignatureOut(Signature)) {
      llvm::Type *OutBaseTy = Function->getReturnType();

      if (OutBaseTy->isVoidTy()) {
        PassOutByPointer = true;
        OutTy = ArgIter->getType();

        ArgIter++;
        --NumRemainingInputs;
      } else {
        // We don't increment Args, since we are using the actual return type.
        OutTy = OutBaseTy->getPointerTo();
      }

      SmallGEPIndices OutBaseGEP(GEPHelper({0, RsExpandKernelDriverInfoPfxFieldOutPtr, 0}));
      OutBasePtr = Builder.CreateLoad(Builder.CreateInBoundsGEP(Arg_p, OutBaseGEP, "out_buf.gep"));

      if (gEnableRsTbaa) {
        OutBasePtr->setMetadata("tbaa", TBAAPointer);
      }

      CastedOutBasePtr = Builder.CreatePointerCast(OutBasePtr, OutTy, "casted_out");
    }

    llvm::SmallVector<llvm::Value*, 8> InBufPtrs;
    llvm::SmallVector<llvm::Value*, 8> InStructTempSlots;

    bccAssert(NumRemainingInputs <= RS_KERNEL_INPUT_LIMIT);

    // Create the loop structure.
    llvm::BasicBlock *LoopHeader = Builder.GetInsertBlock();
    llvm::Value *IV;
    createLoop(Builder, Arg_x1, Arg_x2, &IV);

    llvm::SmallVector<llvm::Value*, 8> CalleeArgs;
    const int CalleeArgsContextIdx =
      ExpandSpecialArguments(Signature, IV, Arg_p, Builder, CalleeArgs,
                             [&NumRemainingInputs]() { --NumRemainingInputs; },
                             LoopHeader->getTerminator());

    // After ExpandSpecialArguments() gets called, NumRemainingInputs
    // counts the number of arguments to the kernel that correspond to
    // an array entry from the InPtr field of the DriverInfo
    // structure.
    const size_t NumInPtrArguments = NumRemainingInputs;

    if (NumInPtrArguments > 0) {
      ExpandInputsLoopInvariant(Builder, LoopHeader, Arg_p, TBAAPointer, ArgIter, NumInPtrArguments,
                                InBufPtrs, InStructTempSlots);
    }

    // Populate the actual call to kernel().
    llvm::SmallVector<llvm::Value*, 8> RootArgs;

    // Calculate the current input and output pointers.

    // Output

    llvm::Value *OutPtr = nullptr;
    if (CastedOutBasePtr) {
      llvm::Value *OutOffset = Builder.CreateSub(IV, Arg_x1);
      OutPtr = Builder.CreateInBoundsGEP(CastedOutBasePtr, OutOffset);

      if (PassOutByPointer) {
        RootArgs.push_back(OutPtr);
      }
    }

    // Inputs

    if (NumInPtrArguments > 0) {
      ExpandInputsBody(Builder, Arg_x1, TBAAAllocation, NumInPtrArguments,
                       InBufPtrs, InStructTempSlots, IV, RootArgs);
    }

    finishArgList(RootArgs, CalleeArgs, CalleeArgsContextIdx, *Function, Builder);

    llvm::Value *RetVal = Builder.CreateCall(Function, RootArgs);

    if (OutPtr && !PassOutByPointer) {
      RetVal->setName("call.result");
      llvm::StoreInst *Store = Builder.CreateStore(RetVal, OutPtr);
      if (gEnableRsTbaa) {
        Store->setMetadata("tbaa", TBAAAllocation);
      }
    }

    return true;
  }

  // Expand a simple reduce-style kernel function.
  //
  // The input is a kernel which represents a binary operation,
  // of the form
  //
  //   define foo @func(foo %a, foo %b),
  //
  // (More generally, it can be of the forms
  //
  //   define void @func(foo* %ret, foo* %a, foo* %b)
  //   define void @func(foo* %ret, foo1 %a, foo1 %b)
  //   define foo1 @func(foo2 %a, foo2 %b)
  //
  // as a result of argument / return value conversions. Here, "foo1"
  // and "foo2" refer to possibly coerced types, and the coerced
  // argument type may be different from the coerced return type. See
  // "Note on coercion" below.)
  //
  // Note also, we do not expect to encounter any case when the
  // arguments are promoted to pointers but the return value is
  // unpromoted to pointer, e.g.
  //
  //   define foo1 @func(foo* %a, foo* %b)
  //
  // and we will throw an assertion in this case.)
  //
  // The input kernel gets expanded into a kernel of the form
  //
  //   define void @func.expand(i8* %inBuf, i8* outBuf, i32 len)
  //
  // which performs a serial reduction of `len` elements from `inBuf`,
  // and stores the result into `outBuf`. In pseudocode, @func.expand
  // does:
  //
  //   inArr := (foo *)inBuf;
  //   accum := inArr[0];
  //   for (i := 1; i < len; ++i) {
  //     accum := foo(accum, inArr[i]);
  //   }
  //   *(foo *)outBuf := accum;
  //
  // Note on coercion
  //
  // Both the return value and the argument types may undergo internal
  // coercion in clang as part of call lowering. As a result, the
  // return value type may differ from the argument type even if the
  // types in the RenderScript signaure are the same. For instance, the
  // kernel
  //
  //   int3 add(int3 a, int3 b) { return a + b; }
  //
  // gets lowered by clang as
  //
  //   define <3 x i32> @add(<4 x i32> %a.coerce, <4 x i32> %b.coerce)
  //
  // under AArch64. The details of this process are found in clang,
  // lib/CodeGen/TargetInfo.cpp, under classifyArgumentType() and
  // classifyReturnType() in ARMABIInfo, AArch64ABIInfo. If the value
  // is passed by pointer, then the pointed-to type is not coerced.
  //
  // Since we lack the original type information, this code does loads
  // and stores of allocation data by way of pointers to the coerced
  // type.
  bool ExpandReduce(llvm::Function *Function) {
    bccAssert(Function);

    ALOGV("Expanding simple reduce kernel %s", Function->getName().str().c_str());

    llvm::DataLayout DL(Module);

    // TBAA Metadata
    llvm::MDNode *TBAARenderScriptDistinct, *TBAARenderScript, *TBAAAllocation;
    llvm::MDBuilder MDHelper(*Context);

    TBAARenderScriptDistinct =
      MDHelper.createTBAARoot(kRenderScriptTBAARootName);
    TBAARenderScript = MDHelper.createTBAANode(kRenderScriptTBAANodeName,
        TBAARenderScriptDistinct);
    TBAAAllocation = MDHelper.createTBAAScalarTypeNode("allocation",
                                                       TBAARenderScript);
    TBAAAllocation = MDHelper.createTBAAStructTagNode(TBAAAllocation,
                                                      TBAAAllocation, 0);

    llvm::Function *ExpandedFunction =
      createEmptyExpandedReduceKernel(Function->getName());

    // Extract the expanded kernel's parameters.  It is guaranteed by
    // createEmptyExpandedReduceKernel that there will be 3 parameters.
    auto ExpandedFunctionArgIter = ExpandedFunction->arg_begin();

    llvm::Value *Arg_inBuf  = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_outBuf = &*(ExpandedFunctionArgIter++);
    llvm::Value *Arg_len    = &*(ExpandedFunctionArgIter++);

    bccAssert(Function->arg_size() == 2 || Function->arg_size() == 3);

    // Check if, instead of returning a value, the original kernel has
    // a pointer parameter which points to a temporary buffer into
    // which the return value gets written.
    const bool ReturnValuePointerStyle = (Function->arg_size() == 3);
    bccAssert(Function->getReturnType()->isVoidTy() == ReturnValuePointerStyle);

    // Check if, instead of being passed by value, the inputs to the
    // original kernel are passed by pointer.
    auto FirstArgIter = Function->arg_begin();
    // The second argument is always an input to the original kernel.
    auto SecondArgIter = std::next(FirstArgIter);
    const bool InputsPointerStyle = SecondArgIter->getType()->isPointerTy();

    // Get the output type (i.e. return type of the original kernel).
    llvm::PointerType *OutPtrTy = nullptr;
    llvm::Type *OutTy = nullptr;
    if (ReturnValuePointerStyle) {
      OutPtrTy = llvm::dyn_cast<llvm::PointerType>(FirstArgIter->getType());
      bccAssert(OutPtrTy && "Expected a pointer parameter to kernel");
      OutTy = OutPtrTy->getElementType();
    } else {
      OutTy = Function->getReturnType();
      bccAssert(!OutTy->isVoidTy());
      OutPtrTy = OutTy->getPointerTo();
    }

    // Get the input type (type of the arguments to the original
    // kernel). Some input types are different from the output type,
    // due to explicit coercion that the compiler performs when
    // lowering the parameters. See "Note on coercion" above.
    llvm::PointerType *InPtrTy;
    llvm::Type *InTy;
    if (InputsPointerStyle) {
      InPtrTy = llvm::dyn_cast<llvm::PointerType>(SecondArgIter->getType());
      bccAssert(InPtrTy && "Expected a pointer parameter to kernel");
      bccAssert(ReturnValuePointerStyle);
      bccAssert(std::next(SecondArgIter)->getType() == InPtrTy &&
                "Input type mismatch");
      InTy = InPtrTy->getElementType();
    } else {
      InTy = SecondArgIter->getType();
      InPtrTy = InTy->getPointerTo();
      if (!ReturnValuePointerStyle) {
        bccAssert(InTy == FirstArgIter->getType() && "Input type mismatch");
      } else {
        bccAssert(InTy == std::next(SecondArgIter)->getType() &&
                  "Input type mismatch");
      }
    }

    // The input type should take up the same amount of space in
    // memory as the output type.
    bccAssert(DL.getTypeAllocSize(InTy) == DL.getTypeAllocSize(OutTy));

    // Construct the actual function body.
    llvm::IRBuilder<> Builder(ExpandedFunction->getEntryBlock().begin());

    // Cast input and output buffers to appropriate types.
    llvm::Value *InBuf = Builder.CreatePointerCast(Arg_inBuf, InPtrTy);
    llvm::Value *OutBuf = Builder.CreatePointerCast(Arg_outBuf, OutPtrTy);

    // Create a slot to pass temporary results back. This needs to be
    // separate from the accumulator slot because the kernel may mark
    // the return value slot as noalias.
    llvm::Value *ReturnBuf = nullptr;
    if (ReturnValuePointerStyle) {
      ReturnBuf = Builder.CreateAlloca(OutTy, nullptr, "ret.tmp");
    }

    // Create a slot to hold the second input if the inputs are passed
    // by pointer to the original kernel. We cannot directly pass a
    // pointer to the input buffer, because the kernel may modify its
    // inputs.
    llvm::Value *SecondInputTempBuf = nullptr;
    if (InputsPointerStyle) {
      SecondInputTempBuf = Builder.CreateAlloca(InTy, nullptr, "in.tmp");
    }

    // Create a slot to accumulate temporary results, and fill it with
    // the first value.
    llvm::Value *AccumBuf = Builder.CreateAlloca(OutTy, nullptr, "accum");
    // Cast to OutPtrTy before loading, since AccumBuf has type OutPtrTy.
    llvm::LoadInst *FirstElementLoad = Builder.CreateLoad(
      Builder.CreatePointerCast(InBuf, OutPtrTy));
    if (gEnableRsTbaa) {
      FirstElementLoad->setMetadata("tbaa", TBAAAllocation);
    }
    // Memory operations with AccumBuf shouldn't be marked with
    // RenderScript TBAA, since this might conflict with TBAA metadata
    // in the kernel function when AccumBuf is passed by pointer.
    Builder.CreateStore(FirstElementLoad, AccumBuf);

    // Loop body

    // Create the loop structure. Note that the first input in the input buffer
    // has already been accumulated, so that we start at index 1.
    llvm::Value *IndVar;
    llvm::Value *Start = llvm::ConstantInt::get(Arg_len->getType(), 1);
    llvm::BasicBlock *Exit = createLoop(Builder, Start, Arg_len, &IndVar);

    llvm::Value *InputPtr = Builder.CreateInBoundsGEP(InBuf, IndVar, "next_input.gep");

    // Set up arguments and call the original (unexpanded) kernel.
    //
    // The original kernel can have at most 3 arguments, which is
    // achieved when the signature looks like:
    //
    //    define void @func(foo* %ret, bar %a, bar %b)
    //
    // (bar can be one of foo/foo.coerce/foo*).
    llvm::SmallVector<llvm::Value *, 3> KernelArgs;

    if (ReturnValuePointerStyle) {
      KernelArgs.push_back(ReturnBuf);
    }

    if (InputsPointerStyle) {
      bccAssert(ReturnValuePointerStyle);
      // Because the return buffer is copied back into the
      // accumulator, it's okay if the accumulator is overwritten.
      KernelArgs.push_back(AccumBuf);

      llvm::LoadInst *InputLoad = Builder.CreateLoad(InputPtr);
      if (gEnableRsTbaa) {
        InputLoad->setMetadata("tbaa", TBAAAllocation);
      }
      Builder.CreateStore(InputLoad, SecondInputTempBuf);

      KernelArgs.push_back(SecondInputTempBuf);
    } else {
      // InPtrTy may be different from OutPtrTy (the type of
      // AccumBuf), so first cast the accumulator buffer to the
      // pointer type corresponding to the input argument type.
      KernelArgs.push_back(
        Builder.CreateLoad(Builder.CreatePointerCast(AccumBuf, InPtrTy)));

      llvm::LoadInst *LoadedArg = Builder.CreateLoad(InputPtr);
      if (gEnableRsTbaa) {
        LoadedArg->setMetadata("tbaa", TBAAAllocation);
      }
      KernelArgs.push_back(LoadedArg);
    }

    llvm::Value *RetVal = Builder.CreateCall(Function, KernelArgs);

    const uint64_t ElementSize = DL.getTypeStoreSize(OutTy);
    const uint64_t ElementAlign = DL.getABITypeAlignment(OutTy);

    // Store the output in the accumulator.
    if (ReturnValuePointerStyle) {
      Builder.CreateMemCpy(AccumBuf, ReturnBuf, ElementSize, ElementAlign);
    } else {
      Builder.CreateStore(RetVal, AccumBuf);
    }

    // Loop exit
    Builder.SetInsertPoint(Exit, Exit->begin());

    llvm::LoadInst *OutputLoad = Builder.CreateLoad(AccumBuf);
    llvm::StoreInst *OutputStore = Builder.CreateStore(OutputLoad, OutBuf);
    if (gEnableRsTbaa) {
      OutputStore->setMetadata("tbaa", TBAAAllocation);
    }

    return true;
  }

  // Certain categories of functions that make up a general
  // reduce-style kernel are called directly from the driver with no
  // expansion needed.  For a function in such a category, we need to
  // promote linkage from static to external, to ensure that the
  // function is visible to the driver in the dynamic symbol table.
  // This promotion is safe because we don't have any kind of cross
  // translation unit linkage model (except for linking against
  // RenderScript libraries), so we do not risk name clashes.
  bool PromoteReduceNewFunction(const char *Name, FunctionSet &PromotedFunctions) {
    if (!Name)  // a presumably-optional function that is not present
      return false;

    llvm::Function *Fn = Module->getFunction(Name);
    bccAssert(Fn != nullptr);
    if (PromotedFunctions.insert(Fn).second) {
      bccAssert(Fn->getLinkage() == llvm::GlobalValue::InternalLinkage);
      Fn->setLinkage(llvm::GlobalValue::ExternalLinkage);
      return true;
    }

    return false;
  }

  // Expand the accumulator function for a general reduce-style kernel.
  //
  // The input is a function of the form
  //
  //   define void @func(accumType* %accum, foo1 in1[, ... fooN inN] [, special arguments])
  //
  // where all arguments except the first are the same as for a foreach kernel.
  //
  // The input accumulator function gets expanded into a function of the form
  //
  //   define void @func.expand(%RsExpandKernelDriverInfoPfx* %p, i32 %x1, i32 %x2, accumType* %accum)
  //
  // which performs a serial accumulaion of elements [x1, x2) into *%accum.
  //
  // In pseudocode, @func.expand does:
  //
  //   for (i = %x1; i < %x2; ++i) {
  //     func(%accum,
  //          *((foo1 *)p->inPtr[0] + i)[, ... *((fooN *)p->inPtr[N-1] + i)
  //          [, p] [, i] [, p->current.y] [, p->current.z]);
  //   }
  //
  // This is very similar to foreach kernel expansion with no output.
  bool ExpandReduceNewAccumulator(llvm::Function *FnAccumulator, uint32_t Signature, size_t NumInputs) {
    ALOGV("Expanding accumulator %s for general reduce kernel",
          FnAccumulator->getName().str().c_str());

    // Create TBAA meta-data.
    llvm::MDNode *TBAARenderScriptDistinct, *TBAARenderScript,
                 *TBAAAllocation, *TBAAPointer;
    llvm::MDBuilder MDHelper(*Context);
    TBAARenderScriptDistinct =
      MDHelper.createTBAARoot(kRenderScriptTBAARootName);
    TBAARenderScript = MDHelper.createTBAANode(kRenderScriptTBAANodeName,
        TBAARenderScriptDistinct);
    TBAAAllocation = MDHelper.createTBAAScalarTypeNode("allocation",
                                                       TBAARenderScript);
    TBAAAllocation = MDHelper.createTBAAStructTagNode(TBAAAllocation,
                                                      TBAAAllocation, 0);
    TBAAPointer = MDHelper.createTBAAScalarTypeNode("pointer",
                                                    TBAARenderScript);
    TBAAPointer = MDHelper.createTBAAStructTagNode(TBAAPointer, TBAAPointer, 0);

    auto AccumulatorArgIter = FnAccumulator->arg_begin();

    // Create empty accumulator function.
    llvm::Function *FnExpandedAccumulator =
        createEmptyExpandedReduceNewAccumulator(FnAccumulator->getName(),
                                                (AccumulatorArgIter++)->getType());

    // Extract the expanded accumulator's parameters.  It is
    // guaranteed by createEmptyExpandedReduceNewAccumulator that
    // there will be 4 parameters.
    bccAssert(FnExpandedAccumulator->arg_size() == kNumExpandedReduceNewAccumulatorParams);
    auto ExpandedAccumulatorArgIter = FnExpandedAccumulator->arg_begin();
    llvm::Value *Arg_p     = &*(ExpandedAccumulatorArgIter++);
    llvm::Value *Arg_x1    = &*(ExpandedAccumulatorArgIter++);
    llvm::Value *Arg_x2    = &*(ExpandedAccumulatorArgIter++);
    llvm::Value *Arg_accum = &*(ExpandedAccumulatorArgIter++);

    // Construct the actual function body.
    llvm::IRBuilder<> Builder(FnExpandedAccumulator->getEntryBlock().begin());

    // Create the loop structure.
    llvm::BasicBlock *LoopHeader = Builder.GetInsertBlock();
    llvm::Value *IndVar;
    createLoop(Builder, Arg_x1, Arg_x2, &IndVar);

    llvm::SmallVector<llvm::Value*, 8> CalleeArgs;
    const int CalleeArgsContextIdx =
        ExpandSpecialArguments(Signature, IndVar, Arg_p, Builder, CalleeArgs,
                               [](){}, LoopHeader->getTerminator());

    llvm::SmallVector<llvm::Value*, 8> InBufPtrs;
    llvm::SmallVector<llvm::Value*, 8> InStructTempSlots;
    ExpandInputsLoopInvariant(Builder, LoopHeader, Arg_p, TBAAPointer, AccumulatorArgIter, NumInputs,
                              InBufPtrs, InStructTempSlots);

    // Populate the actual call to the original accumulator.
    llvm::SmallVector<llvm::Value*, 8> RootArgs;
    RootArgs.push_back(Arg_accum);
    ExpandInputsBody(Builder, Arg_x1, TBAAAllocation, NumInputs, InBufPtrs, InStructTempSlots,
                     IndVar, RootArgs);
    finishArgList(RootArgs, CalleeArgs, CalleeArgsContextIdx, *FnAccumulator, Builder);
    Builder.CreateCall(FnAccumulator, RootArgs);

    return true;
  }

  /// @brief Checks if pointers to allocation internals are exposed
  ///
  /// This function verifies if through the parameters passed to the kernel
  /// or through calls to the runtime library the script gains access to
  /// pointers pointing to data within a RenderScript Allocation.
  /// If we know we control all loads from and stores to data within
  /// RenderScript allocations and if we know the run-time internal accesses
  /// are all annotated with RenderScript TBAA metadata, only then we
  /// can safely use TBAA to distinguish between generic and from-allocation
  /// pointers.
  bool allocPointersExposed(llvm::Module &Module) {
    // Old style kernel function can expose pointers to elements within
    // allocations.
    // TODO: Extend analysis to allow simple cases of old-style kernels.
    for (size_t i = 0; i < mExportForEachCount; ++i) {
      const char *Name = mExportForEachNameList[i];
      uint32_t Signature = mExportForEachSignatureList[i];
      if (Module.getFunction(Name) &&
          !bcinfo::MetadataExtractor::hasForEachSignatureKernel(Signature)) {
        return true;
      }
    }

    // Check for library functions that expose a pointer to an Allocation or
    // that are not yet annotated with RenderScript-specific tbaa information.
    static const std::vector<const char *> Funcs{
      // rsGetElementAt(...)
      "_Z14rsGetElementAt13rs_allocationj",
      "_Z14rsGetElementAt13rs_allocationjj",
      "_Z14rsGetElementAt13rs_allocationjjj",

      // rsSetElementAt()
      "_Z14rsSetElementAt13rs_allocationPvj",
      "_Z14rsSetElementAt13rs_allocationPvjj",
      "_Z14rsSetElementAt13rs_allocationPvjjj",

      // rsGetElementAtYuv_uchar_Y()
      "_Z25rsGetElementAtYuv_uchar_Y13rs_allocationjj",

      // rsGetElementAtYuv_uchar_U()
      "_Z25rsGetElementAtYuv_uchar_U13rs_allocationjj",

      // rsGetElementAtYuv_uchar_V()
      "_Z25rsGetElementAtYuv_uchar_V13rs_allocationjj",
    };

    for (auto FI : Funcs) {
      llvm::Function *Function = Module.getFunction(FI);

      if (!Function) {
        ALOGE("Missing run-time function '%s'", FI);
        return true;
      }

      if (Function->getNumUses() > 0) {
        return true;
      }
    }

    return false;
  }

  /// @brief Connect RenderScript TBAA metadata to C/C++ metadata
  ///
  /// The TBAA metadata used to annotate loads/stores from RenderScript
  /// Allocations is generated in a separate TBAA tree with a
  /// "RenderScript Distinct TBAA" root node. LLVM does assume may-alias for
  /// all nodes in unrelated alias analysis trees. This function makes the
  /// "RenderScript TBAA" node (which is parented by the Distinct TBAA root),
  /// a subtree of the normal C/C++ TBAA tree aside of normal C/C++ types. With
  /// the connected trees every access to an Allocation is resolved to
  /// must-alias if compared to a normal C/C++ access.
  void connectRenderScriptTBAAMetadata(llvm::Module &Module) {
    llvm::MDBuilder MDHelper(*Context);
    llvm::MDNode *TBAARenderScriptDistinct =
      MDHelper.createTBAARoot("RenderScript Distinct TBAA");
    llvm::MDNode *TBAARenderScript = MDHelper.createTBAANode(
        "RenderScript TBAA", TBAARenderScriptDistinct);
    llvm::MDNode *TBAARoot     = MDHelper.createTBAARoot("Simple C/C++ TBAA");
    TBAARenderScript->replaceOperandWith(1, TBAARoot);
  }

  virtual bool runOnModule(llvm::Module &Module) {
    bool Changed  = false;
    this->Module  = &Module;
    Context = &Module.getContext();

    buildTypes();

    bcinfo::MetadataExtractor me(&Module);
    if (!me.extract()) {
      ALOGE("Could not extract metadata from module!");
      return false;
    }

    // Expand forEach_* style kernels.
    mExportForEachCount = me.getExportForEachSignatureCount();
    mExportForEachNameList = me.getExportForEachNameList();
    mExportForEachSignatureList = me.getExportForEachSignatureList();

    for (size_t i = 0; i < mExportForEachCount; ++i) {
      const char *name = mExportForEachNameList[i];
      uint32_t signature = mExportForEachSignatureList[i];
      llvm::Function *kernel = Module.getFunction(name);
      if (kernel) {
        if (bcinfo::MetadataExtractor::hasForEachSignatureKernel(signature)) {
          Changed |= ExpandForEach(kernel, signature);
          kernel->setLinkage(llvm::GlobalValue::InternalLinkage);
        } else if (kernel->getReturnType()->isVoidTy()) {
          Changed |= ExpandOldStyleForEach(kernel, signature);
          kernel->setLinkage(llvm::GlobalValue::InternalLinkage);
        } else {
          // There are some graphics root functions that are not
          // expanded, but that will be called directly. For those
          // functions, we can not set the linkage to internal.
        }
      }
    }

    // Expand simple reduce_* style kernels.
    mExportReduceCount = me.getExportReduceCount();
    mExportReduceNameList = me.getExportReduceNameList();

    for (size_t i = 0; i < mExportReduceCount; ++i) {
      llvm::Function *kernel = Module.getFunction(mExportReduceNameList[i]);
      if (kernel) {
        Changed |= ExpandReduce(kernel);
      }
    }

    // Process general reduce_* style functions.
    const size_t ExportReduceNewCount = me.getExportReduceNewCount();
    const bcinfo::MetadataExtractor::ReduceNew *ExportReduceNewList = me.getExportReduceNewList();
    //   Note that functions can be shared between kernels
    FunctionSet PromotedFunctions, ExpandedAccumulators;

    for (size_t i = 0; i < ExportReduceNewCount; ++i) {
      Changed |= PromoteReduceNewFunction(ExportReduceNewList[i].mInitializerName, PromotedFunctions);
      Changed |= PromoteReduceNewFunction(ExportReduceNewList[i].mOutConverterName, PromotedFunctions);

      // Accumulator
      llvm::Function *accumulator = Module.getFunction(ExportReduceNewList[i].mAccumulatorName);
      bccAssert(accumulator != nullptr);
      if (ExpandedAccumulators.insert(accumulator).second)
        Changed |= ExpandReduceNewAccumulator(accumulator,
                                              ExportReduceNewList[i].mSignature,
                                              ExportReduceNewList[i].mInputCount);
    }

    if (gEnableRsTbaa && !allocPointersExposed(Module)) {
      connectRenderScriptTBAAMetadata(Module);
    }

    return Changed;
  }

  virtual const char *getPassName() const {
    return "forEach_* and reduce_* function expansion";
  }

}; // end RSKernelExpandPass

} // end anonymous namespace

char RSKernelExpandPass::ID = 0;
static llvm::RegisterPass<RSKernelExpandPass> X("kernelexp", "Kernel Expand Pass");

namespace bcc {

const char BCC_INDEX_VAR_NAME[] = "rsIndex";

llvm::ModulePass *
createRSKernelExpandPass(bool pEnableStepOpt) {
  return new RSKernelExpandPass(pEnableStepOpt);
}

} // end namespace bcc
