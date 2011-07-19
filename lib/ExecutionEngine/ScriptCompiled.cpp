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

#include "ScriptCompiled.h"

#include "bcc_internal.h"
#if USE_OLD_JIT
#include "OldJIT/ContextManager.h"
#endif
#include "DebugHelper.h"

namespace bcc {

ScriptCompiled::~ScriptCompiled() {
#if USE_OLD_JIT
  // Deallocate the BCC context
  if (mContext) {
    ContextManager::get().deallocateContext(mContext);
  }

  // Delete the emitted function information
  for (FuncInfoMap::iterator I = mEmittedFunctions.begin(),
       E = mEmittedFunctions.end(); I != E; I++) {
    if (I->second != NULL) {
      delete I->second;
    }
  }
#endif
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

void ScriptCompiled::getExportVarNameList(std::vector<std::string> &varList) {
  varList = mExportVarsName;
}


void ScriptCompiled::getExportFuncNameList(std::vector<std::string> &funcList) {
  funcList = mExportFuncsName;
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
#if USE_OLD_JIT
  FuncInfoMap::const_iterator I = mEmittedFunctions.find(name);
  return (I == mEmittedFunctions.end()) ? NULL : I->second->addr;
#endif

#if USE_MCJIT
  return mCompiler.getSymbolAddress(name);
#endif

  return NULL;
}


void ScriptCompiled::getFuncInfoList(size_t funcInfoListSize,
                                     FuncInfo *funcInfoList) {
  if (funcInfoList) {
    size_t funcCount = getFuncCount();

    if (funcCount > funcInfoListSize) {
      funcCount = funcInfoListSize;
    }

    FuncInfo *info = funcInfoList;
    for (FuncInfoMap::const_iterator
         I = mEmittedFunctions.begin(), E = mEmittedFunctions.end();
         I != E && funcCount > 0; ++I, ++info, --funcCount) {
      info->name = I->first.c_str();
      info->addr = I->second->addr;
      info->size = I->second->size;
    }
  }
}

void ScriptCompiled::getObjectSlotList(size_t objectSlotListSize,
                                       uint32_t *objectSlotList) {
  if (objectSlotList) {
    size_t objectSlotCount = getObjectSlotCount();

    if (objectSlotCount > objectSlotListSize) {
      objectSlotCount = objectSlotListSize;
    }

    for (ObjectSlotList::const_iterator
         I = mObjectSlots.begin(), E = mObjectSlots.end();
         I != E && objectSlotCount > 0; ++I, --objectSlotCount) {
      *objectSlotList++ = *I;
    }
  }

}

} // namespace bcc
