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

#include "Config.h"

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

bool getBooleanProp(const char *str) {
  char buf[PROPERTY_VALUE_MAX];
  property_get(str, buf, "0");
  return strcmp(buf, "0") != 0;
}

} // namespace anonymous

namespace bcc {

Script::~Script() {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    delete mCompiled;
    break;

#if USE_CACHE
  case ScriptStatus::Cached:
    delete mCached;
    break;
#endif

  default:
    break;
  }
}


int Script::readBC(char const *resName,
                   const char *bitcode,
                   size_t bitcodeSize,
                   unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  sourceBC = bitcode;
  sourceResName = resName;
  sourceSize = bitcodeSize;
  return 0;
}


int Script::readModule(char const *resName,
                       llvm::Module *module,
                       unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  sourceModule = module;
  return 0;
}


int Script::linkBC(char const *resName,
                   const char *bitcode,
                   size_t bitcodeSize,
                   unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  libraryBC = bitcode;
  librarySize = bitcodeSize;
  return 0;
}


int Script::prepareExecutable(char const *cachePath, unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

#if USE_CACHE
  // Load Cache File
  mCachePath = cachePath;
  if (cachePath && internalLoadCache() == 0) {
    return 0;
  }
#endif

  return internalCompile();
}


#if USE_CACHE
int Script::internalLoadCache() {
  if (getBooleanProp("debug.bcc.nocache")) {
    // Android system environment property disable the cache mechanism by
    // setting "debug.bcc.nocache".  So we will not load the cache file any
    // way.
    return 1;
  }

  if (!mCachePath) {
    // The application developer has not specify the cachePath, so
    // we don't know where to open the cache file.
    return 1;
  }

  // If we are going to use the cache file.  We have to calculate sha1sum
  // first (no matter we can open the file now or not.)
  if (sourceBC) {
    calcSHA1(sourceSHA1, sourceBC, sourceSize);
  }

  //if (libraryBC) {
  //  calcSHA1(librarySHA1, libraryBC, librarySize);
  //}

  FileHandle file;

  if (file.open(mCachePath, OpenMode::Read) < 0) {
    // Unable to open the cache file in read mode.
    return 1;
  }

  CacheReader reader;

  // Dependencies
#if USE_LIBBCC_SHA1SUM
  reader.addDependency(BCC_FILE_RESOURCE, pathLibBCC, sha1LibBCC);
#endif

  reader.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);

  if (sourceBC) {
    reader.addDependency(BCC_APK_RESOURCE, sourceResName, sourceSHA1);
  }

  //if (libraryBC) {
  //  reader.addDependency(BCC_APK_RESOURCE, libraryResName, librarySHA1);
  //}

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
#endif


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
    if (mCompiled->readBC(sourceResName, sourceBC, sourceSize, 0) != 0) {
      LOGE("Unable to readBC, bitcode=%p, size=%lu\n",
           sourceBC, (unsigned long)sourceSize);
      return 1;
    }
    LOGI("Load sourceBC\n");
  } else if (sourceModule) {
    if (mCompiled->readModule(NULL, sourceModule, 0) != 0) {
      return 1;
    }
    LOGI("Load sourceModule\n");
  }

  // Link the source module with the library module
  if (librarySize == 1 /* link against file */ ||
      libraryBC        /* link against buffer */) {
    if (mCompiled->linkBC(NULL, libraryBC, librarySize, 0) != 0) {
      return 1;
    }

    LOGE("Load Library\n");
  }

  // Compile and JIT the code
  if (mCompiled->compile() != 0) {
    LOGE("Unable to compile.\n");
    return 1;
  }

#if USE_CACHE
  // TODO(logan): Write the cache out
  if (mCachePath && !getBooleanProp("debug.bcc.nocache")) {
    FileHandle file;

    // Remove the file if it already exists before writing the new file.
    // The old file may still be mapped elsewhere in memory and we do not want
    // to modify its contents.  (The same script may be running concurrently in
    // the same process or a different process!)
    ::unlink(mCachePath);

    if (file.open(mCachePath, OpenMode::Write) >= 0) {
      CacheWriter writer;

      // Dependencies
#if USE_LIBBCC_SHA1SUM
      writer.addDependency(BCC_FILE_RESOURCE, pathLibBCC, sha1LibBCC);
#endif
      writer.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);

      if (sourceBC) {
        writer.addDependency(BCC_APK_RESOURCE, sourceResName, sourceSHA1);
      }

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

        if (unlink(mCachePath) != 0) {
          LOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               mCachePath, strerror(errno));
        }
      }
    }
  }
