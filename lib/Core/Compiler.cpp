/*
 * Copyright 2010-2012, The Android Open Source Project
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

#include "bcc/Compiler.h"

#include <llvm/Analysis/Passes.h>
#include <llvm/CodeGen/RegAllocRegistry.h>
#include <llvm/IR/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Scalar.h>

#include "bcc/Assert.h"
#include "bcc/Renderscript/RSExecutable.h"
#include "bcc/Renderscript/RSScript.h"
#include "bcc/Renderscript/RSTransforms.h"
#include "bcc/Script.h"
#include "bcc/Source.h"
#include "bcc/Support/CompilerConfig.h"
#include "bcc/Support/Log.h"
#include "bcc/Support/OutputFile.h"
#include "bcinfo/MetadataExtractor.h"

#include <string>

using namespace bcc;

const char *Compiler::GetErrorString(enum ErrorCode pErrCode) {
  switch (pErrCode) {
  case kSuccess:
    return "Successfully compiled.";
  case kInvalidConfigNoTarget:
    return "Invalid compiler config supplied (getTarget() returns nullptr.) "
           "(missing call to CompilerConfig::initialize()?)";
  case kErrCreateTargetMachine:
    return "Failed to create llvm::TargetMachine.";
  case kErrSwitchTargetMachine:
    return  "Failed to switch llvm::TargetMachine.";
  case kErrNoTargetMachine:
    return "Failed to compile the script since there's no available "
           "TargetMachine. (missing call to Compiler::config()?)";
  case kErrDataLayoutNoMemory:
    return "Out of memory when create DataLayout during compilation.";
  case kErrMaterialization:
    return "Failed to materialize the module.";
  case kErrInvalidOutputFileState:
    return "Supplied output file was invalid (in the error state.)";
  case kErrPrepareOutput:
    return "Failed to prepare file for output.";
  case kPrepareCodeGenPass:
    return "Failed to construct pass list for code-generation.";
  case kErrCustomPasses:
    return "Error occurred while adding custom passes.";
  case kErrInvalidSource:
    return "Error loading input bitcode";
  }

  // This assert should never be reached as the compiler verifies that the
  // above switch coveres all enum values.
  assert(false && "Unknown error code encountered");
  return  "";
}

//===----------------------------------------------------------------------===//
// Instance Methods
//===----------------------------------------------------------------------===//
Compiler::Compiler() : mTarget(nullptr), mEnableOpt(true) {
  return;
}

Compiler::Compiler(const CompilerConfig &pConfig) : mTarget(nullptr),
                                                    mEnableOpt(true) {
  const std::string &triple = pConfig.getTriple();

  enum ErrorCode err = config(pConfig);
  if (err != kSuccess) {
    ALOGE("%s (%s, features: %s)", GetErrorString(err),
          triple.c_str(), pConfig.getFeatureString().c_str());
    return;
  }

  return;
}

enum Compiler::ErrorCode Compiler::config(const CompilerConfig &pConfig) {
  if (pConfig.getTarget() == nullptr) {
    return kInvalidConfigNoTarget;
  }

  llvm::TargetMachine *new_target =
      (pConfig.getTarget())->createTargetMachine(pConfig.getTriple(),
                                                 pConfig.getCPU(),
                                                 pConfig.getFeatureString(),
                                                 pConfig.getTargetOptions(),
                                                 pConfig.getRelocationModel(),
                                                 pConfig.getCodeModel(),
                                                 pConfig.getOptimizationLevel());

  if (new_target == nullptr) {
    return ((mTarget != nullptr) ? kErrSwitchTargetMachine :
                                   kErrCreateTargetMachine);
  }

  // Replace the old TargetMachine.
  delete mTarget;
  mTarget = new_target;

  // Adjust register allocation policy according to the optimization level.
  //  createFastRegisterAllocator: fast but bad quality
  //  createLinearScanRegisterAllocator: not so fast but good quality
  if ((pConfig.getOptimizationLevel() == llvm::CodeGenOpt::None)) {
    llvm::RegisterRegAlloc::setDefault(llvm::createFastRegisterAllocator);
  } else {
    llvm::RegisterRegAlloc::setDefault(llvm::createGreedyRegisterAllocator);
  }

  return kSuccess;
}

Compiler::~Compiler() {
  delete mTarget;
}

enum Compiler::ErrorCode Compiler::runPasses(Script &pScript,
                                             llvm::raw_ostream &pResult) {
  // Pass manager for link-time optimization
  llvm::PassManager passes;

  // Empty MCContext.
  llvm::MCContext *mc_context = nullptr;

  // Prepare DataLayout target data from Module
  llvm::DataLayoutPass *data_layout_pass =
    new (std::nothrow) llvm::DataLayoutPass(*mTarget->getDataLayout());

  if (data_layout_pass == nullptr) {
    return kErrDataLayoutNoMemory;
  }

  // Add DataLayout to the pass manager.
  passes.add(data_layout_pass);

  // Add our custom passes.
  if (!addCustomPasses(pScript, passes)) {
    return kErrCustomPasses;
  }

  if (mTarget->getOptLevel() == llvm::CodeGenOpt::None) {
    passes.add(llvm::createGlobalOptimizerPass());
    passes.add(llvm::createConstantMergePass());

  } else {
    // FIXME: Figure out which passes should be executed.
    llvm::PassManagerBuilder Builder;
    Builder.populateLTOPassManager(passes,
                                   /*Internalize*/ false,
                                   /*RunInliner*/  true);
  }

  // Add passes to the pass manager to emit machine code through MC layer.
  if (mTarget->addPassesToEmitMC(passes, mc_context, pResult,
                                 /* DisableVerify */false)) {
    return kPrepareCodeGenPass;
  }

  // Execute the passes.
  passes.run(pScript.getSource().getModule());

  return kSuccess;
}

