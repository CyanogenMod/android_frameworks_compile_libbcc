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

#include "Script.h"

#include "Config.h"

#if USE_OLD_JIT
#include "OldJIT/CacheReader.h"
#include "OldJIT/CacheWriter.h"
#endif

#include "MCCacheReader.h"
#include "MCCacheWriter.h"

#if USE_OLD_JIT
#include "OldJIT/ContextManager.h"
#endif

#include "DebugHelper.h"
#include "FileHandle.h"
#include "ScriptCompiled.h"
#include "ScriptCached.h"
#include "Sha1Helper.h"
#include "SourceInfo.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

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

  for (size_t i = 0; i < 2; ++i) {
    delete mSourceList[i];
  }
}


int Script::addSourceBC(size_t idx,
                        char const *resName,
                        const char *bitcode,
                        size_t bitcodeSize,
                        unsigned long flags) {

  if (!resName) {
    mErrorCode = BCC_INVALID_VALUE;
    LOGE("Invalid argument: resName = NULL\n");
    return 1;
  }

  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Bad operation: Adding source after bccPrepareExecutable\n");
    return 1;
  }

  if (!bitcode) {
    mErrorCode = BCC_INVALID_VALUE;
    LOGE("Invalid argument: bitcode = NULL\n");
    return 1;
  }

  mSourceList[idx] = SourceInfo::createFromBuffer(resName,
                                                  bitcode, bitcodeSize,
                                                  flags);

  if (!mSourceList[idx]) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    LOGE("Out of memory while adding source bitcode\n");
    return 1;
  }

  return 0;
}


int Script::addSourceModule(size_t idx,
                            llvm::Module *module,
                            unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Bad operation: Adding source after bccPrepareExecutable\n");
    return 1;
  }

  if (!module) {
    mErrorCode = BCC_INVALID_VALUE;
    LOGE("Invalid argument: module = NULL\n");
    return 1;
  }

  mSourceList[idx] = SourceInfo::createFromModule(module, flags);

  if (!mSourceList[idx]) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    LOGE("Out of memory when add source module\n");
    return 1;
  }

  return 0;
}


int Script::addSourceFile(size_t idx,
                          char const *path,
                          unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Bad operation: Adding source after bccPrepareExecutable\n");
    return 1;
  }

  if (!path) {
    mErrorCode = BCC_INVALID_VALUE;
    LOGE("Invalid argument: path = NULL\n");
    return 1;
  }

  struct stat sb;
  if (stat(path, &sb) != 0) {
    mErrorCode = BCC_INVALID_VALUE;
    LOGE("File not found: %s\n", path);
    return 1;
  }

  mSourceList[idx] = SourceInfo::createFromFile(path, flags);

  if (!mSourceList[idx]) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    LOGE("Out of memory while adding source file\n");
    return 1;
  }

  return 0;
}

int Script::prepareSharedObject(char const *cacheDir,
                                char const *cacheName,
                                unsigned long flags) {
#if USE_CACHE
  if (cacheDir && cacheName) {
    // Set Cache Directory and File Name
    mCacheDir = cacheDir;
    mCacheName = cacheName;

    if (!mCacheDir.empty() && *mCacheDir.rbegin() != '/') {
      mCacheDir.push_back('/'); // Ensure mCacheDir is end with '/'
    }

    // Check Cache File
    if (internalLoadCache(true) == 0) {
      return 0;
    }
  }
#endif
  int status = internalCompile(true);
  if (status != 0) {
    LOGE("LLVM error message: %s\n", getCompilerErrorMessage());
  }
  return status;
}


int Script::prepareExecutable(char const *cacheDir,
                              char const *cacheName,
                              unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    LOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

#if USE_CACHE
  if (cacheDir && cacheName) {
    // Set Cache Directory and File Name
    mCacheDir = cacheDir;
    mCacheName = cacheName;

    if (!mCacheDir.empty() && *mCacheDir.rbegin() != '/') {
      mCacheDir.push_back('/'); // Ensure mCacheDir is end with '/'
    }

    // Load Cache File
    if (internalLoadCache(false) == 0) {
      return 0;
    }
  }
#endif

  int status = internalCompile(false);
  if (status != 0) {
    LOGE("LLVM error message: %s\n", getCompilerErrorMessage());
  }
  return status;
}


