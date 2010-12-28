/*
 * copyright 2010, the android open source project
 *
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/license-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "as is" basis,
 * without warranties or conditions of any kind, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 */

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "Script.h"

#include "ScriptCompiled.h"

#include <new>

namespace bcc {

Script::~Script() {
  if (mStatus == ScriptStatus::Compiled) {
    delete mCompiled;
  }
}


int Script::readBC(const char *bitcode,
                   size_t bitcodeSize,
                   long bitcodeFileModTime,
                   long bitcodeFileCRC32,
                   const BCCchar *resName,
                   const BCCchar *cacheDir) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  mCompiled = new (nothrow) ScriptCompiled(this);

  if (!mCompiled) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    return 1;
  }

  mStatus = ScriptStatus::Compiled;

  if (mpExtSymbolLookupFn) {
    mCompiled->registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  return mCompiled->readBC(bitcode, bitcodeSize,
                           bitcodeFileModTime, bitcodeFileCRC32,
                           resName, cacheDir);
}


int Script::readModule(llvm::Module *module) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  mCompiled = new (nothrow) ScriptCompiled(this);

  if (!mCompiled) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    return 1;
  }

  mStatus = ScriptStatus::Compiled;

  if (mpExtSymbolLookupFn) {
    mCompiled->registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  return mCompiled->readModule(module);
}


int Script::linkBC(const char *bitcode, size_t bitcodeSize) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  return mCompiled->linkBC(bitcode, bitcodeSize);
}


int Script::loadCacheFile() {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  return mCompiled->loadCacheFile();
}


int Script::compile() {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  return mCompiled->compile();
}


char const *Script::getCompilerErrorMessage() {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }

  return mCompiled->getCompilerErrorMessage();
}


void *Script::lookup(const char *name) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }

  return mCompiled->lookup(name);
}


void Script::getExportVars(BCCsizei *actualVarCount,
                           BCCsizei maxVarCount,
                           BCCvoid **vars) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->getExportVars(actualVarCount, maxVarCount, vars);
}


void Script::getExportFuncs(BCCsizei *actualFuncCount,
                            BCCsizei maxFuncCount,
                            BCCvoid **funcs) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->getExportFuncs(actualFuncCount, maxFuncCount, funcs);
}


void Script::getPragmas(BCCsizei *actualStringCount,
                        BCCsizei maxStringCount,
                        BCCchar **strings) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->getPragmas(actualStringCount, maxStringCount, strings);
}


void Script::getFunctions(BCCsizei *actualFunctionCount,
                          BCCsizei maxFunctionCount,
                          BCCchar **functions) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->getFunctions(actualFunctionCount, maxFunctionCount, functions);
}


void Script::getFunctionBinary(BCCchar *function,
                               BCCvoid **base,
                               BCCsizei *length) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->getFunctionBinary(function, base, length);
}


void Script::registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
  mpExtSymbolLookupFn = pFn;
  mpExtSymbolLookupFnContext = pContext;

  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return;
  }

  mCompiled->registerSymbolCallback(pFn, pContext);
}

} // namespace bcc
