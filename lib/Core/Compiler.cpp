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

#include "bcc/Script.h"
#include "bcc/Source.h"
#include "bcc/Support/CompilerConfig.h"
#include "bcc/Support/Log.h"
#include "bcc/Support/OutputFile.h"

using namespace bcc;

const char *Compiler::GetErrorString(enum ErrorCode pErrCode) {
  static const char *ErrorString[] = {
    /* kSuccess */
    "Successfully compiled.",
    /* kInvalidConfigNoTarget */
    "Invalid compiler config supplied (getTarget() returns NULL.) "
    "(missing call to CompilerConfig::initialize()?)",
    /* kErrCreateTargetMachine */
    "Failed to create llvm::TargetMachine.",
    /* kErrSwitchTargetMachine */
    "Failed to switch llvm::TargetMachine.",
    /* kErrNoTargetMachine */
    "Failed to compile the script since there's no available TargetMachine."
    " (missing call to Compiler::config()?)",
    /* kErrDataLayoutNoMemory */
    "Out of memory when create DataLayout during compilation.",
    /* kErrMaterialization */
    "Failed to materialize the module.",
    /* kErrInvalidOutputFileState */
    "Supplied output file was invalid (in the error state.)",
    /* kErrPrepareOutput */
    "Failed to prepare file for output.",
    /* kPrepareCodeGenPass */
    "Failed to construct pass list for code-generation.",

    /* kErrHookBeforeAddLTOPasses */
    "Error occurred during beforeAddLTOPasses() in subclass.",
    /* kErrHookAfterAddLTOPasses */
    "Error occurred during afterAddLTOPasses() in subclass.",
    /* kErrHookBeforeExecuteLTOPasses */
    "Error occurred during beforeExecuteLTOPasses() in subclass.",
    /* kErrHookAfterExecuteLTOPasses */
    "Error occurred during afterExecuteLTOPasses() in subclass.",

    /* kErrHookBeforeAddCodeGenPasses */
    "Error occurred during beforeAddCodeGenPasses() in subclass.",
    /* kErrHookAfterAddCodeGenPasses */
    "Error occurred during afterAddCodeGenPasses() in subclass.",
    /* kErrHookBeforeExecuteCodeGenPasses */
    "Error occurred during beforeExecuteCodeGenPasses() in subclass.",
    /* kErrHookAfterExecuteCodeGenPasses */
    "Error occurred during afterExecuteCodeGenPasses() in subclass.",

    /* kMaxErrorCode */
    "(Unknown error code)"
  };

  if (pErrCode > kMaxErrorCode) {
    pErrCode = kMaxErrorCode;
  }

  return ErrorString[ static_cast<size_t>(pErrCode) ];
}

//===----------------------------------------------------------------------===//
// Instance Methods
//===----------------------------------------------------------------------===//
Compiler::Compiler() : mTarget(NULL), mEnableLTO(true) {
  return;
}

