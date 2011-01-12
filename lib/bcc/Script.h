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

#include <stddef.h>

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
    unsigned char sourceSHA1[20];
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
               cacheFile(NULL),
               sourceBC(NULL), sourceResName(NULL), sourceSize(0),
               sourceModule(NULL), libraryBC(NULL), librarySize(0),
               mpExtSymbolLookupFn(NULL), mpExtSymbolLookupFnContext(NULL) {
      Compiler::GlobalInitialization();
    }

    ~Script();

    int readBC(const char *bitcode,
               size_t bitcodeSize,
               const BCCchar *resName,
               const BCCchar *cacheDir);

    int readModule(llvm::Module *module);

    int linkBC(const char *bitcode, size_t bitcodeSize);

    int prepareExecutable();

    char const *getCompilerErrorMessage();

    void *lookup(const char *name);


    size_t getExportVarCount() const;

    size_t getExportFuncCount() const;

    size_t getPragmaCount() const;

    size_t getFuncCount() const;


    void getExportVarList(size_t size, void **list);

    void getExportFuncList(size_t size, void **list);

    void getPragmaList(size_t size,
                       char const **keyList,
                       char const **valueList);

    void getFuncNameList(size_t size, char const **list);

    void getFuncBinary(char const *funcname, void **base, size_t *length);


    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext);

    char *getContext();


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
