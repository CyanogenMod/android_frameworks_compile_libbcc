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

#ifndef BCC_SCRIPTCACHED_H
#define BCC_SCRIPTCACHED_H

#include "Config.h"

#include <bcc/bcc.h>
#include <bcc/bcc_cache.h>
#include <bcc/bcc_mccache.h>
#include "bcc_internal.h"

#if USE_MCJIT
#include "librsloader.h"
#endif

#include <llvm/ADT/SmallVector.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <stddef.h>

namespace llvm {
  class Module;
}

namespace bcc {
  class Script;

  class ScriptCached {
    friend class CacheReader;
    friend class MCCacheReader;

  private:
    enum { SMALL_VECTOR_QUICKN = 16 };

    typedef llvm::SmallVector<std::pair<char const *, char const *>,
                              SMALL_VECTOR_QUICKN> PragmaList;

    typedef std::map<std::string, std::pair<void *, size_t> > FuncTable;

  private:
    Script *mpOwner;

    OBCC_ExportVarList *mpExportVars;
    OBCC_ExportFuncList *mpExportFuncs;
    PragmaList mPragmas;
    OBCC_ObjectSlotList *mpObjectSlotList;

    FuncTable mFunctions;

#if USE_OLD_JIT
    char *mContext;
#endif

#if USE_MCJIT
    RSExecRef mRSExecutable;
#endif

    OBCC_StringPool *mpStringPoolRaw;
    std::vector<char const *> mStringPool;

    bool mLibRSThreadable;

  public:
    ScriptCached(Script *owner)
      : mpOwner(owner),
        mpExportVars(NULL),
        mpExportFuncs(NULL),
        mpObjectSlotList(NULL),
#if USE_OLD_JIT
        mContext(NULL),
#endif
        mpStringPoolRaw(NULL),
        mLibRSThreadable(false) {
    }

    ~ScriptCached();

    void *lookup(const char *name);


    size_t getExportVarCount() const {
      return mpExportVars->count;
    }

    size_t getExportFuncCount() const {
      return mpExportFuncs->count;
    }

    size_t getPragmaCount() const {
      return mPragmas.size();
    }

    size_t getFuncCount() const {
      return mFunctions.size();
    }

    size_t getObjectSlotCount() const {
      return mpObjectSlotList->count;
    }

    void getExportVarList(size_t varListSize, void **varList);

    void getExportFuncList(size_t funcListSize, void **funcList);

    void getPragmaList(size_t pragmaListSize,
                       char const **keyList,
                       char const **valueList);

    void getFuncInfoList(size_t funcInfoListSize, FuncInfo *funcNameList);

    void getObjectSlotList(size_t objectSlotListSize,
                           uint32_t *objectSlotList);

#if USE_OLD_JIT
    char *getContext() {
      return mContext;
    }
#endif

    // Dirty hack for libRS.
    // TODO(all): This should be removed in the future.
    bool isLibRSThreadable() const {
      return mLibRSThreadable;
    }

#if 0
    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      mCompiler.registerSymbolCallback(pFn, pContext);
    }
#endif
  };

} // namespace bcc

#endif // BCC_SCRIPTCACHED_H
