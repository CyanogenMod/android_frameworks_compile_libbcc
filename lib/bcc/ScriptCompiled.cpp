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

void ScriptCompiled::getExportVarList(size_t varListSize, void **varList) {
  if (varList) {
    size_t varCount = getExportVarCount();

    if (varCount > varListSize) {
      varCount = varListSize;
    }

    for (ExportVarList::const_iterator
         I = mExportVars.begin(), E = mExportVars.end();
         I != E && varCount > 0; ++I, --varCount) {
      *varList++ = *I;
    }
  }
}


void ScriptCompiled::getExportFuncList(size_t funcListSize, void **funcList) {
  if (funcList) {
    size_t funcCount = getExportFuncCount();

    if (funcCount > funcListSize) {
      funcCount = funcListSize;
    }

    for (ExportFuncList::const_iterator
         I = mExportFuncs.begin(), E = mExportFuncs.end();
         I != E && funcCount > 0; ++I, --funcCount) {
      *funcList++ = *I;
    }
  }
}


void ScriptCompiled::getPragmaList(size_t pragmaListSize,
                                   char const **keyList,
                                   char const **valueList) {
  size_t pragmaCount = getPragmaCount();

  if (pragmaCount > pragmaListSize) {
    pragmaCount = pragmaListSize;
  }

  for (PragmaList::const_iterator
       I = mPragmas.begin(), E = mPragmas.end();
       I != E && pragmaCount > 0; ++I, --pragmaCount) {
    if (keyList) { *keyList++ = I->first.c_str(); }
    if (valueList) { *valueList++ = I->second.c_str(); }
  }
}


void *ScriptCompiled::lookup(const char *name) {
  EmittedFunctionsMapTy::const_iterator I = mEmittedFunctions.find(name);
  return (I == mEmittedFunctions.end()) ? NULL : I->second->Code;
}


void ScriptCompiled::getFuncNameList(size_t funcNameListSize,
                                     char const **funcNameList) {
  if (funcNameList) {
    size_t funcCount = getFuncCount();

    if (funcCount > funcNameListSize) {
      funcCount = funcNameListSize;
    }

    for (EmittedFunctionsMapTy::const_iterator
         I = mEmittedFunctions.begin(), E = mEmittedFunctions.end();
         I != E && funcCount > 0; ++I, --funcCount) {
      *funcNameList++ = I->first.c_str();
    }
  }
}


void ScriptCompiled::getFuncBinary(char const *funcname,
                                   void **base,
                                   size_t *length) {
  EmittedFunctionsMapTy::const_iterator I = mEmittedFunctions.find(funcname);

#define DEREF_ASSIGN(VAR, VALUE) if (VAR) { *(VAR) = (VALUE); }

  if (I == mEmittedFunctions.end()) {
    DEREF_ASSIGN(base, NULL);
    DEREF_ASSIGN(length, 0);
  } else {
    DEREF_ASSIGN(base, I->second->Code);
    DEREF_ASSIGN(length, I->second->Size);
  }

#undef DEREF_ASSIGN
}


} // namespace bcc
