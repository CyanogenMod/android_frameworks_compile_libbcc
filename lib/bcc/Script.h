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

#include <bcc/bcc.h>

#include "Compiler.h"

namespace llvm {
  class Module;
}

namespace bcc {
  class ScriptCompiled;
  class ScriptCached;

  namespace ScriptStatus {
    enum StatusType {
      Unknown,
      Compiled,
      Cached,
    };
  }

  class Script {
  private:
    BCCenum mErrorCode;

    ScriptStatus::StatusType mStatus;

    union {
      ScriptCompiled *mCompiled;
      ScriptCached *mCached;
    };

    char *cacheFile;

    // ReadBC
    char const *sourceBC;
    char const *sourceResName;
    size_t sourceSize;

    // ReadModule
    llvm::Module *sourceModule;

    // LinkBC
    char const *libraryBC;
    size_t librarySize;

    // Register Symbol Lookup Function
    BCCSymbolLookupFn mpExtSymbolLookupFn;
    BCCvoid *mpExtSymbolLookupFnContext;

  public:
    Script() : mErrorCode(BCC_NO_ERROR), mStatus(ScriptStatus::Unknown),
               cacheFile(NULL), sourceBC(NULL), sourceResName(NULL),
               sourceSize(0), sourceModule(NULL), mpExtSymbolLookupFn(NULL),
               mpExtSymbolLookupFnContext(NULL) {
      Compiler::GlobalInitialization();
    }

    ~Script();

    int readBC(const char *bitcode,
               size_t bitcodeSize,
               const BCCchar *resName,
               const BCCchar *cacheDir);

    int readModule(llvm::Module *module);

    int linkBC(const char *bitcode, size_t bitcodeSize);

    int compile(); // deprecated

    char const *getCompilerErrorMessage();

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

    char const *getContext() const;

    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext);


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

  private:
    int internalLoadCache();
    int internalCompile();

  };

} // namespace bcc

#endif // BCC_SCRIPT_H