Compiler::Compiler(const CompilerConfig &pConfig) : mTarget(NULL),
                                                    mEnableLTO(true) {
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
  if (pConfig.getTarget() == NULL) {
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

  if (new_target == NULL) {
    return ((mTarget != NULL) ? kErrSwitchTargetMachine :
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

  // Relax all machine instructions.
  mTarget->setMCRelaxAll(true);

  return kSuccess;
}

Compiler::~Compiler() {
  delete mTarget;
}

enum Compiler::ErrorCode Compiler::runLTO(Script &pScript) {
  llvm::DataLayout *data_layout = NULL;

  // Pass manager for link-time optimization
  llvm::PassManager lto_passes;

  // Prepare DataLayout target data from Module
  data_layout = new (std::nothrow) llvm::DataLayout(*mTarget->getDataLayout());
  if (data_layout == NULL) {
    return kErrDataLayoutNoMemory;
  }

  // Add DataLayout to the pass manager.
  lto_passes.add(data_layout);

  // Invokde "beforeAddLTOPasses" before adding the first pass.
  if (!beforeAddLTOPasses(pScript, lto_passes)) {
    return kErrHookBeforeAddLTOPasses;
  }

  if (mTarget->getOptLevel() == llvm::CodeGenOpt::None) {
    lto_passes.add(llvm::createGlobalOptimizerPass());
    lto_passes.add(llvm::createConstantMergePass());
  } else {
    // FIXME: Figure out which passes should be executed.
    llvm::PassManagerBuilder Builder;
    Builder.populateLTOPassManager(lto_passes, /*Internalize*/false,
                                   /*RunInliner*/true);
  }

  // Invokde "afterAddLTOPasses" after pass manager finished its
  // construction.
  if (!afterAddLTOPasses(pScript, lto_passes)) {
    return kErrHookAfterAddLTOPasses;
  }

  // Invokde "beforeExecuteLTOPasses" before executing the passes.
  if (!beforeExecuteLTOPasses(pScript, lto_passes)) {
    return kErrHookBeforeExecuteLTOPasses;
  }

  lto_passes.run(pScript.getSource().getModule());

  // Invokde "afterExecuteLTOPasses" before returning.
  if (!afterExecuteLTOPasses(pScript)) {
    return kErrHookAfterExecuteLTOPasses;
  }

  return kSuccess;
}

enum Compiler::ErrorCode Compiler::runCodeGen(Script &pScript,
                                              llvm::raw_ostream &pResult) {
  llvm::DataLayout *data_layout;
  llvm::MCContext *mc_context = NULL;

  // Create pass manager for MC code generation.
  llvm::PassManager codegen_passes;

  // Prepare DataLayout target data from Module
  data_layout = new (std::nothrow) llvm::DataLayout(*mTarget->getDataLayout());
  if (data_layout == NULL) {
    return kErrDataLayoutNoMemory;
  }

  // Add DataLayout to the pass manager.
  codegen_passes.add(data_layout);

  // Invokde "beforeAddCodeGenPasses" before adding the first pass.
  if (!beforeAddCodeGenPasses(pScript, codegen_passes)) {
    return kErrHookBeforeAddCodeGenPasses;
  }

  // Add passes to the pass manager to emit machine code through MC layer.
  if (mTarget->addPassesToEmitMC(codegen_passes, mc_context, pResult,
                                 /* DisableVerify */false)) {
    return kPrepareCodeGenPass;
  }

  // Invokde "afterAddCodeGenPasses" after pass manager finished its
  // construction.
  if (!afterAddCodeGenPasses(pScript, codegen_passes)) {
    return kErrHookAfterAddCodeGenPasses;
  }

  // Invokde "beforeExecuteCodeGenPasses" before executing the passes.
  if (!beforeExecuteCodeGenPasses(pScript, codegen_passes)) {
    return kErrHookBeforeExecuteCodeGenPasses;
  }

  // Execute the pass.
  codegen_passes.run(pScript.getSource().getModule());

  // Invokde "afterExecuteCodeGenPasses" before returning.
  if (!afterExecuteCodeGenPasses(pScript)) {
    return kErrHookAfterExecuteCodeGenPasses;
  }

  return kSuccess;
}

enum Compiler::ErrorCode Compiler::compile(Script &pScript,
                                           llvm::raw_ostream &pResult,
                                           llvm::raw_ostream *IRStream) {
  llvm::Module &module = pScript.getSource().getModule();
  enum ErrorCode err;

  if (mTarget == NULL) {
    return kErrNoTargetMachine;
  }

  // Materialize the bitcode module.
  if (module.getMaterializer() != NULL) {
    std::string error;
    // A module with non-null materializer means that it is a lazy-load module.
    // Materialize it now via invoking MaterializeAllPermanently(). This
    // function returns false when the materialization is successful.
    if (module.MaterializeAllPermanently(&error)) {
      ALOGE("Failed to materialize the module `%s'! (%s)",
            module.getModuleIdentifier().c_str(), error.c_str());
      return kErrMaterialization;
    }
  }

  if (mEnableLTO && ((err = runLTO(pScript)) != kSuccess)) {
    return err;
  }

  if (IRStream)
    *IRStream << module;

  if ((err = runCodeGen(pScript, pResult)) != kSuccess) {
    return err;
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
  if (out == NULL) {
    return kErrPrepareOutput;
  }

  // Delegate the request.
  enum Compiler::ErrorCode err = compile(pScript, *out, IRStream);

  // Close the output before return.
  delete out;

  return err;
}
