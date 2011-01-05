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

namespace {

// Input: cacheDir
// Input: resName
// Input: extName
//
// Note: cacheFile = resName + extName
//
// Output: Returns cachePath == cacheDir + cacheFile
char *genCacheFileName(const char *cacheDir,
                       const char *resName,
                       const char *extName) {
  char cachePath[512];
  char cacheFile[sizeof(cachePath)];
  const size_t kBufLen = sizeof(cachePath) - 1;

  cacheFile[0] = '\0';
  // Note: resName today is usually something like
  //       "/com.android.fountain:raw/fountain"
  if (resName[0] != '/') {
    // Get the absolute path of the raw/***.bc file.

    // Generate the absolute path.  This doesn't do everything it
    // should, e.g. if resName is "./out/whatever" it doesn't crunch
    // the leading "./" out because this if-block is not triggered,
    // but it'll make do.
    //
    if (getcwd(cacheFile, kBufLen) == NULL) {
      LOGE("Can't get CWD while opening raw/***.bc file\n");
      return NULL;
    }
    // Append "/" at the end of cacheFile so far.
    strncat(cacheFile, "/", kBufLen);
  }

  // cacheFile = resName + extName
  //
  strncat(cacheFile, resName, kBufLen);
  if (extName != NULL) {
    // TODO(srhines): strncat() is a bit dangerous
    strncat(cacheFile, extName, kBufLen);
  }

  // Turn the path into a flat filename by replacing
  // any slashes after the first one with '@' characters.
  char *cp = cacheFile + 1;
  while (*cp != '\0') {
    if (*cp == '/') {
      *cp = '@';
    }
    cp++;
  }

  // Tack on the file name for the actual cache file path.
  strncpy(cachePath, cacheDir, kBufLen);
  strncat(cachePath, cacheFile, kBufLen);

  LOGV("Cache file for '%s' '%s' is '%s'\n", resName, extName, cachePath);
  return strdup(cachePath);
}

} // namespace anonymous

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
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  cacheFile = genCacheFileName(cacheDir, resName, ".oBCC");

  sourceBC = bitcode;
  sourceResName = resName;
  sourceSize = bitcodeSize;

  return 0;
}


int Script::readModule(llvm::Module *module) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  sourceModule = module;
  return 0;
}


int Script::linkBC(const char *bitcode, size_t bitcodeSize) {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return 1;
  }

  return mCompiled->linkBC(bitcode, bitcodeSize);
}


int Script::compile() {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  // Load Cache File
  // TODO(logan): Complete this.

  // Compile
  mCompiled = new (nothrow) ScriptCompiled(this);

  if (!mCompiled) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    LOGE("Out of memory: %s %d\n", __FILE__, __LINE__);
    return 1;
  }

  mStatus = ScriptStatus::Compiled;

  if (mpExtSymbolLookupFn) {
    mCompiled->registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  if (sourceBC) {
    int ret = mCompiled->readBC(sourceBC, sourceSize, 0, 0, sourceResName, 0);
    if (ret != 0) {
      return ret;
    }
  } else if (sourceModule) {
    int ret = mCompiled->readModule(sourceModule);
    if (ret != 0) {
      return ret;
    }
  }

  // TODO(logan): Link

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
