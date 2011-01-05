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

#ifndef BCC_SCRIPTCOMPILED_H
#define BCC_SCRIPTCOMPILED_H

#include "Compiler.h"

#include <bcc/bcc.h>

#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
  class Module;
}

namespace bcc {
  class EmittedFuncInfo;
  class Script;

  class ScriptCompiled {
    friend class Compiler;
    friend class CodeEmitter;

  private:
    typedef std::list<std::pair<std::string, std::string> > PragmaList;
    typedef std::list<void*> ExportVarList;
    typedef std::list<void*> ExportFuncList;
    typedef std::map<std::string, EmittedFuncInfo *> EmittedFunctionsMapTy;

  private:
    Script *mpOwner;

    Compiler mCompiler;

    PragmaList mPragmas;
    ExportVarList mExportVars;
    ExportFuncList mExportFuncs;
    EmittedFunctionsMapTy mEmittedFunctions;

  public:
    ScriptCompiled(Script *owner) : mpOwner(owner), mCompiler(this) {
    }

    ~ScriptCompiled();

    int readBC(const char *bitcode,
               size_t bitcodeSize,
               const BCCchar *resName,
               const BCCchar *cacheDir) {
      return mCompiler.readBC(bitcode, bitcodeSize, resName, cacheDir);
    }

    int linkBC(const char *bitcode, size_t bitcodeSize) {
      return mCompiler.linkBC(bitcode, bitcodeSize);
    }

    int compile() {
      return mCompiler.compile();
    }

    char const *getCompilerErrorMessage() {
      return mCompiler.getErrorMessage();
    }

    void *lookup(const char *name);

    void getExportVars(BCCsizei *actualVarCount,
                       BCCsizei maxVarCount,
                       BCCvoid **vars);

    void getExportFuncs(BCCsizei *actualFuncCount,
                        BCCsizei maxFuncCount,
                        BCCvoid **funcs);

    void getPragmas(BCCsizei *actualStringCount,
                    BCCsizei maxStringCount,
                    BCCchar **strings);

    void getFunctions(BCCsizei *actualFunctionCount,
                      BCCsizei maxFunctionCount,
                      BCCchar **functions);

    void getFunctionBinary(BCCchar *function,
                           BCCvoid **base,
                           BCCsizei *length);

    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      mCompiler.registerSymbolCallback(pFn, pContext);
    }

    int readModule(llvm::Module *module) {
      return mCompiler.readModule(module);
    }
  };

} // namespace bcc

#endif // BCC_SCRIPTCOMPILED_H
