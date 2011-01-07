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

#include "CacheReader.h"
#include "CacheWriter.h"
#include "FileHandle.h"
#include "ScriptCompiled.h"
#include "ScriptCached.h"
#include "Sha1Helper.h"

#include <errno.h>

#include <new>
#include <string.h>
#include <cutils/properties.h>


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

bool getBooleanProp(const char *str) {
    char buf[PROPERTY_VALUE_MAX];
    property_get(str, buf, "0");
    return strcmp(buf, "0") != 0;
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
                   const BCCchar *resName,
                   const BCCchar *cacheDir) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  sourceBC = bitcode;
  sourceResName = resName;
  sourceSize = bitcodeSize;

  if (cacheDir && resName) {
    cacheFile = genCacheFileName(cacheDir, resName, ".oBCC");
  }

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
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  libraryBC = bitcode;
  librarySize = bitcodeSize;
  return 0;
}


int Script::compile() {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  // Load Cache File
  if (cacheFile && internalLoadCache() == 0) {
    return 0;
  }

  return internalCompile();
}


int Script::internalLoadCache() {
  if (getBooleanProp("debug.bcc.nocache")) {
    // Android system environment property disable the cache mechanism by
    // setting "debug.bcc.nocache".  So we will not load the cache file any
    // way.
    return 1;
  }

  if (!cacheFile) {
    // The application developer has not specify resName or cacheDir, so
    // we don't know where to open the cache file.
    return 1;
  }

  FileHandle file;

  if (file.open(cacheFile, OpenMode::Read) < 0) {
    // Unable to open the cache file in read mode.
    return 1;
  }

  CacheReader reader;

  // Dependencies
  reader.addDependency(BCC_FILE_RESOURCE, pathLibBCC, sha1LibBCC);
  reader.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);

  if (sourceBC) {
    calcSHA1(sourceSHA1, sourceBC, sourceSize);
    reader.addDependency(BCC_APK_RESOURCE, sourceResName, sourceSHA1);
  }

  // Read cache file
  ScriptCached *cached = reader.readCacheFile(&file, this);
  if (!cached) {
    return 1;
  }

  mCached = cached;
  mStatus = ScriptStatus::Cached;

  // Dirty hack for libRS.
  // TODO(all):  This dirty hack should be removed in the future.
  if (cached->isLibRSThreadable() && mpExtSymbolLookupFn) {
    mpExtSymbolLookupFn(mpExtSymbolLookupFnContext, "__clearThreadable");
  }

  return 0;
}


int Script::internalCompile() {
  // Create the ScriptCompiled object
  mCompiled = new (nothrow) ScriptCompiled(this);

  if (!mCompiled) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    LOGE("Out of memory: %s %d\n", __FILE__, __LINE__);
    return 1;
  }

  mStatus = ScriptStatus::Compiled;

  // Register symbol lookup function
  if (mpExtSymbolLookupFn) {
    mCompiled->registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  // Setup the source bitcode / module
  if (sourceBC) {
    if (mCompiled->readBC(sourceBC, sourceSize, sourceResName, 0) != 0) {
      return 1;
    }
  } else if (sourceModule) {
    if (mCompiled->readModule(sourceModule) != 0) {
      return 1;
    }
  }

  // Link the source module with the library module
  if (libraryBC) {
    if (mCompiled->linkBC(libraryBC, librarySize) != 0) {
      return 1;
    }
  }

  // Compile and JIT the code
  if (mCompiled->compile() != 0) {
    return 1;
  }

  // TODO(logan): Write the cache out
  if (cacheFile && !getBooleanProp("debug.bcc.nocache")) {
    FileHandle file;

    if (file.open(cacheFile, OpenMode::Write) >= 0) {
      CacheWriter writer;

      // libRS is threadable dirty hack
      // TODO: This should be removed in the future
      uint32_t libRS_threadable = 0;
      if (mpExtSymbolLookupFn) {
        libRS_threadable =
          (uint32_t)mpExtSymbolLookupFn(mpExtSymbolLookupFnContext,
                                        "__isThreadable");
      }

      if (!writer.writeCacheFile(&file, this, libRS_threadable)) {
        file.truncate();
        file.close();

        if (unlink(cacheFile) != 0) {
          LOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               cacheFile, strerror(errno));
        }
      }
    }
  }

  return 0;
}


char const *Script::getCompilerErrorMessage() {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }

  return mCompiled->getCompilerErrorMessage();
}


void *Script::lookup(const char *name) {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    return mCompiled->lookup(name);

  case ScriptStatus::Cached:
    return mCached->lookup(name);

  default:
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }
}


void Script::getExportVars(BCCsizei *actualVarCount,
                           BCCsizei maxVarCount,
                           BCCvoid **vars) {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    mCompiled->getExportVars(actualVarCount, maxVarCount, vars);
    break;

  case ScriptStatus::Cached:
    mCached->getExportVars(actualVarCount, maxVarCount, vars);
    break;

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getExportFuncs(BCCsizei *actualFuncCount,
                            BCCsizei maxFuncCount,
                            BCCvoid **funcs) {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    mCompiled->getExportFuncs(actualFuncCount, maxFuncCount, funcs);
    break;

  case ScriptStatus::Cached:
    mCached->getExportFuncs(actualFuncCount, maxFuncCount, funcs);
    break;

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getPragmas(BCCsizei *actualStringCount,
                        BCCsizei maxStringCount,
                        BCCchar **strings) {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    mCompiled->getPragmas(actualStringCount, maxStringCount, strings);
    break;

  case ScriptStatus::Cached:
    mCached->getPragmas(actualStringCount, maxStringCount, strings);
    break;

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getFunctions(BCCsizei *actualFunctionCount,
                          BCCsizei maxFunctionCount,
                          BCCchar **functions) {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    mCompiled->getFunctions(actualFunctionCount, maxFunctionCount, functions);
    break;

  case ScriptStatus::Cached:
    mCached->getFunctions(actualFunctionCount, maxFunctionCount, functions);
    break;

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}

char *Script::getContext() {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    return mCompiled->getContext();

  case ScriptStatus::Cached:
    return mCached->getContext();

  default:
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }
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

  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
  }
}

} // namespace bcc
