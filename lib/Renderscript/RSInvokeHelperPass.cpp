/*
 * Copyright 2014, The Android Open Source Project
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

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include "bcc/Config/Config.h"
#include "bcc/Support/Log.h"

#include "bcinfo/MetadataExtractor.h"

using namespace bcc;

namespace {

class RSInvokeHelperPass : public llvm::FunctionPass {
private:
  static char ID;

  llvm::StructType* rsAllocationType;
  llvm::StructType* rsElementType;
  llvm::StructType* rsSamplerType;
  llvm::StructType* rsScriptType;
  llvm::StructType* rsTypeType;

  llvm::Constant* rsAllocationSetObj;
  llvm::Constant* rsElementSetObj;
  llvm::Constant* rsSamplerSetObj;
  llvm::Constant* rsScriptSetObj;
  llvm::Constant* rsTypeSetObj;


public:
  RSInvokeHelperPass()
    : FunctionPass(ID) {

    }

  virtual bool doInitialization(llvm::Module &M) override {
    llvm::FunctionType * SetObjType = nullptr;
    llvm::SmallVector<llvm::Type*, 4> rsBaseObj;
    rsBaseObj.append(4, llvm::Type::getInt64PtrTy(M.getContext()));

    rsAllocationType = llvm::StructType::create(rsBaseObj, "struct.rs_allocation");
    rsElementType = llvm::StructType::create(rsBaseObj, "struct.rs_element");
    rsSamplerType = llvm::StructType::create(rsBaseObj, "struct.rs_sampler");
    rsScriptType = llvm::StructType::create(rsBaseObj, "struct.rs_script");
    rsTypeType = llvm::StructType::create(rsBaseObj, "struct.rs_type");

    llvm::SmallVector<llvm::Value*, 1> SetObjParams;
    llvm::SmallVector<llvm::Type*, 2> SetObjTypeParams;

    // get rsSetObject(rs_allocation*, rs_allocation*)
    // according to AArch64 calling convention, these are both pointers because of the size of the struct
    SetObjTypeParams.push_back(rsAllocationType->getPointerTo());
    SetObjTypeParams.push_back(rsAllocationType->getPointerTo());
    SetObjType = llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()), SetObjTypeParams, false);
    rsAllocationSetObj = M.getOrInsertFunction("_Z11rsSetObjectP13rs_allocationS_", SetObjType);
    SetObjTypeParams.clear();

    SetObjTypeParams.push_back(rsElementType->getPointerTo());
    SetObjTypeParams.push_back(rsElementType->getPointerTo());
    SetObjType = llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()), SetObjTypeParams, false);
    rsElementSetObj = M.getOrInsertFunction("_Z11rsSetObjectP10rs_elementS_", SetObjType);
    SetObjTypeParams.clear();

    SetObjTypeParams.push_back(rsSamplerType->getPointerTo());
    SetObjTypeParams.push_back(rsSamplerType->getPointerTo());
    SetObjType = llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()), SetObjTypeParams, false);
    rsSamplerSetObj = M.getOrInsertFunction("_Z11rsSetObjectP10rs_samplerS_", SetObjType);
    SetObjTypeParams.clear();

    SetObjTypeParams.push_back(rsScriptType->getPointerTo());
    SetObjTypeParams.push_back(rsScriptType->getPointerTo());
    SetObjType = llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()), SetObjTypeParams, false);
    rsScriptSetObj = M.getOrInsertFunction("_Z11rsSetObjectP9rs_scriptS_", SetObjType);
    SetObjTypeParams.clear();

    SetObjTypeParams.push_back(rsTypeType->getPointerTo());
    SetObjTypeParams.push_back(rsTypeType->getPointerTo());
    SetObjType = llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()), SetObjTypeParams, false);
    rsTypeSetObj = M.getOrInsertFunction("_Z11rsSetObjectP7rs_typeS_", SetObjType);
    SetObjTypeParams.clear();

    return true;
  }

  bool insertSetObjectHelper(llvm::CallInst *Call, llvm::Value *V, llvm::StringRef StructName) {
    llvm::Constant *SetObj = nullptr;
    if (StructName.equals(rsAllocationType->getName())) {
      SetObj = rsAllocationSetObj;
    } else if (StructName.equals(rsElementType->getName())) {
      SetObj = rsElementSetObj;
    } else if (StructName.equals(rsSamplerType->getName())) {
      SetObj = rsSamplerSetObj;
    } else if (StructName.equals(rsScriptType->getName())) {
      SetObj = rsScriptSetObj;
    } else if (StructName.equals(rsTypeType->getName())) {
      SetObj = rsTypeSetObj;
    } else {
      return false; // this is for graphics types and matrices; do nothing
    }

    llvm::SmallVector<llvm::Value*, 2> SetObjParams;
    SetObjParams.push_back(V);
    SetObjParams.push_back(V);

    llvm::CallInst::Create(SetObj, SetObjParams, "", Call);
    return true;
  }


  // this only modifies .helper functions that take certain RS base object types
  virtual bool runOnFunction(llvm::Function &F) override {
    if (!F.getName().startswith(".helper"))
      return false;

    bool changed = false;
    const llvm::Function::ArgumentListType &argList(F.getArgumentList());
    bool containsBaseObj = false;

    // .helper methods should have one arg only, an anonymous struct
    // that struct may contain BaseObjs
    for (auto arg = argList.begin(); arg != argList.end(); arg++) {
      llvm::Type *argType = arg->getType();
      if (!argType->isPointerTy() || !argType->getPointerElementType()->isStructTy())
        continue;

      llvm::StructType *argStructType = llvm::dyn_cast<llvm::StructType>(argType->getPointerElementType());

      for (unsigned int i = 0; i < argStructType->getNumElements(); i++) {
        llvm::Type *currentType = argStructType->getElementType(i);
        if (currentType->isStructTy() && currentType->getStructName().startswith("struct.rs_")) {
          containsBaseObj = true;
        }
      }
      break;
    }


    if (containsBaseObj) {
      // modify the thing that should not be
      auto &BBList(F.getBasicBlockList());
      for (auto &BB : BBList) {
        auto &InstList(BB.getInstList());
        for (auto &Inst : InstList) {
          // don't care about anything except call instructions that we didn't already add
          if (llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(&Inst)) {
            for (unsigned int i = 0; i < call->getNumArgOperands(); i++) {
              llvm::Value *V = call->getArgOperand(i);
              if (V->getType()->isPointerTy() && V->getType()->getPointerElementType()->isStructTy() &&
                  V->getType()->getPointerElementType()->getStructName().startswith("struct.rs_")) {
                // get just the object type name with no prefix or suffix
                size_t LastDot = V->getType()->getPointerElementType()->getStructName().rfind('.');
                llvm::StringRef StructName = V->getType()->getPointerElementType()->getStructName().slice(0, LastDot);

                // generate the new call instruction and insert it
                changed |= insertSetObjectHelper(call, V, StructName);
              }
            }
          }
        }
      }
    }

    return changed;
  }

  virtual const char *getPassName() const {
    return ".helper method expansion for large RS objects";
  }
}; // end RSInvokeHelperPass class
} // end anonymous namespace

char RSInvokeHelperPass::ID = 0;

namespace bcc {

llvm::FunctionPass *
createRSInvokeHelperPass(){
  return new RSInvokeHelperPass();
}

}
