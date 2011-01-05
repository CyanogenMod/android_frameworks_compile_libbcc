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

#include "ScriptCompiled.h"

namespace bcc {

void ScriptCompiled::getExportVars(BCCsizei *actualVarCount,
                                   BCCsizei maxVarCount,
                                   BCCvoid **vars) {
  int varCount;

#if 0
  if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
    varCount = static_cast<int>(mCacheHdr->exportVarsCount);
    if (actualVarCount)
      *actualVarCount = varCount;
    if (varCount > maxVarCount)
      varCount = maxVarCount;
    if (vars) {
      uint32_t *cachedVars = (uint32_t *)(mCacheMapAddr +
                                          mCacheHdr->exportVarsOffset);

      for (int i = 0; i < varCount; i++) {
        *vars = (BCCvoid *)((char *)(*cachedVars) + mCacheDiff);
        vars++;
        cachedVars++;
      }
    }
    return;
  }
#endif

  varCount = mExportVars.size();
  if (actualVarCount)
    *actualVarCount = varCount;
  if (varCount > maxVarCount)
    varCount = maxVarCount;
  if (vars) {
    for (ExportVarList::const_iterator
         I = mExportVars.begin(), E = mExportVars.end(); I != E; I++) {
      *vars++ = *I;
    }
  }
}


void ScriptCompiled::getExportFuncs(BCCsizei *actualFuncCount,
                                    BCCsizei maxFuncCount,
                                    BCCvoid **funcs) {
  int funcCount;

#if 0
  if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
    funcCount = static_cast<int>(mCacheHdr->exportFuncsCount);
    if (actualFuncCount)
      *actualFuncCount = funcCount;
    if (funcCount > maxFuncCount)
      funcCount = maxFuncCount;
    if (funcs) {
      uint32_t *cachedFuncs = (uint32_t *)(mCacheMapAddr +
                                           mCacheHdr->exportFuncsOffset);

      for (int i = 0; i < funcCount; i++) {
        *funcs = (BCCvoid *)((char *)(*cachedFuncs) + mCacheDiff);
        funcs++;
        cachedFuncs++;
      }
    }
    return;
  }
#endif

  funcCount = mExportFuncs.size();
  if (actualFuncCount)
    *actualFuncCount = funcCount;
  if (funcCount > maxFuncCount)
    funcCount = maxFuncCount;
  if (funcs) {
    for (ExportFuncList::const_iterator
         I = mExportFuncs.begin(), E = mExportFuncs.end(); I != E; I++) {
      *funcs++ = *I;
    }
  }
}


void ScriptCompiled::getPragmas(BCCsizei *actualStringCount,
                                BCCsizei maxStringCount,
                                BCCchar **strings) {
  int stringCount;

#if 0
  if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
    stringCount = static_cast<int>(mCacheHdr->exportPragmasCount) * 2;

    if (actualStringCount)
      *actualStringCount = stringCount;

    if (stringCount > maxStringCount)
      stringCount = maxStringCount;

    if (strings) {
      char *pragmaTab = mCacheMapAddr + mCacheHdr->exportPragmasOffset;

      oBCCPragmaEntry *cachedPragmaEntries = (oBCCPragmaEntry *)pragmaTab;

      for (int i = 0; stringCount >= 2; stringCount -= 2, i++) {
        *strings++ = pragmaTab + cachedPragmaEntries[i].pragmaNameOffset;
        *strings++ = pragmaTab + cachedPragmaEntries[i].pragmaValueOffset;
      }
    }

    return;
  }
#endif

  stringCount = mPragmas.size() * 2;

  if (actualStringCount)
    *actualStringCount = stringCount;
  if (stringCount > maxStringCount)
    stringCount = maxStringCount;
  if (strings) {
    size_t i = 0;
    for (PragmaList::const_iterator it = mPragmas.begin();
         stringCount >= 2; stringCount -= 2, it++, ++i) {
      *strings++ = const_cast<BCCchar*>(it->first.c_str());
      *strings++ = const_cast<BCCchar*>(it->second.c_str());
    }
  }
}

} // namespace bcc
