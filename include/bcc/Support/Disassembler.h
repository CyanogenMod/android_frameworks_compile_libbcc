/*
 * Copyright 2011-2012, The Android Open Source Project
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

#ifndef BCC_SUPPORT_DISASSEMBLER_H
#define BCC_SUPPORT_DISASSEMBLER_H

#include "bcc/Config/Config.h"

#if USE_DISASSEMBLER

#include <string>

namespace llvm {
  class Target;
  class TargetMachine;
}

namespace bcc {

void InitializeDisassembler();

void Disassemble(char const *OutputFileName,
                 llvm::Target const *Target,
                 llvm::TargetMachine *TM,
                 std::string const &Name,
                 unsigned char const *Func,
                 size_t FuncSize);

} // end namespace bcc

#endif // USE_DISASSEMBLER

#endif // BCC_SUPPORT_DISASSEMBLER_H