enum Compiler::ErrorCode Compiler::compile(Script &pScript,
                                           llvm::raw_ostream &pResult,
                                           llvm::raw_ostream *IRStream) {
  llvm::Module &module = pScript.getSource().getModule();
  enum ErrorCode err;

  if (mTarget == nullptr) {
    return kErrNoTargetMachine;
  }

  const std::string &triple = module.getTargetTriple();
  const llvm::DataLayout *dl = getTargetMachine().getDataLayout();
  unsigned int pointerSize = dl->getPointerSizeInBits();
  if (triple == "armv7-none-linux-gnueabi") {
    if (pointerSize != 32) {
      return kErrInvalidSource;
    }
  } else if (triple == "aarch64-none-linux-gnueabi") {
    if (pointerSize != 64) {
      return kErrInvalidSource;
    }
  } else {
    return kErrInvalidSource;
  }

  // Materialize the bitcode module.
  if (module.getMaterializer() != nullptr) {
    // A module with non-null materializer means that it is a lazy-load module.
    // Materialize it now via invoking MaterializeAllPermanently(). This
    // function returns false when the materialization is successful.
    std::error_code ec = module.materializeAllPermanently();
    if (ec) {
      ALOGE("Failed to materialize the module `%s'! (%s)",
            module.getModuleIdentifier().c_str(), ec.message().c_str());
      return kErrMaterialization;
    }
  }

  if ((err = runPasses(pScript, pResult)) != kSuccess) {
    return err;
  }

  if (IRStream) {
    *IRStream << module;
  }

  return kSuccess;
}

enum Compiler::ErrorCode Compiler::compile(Script &pScript,
                                           OutputFile &pResult,
                                           llvm::raw_ostream *IRStream) {
  // Check the state of the specified output file.
  if (pResult.hasError()) {
    return kErrInvalidOutputFileState;
  }

  // Open the output file decorated in llvm::raw_ostream.
  llvm::raw_ostream *out = pResult.dup();
  if (out == nullptr) {
    return kErrPrepareOutput;
  }

  // Delegate the request.
  enum Compiler::ErrorCode err = compile(pScript, *out, IRStream);

  // Close the output before return.
  delete out;

  return err;
}

bool Compiler::addInternalizeSymbolsPass(Script &pScript, llvm::PassManager &pPM) {
  // Add a pass to internalize the symbols that don't need to have global
  // visibility.
  RSScript &script = static_cast<RSScript &>(pScript);
  llvm::Module &module = script.getSource().getModule();
  bcinfo::MetadataExtractor me(&module);
  if (!me.extract()) {
    bccAssert(false && "Could not extract metadata for module!");
    return false;
  }

  // The vector contains the symbols that should not be internalized.
  std::vector<const char *> export_symbols;

  // Special RS functions should always be global symbols.
  const char **special_functions = RSExecutable::SpecialFunctionNames;
  while (*special_functions != nullptr) {
    export_symbols.push_back(*special_functions);
    special_functions++;
  }

  // Visibility of symbols appeared in rs_export_var and rs_export_func should
  // also be preserved.
  size_t exportVarCount = me.getExportVarCount();
  size_t exportFuncCount = me.getExportFuncCount();
  size_t exportForEachCount = me.getExportForEachSignatureCount();
  const char **exportVarNameList = me.getExportVarNameList();
  const char **exportFuncNameList = me.getExportFuncNameList();
  const char **exportForEachNameList = me.getExportForEachNameList();
  size_t i;

  for (i = 0; i < exportVarCount; ++i) {
    export_symbols.push_back(exportVarNameList[i]);
  }

  for (i = 0; i < exportFuncCount; ++i) {
    export_symbols.push_back(exportFuncNameList[i]);
  }

  // Expanded foreach functions should not be internalized, too.
  // expanded_foreach_funcs keeps the .expand version of the kernel names
  // around until createInternalizePass() is finished making its own
  // copy of the visible symbols.
  std::vector<std::string> expanded_foreach_funcs;
  for (i = 0; i < exportForEachCount; ++i) {
    expanded_foreach_funcs.push_back(
        std::string(exportForEachNameList[i]) + ".expand");
  }

  for (i = 0; i < exportForEachCount; i++) {
      export_symbols.push_back(expanded_foreach_funcs[i].c_str());
  }

  pPM.add(llvm::createInternalizePass(export_symbols));

  return true;
}

bool Compiler::addExpandForEachPass(Script &pScript, llvm::PassManager &pPM) {
  // Script passed to RSCompiler must be a RSScript.
  RSScript &script = static_cast<RSScript &>(pScript);

  // Expand ForEach on CPU path to reduce launch overhead.
  bool pEnableStepOpt = true;
  pPM.add(createRSForEachExpandPass(pEnableStepOpt));
  if (script.getEmbedInfo())
    pPM.add(createRSEmbedInfoPass());

  return true;
}

bool Compiler::addCustomPasses(Script &pScript, llvm::PassManager &pPM) {
  if (!addExpandForEachPass(pScript, pPM))
    return false;

  if (!addInternalizeSymbolsPass(pScript, pPM))
    return false;

  return true;
}
