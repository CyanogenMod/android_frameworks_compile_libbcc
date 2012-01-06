/*
 * Copyright 2010, The Android Open Source Project
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

#ifndef BCC_COMPILER_OPTION_H
#define BCC_COMPILER_OPTION_H

#include "Config.h"

#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/CodeGen.h"

namespace bcc {

typedef struct CompilerOption {
  llvm::TargetOptions TargetOpt;
  llvm::CodeModel::Model CodeModelOpt;
  llvm::Reloc::Model RelocModelOpt;
  bool LoadAfterCompile;

  // Constructor setup "default configuration". The "default configuration"
  // here means the configuration for running RenderScript (more specifically,
  // one can declare a CompilerOption object (call default constructor) and then
  // pass to the Compiler::compiler() without any modification for RenderScript,
  // see Script::prepareExecutable(...))
  CompilerOption() {
    //-- Setup options to llvm::TargetMachine --//

    // Use hardfloat ABI
    //
    // TODO(all): Need to detect the CPU capability and decide whether to use
    // softfp. To use softfp, change following 2 lines to
    //
    // options.FloatABIType = llvm::FloatABI::Soft;
    // options.UseSoftFloat = true;
    TargetOpt.FloatABIType = llvm::FloatABI::Soft;
    TargetOpt.UseSoftFloat = false;

    //-- Setup relocation model  --//
    RelocModelOpt = llvm::Reloc::Static;

    //-- Load the result object after successful compilation  --//
    LoadAfterCompile = true;
  }
} CompilerOption;

} // namespace bcc

#endif  // BCC_COMPILER_OPTION_H
