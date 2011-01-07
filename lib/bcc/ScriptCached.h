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

#include <bcc/bcc.h>
#include <bcc/bcc_cache.h>

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

    FuncTable mFunctions;

    char *mContext;

    OBCC_StringPool *mpStringPoolRaw;
    std::vector<char const *> mStringPool;

    bool mLibRSThreadable;

  public:
    ScriptCached(Script *owner)
      : mpOwner(owner), mpExportVars(NULL), mpExportFuncs(NULL),
        mContext(NULL), mpStringPoolRaw(NULL), mLibRSThreadable(false) {
    }

    ~ScriptCached();

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

    char *getContext() {
      return mContext;
    }

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
