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

#ifndef BCC_COMPILER_H
#define BCC_COMPILER_H

#include <bcc/bcc.h>

#include "CodeGen/CodeEmitter.h"
#include "CodeGen/CodeMemoryManager.h"

#if USE_MCJIT
#include "librsloader.h"
#endif

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Target/TargetMachine.h"

#include <stddef.h>

#include <list>
#include <string>
#include <vector>
#include <utility>


namespace llvm {
  class LLVMContext;
  class Module;
  class MemoryBuffer;
  class NamedMDNode;
  class TargetData;
}


namespace bcc {
  class ScriptCompiled;

  class Compiler {
  private:
    //////////////////////////////////////////////////////////////////////////
    // The variable section below (e.g., Triple, CodeGenOptLevel)
    // is initialized in GlobalInitialization()
    //
    static bool GlobalInitialized;

    // If given, this will be the name of the target triple to compile for.
    // If not given, the initial values defined in this file will be used.
    static std::string Triple;

    static llvm::CodeGenOpt::Level CodeGenOptLevel;

    // End of section of GlobalInitializing variables
    /////////////////////////////////////////////////////////////////////////
    // If given, the name of the target CPU to generate code for.
    static std::string CPU;

    // The list of target specific features to enable or disable -- this should
    // be a list of strings starting with '+' (enable) or '-' (disable).
    static std::vector<std::string> Features;

    static void LLVMErrorHandler(void *UserData, const std::string &Message);

    static const llvm::StringRef PragmaMetadataName;
    static const llvm::StringRef ExportVarMetadataName;
    static const llvm::StringRef ExportFuncMetadataName;
    static const llvm::StringRef ObjectSlotMetadataName;

    friend class CodeEmitter;
    friend class CodeMemoryManager;


  private:
    ScriptCompiled *mpResult;

    std::string mError;

#if USE_OLD_JIT
    // The memory manager for code emitter
    llvm::OwningPtr<CodeMemoryManager> mCodeMemMgr;

    // The CodeEmitter
    llvm::OwningPtr<CodeEmitter> mCodeEmitter;
#endif

#if USE_MCJIT
    // Compilation buffer for MCJIT
    llvm::SmallVector<char, 1024> mEmittedELFExecutable;

    // Loaded and relocated executable
    RSExecRef mRSExecutable;
#endif

    BCCSymbolLookupFn mpSymbolLookupFn;
    void *mpSymbolLookupContext;

    llvm::LLVMContext *mContext;
    llvm::Module *mModule;

    bool mHasLinked;

  public:
    Compiler(ScriptCompiled *result);

    static void GlobalInitialization();

    static std::string const &getTargetTriple() {
      return Triple;
    }

    void registerSymbolCallback(BCCSymbolLookupFn pFn, void *pContext) {
      mpSymbolLookupFn = pFn;
      mpSymbolLookupContext = pContext;
    }

#if USE_OLD_JIT
    CodeMemoryManager *createCodeMemoryManager();

    CodeEmitter *createCodeEmitter();
#endif

#if USE_MCJIT
    void *getSymbolAddress(char const *name);

    const llvm::SmallVector<char, 1024> &getELF() const {
      return mEmittedELFExecutable;
    }
#endif

    llvm::Module *parseBitcodeFile(llvm::MemoryBuffer *MEM);

    int readModule(llvm::Module *module) {
      mModule = module;
      return hasError();
    }

    int linkModule(llvm::Module *module);

    int compile(bool compileOnly);

    char const *getErrorMessage() {
      return mError.c_str();
    }

    const llvm::Module *getModule() const {
      return mModule;
    }

    ~Compiler();

  private:

    int runCodeGen(llvm::TargetData *TD, llvm::TargetMachine *TM,
                   llvm::NamedMDNode const *ExportVarMetadata,
                   llvm::NamedMDNode const *ExportFuncMetadata);

    int runMCCodeGen(llvm::TargetData *TD, llvm::TargetMachine *TM);

#if USE_MCJIT
    static void *resolveSymbolAdapter(void *context, char const *name);
#endif

    int runLTO(llvm::TargetData *TD,
               llvm::NamedMDNode const *ExportVarMetadata,
               llvm::NamedMDNode const *ExportFuncMetadata);

    bool hasError() const {
      return !mError.empty();
    }

    void setError(const char *Error) {
      mError.assign(Error);  // Copying
    }

    void setError(const std::string &Error) {
      mError = Error;
    }

  };  // End of class Compiler

} // namespace bcc

#endif // BCC_COMPILER_H
