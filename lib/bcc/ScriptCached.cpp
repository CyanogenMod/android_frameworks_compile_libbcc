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

void ScriptCached::getExportVars(BCCsizei *actualVarCount,
                                 BCCsizei maxVarCount,
                                 BCCvoid **vars) {
  int varCount = static_cast<int>(mpExportVars->count);

  if (actualVarCount)
    *actualVarCount = varCount;
  if (varCount > maxVarCount)
    varCount = maxVarCount;
  if (vars) {
    void **ptr = mpExportVars->cached_addr_list;
    for (int i = 0; i < varCount; i++) {
      *vars++ = *ptr++;
    }
  }
}


void ScriptCached::getExportFuncs(BCCsizei *actualFuncCount,
                                  BCCsizei maxFuncCount,
                                  BCCvoid **funcs) {
  int funcCount = static_cast<int>(mpExportFuncs->count);

  if (actualFuncCount)
    *actualFuncCount = funcCount;
  if (funcCount > maxFuncCount)
    funcCount = maxFuncCount;
  if (funcs) {
    void **ptr = mpExportFuncs->cached_addr_list;
    for (int i = 0; i < funcCount; i++) {
      *funcs++ = *ptr++;
    }
  }
}


void ScriptCached::getPragmas(BCCsizei *actualStringCount,
                              BCCsizei maxStringCount,
                              BCCchar **strings) {
  int stringCount = static_cast<int>(mPragmas.size()) * 2;

  if (actualStringCount)
    *actualStringCount = stringCount;

  if (stringCount > maxStringCount)
    stringCount = maxStringCount;

  if (strings) {
    for (int i = 0; stringCount >= 2; stringCount -= 2, ++i) {
      *strings++ = const_cast<BCCchar *>(mPragmas[i].first);
      *strings++ = const_cast<BCCchar *>(mPragmas[i].second);
    }
  }
}


void *ScriptCached::lookup(const char *name) {
  FuncTable::const_iterator I = mFunctions.find(name);
  return (I == mFunctions.end()) ? NULL : I->second.first;
}


void ScriptCached::getFunctions(BCCsizei *actualFunctionCount,
                                BCCsizei maxFunctionCount,
                                BCCchar **functions) {
  int functionCount = mFunctions.size();

  if (actualFunctionCount)
    *actualFunctionCount = functionCount;
  if (functionCount > maxFunctionCount)
    functionCount = maxFunctionCount;
  if (functions) {
    for (FuncTable::const_iterator
         I = mFunctions.begin(), E = mFunctions.end();
         I != E && (functionCount > 0); I++, functionCount--) {
      *functions++ = const_cast<BCCchar*>(I->first.c_str());
    }
  }
}


void ScriptCached::getFunctionBinary(BCCchar *funcname,
                                     BCCvoid **base,
                                     BCCsizei *length) {
  FuncTable::const_iterator I = mFunctions.find(funcname);
  if (I == mFunctions.end()) {
    *base = NULL;
    *length = 0;
  } else {
    *base = I->second.first;
    *length = I->second.second;
  }
}


} // namespace bcc