#if USE_CACHE
int Script::internalLoadCache(bool checkOnly) {
  if (getBooleanProp("debug.bcc.nocache")) {
    // Android system environment property disable the cache mechanism by
    // setting "debug.bcc.nocache".  So we will not load the cache file any
    // way.
    return 1;
  }

  if (mCacheDir.empty() || mCacheName.empty()) {
    // The application developer has not specify the cachePath, so
    // we don't know where to open the cache file.
    return 1;
  }

#if USE_OLD_JIT
  std::string objPath(mCacheDir + mCacheName + ".jit-image");
  std::string infoPath(mCacheDir + mCacheName + ".oBCC"); // TODO: .info instead
#elif USE_MCJIT
  std::string objPath(mCacheDir + mCacheName + ".o");
  std::string infoPath(mCacheDir + mCacheName + ".info");
#endif

  FileHandle objFile;
  if (objFile.open(objPath.c_str(), OpenMode::Read) < 0) {
    // Unable to open the executable file in read mode.
    return 1;
  }

  FileHandle infoFile;
  if (infoFile.open(infoPath.c_str(), OpenMode::Read) < 0) {
    // Unable to open the metadata information file in read mode.
    return 1;
  }

#if USE_OLD_JIT
  CacheReader reader;
#elif USE_MCJIT
  MCCacheReader reader;

  // Register symbol lookup function
  if (mpExtSymbolLookupFn) {
    reader.registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }
#endif

  // Dependencies
  reader.addDependency(BCC_FILE_RESOURCE, pathLibBCC_SHA1, sha1LibBCC_SHA1);
  reader.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);

  for (size_t i = 0; i < 2; ++i) {
    if (mSourceList[i]) {
      mSourceList[i]->introDependency(reader);
    }
  }

  if (checkOnly)
    return !reader.checkCacheFile(&objFile, &infoFile, this);

  // Read cache file
  ScriptCached *cached = reader.readCacheFile(&objFile, &infoFile, this);

  if (!cached) {
    mIsContextSlotNotAvail = reader.isContextSlotNotAvail();
    return 1;
  }

  mCached = cached;
  mStatus = ScriptStatus::Cached;

  // Dirty hack for libRS.
  // TODO(all):  This dirty hack should be removed in the future.
  if (!cached->isLibRSThreadable() && mpExtSymbolLookupFn) {
    mpExtSymbolLookupFn(mpExtSymbolLookupFnContext, "__clearThreadable");
  }

  return 0;
}
#endif

int Script::internalCompile(bool compileOnly) {
  // Create the ScriptCompiled object
  mCompiled = new (std::nothrow) ScriptCompiled(this);

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

  // Parse Bitcode File (if necessary)
  for (size_t i = 0; i < 2; ++i) {
    if (mSourceList[i] && mSourceList[i]->prepareModule(mCompiled) != 0) {
      LOGE("Unable to parse bitcode for source[%lu]\n", (unsigned long)i);
      return 1;
    }
  }

  // Set the main source module
  if (!mSourceList[0] || !mSourceList[0]->getModule()) {
    LOGE("Source bitcode is not setted.\n");
    return 1;
  }

  if (mCompiled->readModule(mSourceList[0]->takeModule()) != 0) {
    LOGE("Unable to read source module\n");
    return 1;
  }

  // Link the source module with the library module
  if (mSourceList[1]) {
    if (mCompiled->linkModule(mSourceList[1]->takeModule()) != 0) {
      LOGE("Unable to link library module\n");
      return 1;
    }
  }

  // Compile and JIT the code
  if (mCompiled->compile(compileOnly) != 0) {
    LOGE("Unable to compile.\n");
    return 1;
  }

#if USE_CACHE
  // Note: If we re-compile the script because the cached context slot not
  // available, then we don't have to write the cache.

  // Note: If the address of the context is not in the context slot, then
  // we don't have to cache it.

  if (!mCacheDir.empty() &&
      !mCacheName.empty() &&
#if USE_OLD_JIT
      !mIsContextSlotNotAvail &&
      ContextManager::get().isManagingContext(getContext()) &&
#endif
      !getBooleanProp("debug.bcc.nocache")) {

#if USE_OLD_JIT
    std::string objPath(mCacheDir + mCacheName + ".jit-image");
    std::string infoPath(mCacheDir + mCacheName + ".oBCC");
#elif USE_MCJIT
    std::string objPath(mCacheDir + mCacheName + ".o");
    std::string infoPath(mCacheDir + mCacheName + ".info");
#endif


    // Remove the file if it already exists before writing the new file.
    // The old file may still be mapped elsewhere in memory and we do not want
    // to modify its contents.  (The same script may be running concurrently in
    // the same process or a different process!)
    ::unlink(objPath.c_str());
#if !USE_OLD_JIT && USE_MCJIT
    ::unlink(infoPath.c_str());
#endif

    FileHandle objFile;
    FileHandle infoFile;

    if (objFile.open(objPath.c_str(), OpenMode::Write) >= 0 &&
        infoFile.open(infoPath.c_str(), OpenMode::Write) >= 0) {

#if USE_OLD_JIT
      CacheWriter writer;
#elif USE_MCJIT
      MCCacheWriter writer;
#endif

#ifdef TARGET_BUILD
      // Dependencies
      writer.addDependency(BCC_FILE_RESOURCE, pathLibBCC_SHA1, sha1LibBCC_SHA1);
      writer.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);
#endif

      for (size_t i = 0; i < 2; ++i) {
        if (mSourceList[i]) {
          mSourceList[i]->introDependency(writer);
        }
      }

      // libRS is threadable dirty hack
      // TODO: This should be removed in the future
      uint32_t libRS_threadable = 0;
      if (mpExtSymbolLookupFn) {
        libRS_threadable =
          (uint32_t)mpExtSymbolLookupFn(mpExtSymbolLookupFnContext,
                                        "__isThreadable");
      }

      if (!writer.writeCacheFile(&objFile, &infoFile, this, libRS_threadable)) {
        objFile.truncate();
        objFile.close();

        if (unlink(objPath.c_str()) != 0) {
          LOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               objPath.c_str(), strerror(errno));
        }

        infoFile.truncate();
        infoFile.close();

        if (unlink(infoPath.c_str()) != 0) {
          LOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               infoPath.c_str(), strerror(errno));
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
    case ScriptStatus::Compiled: {
      return mCompiled->lookup(name);
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->lookup(name);
    }
#endif

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
      return NULL;
    }
  }
}


