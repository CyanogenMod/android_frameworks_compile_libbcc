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

#include "ContextManager.h"
#include "EmittedFuncInfo.h"

namespace bcc {

ScriptCompiled::~ScriptCompiled() {
  // Deallocate the BCC context
  if (mContext) {
    deallocateContext(mContext);
  }

  // Delete the emitted function information
  for (EmittedFunctionsMapTy::iterator I = mEmittedFunctions.begin(),
       E = mEmittedFunctions.end(); I != E; I++) {
    if (I->second != NULL) {
      delete I->second;
    }
  }
}

void ScriptCompiled::getExportVars(BCCsizei *actualVarCount,
                                   BCCsizei maxVarCount,
                                   BCCvoid **vars) {
  int varCount = mExportVars.size();
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
  int funcCount = mExportFuncs.size();
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
  int stringCount = mPragmas.size() * 2;

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


void *ScriptCompiled::lookup(const char *name) {
  EmittedFunctionsMapTy::const_iterator I = mEmittedFunctions.find(name);
  return (I == mEmittedFunctions.end()) ? NULL : I->second->Code;
}


void ScriptCompiled::getFunctions(BCCsizei *actualFunctionCount,
                                  BCCsizei maxFunctionCount,
                                  BCCchar **functions) {

  int functionCount = mEmittedFunctions.size();

  if (actualFunctionCount)
    *actualFunctionCount = functionCount;
  if (functionCount > maxFunctionCount)
    functionCount = maxFunctionCount;
  if (functions) {
    for (EmittedFunctionsMapTy::const_iterator
         I = mEmittedFunctions.begin(), E = mEmittedFunctions.end();
         I != E && (functionCount > 0); I++, functionCount--) {
      *functions++ = const_cast<BCCchar*>(I->first.c_str());
    }
  }
}


void ScriptCompiled::getFunctionBinary(BCCchar *funcname,
                                       BCCvoid **base,
                                       BCCsizei *length) {
  EmittedFunctionsMapTy::const_iterator I = mEmittedFunctions.find(funcname);
  if (I == mEmittedFunctions.end()) {
    *base = NULL;
    *length = 0;
  } else {
    *base = I->second->Code;
    *length = I->second->Size;
  }
}


} // namespace bcc