#endif // USE_CACHE

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
  case ScriptStatus::Compiled:  return mCompiled->lookup(name);
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->lookup(name);
#endif

  default:
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }
}


size_t Script::getExportVarCount() const {
  switch (mStatus) {
  case ScriptStatus::Compiled:  return mCompiled->getExportVarCount();
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getExportVarCount();
#endif
  default:                      return 0;
  }
}


size_t Script::getExportFuncCount() const {
  switch (mStatus) {
  case ScriptStatus::Compiled:  return mCompiled->getExportFuncCount();
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getExportFuncCount();
#endif
  default:                      return 0;
  }
}


size_t Script::getPragmaCount() const {
  switch (mStatus) {
  case ScriptStatus::Compiled:  return mCompiled->getPragmaCount();
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getPragmaCount();
#endif
  default:                      return 0;
  }
}


size_t Script::getFuncCount() const {
  switch (mStatus) {
  case ScriptStatus::Compiled:  return mCompiled->getFuncCount();
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getFuncCount();
#endif
  default:                      return 0;
  }
}


size_t Script::getObjectSlotCount() const {
  switch (mStatus) {
  case ScriptStatus::Compiled:  return mCompiled->getObjectSlotCount();
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getObjectSlotCount();
#endif
  default:                      return 0;
  }
}


void Script::getExportVarList(size_t varListSize, void **varList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
  case ScriptStatus::STATUS: \
    m##STATUS->getExportVarList(varListSize, varList); \
    break;

#if USE_CACHE
  DELEGATE(Cached);
#endif

  DELEGATE(Compiled);
#undef DELEGATE

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getExportFuncList(size_t funcListSize, void **funcList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
  case ScriptStatus::STATUS: \
    m##STATUS->getExportFuncList(funcListSize, funcList); \
    break;

#if USE_CACHE
  DELEGATE(Cached);
#endif

  DELEGATE(Compiled);
#undef DELEGATE

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getPragmaList(size_t pragmaListSize,
                           char const **keyList,
                           char const **valueList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
  case ScriptStatus::STATUS: \
    m##STATUS->getPragmaList(pragmaListSize, keyList, valueList); \
    break;

#if USE_CACHE
  DELEGATE(Cached);
#endif

  DELEGATE(Compiled);
#undef DELEGATE

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getFuncInfoList(size_t funcInfoListSize,
                             FuncInfo *funcInfoList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
  case ScriptStatus::STATUS: \
    m##STATUS->getFuncInfoList(funcInfoListSize, funcInfoList); \
    break;

#if USE_CACHE
  DELEGATE(Cached);
#endif

  DELEGATE(Compiled);
#undef DELEGATE

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


void Script::getObjectSlotList(size_t objectSlotListSize,
                               uint32_t *objectSlotList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
  case ScriptStatus::STATUS: \
    m##STATUS->getObjectSlotList(objectSlotListSize, objectSlotList); \
    break;

#if USE_CACHE
  DELEGATE(Cached);
#endif

  DELEGATE(Compiled);
#undef DELEGATE

  default:
    mErrorCode = BCC_INVALID_OPERATION;
  }
}


char *Script::getContext() {
  switch (mStatus) {
#if USE_CACHE
  case ScriptStatus::Cached:    return mCached->getContext();
#endif
  case ScriptStatus::Compiled:  return mCompiled->getContext();

  default:
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }
}


int Script::registerSymbolCallback(BCCSymbolLookupFn pFn, void *pContext) {
  mpExtSymbolLookupFn = pFn;
  mpExtSymbolLookupFnContext = pContext;

  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }
  return 0;
}

} // namespace bcc