size_t Script::getExportVarCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportVarCount();
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getExportVarCount();
    }
#endif

    default: {
      return 0;
    }
  }
}


size_t Script::getExportFuncCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportFuncCount();
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getExportFuncCount();
    }
#endif

    default: {
      return 0;
    }
  }
}


size_t Script::getPragmaCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getPragmaCount();
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getPragmaCount();
    }
#endif

    default: {
      return 0;
    }
  }
}


size_t Script::getFuncCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getFuncCount();
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getFuncCount();
    }
#endif

    default: {
      return 0;
    }
  }
}


size_t Script::getObjectSlotCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getObjectSlotCount();
    }

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getObjectSlotCount();
    }
#endif

    default: {
      return 0;
    }
  }
}


void Script::getExportVarList(size_t varListSize, void **varList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                           \
      m##STATUS->getExportVarList(varListSize, varList); \
      break;

#if USE_CACHE
    DELEGATE(Cached);
#endif

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void Script::getExportVarNameList(std::vector<std::string> &varList) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportVarNameList(varList);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void Script::getExportFuncList(size_t funcListSize, void **funcList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                              \
      m##STATUS->getExportFuncList(funcListSize, funcList); \
      break;

#if USE_CACHE
    DELEGATE(Cached);
#endif

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void Script::getExportFuncNameList(std::vector<std::string> &funcList) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportFuncNameList(funcList);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void Script::getPragmaList(size_t pragmaListSize,
                           char const **keyList,
                           char const **valueList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                                      \
      m##STATUS->getPragmaList(pragmaListSize, keyList, valueList); \
      break;

#if USE_CACHE
    DELEGATE(Cached);
#endif

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void Script::getFuncInfoList(size_t funcInfoListSize,
                             FuncInfo *funcInfoList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                                    \
      m##STATUS->getFuncInfoList(funcInfoListSize, funcInfoList); \
      break;

#if USE_CACHE
    DELEGATE(Cached);
#endif

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void Script::getObjectSlotList(size_t objectSlotListSize,
                               uint32_t *objectSlotList) {
  switch (mStatus) {
#define DELEGATE(STATUS)     \
    case ScriptStatus::STATUS:                                          \
      m##STATUS->getObjectSlotList(objectSlotListSize, objectSlotList); \
      break;

#if USE_CACHE
    DELEGATE(Cached);
#endif

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


#if USE_OLD_JIT
char *Script::getContext() {
  switch (mStatus) {

#if USE_CACHE
    case ScriptStatus::Cached: {
      return mCached->getContext();
    }
#endif

    case ScriptStatus::Compiled: {
      return mCompiled->getContext();
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
      return NULL;
    }
  }
}
#endif


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

#if USE_MCJIT
size_t Script::getELFSize() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getELFSize();
    }

    default: {
      return 0;
    }
  }
}

const char *Script::getELF() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getELF();
    }

    default: {
      return NULL;
    }
  }
}
#endif

} // namespace bcc
