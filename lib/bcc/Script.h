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

#ifndef BCC_SCRIPT_H
#define BCC_SCRIPT_H

#include "Compiler.h"

#include <bcc/bcc.h>

namespace llvm {
  class Module;
}

namespace bcc {

  class Script {
  private:
    BCCenum mErrorCode;

    Compiler mCompiler;

  public:
    Script() {
      mErrorCode = BCC_NO_ERROR;
    }

    ~Script() {
    }

    //////////////////////////////////////////////////////////////////////////
    // Part I. Compiler
    //////////////////////////////////////////////////////////////////////////

    int readBC(const char *bitcode,
               size_t bitcodeSize,
               long bitcodeFileModTime,
               long bitcodeFileCRC32,
               const BCCchar *resName,
               const BCCchar *cacheDir) {
      return mCompiler.readBC(bitcode, bitcodeSize,
                              bitcodeFileModTime, bitcodeFileCRC32,
                              resName, cacheDir);
    }

    int linkBC(const char *bitcode, size_t bitcodeSize) {
      return mCompiler.linkBC(bitcode, bitcodeSize);
    }

    int loadCacheFile() {
      return mCompiler.loadCacheFile();
    }

    int compile() {
      return mCompiler.compile();
    }

    char const *getCompilerErrorMessage() {
      return mCompiler.getErrorMessage();
    }

    void *lookup(const char *name) {
      return mCompiler.lookup(name);
    }

    void getExportVars(BCCsizei *actualVarCount,
                       BCCsizei maxVarCount,
                       BCCvoid **vars) {
      mCompiler.getExportVars(actualVarCount, maxVarCount, vars);
    }

    void getExportFuncs(BCCsizei *actualFuncCount,
                        BCCsizei maxFuncCount,
                        BCCvoid **funcs) {
      mCompiler.getExportFuncs(actualFuncCount, maxFuncCount, funcs);
    }

    void getPragmas(BCCsizei *actualStringCount,
                    BCCsizei maxStringCount,
                    BCCchar **strings) {
      mCompiler.getPragmas(actualStringCount, maxStringCount, strings);
    }

    void getFunctions(BCCsizei *actualFunctionCount,
                      BCCsizei maxFunctionCount,
                      BCCchar **functions) {
      mCompiler.getFunctions(actualFunctionCount, maxFunctionCount, functions);
    }

    void getFunctionBinary(BCCchar *function,
                           BCCvoid **base,
                           BCCsizei *length) {
      mCompiler.getFunctionBinary(function, base, length);
    }

    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      mCompiler.registerSymbolCallback(pFn, pContext);
    }

    int readModule(llvm::Module *module) {
      return mCompiler.readModule(module);
    }


    //////////////////////////////////////////////////////////////////////////
    // Error handling
    //////////////////////////////////////////////////////////////////////////

    void setError(BCCenum error) {
      if (mErrorCode == BCC_NO_ERROR && error != BCC_NO_ERROR) {
        mErrorCode = error;
      }
    }

    BCCenum getError() {
      BCCenum result = mErrorCode;
      mErrorCode = BCC_NO_ERROR;
      return result;
    }
  };

} // namespace bcc

#endif // BCC_SCRIPT_H
