/*
 * Copyright 2015, The Android Open Source Project
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
#include "bcc/Support/Log.h"

#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>

#include <sstream>
#include <vector>

namespace {

static const bool kDebugGlobalInfo = false;

/* RSGlobalInfoPass: Embeds additional information about RenderScript global
 * variables into the Module. The 4 variables added are specified as follows:
 * 1) .rs.global_entries
 *    i32 - int
 *    Optional number of global variables.
 * 2) .rs.global_names
 *    [N * i8*] - const char *[N]
 *    Optional global variable name info. Each entry corresponds to the name
 *    of 1 of the N global variables.
 * 3) .rs.global_addresses
 *    [N * i8*] - void*[N] or void**
 *    Optional global variable address info. Each entry corresponds to the
 *    address of 1 of the N global variables.
 * 4) .rs.global_sizes
 *    [N * i32] or [N * i64] - size_t[N]
 *    Optional global variable size info. Each entry corresponds to the size
 *    of 1 of the N global variables.
 */
class RSGlobalInfoPass: public llvm::ModulePass {
private:

public:
  static char ID;

  // If true, we don't include information about immutable global variables
  // in our various exported data structures.
  bool mSkipConstants;

  RSGlobalInfoPass(bool pSkipConstants = false)
    : ModulePass (ID), mSkipConstants(pSkipConstants) {
  }

  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    // This pass does not use any other analysis passes, but it does
    // add new global variables.
  }

  bool runOnModule(llvm::Module &M) override {
    std::vector<llvm::Constant *> GVAddresses;
    std::vector<llvm::Constant *> GVNames;
    std::vector<uint32_t> GVSizes32;
    std::vector<uint64_t> GVSizes64;

    const llvm::DataLayout &DL = M.getDataLayout();
    const size_t PointerSizeInBits = DL.getPointerSizeInBits();

    bccAssert(PointerSizeInBits == 32 || PointerSizeInBits == 64);

    int GlobalNumber = 0;

    for (auto &GV : M.globals()) {
      // Skip constant variables if we were configured to do so.
      if (mSkipConstants && GV.isConstant()) {
        continue;
      }

      // In LLVM, an instance of GlobalVariable is actually a Value
      // corresponding to the address of it.
      GVAddresses.push_back(&GV);

      // Since these are all global variables, their type is actually a
      // pointer to the underlying data. We can extract the total underlying
      // storage size by looking at the first contained type.
      auto TypeSize = DL.getTypeAllocSize(GV.getType()->getContainedType(0));
      if (PointerSizeInBits == 32) {
        GVSizes32.push_back(TypeSize);
      } else {
        GVSizes64.push_back(TypeSize);
      }
    }

    // Create the new strings for storing the names of the global variables.
    // This has to be done as a separate pass (over the original global
    // variables), because these strings are new global variables themselves.
    for (auto GVA : GVAddresses) {
      llvm::Constant *C =
          llvm::ConstantDataArray::getString(M.getContext(), GVA->getName());
      std::stringstream VarName;
      VarName << ".rs.name_str_" << GlobalNumber++;
      llvm::Value *V = M.getOrInsertGlobal(VarName.str(), C->getType());
      llvm::GlobalVariable *VarAsStr = llvm::dyn_cast<llvm::GlobalVariable>(V);
      VarAsStr->setInitializer(C);
      VarAsStr->setConstant(true);
      VarAsStr->setLinkage(llvm::GlobalValue::PrivateLinkage);
      VarAsStr->setUnnamedAddr(true);
      GVNames.push_back(VarAsStr);
    }

    if (PointerSizeInBits == 32) {
      bccAssert(GVAddresses.size() == GVSizes32.size());
      bccAssert(GVSizes64.size() == 0);
    } else {
      bccAssert(GVSizes32.size() == 0);
      bccAssert(GVAddresses.size() == GVSizes64.size());
    }
    size_t NumGlobals = GVAddresses.size();

    // i32
    llvm::Type *Int32Ty = llvm::Type::getInt32Ty(M.getContext());

    // i32 or i64 depending on our actual size_t
    llvm::Type *SizeTy = llvm::Type::getIntNTy(M.getContext(),
                                               PointerSizeInBits);

    // i8* - LLVM uses this to represent void* and char*
    llvm::Type *VoidPtrTy = llvm::Type::getInt8PtrTy(M.getContext());

    // [NumGlobals * i8*]
    llvm::ArrayType *VoidPtrArrayTy = llvm::ArrayType::get(VoidPtrTy,
                                                           NumGlobals);
    // [NumGlobals * i32] or [NumGlobals * i64]
    llvm::ArrayType *SizeArrayTy = llvm::ArrayType::get(SizeTy, NumGlobals);

    // 1) @.rs.global_entries = constant i32 NumGlobals
    llvm::Value *V = M.getOrInsertGlobal(".rs.global_entries", Int32Ty);
    llvm::GlobalVariable *GlobalEntries =
        llvm::dyn_cast<llvm::GlobalVariable>(V);
    llvm::Constant *GlobalEntriesInit =
        llvm::ConstantInt::get(Int32Ty, NumGlobals);
    GlobalEntries->setInitializer(GlobalEntriesInit);
    GlobalEntries->setConstant(true);

    // 2) @.rs.global_names = constant [N * i8*] [...]
    V = M.getOrInsertGlobal(".rs.global_names", VoidPtrArrayTy);
    llvm::GlobalVariable *GlobalNames =
        llvm::dyn_cast<llvm::GlobalVariable>(V);
    llvm::Constant *GlobalNamesInit =
        llvm::ConstantArray::get(VoidPtrArrayTy, GVNames);
    GlobalNames->setInitializer(GlobalNamesInit);
    GlobalNames->setConstant(true);

    // 3) @.rs.global_addresses = constant [N * i8*] [...]
    V = M.getOrInsertGlobal(".rs.global_addresses", VoidPtrArrayTy);
    llvm::GlobalVariable *GlobalAddresses =
        llvm::dyn_cast<llvm::GlobalVariable>(V);
    llvm::Constant *GlobalAddressesInit =
        llvm::ConstantArray::get(VoidPtrArrayTy, GVAddresses);
    GlobalAddresses->setInitializer(GlobalAddressesInit);
    GlobalAddresses->setConstant(true);


    // 4) @.rs.global_sizes = constant [N * i32 or i64] [...]
    V = M.getOrInsertGlobal(".rs.global_sizes", SizeArrayTy);
    llvm::GlobalVariable *GlobalSizes =
        llvm::dyn_cast<llvm::GlobalVariable>(V);
    llvm::Constant *GlobalSizesInit;
    if (PointerSizeInBits == 32) {
      GlobalSizesInit = llvm::ConstantDataArray::get(M.getContext(), GVSizes32);
    } else {
      GlobalSizesInit = llvm::ConstantDataArray::get(M.getContext(), GVSizes64);
    }
    GlobalSizes->setInitializer(GlobalSizesInit);
    GlobalSizes->setConstant(true);

    if (kDebugGlobalInfo) {
      GlobalEntries->dump();
      GlobalNames->dump();
      GlobalAddresses->dump();
      GlobalSizes->dump();
    }

    // Upon completion, this pass has always modified the Module.
    return true;
  }
};

}

char RSGlobalInfoPass::ID = 0;

static llvm::RegisterPass<RSGlobalInfoPass> X("embed-rs-global-info",
  "Embed additional information about RenderScript global variables");

namespace bcc {

llvm::ModulePass * createRSGlobalInfoPass(bool pSkipConstants) {
  return new RSGlobalInfoPass(pSkipConstants);
}

}
