//===-- CodeEmitter.cpp - CodeEmitter Class -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See external/llvm/LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CodeEmitter class.
//
//===----------------------------------------------------------------------===//

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "CodeEmitter.h"

#include "Config.h"

#if DEBUG_OLD_JIT_DISASSEMBLER
#include "Disassembler/Disassembler.h"
#endif

#include "CodeMemoryManager.h"
#include "ExecutionEngine/Runtime.h"
#include "ExecutionEngine/ScriptCompiled.h"

#include <bcc/bcc.h>
#include <bcc/bcc_cache.h>
#include "ExecutionEngine/bcc_internal.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRelocation.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/JITCodeEmitter.h"

#include "llvm/ExecutionEngine/GenericValue.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Host.h"

#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/TargetJITInfo.h"

#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/GlobalAlias.h"
#include "llvm/GlobalValue.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Instruction.h"
#include "llvm/Type.h"

#include <algorithm>
#include <vector>
#include <set>
#include <string>

#include <stddef.h>


namespace bcc {

// Will take the ownership of @MemMgr
CodeEmitter::CodeEmitter(ScriptCompiled *result, CodeMemoryManager *pMemMgr)
    : mpResult(result),
      mpMemMgr(pMemMgr),
      mpTarget(NULL),
      mpTJI(NULL),
      mpTD(NULL),
      mpCurEmitFunction(NULL),
      mpConstantPool(NULL),
      mpJumpTable(NULL),
      mpMMI(NULL),
      mpSymbolLookupFn(NULL),
      mpSymbolLookupContext(NULL) {
}


CodeEmitter::~CodeEmitter() {
}


// Once you finish the compilation on a translation unit, you can call this
// function to recycle the memory (which is used at compilation time and not
// needed for runtime).
//
//  NOTE: You should not call this funtion until the code-gen passes for a
//        given module is done. Otherwise, the results is undefined and may
//        cause the system crash!
void CodeEmitter::releaseUnnecessary() {
  mMBBLocations.clear();
  mLabelLocations.clear();
  mGlobalAddressMap.clear();
  mFunctionToLazyStubMap.clear();
  GlobalToIndirectSymMap.clear();
  ExternalFnToStubMap.clear();
  PendingFunctions.clear();
}


void CodeEmitter::reset() {
  releaseUnnecessary();

  mpResult = NULL;

  mpSymbolLookupFn = NULL;
  mpSymbolLookupContext = NULL;

  mpTJI = NULL;
  mpTD = NULL;

  mpMemMgr->reset();
}


void *CodeEmitter::UpdateGlobalMapping(const llvm::GlobalValue *GV, void *Addr) {
  if (Addr == NULL) {
    // Removing mapping
    GlobalAddressMapTy::iterator I = mGlobalAddressMap.find(GV);
    void *OldVal;

    if (I == mGlobalAddressMap.end()) {
      OldVal = NULL;
    } else {
      OldVal = I->second;
      mGlobalAddressMap.erase(I);
    }

    return OldVal;
  }

  void *&CurVal = mGlobalAddressMap[GV];
  void *OldVal = CurVal;

  CurVal = Addr;

  return OldVal;
}


unsigned int CodeEmitter::GetConstantPoolSizeInBytes(
                                    llvm::MachineConstantPool *MCP) {
  const std::vector<llvm::MachineConstantPoolEntry> &Constants =
      MCP->getConstants();

  if (Constants.empty())
    return 0;

  unsigned int Size = 0;
  for (int i = 0, e = Constants.size(); i != e; i++) {
    llvm::MachineConstantPoolEntry CPE = Constants[i];
    unsigned int AlignMask = CPE.getAlignment() - 1;
    Size = (Size + AlignMask) & ~AlignMask;
    llvm::Type *Ty = CPE.getType();
    Size += mpTD->getTypeAllocSize(Ty);
  }

  return Size;
}

// This function converts a Constant* into a GenericValue. The interesting
// part is if C is a ConstantExpr.
void CodeEmitter::GetConstantValue(const llvm::Constant *C,
                                   llvm::GenericValue &Result) {
  if (C->getValueID() == llvm::Value::UndefValueVal)
    return;
  else if (C->getValueID() == llvm::Value::ConstantExprVal) {
    const llvm::ConstantExpr *CE = (llvm::ConstantExpr*) C;
    const llvm::Constant *Op0 = CE->getOperand(0);

    switch (CE->getOpcode()) {
      case llvm::Instruction::GetElementPtr: {
        // Compute the index
        llvm::SmallVector<llvm::Value*, 8> Indices(CE->op_begin() + 1,
                                                   CE->op_end());
        uint64_t Offset = mpTD->getIndexedOffset(Op0->getType(), Indices);

        GetConstantValue(Op0, Result);
        Result.PointerVal =
            static_cast<uint8_t*>(Result.PointerVal) + Offset;

        return;
      }
      case llvm::Instruction::Trunc: {
        uint32_t BitWidth =
            llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

        GetConstantValue(Op0, Result);
        Result.IntVal = Result.IntVal.trunc(BitWidth);

        return;
      }
      case llvm::Instruction::ZExt: {
        uint32_t BitWidth =
            llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

        GetConstantValue(Op0, Result);
        Result.IntVal = Result.IntVal.zext(BitWidth);

        return;
      }
      case llvm::Instruction::SExt: {
        uint32_t BitWidth =
            llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

        GetConstantValue(Op0, Result);
        Result.IntVal = Result.IntVal.sext(BitWidth);

        return;
      }
      case llvm::Instruction::FPTrunc: {
        // TODO(all): fixme: long double
        GetConstantValue(Op0, Result);
        Result.FloatVal = static_cast<float>(Result.DoubleVal);
        return;
      }
      case llvm::Instruction::FPExt: {
        // TODO(all): fixme: long double
        GetConstantValue(Op0, Result);
        Result.DoubleVal = static_cast<double>(Result.FloatVal);
        return;
      }
      case llvm::Instruction::UIToFP: {
        GetConstantValue(Op0, Result);
        if (CE->getType()->isFloatTy())
          Result.FloatVal =
              static_cast<float>(Result.IntVal.roundToDouble());
        else if (CE->getType()->isDoubleTy())
          Result.DoubleVal = Result.IntVal.roundToDouble();
        else if (CE->getType()->isX86_FP80Ty()) {
          const uint64_t zero[] = { 0, 0 };
          llvm::APFloat apf(llvm::APInt(80, 2, zero));
          apf.convertFromAPInt(Result.IntVal,
                               false,
                               llvm::APFloat::rmNearestTiesToEven);
          Result.IntVal = apf.bitcastToAPInt();
        }
        return;
      }
      case llvm::Instruction::SIToFP: {
        GetConstantValue(Op0, Result);
        if (CE->getType()->isFloatTy())
          Result.FloatVal =
              static_cast<float>(Result.IntVal.signedRoundToDouble());
        else if (CE->getType()->isDoubleTy())
          Result.DoubleVal = Result.IntVal.signedRoundToDouble();
        else if (CE->getType()->isX86_FP80Ty()) {
          const uint64_t zero[] = { 0, 0 };
          llvm::APFloat apf = llvm::APFloat(llvm::APInt(80, 2, zero));
          apf.convertFromAPInt(Result.IntVal,
                               true,
                               llvm::APFloat::rmNearestTiesToEven);
          Result.IntVal = apf.bitcastToAPInt();
        }
        return;
      }
      // double->APInt conversion handles sign
      case llvm::Instruction::FPToUI:
      case llvm::Instruction::FPToSI: {
        uint32_t BitWidth =
            llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

        GetConstantValue(Op0, Result);
        if (Op0->getType()->isFloatTy())
          Result.IntVal =
           llvm::APIntOps::RoundFloatToAPInt(Result.FloatVal, BitWidth);
        else if (Op0->getType()->isDoubleTy())
          Result.IntVal =
              llvm::APIntOps::RoundDoubleToAPInt(Result.DoubleVal,
                                                 BitWidth);
        else if (Op0->getType()->isX86_FP80Ty()) {
          llvm::APFloat apf = llvm::APFloat(Result.IntVal);
          uint64_t V;
          bool Ignored;
          apf.convertToInteger(&V,
                               BitWidth,
                               CE->getOpcode() == llvm::Instruction::FPToSI,
                               llvm::APFloat::rmTowardZero,
                               &Ignored);
          Result.IntVal = V;  // endian?
        }
        return;
      }
      case llvm::Instruction::PtrToInt: {
        uint32_t PtrWidth = mpTD->getPointerSizeInBits();

        GetConstantValue(Op0, Result);
        Result.IntVal = llvm::APInt(PtrWidth, uintptr_t
                                    (Result.PointerVal));

        return;
      }
      case llvm::Instruction::IntToPtr: {
        uint32_t PtrWidth = mpTD->getPointerSizeInBits();

        GetConstantValue(Op0, Result);
        if (PtrWidth != Result.IntVal.getBitWidth())
          Result.IntVal = Result.IntVal.zextOrTrunc(PtrWidth);
        bccAssert(Result.IntVal.getBitWidth() <= 64 && "Bad pointer width");

        Result.PointerVal =
            llvm::PointerTy(
                static_cast<uintptr_t>(Result.IntVal.getZExtValue()));

        return;
      }
      case llvm::Instruction::BitCast: {
        GetConstantValue(Op0, Result);
        const llvm::Type *DestTy = CE->getType();

        switch (Op0->getType()->getTypeID()) {
          case llvm::Type::IntegerTyID: {
            bccAssert(DestTy->isFloatingPointTy() && "invalid bitcast");
            if (DestTy->isFloatTy())
              Result.FloatVal = Result.IntVal.bitsToFloat();
            else if (DestTy->isDoubleTy())
              Result.DoubleVal = Result.IntVal.bitsToDouble();
            break;
          }
          case llvm::Type::FloatTyID: {
            bccAssert(DestTy->isIntegerTy(32) && "Invalid bitcast");
            Result.IntVal.floatToBits(Result.FloatVal);
            break;
          }
          case llvm::Type::DoubleTyID: {
            bccAssert(DestTy->isIntegerTy(64) && "Invalid bitcast");
            Result.IntVal.doubleToBits(Result.DoubleVal);
            break;
          }
          case llvm::Type::PointerTyID: {
            bccAssert(DestTy->isPointerTy() && "Invalid bitcast");
            break;  // getConstantValue(Op0) above already converted it
          }
          default: {
            llvm_unreachable("Invalid bitcast operand");
          }
        }
        return;
      }
      case llvm::Instruction::Add:
      case llvm::Instruction::FAdd:
      case llvm::Instruction::Sub:
      case llvm::Instruction::FSub:
      case llvm::Instruction::Mul:
      case llvm::Instruction::FMul:
      case llvm::Instruction::UDiv:
      case llvm::Instruction::SDiv:
      case llvm::Instruction::URem:
      case llvm::Instruction::SRem:
      case llvm::Instruction::And:
      case llvm::Instruction::Or:
      case llvm::Instruction::Xor: {
        llvm::GenericValue LHS, RHS;
        GetConstantValue(Op0, LHS);
        GetConstantValue(CE->getOperand(1), RHS);

        switch (Op0->getType()->getTypeID()) {
          case llvm::Type::IntegerTyID: {
            switch (CE->getOpcode()) {
              case llvm::Instruction::Add: {
                Result.IntVal = LHS.IntVal + RHS.IntVal;
                break;
              }
              case llvm::Instruction::Sub: {
                Result.IntVal = LHS.IntVal - RHS.IntVal;
                break;
              }
              case llvm::Instruction::Mul: {
                Result.IntVal = LHS.IntVal * RHS.IntVal;
                break;
              }
              case llvm::Instruction::UDiv: {
                Result.IntVal = LHS.IntVal.udiv(RHS.IntVal);
                break;
              }
              case llvm::Instruction::SDiv: {
                Result.IntVal = LHS.IntVal.sdiv(RHS.IntVal);
                break;
              }
              case llvm::Instruction::URem: {
                Result.IntVal = LHS.IntVal.urem(RHS.IntVal);
                break;
              }
              case llvm::Instruction::SRem: {
                Result.IntVal = LHS.IntVal.srem(RHS.IntVal);
                break;
              }
              case llvm::Instruction::And: {
                Result.IntVal = LHS.IntVal & RHS.IntVal;
                break;
              }
              case llvm::Instruction::Or: {
                Result.IntVal = LHS.IntVal | RHS.IntVal;
                break;
              }
              case llvm::Instruction::Xor: {
                Result.IntVal = LHS.IntVal ^ RHS.IntVal;
                break;
              }
              default: {
                llvm_unreachable("Invalid integer opcode");
              }
            }
            break;
          }
          case llvm::Type::FloatTyID: {
            switch (CE->getOpcode()) {
              case llvm::Instruction::FAdd: {
                Result.FloatVal = LHS.FloatVal + RHS.FloatVal;
                break;
              }
              case llvm::Instruction::FSub: {
                Result.FloatVal = LHS.FloatVal - RHS.FloatVal;
                break;
              }
              case llvm::Instruction::FMul: {
                Result.FloatVal = LHS.FloatVal * RHS.FloatVal;
                break;
              }
              case llvm::Instruction::FDiv: {
                Result.FloatVal = LHS.FloatVal / RHS.FloatVal;
                break;
              }
              case llvm::Instruction::FRem: {
                Result.FloatVal = ::fmodf(LHS.FloatVal, RHS.FloatVal);
                break;
              }
              default: {
                llvm_unreachable("Invalid float opcode");
              }
            }
            break;
          }
          case llvm::Type::DoubleTyID: {
            switch (CE->getOpcode()) {
              case llvm::Instruction::FAdd: {
                Result.DoubleVal = LHS.DoubleVal + RHS.DoubleVal;
                break;
              }
              case llvm::Instruction::FSub: {
                Result.DoubleVal = LHS.DoubleVal - RHS.DoubleVal;
                break;
              }
              case llvm::Instruction::FMul: {
                Result.DoubleVal = LHS.DoubleVal * RHS.DoubleVal;
                break;
              }
              case llvm::Instruction::FDiv: {
                Result.DoubleVal = LHS.DoubleVal / RHS.DoubleVal;
                break;
              }
              case llvm::Instruction::FRem: {
                Result.DoubleVal = ::fmod(LHS.DoubleVal, RHS.DoubleVal);
                break;
              }
              default: {
                llvm_unreachable("Invalid double opcode");
              }
            }
            break;
          }
          case llvm::Type::X86_FP80TyID:
          case llvm::Type::PPC_FP128TyID:
          case llvm::Type::FP128TyID: {
            llvm::APFloat apfLHS = llvm::APFloat(LHS.IntVal);
            switch (CE->getOpcode()) {
              case llvm::Instruction::FAdd: {
                apfLHS.add(llvm::APFloat(RHS.IntVal),
                           llvm::APFloat::rmNearestTiesToEven);
                break;
              }
              case llvm::Instruction::FSub: {
                apfLHS.subtract(llvm::APFloat(RHS.IntVal),
                                llvm::APFloat::rmNearestTiesToEven);
                break;
              }
              case llvm::Instruction::FMul: {
                apfLHS.multiply(llvm::APFloat(RHS.IntVal),
                                llvm::APFloat::rmNearestTiesToEven);
                break;
              }
              case llvm::Instruction::FDiv: {
                apfLHS.divide(llvm::APFloat(RHS.IntVal),
                              llvm::APFloat::rmNearestTiesToEven);
                break;
              }
              case llvm::Instruction::FRem: {
                apfLHS.mod(llvm::APFloat(RHS.IntVal),
                           llvm::APFloat::rmNearestTiesToEven);
                break;
              }
              default: {
                llvm_unreachable("Invalid long double opcode");
              }
            }
            Result.IntVal = apfLHS.bitcastToAPInt();
            break;
          }
          default: {
            llvm_unreachable("Bad add type!");
          }
        }  // End switch (Op0->getType()->getTypeID())
        return;
      }
      default: {
        break;
      }
    }   // End switch (CE->getOpcode())

    std::string msg;
    llvm::raw_string_ostream Msg(msg);
    Msg << "ConstantExpr not handled: " << *CE;
    llvm::report_fatal_error(Msg.str());
  }  // C->getValueID() == llvm::Value::ConstantExprVal

  switch (C->getType()->getTypeID()) {
    case llvm::Type::FloatTyID: {
      Result.FloatVal =
          llvm::cast<llvm::ConstantFP>(C)->getValueAPF().convertToFloat();
      break;
    }
    case llvm::Type::DoubleTyID: {
      Result.DoubleVal =
          llvm::cast<llvm::ConstantFP>(C)->getValueAPF().convertToDouble();
      break;
    }
    case llvm::Type::X86_FP80TyID:
    case llvm::Type::FP128TyID:
    case llvm::Type::PPC_FP128TyID: {
      Result.IntVal =
          llvm::cast<llvm::ConstantFP>(C)->getValueAPF().bitcastToAPInt();
      break;
    }
    case llvm::Type::IntegerTyID: {
      Result.IntVal =
          llvm::cast<llvm::ConstantInt>(C)->getValue();
      break;
    }
    case llvm::Type::PointerTyID: {
      switch (C->getValueID()) {
        case llvm::Value::ConstantPointerNullVal: {
          Result.PointerVal = NULL;
          break;
        }
        case llvm::Value::FunctionVal: {
          const llvm::Function *F = static_cast<const llvm::Function*>(C);
          Result.PointerVal =
              GetPointerToFunctionOrStub(const_cast<llvm::Function*>(F));
          break;
        }
        case llvm::Value::GlobalVariableVal: {
          const llvm::GlobalVariable *GV =
              static_cast<const llvm::GlobalVariable*>(C);
          Result.PointerVal =
            GetOrEmitGlobalVariable(const_cast<llvm::GlobalVariable*>(GV));
          break;
        }
        case llvm::Value::BlockAddressVal: {
          bccAssert(false && "JIT does not support address-of-label yet!");
        }
        default: {
          llvm_unreachable("Unknown constant pointer type!");
        }
      }
      break;
    }
    default: {
      std::string msg;
      llvm::raw_string_ostream Msg(msg);
      Msg << "ERROR: Constant unimplemented for type: " << *C->getType();
      llvm::report_fatal_error(Msg.str());
      break;
    }
  }
  return;
}


// Stores the data in @Val of type @Ty at address @Addr.
void CodeEmitter::StoreValueToMemory(const llvm::GenericValue &Val,
                                     void *Addr,
                                     llvm::Type *Ty) {
  const unsigned int StoreBytes = mpTD->getTypeStoreSize(Ty);

  switch (Ty->getTypeID()) {
    case llvm::Type::IntegerTyID: {
      const llvm::APInt &IntVal = Val.IntVal;
      bccAssert(((IntVal.getBitWidth() + 7) / 8 >= StoreBytes) &&
          "Integer too small!");

      const uint8_t *Src =
        reinterpret_cast<const uint8_t*>(IntVal.getRawData());

      if (llvm::sys::isLittleEndianHost()) {
        // Little-endian host - the source is ordered from LSB to MSB.
        // Order the destination from LSB to MSB: Do a straight copy.
        memcpy(Addr, Src, StoreBytes);
      } else {
        // Big-endian host - the source is an array of 64 bit words
        // ordered from LSW to MSW.
        //
        // Each word is ordered from MSB to LSB.
        //
        // Order the destination from MSB to LSB:
        //  Reverse the word order, but not the bytes in a word.
        unsigned int i = StoreBytes;
        while (i > sizeof(uint64_t)) {
          i -= sizeof(uint64_t);
          ::memcpy(reinterpret_cast<uint8_t*>(Addr) + i,
              Src,
              sizeof(uint64_t));
          Src += sizeof(uint64_t);
        }
        ::memcpy(Addr, Src + sizeof(uint64_t) - i, i);
      }
      break;
    }
    case llvm::Type::FloatTyID: {
      *reinterpret_cast<float*>(Addr) = Val.FloatVal;
      break;
    }
    case llvm::Type::DoubleTyID: {
      *reinterpret_cast<double*>(Addr) = Val.DoubleVal;
      break;
    }
    case llvm::Type::X86_FP80TyID: {
      memcpy(Addr, Val.IntVal.getRawData(), 10);
      break;
    }
    case llvm::Type::PointerTyID: {
      // Ensure 64 bit target pointers are fully initialized on 32 bit
      // hosts.
      if (StoreBytes != sizeof(llvm::PointerTy))
        memset(Addr, 0, StoreBytes);
      *((llvm::PointerTy*) Addr) = Val.PointerVal;
      break;
    }
    default: {
      break;
    }
  }

  if (llvm::sys::isLittleEndianHost() != mpTD->isLittleEndian())
    std::reverse(reinterpret_cast<uint8_t*>(Addr),
        reinterpret_cast<uint8_t*>(Addr) + StoreBytes);

  return;
}


// Recursive function to apply a @Constant value into the specified memory
// location @Addr.
void CodeEmitter::InitializeConstantToMemory(const llvm::Constant *C, void *Addr) {
  switch (C->getValueID()) {
    case llvm::Value::UndefValueVal: {
      // Nothing to do
      break;
    }
    case llvm::Value::ConstantVectorVal: {
      // dynamic cast may hurt performance
      const llvm::ConstantVector *CP = (llvm::ConstantVector*) C;

      unsigned int ElementSize = mpTD->getTypeAllocSize
        (CP->getType()->getElementType());

      for (int i = 0, e = CP->getNumOperands(); i != e;i++)
        InitializeConstantToMemory(
            CP->getOperand(i),
            reinterpret_cast<uint8_t*>(Addr) + i * ElementSize);
      break;
    }
    case llvm::Value::ConstantAggregateZeroVal: {
      memset(Addr, 0, (size_t) mpTD->getTypeAllocSize(C->getType()));
      break;
    }
    case llvm::Value::ConstantArrayVal: {
      const llvm::ConstantArray *CPA = (llvm::ConstantArray*) C;
      unsigned int ElementSize = mpTD->getTypeAllocSize
        (CPA->getType()->getElementType());

      for (int i = 0, e = CPA->getNumOperands(); i != e; i++)
        InitializeConstantToMemory(
            CPA->getOperand(i),
            reinterpret_cast<uint8_t*>(Addr) + i * ElementSize);
      break;
    }
    case llvm::Value::ConstantStructVal: {
      const llvm::ConstantStruct *CPS =
          static_cast<const llvm::ConstantStruct*>(C);
      const llvm::StructLayout *SL = mpTD->getStructLayout
        (llvm::cast<llvm::StructType>(CPS->getType()));

      for (int i = 0, e = CPS->getNumOperands(); i != e; i++)
        InitializeConstantToMemory(
            CPS->getOperand(i),
            reinterpret_cast<uint8_t*>(Addr) + SL->getElementOffset(i));
      break;
    }
    default: {
      if (C->getType()->isFirstClassType()) {
        llvm::GenericValue Val;
        GetConstantValue(C, Val);
        StoreValueToMemory(Val, Addr, C->getType());
      } else {
        llvm_unreachable("Unknown constant type to initialize memory "
                         "with!");
      }
      break;
    }
  }
  return;
}


void CodeEmitter::emitConstantPool(llvm::MachineConstantPool *MCP) {
  if (mpTJI->hasCustomConstantPool())
    return;

  // Constant pool address resolution is handled by the target itself in ARM
  // (TargetJITInfo::hasCustomConstantPool() returns true).
#if !defined(PROVIDE_ARM_CODEGEN)
  const std::vector<llvm::MachineConstantPoolEntry> &Constants =
    MCP->getConstants();

  if (Constants.empty())
    return;

  unsigned Size = GetConstantPoolSizeInBytes(MCP);
  unsigned Align = MCP->getConstantPoolAlignment();

  mpConstantPoolBase = allocateSpace(Size, Align);
  mpConstantPool = MCP;

  if (mpConstantPoolBase == NULL)
    return;  // out of memory

  unsigned Offset = 0;
  for (int i = 0, e = Constants.size(); i != e; i++) {
    llvm::MachineConstantPoolEntry CPE = Constants[i];
    unsigned AlignMask = CPE.getAlignment() - 1;
    Offset = (Offset + AlignMask) & ~AlignMask;

    uintptr_t CAddr = (uintptr_t) mpConstantPoolBase + Offset;
    mConstPoolAddresses.push_back(CAddr);

    if (CPE.isMachineConstantPoolEntry())
      llvm::report_fatal_error
        ("Initialize memory with machine specific constant pool"
         " entry has not been implemented!");

    InitializeConstantToMemory(CPE.Val.ConstVal, (void*) CAddr);

    llvm::Type *Ty = CPE.Val.ConstVal->getType();
    Offset += mpTD->getTypeAllocSize(Ty);
  }
#endif
  return;
}


void CodeEmitter::initJumpTableInfo(llvm::MachineJumpTableInfo *MJTI) {
  if (mpTJI->hasCustomJumpTables())
    return;

  const std::vector<llvm::MachineJumpTableEntry> &JT =
    MJTI->getJumpTables();
  if (JT.empty())
    return;

  unsigned NumEntries = 0;
  for (int i = 0, e = JT.size(); i != e; i++)
    NumEntries += JT[i].MBBs.size();

  unsigned EntrySize = MJTI->getEntrySize(*mpTD);

  mpJumpTable = MJTI;
  mpJumpTableBase = allocateSpace(NumEntries * EntrySize,
      MJTI->getEntryAlignment(*mpTD));

  return;
}


void CodeEmitter::emitJumpTableInfo(llvm::MachineJumpTableInfo *MJTI) {
  if (mpTJI->hasCustomJumpTables())
    return;

  const std::vector<llvm::MachineJumpTableEntry> &JT =
    MJTI->getJumpTables();
  if (JT.empty() || mpJumpTableBase == 0)
    return;

  bccAssert(mpTargetMachine->getRelocationModel() == llvm::Reloc::Static &&
            (MJTI->getEntrySize(*mpTD) == sizeof(mpTD /* a pointer type */)) &&
            "Cross JIT'ing?");

  // For each jump table, map each target in the jump table to the
  // address of an emitted MachineBasicBlock.
  intptr_t *SlotPtr = reinterpret_cast<intptr_t*>(mpJumpTableBase);
  for (int i = 0, ie = JT.size(); i != ie; i++) {
    const std::vector<llvm::MachineBasicBlock*> &MBBs = JT[i].MBBs;
    // Store the address of the basic block for this jump table slot in the
    // memory we allocated for the jump table in 'initJumpTableInfo'
    for (int j = 0, je = MBBs.size(); j != je; j++)
      *SlotPtr++ = getMachineBasicBlockAddress(MBBs[j]);
  }
}


void *CodeEmitter::GetPointerToGlobal(llvm::GlobalValue *V,
                                      void *Reference,
                                      bool MayNeedFarStub) {
  switch (V->getValueID()) {
    case llvm::Value::FunctionVal: {
      llvm::Function *F = (llvm::Function*) V;

      // If we have code, go ahead and return that.
      if (void *ResultPtr = GetPointerToGlobalIfAvailable(F))
        return ResultPtr;

      if (void *FnStub = GetLazyFunctionStubIfAvailable(F))
        // Return the function stub if it's already created.
        // We do this first so that:
        //   we're returning the same address for the function as any
        //   previous call.
        //
        // TODO(llvm.org): Yes, this is wrong. The lazy stub isn't
        //                 guaranteed to be close enough to call.
        return FnStub;

      // If we know the target can handle arbitrary-distance calls, try to
      //  return a direct pointer.
      if (!MayNeedFarStub) {
        //
        // x86_64 architecture may encounter the bug:
        //   http://llvm.org/bugs/show_bug.cgi?id=5201
        // which generate instruction "call" instead of "callq".
        //
        // And once the real address of stub is greater than 64-bit
        // long, the replacement will truncate to 32-bit resulting a
        // serious problem.
#if !defined(__x86_64__)
        // If this is an external function pointer, we can force the JIT
        // to 'compile' it, which really just adds it to the map.
        if (F->isDeclaration() || F->hasAvailableExternallyLinkage()) {
          return GetPointerToFunction(F, /* AbortOnFailure = */false);
          // Changing to false because wanting to allow later calls to
          // mpTJI->relocate() without aborting. For caching purpose
        }
#endif
      }

      // Otherwise, we may need a to emit a stub, and, conservatively, we
      // always do so.
      return GetLazyFunctionStub(F);
      break;
    }
    case llvm::Value::GlobalVariableVal: {
      return GetOrEmitGlobalVariable((llvm::GlobalVariable*) V);
      break;
    }
    case llvm::Value::GlobalAliasVal: {
      llvm::GlobalAlias *GA = (llvm::GlobalAlias*) V;
      const llvm::GlobalValue *GV = GA->resolveAliasedGlobal(false);

      switch (GV->getValueID()) {
        case llvm::Value::FunctionVal: {
          // TODO(all): is there's any possibility that the function is not
          // code-gen'd?
          return GetPointerToFunction(
              static_cast<const llvm::Function*>(GV),
              /* AbortOnFailure = */false);
          // Changing to false because wanting to allow later calls to
          // mpTJI->relocate() without aborting. For caching purpose
          break;
        }
        case llvm::Value::GlobalVariableVal: {
          if (void *P = mGlobalAddressMap[GV])
            return P;

          llvm::GlobalVariable *GVar = (llvm::GlobalVariable*) GV;
          EmitGlobalVariable(GVar);

          return mGlobalAddressMap[GV];
          break;
        }
        case llvm::Value::GlobalAliasVal: {
          bccAssert(false && "Alias should be resolved ultimately!");
        }
      }
      break;
    }
    default: {
      break;
    }
  }
  llvm_unreachable("Unknown type of global value!");
}


// If the specified function has been code-gen'd, return a pointer to the
// function. If not, compile it, or use a stub to implement lazy compilation
// if available.
void *CodeEmitter::GetPointerToFunctionOrStub(llvm::Function *F) {
  // If we have already code generated the function, just return the
  // address.
  if (void *Addr = GetPointerToGlobalIfAvailable(F))
    return Addr;

  // Get a stub if the target supports it.
  return GetLazyFunctionStub(F);
}


void *CodeEmitter::GetLazyFunctionStub(llvm::Function *F) {
  // If we already have a lazy stub for this function, recycle it.
  void *&Stub = mFunctionToLazyStubMap[F];
  if (Stub)
    return Stub;

  // In any cases, we should NOT resolve function at runtime (though we are
  // able to). We resolve this right now.
  void *Actual = NULL;
  if (F->isDeclaration() || F->hasAvailableExternallyLinkage()) {
    Actual = GetPointerToFunction(F, /* AbortOnFailure = */false);
    // Changing to false because wanting to allow later calls to
    // mpTJI->relocate() without aborting. For caching purpose
  }

  // Codegen a new stub, calling the actual address of the external
  // function, if it was resolved.
  llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
  startGVStub(F, SL.Size, SL.Alignment);
  Stub = mpTJI->emitFunctionStub(F, Actual, *this);
  finishGVStub();

  // We really want the address of the stub in the GlobalAddressMap for the
  // JIT, not the address of the external function.
  UpdateGlobalMapping(F, Stub);

  if (!Actual) {
    PendingFunctions.insert(F);
  } else {
#if DEBUG_OLD_JIT_DISASSEMBLER
    Disassemble(DEBUG_OLD_JIT_DISASSEMBLER_FILE,
                mpTarget, mpTargetMachine, F->getName(),
                (unsigned char const *)Stub, SL.Size);
#endif
  }

  return Stub;
}


void *CodeEmitter::GetPointerToFunction(const llvm::Function *F,
                                        bool AbortOnFailure) {
  void *Addr = GetPointerToGlobalIfAvailable(F);
  if (Addr)
    return Addr;

  bccAssert((F->isDeclaration() || F->hasAvailableExternallyLinkage()) &&
            "Internal error: only external defined function routes here!");

  // Handle the failure resolution by ourselves.
  Addr = GetPointerToNamedSymbol(F->getName().str().c_str(),
                                 /* AbortOnFailure = */ false);

  // If we resolved the symbol to a null address (eg. a weak external)
  // return a null pointer let the application handle it.
  if (Addr == NULL) {
    if (AbortOnFailure)
      llvm::report_fatal_error("Could not resolve external function "
                               "address: " + F->getName());
    else
      return NULL;
  }

  AddGlobalMapping(F, Addr);

  return Addr;
}


void *CodeEmitter::GetPointerToNamedSymbol(const std::string &Name,
                                           bool AbortOnFailure) {
  if (void *Addr = FindRuntimeFunction(Name.c_str()))
    return Addr;

  if (mpSymbolLookupFn)
    if (void *Addr = mpSymbolLookupFn(mpSymbolLookupContext, Name.c_str()))
      return Addr;

  if (AbortOnFailure)
    llvm::report_fatal_error("Program used external symbol '" + Name +
                            "' which could not be resolved!");

  return NULL;
}


// Return the address of the specified global variable, possibly emitting it
// to memory if needed. This is used by the Emitter.
void *CodeEmitter::GetOrEmitGlobalVariable(llvm::GlobalVariable *GV) {
  void *Ptr = GetPointerToGlobalIfAvailable(GV);
  if (Ptr)
    return Ptr;

  if (GV->isDeclaration() || GV->hasAvailableExternallyLinkage()) {
    // If the global is external, just remember the address.
    Ptr = GetPointerToNamedSymbol(GV->getName().str(), true);
    AddGlobalMapping(GV, Ptr);
  } else {
    // If the global hasn't been emitted to memory yet, allocate space and
    // emit it into memory.
    Ptr = GetMemoryForGV(GV);
    AddGlobalMapping(GV, Ptr);
    EmitGlobalVariable(GV);
  }

  return Ptr;
}


// This method abstracts memory allocation of global variable so that the
// JIT can allocate thread local variables depending on the target.
void *CodeEmitter::GetMemoryForGV(llvm::GlobalVariable *GV) {
  void *Ptr;

  llvm::Type *GlobalType = GV->getType()->getElementType();
  size_t S = mpTD->getTypeAllocSize(GlobalType);
  size_t A = mpTD->getPreferredAlignment(GV);

  if (GV->isThreadLocal()) {
    // We can support TLS by
    //
    //  Ptr = TJI.allocateThreadLocalMemory(S);
    //
    // But I tend not to.
    // (should we disable this in the front-end (i.e., slang)?).
    llvm::report_fatal_error
        ("Compilation of Thread Local Storage (TLS) is disabled!");

  } else if (mpTJI->allocateSeparateGVMemory()) {
    if (A <= 8) {
      Ptr = malloc(S);
    } else {
      // Allocate (S + A) bytes of memory, then use an aligned pointer
      // within that space.
      Ptr = malloc(S + A);
      unsigned int MisAligned = ((intptr_t) Ptr & (A - 1));
      Ptr = reinterpret_cast<uint8_t*>(Ptr) +
                (MisAligned ? (A - MisAligned) : 0);
    }
  } else {
    Ptr = allocateGlobal(S, A);
  }

  return Ptr;
}


void CodeEmitter::EmitGlobalVariable(llvm::GlobalVariable *GV) {
  void *GA = GetPointerToGlobalIfAvailable(GV);

  if (GV->isThreadLocal())
    llvm::report_fatal_error
        ("We don't support Thread Local Storage (TLS)!");

  if (GA == NULL) {
    // If it's not already specified, allocate memory for the global.
    GA = GetMemoryForGV(GV);
    AddGlobalMapping(GV, GA);
  }

  InitializeConstantToMemory(GV->getInitializer(), GA);

  // You can do some statistics on global variable here.
  return;
}


void *CodeEmitter::GetPointerToGVIndirectSym(llvm::GlobalValue *V, void *Reference) {
  // Make sure GV is emitted first, and create a stub containing the fully
  // resolved address.
  void *GVAddress = GetPointerToGlobal(V, Reference, false);

  // If we already have a stub for this global variable, recycle it.
  void *&IndirectSym = GlobalToIndirectSymMap[V];
  // Otherwise, codegen a new indirect symbol.
  if (!IndirectSym)
    IndirectSym = mpTJI->emitGlobalValueIndirectSym(V, GVAddress, *this);

  return IndirectSym;
}


// Return a stub for the function at the specified address.
void *CodeEmitter::GetExternalFunctionStub(void *FnAddr) {
  void *&Stub = ExternalFnToStubMap[FnAddr];
  if (Stub)
    return Stub;

  llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
  startGVStub(0, SL.Size, SL.Alignment);
  Stub = mpTJI->emitFunctionStub(0, FnAddr, *this);
  finishGVStub();

  return Stub;
}


void CodeEmitter::setTargetMachine(llvm::TargetMachine &TM) {
  mpTargetMachine = &TM;

  // Set Target
  mpTarget = &TM.getTarget();
  // Set TargetJITInfo
  mpTJI = TM.getJITInfo();
  // set TargetData
  mpTD = TM.getTargetData();

  bccAssert(!mpTJI->needsGOT() && "We don't support GOT needed target!");

  return;
}


// This callback is invoked when the specified function is about to be code
// generated.  This initializes the BufferBegin/End/Ptr fields.
void CodeEmitter::startFunction(llvm::MachineFunction &F) {
  uintptr_t ActualSize = 0;

  mpMemMgr->setMemoryWritable();

  // BufferBegin, BufferEnd and CurBufferPtr are all inherited from class
  // MachineCodeEmitter, which is the super class of the class
  // JITCodeEmitter.
  //
  // BufferBegin/BufferEnd - Pointers to the start and end of the memory
  //                         allocated for this code buffer.
  //
  // CurBufferPtr - Pointer to the next byte of memory to fill when emitting
  //                code. This is guranteed to be in the range
  //                [BufferBegin, BufferEnd].  If this pointer is at
  //                BufferEnd, it will never move due to code emission, and
  //                all code emission requests will be ignored (this is the
  //                buffer overflow condition).
  BufferBegin = CurBufferPtr =
      mpMemMgr->startFunctionBody(F.getFunction(), ActualSize);
  BufferEnd = BufferBegin + ActualSize;

  if (mpCurEmitFunction == NULL) {
    mpCurEmitFunction = new FuncInfo(); // TODO(all): Allocation check!
    mpCurEmitFunction->name = NULL;
    mpCurEmitFunction->addr = NULL;
    mpCurEmitFunction->size = 0;
  }

  // Ensure the constant pool/jump table info is at least 4-byte aligned.
  emitAlignment(16);

  emitConstantPool(F.getConstantPool());
  if (llvm::MachineJumpTableInfo *MJTI = F.getJumpTableInfo())
    initJumpTableInfo(MJTI);

  // About to start emitting the machine code for the function.
  emitAlignment(std::max(F.getFunction()->getAlignment(), 8U));

  UpdateGlobalMapping(F.getFunction(), CurBufferPtr);

  mpCurEmitFunction->addr = CurBufferPtr;

  mMBBLocations.clear();
}


// This callback is invoked when the specified function has finished code
// generation. If a buffer overflow has occurred, this method returns true
// (the callee is required to try again).
bool CodeEmitter::finishFunction(llvm::MachineFunction &F) {
  if (CurBufferPtr == BufferEnd) {
    // No enough memory
    mpMemMgr->endFunctionBody(F.getFunction(), BufferBegin, CurBufferPtr);
    return false;
  }

  if (llvm::MachineJumpTableInfo *MJTI = F.getJumpTableInfo())
    emitJumpTableInfo(MJTI);

  if (!mRelocations.empty()) {
    //ptrdiff_t BufferOffset = BufferBegin - mpMemMgr->getCodeMemBase();

    // Resolve the relocations to concrete pointers.
    for (int i = 0, e = mRelocations.size(); i != e; i++) {
      llvm::MachineRelocation &MR = mRelocations[i];
      void *ResultPtr = NULL;

      if (!MR.letTargetResolve()) {
        if (MR.isExternalSymbol()) {
          ResultPtr = GetPointerToNamedSymbol(MR.getExternalSymbol(), true);

          if (MR.mayNeedFarStub()) {
            ResultPtr = GetExternalFunctionStub(ResultPtr);
          }

        } else if (MR.isGlobalValue()) {
          ResultPtr = GetPointerToGlobal(MR.getGlobalValue(),
                                         BufferBegin
                                           + MR.getMachineCodeOffset(),
                                         MR.mayNeedFarStub());
        } else if (MR.isIndirectSymbol()) {
          ResultPtr =
              GetPointerToGVIndirectSym(
                  MR.getGlobalValue(),
                  BufferBegin + MR.getMachineCodeOffset());
        } else if (MR.isBasicBlock()) {
          ResultPtr =
              (void*) getMachineBasicBlockAddress(MR.getBasicBlock());
        } else if (MR.isConstantPoolIndex()) {
          ResultPtr =
             (void*) getConstantPoolEntryAddress(MR.getConstantPoolIndex());
        } else {
          bccAssert(MR.isJumpTableIndex() && "Unknown type of relocation");
          ResultPtr =
              (void*) getJumpTableEntryAddress(MR.getJumpTableIndex());
        }

        if (!MR.isExternalSymbol() || MR.mayNeedFarStub()) {
          // TODO(logan): Cache external symbol relocation entry.
          // Currently, we are not caching them.  But since Android
          // system is using prelink, it is not a problem.
#if 0
          // Cache the relocation result address
          mCachingRelocations.push_back(
            oBCCRelocEntry(MR.getRelocationType(),
                           MR.getMachineCodeOffset() + BufferOffset,
                           ResultPtr));
#endif
        }

        MR.setResultPointer(ResultPtr);
      }
    }

    mpTJI->relocate(BufferBegin, &mRelocations[0], mRelocations.size(),
                    mpMemMgr->getGOTBase());
  }

  mpMemMgr->endFunctionBody(F.getFunction(), BufferBegin, CurBufferPtr);
  // CurBufferPtr may have moved beyond FnEnd, due to memory allocation for
  // global variables that were referenced in the relocations.
  if (CurBufferPtr == BufferEnd)
    return false;

  // Now that we've succeeded in emitting the function.
  mpCurEmitFunction->size = CurBufferPtr - BufferBegin;

#if DEBUG_OLD_JIT_DISASSEMBLER
  // FnStart is the start of the text, not the start of the constant pool
  // and other per-function data.
  uint8_t *FnStart =
      reinterpret_cast<uint8_t*>(
          GetPointerToGlobalIfAvailable(F.getFunction()));

  // FnEnd is the end of the function's machine code.
  uint8_t *FnEnd = CurBufferPtr;
#endif

  BufferBegin = CurBufferPtr = 0;

  if (F.getFunction()->hasName()) {
    std::string const &name = F.getFunction()->getNameStr();
    mpResult->mEmittedFunctions[name] = mpCurEmitFunction;
    mpCurEmitFunction = NULL;
  }

  mRelocations.clear();
  mConstPoolAddresses.clear();

  if (mpMMI)
    mpMMI->EndFunction();

  updateFunctionStub(F.getFunction());

  // Mark code region readable and executable if it's not so already.
  mpMemMgr->setMemoryExecutable();

#if DEBUG_OLD_JIT_DISASSEMBLER
  Disassemble(DEBUG_OLD_JIT_DISASSEMBLER_FILE,
              mpTarget, mpTargetMachine, F.getFunction()->getName(),
              (unsigned char const *)FnStart, FnEnd - FnStart);
#endif

  return false;
}


void CodeEmitter::startGVStub(const llvm::GlobalValue *GV, unsigned StubSize,
                 unsigned Alignment) {
  mpSavedBufferBegin = BufferBegin;
  mpSavedBufferEnd = BufferEnd;
  mpSavedCurBufferPtr = CurBufferPtr;

  BufferBegin = CurBufferPtr = mpMemMgr->allocateStub(GV, StubSize,
                                                      Alignment);
  BufferEnd = BufferBegin + StubSize + 1;

  return;
}


void CodeEmitter::startGVStub(void *Buffer, unsigned StubSize) {
  mpSavedBufferBegin = BufferBegin;
  mpSavedBufferEnd = BufferEnd;
  mpSavedCurBufferPtr = CurBufferPtr;

  BufferBegin = CurBufferPtr = reinterpret_cast<uint8_t *>(Buffer);
  BufferEnd = BufferBegin + StubSize + 1;

  return;
}


void CodeEmitter::finishGVStub() {
  bccAssert(CurBufferPtr != BufferEnd && "Stub overflowed allocated space.");

  // restore
  BufferBegin = mpSavedBufferBegin;
  BufferEnd = mpSavedBufferEnd;
  CurBufferPtr = mpSavedCurBufferPtr;
}


// Allocates and fills storage for an indirect GlobalValue, and returns the
// address.
void *CodeEmitter::allocIndirectGV(const llvm::GlobalValue *GV,
                      const uint8_t *Buffer, size_t Size,
                      unsigned Alignment) {
  uint8_t *IndGV = mpMemMgr->allocateStub(GV, Size, Alignment);
  memcpy(IndGV, Buffer, Size);
  return IndGV;
}


// Allocate memory for a global. Unlike allocateSpace, this method does not
// allocate memory in the current output buffer, because a global may live
// longer than the current function.
void *CodeEmitter::allocateGlobal(uintptr_t Size, unsigned Alignment) {
  // Delegate this call through the memory manager.
  return mpMemMgr->allocateGlobal(Size, Alignment);
}


// This should be called by the target when a new basic block is about to be
// emitted. This way the MCE knows where the start of the block is, and can
// implement getMachineBasicBlockAddress.
void CodeEmitter::StartMachineBasicBlock(llvm::MachineBasicBlock *MBB) {
  if (mMBBLocations.size() <= (unsigned) MBB->getNumber())
    mMBBLocations.resize((MBB->getNumber() + 1) * 2);
  mMBBLocations[MBB->getNumber()] = getCurrentPCValue();
  return;
}


// Return the address of the jump table with index @Index in the function
// that last called initJumpTableInfo.
uintptr_t CodeEmitter::getJumpTableEntryAddress(unsigned Index) const {
  const std::vector<llvm::MachineJumpTableEntry> &JT =
      mpJumpTable->getJumpTables();

  bccAssert((Index < JT.size()) && "Invalid jump table index!");

  unsigned int Offset = 0;
  unsigned int EntrySize = mpJumpTable->getEntrySize(*mpTD);

  for (unsigned i = 0; i < Index; i++)
    Offset += JT[i].MBBs.size();
  Offset *= EntrySize;

  return (uintptr_t)(reinterpret_cast<uint8_t*>(mpJumpTableBase) + Offset);
}


// Return the address of the specified MachineBasicBlock, only usable after
// the label for the MBB has been emitted.
uintptr_t CodeEmitter::getMachineBasicBlockAddress(
                                        llvm::MachineBasicBlock *MBB) const {
  bccAssert(mMBBLocations.size() > (unsigned) MBB->getNumber() &&
            mMBBLocations[MBB->getNumber()] &&
            "MBB not emitted!");
  return mMBBLocations[MBB->getNumber()];
}


void CodeEmitter::updateFunctionStub(const llvm::Function *F) {
  // Get the empty stub we generated earlier.
  void *Stub;
  std::set<const llvm::Function*>::iterator I = PendingFunctions.find(F);
  if (I != PendingFunctions.end())
    Stub = mFunctionToLazyStubMap[F];
  else
    return;

  void *Addr = GetPointerToGlobalIfAvailable(F);

  bccAssert(Addr != Stub &&
            "Function must have non-stub address to be updated.");

  // Tell the target jit info to rewrite the stub at the specified address,
  // rather than creating a new one.
  llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
  startGVStub(Stub, SL.Size);
  mpTJI->emitFunctionStub(F, Addr, *this);
  finishGVStub();

#if DEBUG_OLD_JIT_DISASSEMBLER
  Disassemble(DEBUG_OLD_JIT_DISASSEMBLER_FILE,
              mpTarget, mpTargetMachine, F->getName(),
              (unsigned char const *)Stub, SL.Size);
#endif

  PendingFunctions.erase(I);
}


} // namespace bcc
