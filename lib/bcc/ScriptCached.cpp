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

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "ScriptCached.h"

#include <bcc/bcc_cache.h>

#include "ContextManager.h"
#include "EmittedFuncInfo.h"

#include <stdlib.h>

namespace bcc {

ScriptCached::~ScriptCached() {
  // Deallocate the bcc script context
  if (mContext) {
    deallocateContext(mContext);
  }

  // Deallocate string pool, exported var list, exported func list
  if (mpStringPoolRaw) { free(mpStringPoolRaw); }
  if (mpExportVars) { free(mpExportVars); }
  if (mpExportFuncs) { free(mpExportFuncs); }
}

void ScriptCached::getExportVarList(size_t varListSize, void **varList) {
  if (varList) {
    size_t varCount = getExportVarCount();

    if (varCount > varListSize) {
      varCount = varListSize;
    }

    memcpy(varList, mpExportVars->cached_addr_list, sizeof(void *) * varCount);
  }
}


void ScriptCached::getExportFuncList(size_t funcListSize, void **funcList) {
  if (funcList) {
    size_t funcCount = getExportFuncCount();

    if (funcCount > funcListSize) {
      funcCount = funcListSize;
    }

    memcpy(funcList, mpExportFuncs->cached_addr_list,
           sizeof(void *) * funcCount);
  }
}


void ScriptCached::getPragmaList(size_t pragmaListSize,
                                 char const **keyList,
                                 char const **valueList) {
  size_t pragmaCount = getPragmaCount();

  if (pragmaCount > pragmaListSize) {
    pragmaCount = pragmaListSize;
  }

  if (keyList) {
    for (size_t i = 0; i < pragmaCount; ++i) {
      *keyList++ = mPragmas[i].first;
    }
  }

  if (valueList) {
    for (size_t i = 0; i < pragmaCount; ++i) {
      *valueList++ = mPragmas[i].second;
    }
  }
}


void *ScriptCached::lookup(const char *name) {
  FuncTable::const_iterator I = mFunctions.find(name);
  return (I == mFunctions.end()) ? NULL : I->second.first;
}


void ScriptCached::getFuncNameList(size_t funcNameListSize,
                                   char const **funcNameList) {
  if (funcNameList) {
    size_t funcCount = getFuncCount();

    if (funcCount > funcNameListSize) {
      funcCount = funcNameListSize;
    }

    for (FuncTable::const_iterator
         I = mFunctions.begin(), E = mFunctions.end();
         I != E && funcCount > 0; I++, funcCount--) {
      *funcNameList++ = I->first.c_str();
    }
  }
}


void ScriptCached::getFuncBinary(char const *funcname,
                                 void **base,
                                 size_t *length) {
  FuncTable::const_iterator I = mFunctions.find(funcname);

#define DEREF_ASSIGN(VAR, VALUE) if (VAR) { *(VAR) = (VALUE); }

  if (I == mFunctions.end()) {
    DEREF_ASSIGN(base, NULL);
    DEREF_ASSIGN(length, 0);
  } else {
    DEREF_ASSIGN(base, I->second.first);
    DEREF_ASSIGN(length, I->second.second);
  }

#undef DEREF_ASSIGN
}


} // namespace bcc
