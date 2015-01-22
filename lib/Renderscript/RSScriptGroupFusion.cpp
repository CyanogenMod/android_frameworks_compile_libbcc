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

#include "bcc/Renderscript/RSScriptGroupFusion.h"

#include "bcc/Assert.h"
#include "bcc/BCCContext.h"
#include "bcc/Renderscript/RSMetadata.h"
#include "bcc/Renderscript/RSScript.h"
#include "bcc/Source.h"
#include "bcc/Support/Log.h"
#include "bcinfo/MetadataExtractor.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <string>

using llvm::Function;

using std::map;
using std::pair;
using std::string;

namespace bcc {

namespace {

struct SourceCompare {
  bool operator()(const Source* lhs, const Source* rhs) const {
    return lhs->getName().compare(rhs->getName()) < 0;
  }
};

typedef map<const Source*,
            map<int, pair<const Function*, int>>, SourceCompare> SlotMap;

const Function* getFunction(const Source* source, const int slot) {
  const llvm::Module* module = &source->getModule();
  bcinfo::MetadataExtractor metadata(module);
  if (!metadata.extract()) {
    return nullptr;
  }
  const char* functionName = metadata.getExportForEachNameList()[slot];
  return module->getFunction(functionName);
}

llvm::Type* getArgType(const Source* source, const int slot) {
  const Function* func = getFunction(source, slot);
  if (func == nullptr) {
    return nullptr;
  }
  auto argIter = func->getArgumentList().begin();
  return argIter->getType();
}

llvm::Type* getReturnType(const Source* source, const int slot) {
  const Function* func = getFunction(source, slot);
  if (func == nullptr) {
    return nullptr;
  }
  return func->getReturnType();
}

pair<const Function*, int> getFunction(
    SlotMap& slotMap, llvm::Linker& linker, const Source* source,
    const int slot) {
  auto it1 = slotMap.find(source);
  if (it1 == slotMap.end()) {
    llvm::Module* module = (llvm::Module*)&source->getModule();
    if (linker.linkInModule(module)) {
      ALOGE("Linking for module in source %s failed.",
            source->getName().c_str());
      return std::make_pair(nullptr, 0);
    }
  }
  auto &functions = slotMap[source];

  auto it2 = functions.find(slot);
  if (it2 == functions.end()) {
    bcinfo::MetadataExtractor metadata(&source->getModule());
    metadata.extract();
    const char* functionName = metadata.getExportForEachNameList()[slot];
    if (functionName == nullptr) {
      return std::make_pair(nullptr, 0);
    }

    if (metadata.getExportForEachInputCountList()[slot] > 1) {
      // TODO: Handle multiple inputs.
      ALOGW("Kernel %s has multiple inputs", functionName);
      return std::make_pair(nullptr, 0);
    }

    const uint32_t signature = metadata.getExportForEachSignatureList()[slot];
    int dim = 0;
    if (metadata.hasForEachSignatureX(signature)) {
      dim++;
    }
    if (metadata.hasForEachSignatureY(signature)) {
      dim++;
    }

    const Function* function = linker.getModule()->getFunction(functionName);
    it2 = functions.emplace(slot, std::make_pair(function, dim)).first;
  }
  return it2->second;
}

}  // anonymous namespace

llvm::Module*
fuseKernels(bcc::BCCContext& Context,
            const std::vector<const Source *>& sources,
            const std::vector<int>& slots) {
  bccAssert(sources.size() > 1 && "Need at least two kernels for kernel merging");
  bccAssert(sources.size() == slots.size() && "sources and slots differ in size");

  llvm::LLVMContext& context = Context.getLLVMContext();
  std::unique_ptr<llvm::Module> module(
      new llvm::Module("Merged ScriptGroup", context));
  if (module == nullptr) {
    ALOGE("out of memory while creating module for fused kernels");
    return nullptr;
  }
  llvm::Linker linker(module.get());
  SlotMap slotMap;

  llvm::Type* inputType = getArgType(sources.front(), slots.front());
  if (inputType == nullptr) {
    return nullptr;
  }
  llvm::Type* returnType = getReturnType(sources.back(), slots.back());
  if (returnType == nullptr) {
    return nullptr;
  }
  llvm::Type* I32Ty = llvm::IntegerType::get(context, 32);
  Function* fusedKernel =
      (Function*)(module->getOrInsertFunction(
          "__rs_fused_kernels", returnType, inputType, I32Ty, I32Ty, nullptr));

  llvm::BasicBlock* block = llvm::BasicBlock::Create(context, "entry",
                                                     fusedKernel);
  llvm::IRBuilder<> builder(block);

  Function::arg_iterator argIter = fusedKernel->arg_begin();
  llvm::Value* dataElement = argIter++;
  dataElement->setName("DataIn");
  llvm::Value* X = argIter++;
  X->setName("x");
  llvm::Value* Y = argIter++;
  Y->setName("y");

  auto slotIter = slots.begin();
  for (const Source* source : sources) {
    int slot = *slotIter++;

    const auto& p = getFunction(slotMap, linker, source, slot);
    const Function* function = p.first;
    if (function == nullptr) {
      return nullptr;
    }
    const int dim = p.second;

    std::vector<llvm::Value*> args;
    args.push_back(dataElement);
    if (dim > 0) {
      args.push_back(X);
      if (dim > 1) {
        args.push_back(Y);
      }
    }

    dataElement = builder.CreateCall((llvm::Value*)function, args);
  }

  builder.CreateRet(dataElement);

  bcc::RSMetadata metadata(*module);
  metadata.deleteAll();
  metadata.markForEachFunction(*fusedKernel, bcc::RSMetadata::FOREACH_KERNEL
                               | bcc::RSMetadata::FOREACH_IN
                               | bcc::RSMetadata::FOREACH_OUT
                               | bcc::RSMetadata::FOREACH_X
                               | bcc::RSMetadata::FOREACH_Y);

  return module.release();
}

}  // namespace bcc
