/*
 * Copyright 2012, The Android Open Source Project
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

#include "Script.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <new>
#include <cstring>

#include <llvm/ADT/STLExtras.h>

#include <cutils/properties.h>

#include "Config.h"

#include "MCCacheReader.h"
#include "MCCacheWriter.h"
#include "CompilerOption.h"

#include "DebugHelper.h"
#include "FileHandle.h"
#include "GDBJITRegistrar.h"
#include "ScriptCompiled.h"
#include "ScriptCached.h"
#include "Sha1Helper.h"
#include "Source.h"

namespace {

bool getBooleanProp(const char *str) {
  char buf[PROPERTY_VALUE_MAX];
  property_get(str, buf, "0");
  return strcmp(buf, "0") != 0;
}

} // namespace anonymous

namespace bcc {

RSScript::SourceDependency::SourceDependency(MCO_ResourceType pSourceType,
                                             const std::string &pSourceName,
                                             const uint8_t *pSHA1)
  : mSourceType(pSourceType), mSourceName(pSourceName) {
  ::memcpy(mSHA1, pSHA1, sizeof(mSHA1));
  return;
}

RSScript::RSScript(Source &pSource)
  : Script(pSource),
    mpExtSymbolLookupFn(NULL),
    mpExtSymbolLookupFnContext(NULL) {
  resetState();
  return;
}

RSScript::~RSScript() {
  switch (mStatus) {
  case ScriptStatus::Compiled:
    delete mCompiled;
    break;

  case ScriptStatus::Cached:
    delete mCached;
    break;

  default:
    break;
  }
  llvm::DeleteContainerPointers(mSourceDependencies);
}

void RSScript::resetState() {
  mErrorCode = BCC_NO_ERROR;
  mStatus = ScriptStatus::Unknown;
  mObjectType = ScriptObject::Unknown;
  mIsContextSlotNotAvail = false;
  // FIXME: mpExtSymbolLookupFn and mpExtSymbolLookupFnContext should be assign
  //        to NULL during state resetting.
  //mpExtSymbolLookupFn = NULL;
  //mpExtSymbolLookupFnContext = NULL;
  llvm::DeleteContainerPointers(mSourceDependencies);
  return;
}


bool RSScript::doReset() {
  resetState();
  return true;
}

bool RSScript::addSourceDependency(MCO_ResourceType pSourceType,
                                   const std::string &pSourceName,
                                   const uint8_t *pSHA1) {
  SourceDependency *source_dep =
      new (std::nothrow) SourceDependency(pSourceType, pSourceName, pSHA1);
  if (source_dep == NULL) {
    ALOGE("Out of memory when record dependency information of `%s'!",
          pSourceName.c_str());
    return false;
  }

  mSourceDependencies.push_back(source_dep);
  return true;
}

int RSScript::prepareRelocatable(char const *objPath,
                               llvm::Reloc::Model RelocModel,
                               unsigned long flags) {
  CompilerOption option;
  option.RelocModelOpt = RelocModel;
  option.LoadAfterCompile = false;

  int status = internalCompile(option);
  if (status != 0) {
    ALOGE("LLVM error message: %s\n", getCompilerErrorMessage());
    return status;
  }

  FileHandle objFile;
  if (objFile.open(objPath, OpenMode::Write) < 1) {
    ALOGE("Failed to open %s for write.\n", objPath);
    return 1;
  }

  if (static_cast<size_t>(objFile.write(getELF(),
                                        getELFSize())) != getELFSize()) {
      objFile.close();
      ::unlink(objPath);
      ALOGE("Unable to write ELF to file %s.\n", objPath);
      return false;
  }

  mObjectType = ScriptObject::Relocatable;

  return 0;
}


int RSScript::prepareSharedObject(char const *objPath,
                                char const *dsoPath,
                                unsigned long flags) {
  // TODO: Support cached shared object.
  return 1;
}


int RSScript::prepareExecutable(char const *cacheDir,
                              char const *cacheName,
                              unsigned long flags) {
  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    ALOGE("Invalid operation: %s\n", __func__);
    return 1;
  }

  int status = internalLoadCache(cacheDir, cacheName, /* checkOnly */ false);

  if (status != 0) {
    CompilerOption option;
    status = internalCompile(option);

    if (status != 0) {
      ALOGE("LLVM error message: %s\n", getCompilerErrorMessage());
      return status;
    }

    status = writeCache();
    if (status != 0) {
      ALOGE("Failed to write the cache for %s\n", cacheName);
      return status;
    }
  }

  // FIXME: Registration can be conditional on the presence of debug metadata
  registerObjectWithGDB(getELF(), getELFSize()); // thread-safe registration

  mObjectType = ScriptObject::Executable;

  return status;
}

int RSScript::internalLoadCache(char const *cacheDir, char const *cacheName,
                              bool checkOnly) {
  if ((cacheDir == NULL) || (cacheName == NULL)) {
    return 1;
  }

  // Set cache file Name
  mCacheName = cacheName;

  // Santize mCacheDir. Ensure that mCacheDir ends with '/'.
  mCacheDir = cacheDir;
  if (!mCacheDir.empty() && *mCacheDir.rbegin() != '/') {
    mCacheDir.push_back('/');
  }

  if (!isCacheable()) {
    return 1;
  }

  std::string objPath = getCachedObjectPath();
  std::string infoPath = getCacheInfoPath();

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

  MCCacheReader reader;

  // Register symbol lookup function
  if (mpExtSymbolLookupFn) {
    reader.registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  // Dependencies
  reader.addDependency(BCC_FILE_RESOURCE, pathLibBCC_SHA1, sha1LibBCC_SHA1);
  reader.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);

  for (unsigned i = 0; i < mSourceDependencies.size(); i++) {
    const SourceDependency *source_dep = mSourceDependencies[i];
    reader.addDependency(source_dep->getSourceType(),
                         source_dep->getSourceName(),
                         source_dep->getSHA1Checksum());
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

int RSScript::internalCompile(const CompilerOption &option) {
  // Create the ScriptCompiled object
  mCompiled = new (std::nothrow) ScriptCompiled(this);

  if (!mCompiled) {
    mErrorCode = BCC_OUT_OF_MEMORY;
    ALOGE("Out of memory: %s %d\n", __FILE__, __LINE__);
    return 1;
  }

  mStatus = ScriptStatus::Compiled;

  // Register symbol lookup function
  if (mpExtSymbolLookupFn) {
    mCompiled->registerSymbolCallback(mpExtSymbolLookupFn,
                                      mpExtSymbolLookupFnContext);
  }

  // Set the main source module
  if (mCompiled->readModule(getSource().getModule()) != 0) {
    ALOGE("Unable to read source module\n");
    return 1;
  }

  // Compile and JIT the code
  if (mCompiled->compile(option) != 0) {
    ALOGE("Unable to compile.\n");
    return 1;
  }

  return 0;
}

int RSScript::writeCache() {
  // Not compiled script or encountered error during the compilation.
  if ((mStatus != ScriptStatus::Compiled) ||
      (getCompilerErrorMessage() == NULL))
    return 1;

  // Note: If we re-compile the script because the cached context slot not
  // available, then we don't have to write the cache.

  // Note: If the address of the context is not in the context slot, then
  // we don't have to cache it.

  if (isCacheable()) {

    std::string objPath = getCachedObjectPath();
    std::string infoPath = getCacheInfoPath();

    // Remove the file if it already exists before writing the new file.
    // The old file may still be mapped elsewhere in memory and we do not want
    // to modify its contents.  (The same script may be running concurrently in
    // the same process or a different process!)
    ::unlink(objPath.c_str());
    ::unlink(infoPath.c_str());

    FileHandle objFile;
    FileHandle infoFile;

    if (objFile.open(objPath.c_str(), OpenMode::Write) >= 0 &&
        infoFile.open(infoPath.c_str(), OpenMode::Write) >= 0) {

      MCCacheWriter writer;

#ifdef TARGET_BUILD
      // Dependencies
      writer.addDependency(BCC_FILE_RESOURCE, pathLibBCC_SHA1, sha1LibBCC_SHA1);
      writer.addDependency(BCC_FILE_RESOURCE, pathLibRS, sha1LibRS);
#endif

      for (unsigned i = 0; i < mSourceDependencies.size(); i++) {
        const SourceDependency *source_dep = mSourceDependencies[i];
        writer.addDependency(source_dep->getSourceType(),
                             source_dep->getSourceName(),
                             source_dep->getSHA1Checksum());
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
          ALOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               objPath.c_str(), strerror(errno));
        }

        infoFile.truncate();
        infoFile.close();

        if (unlink(infoPath.c_str()) != 0) {
          ALOGE("Unable to remove the invalid cache file: %s. (reason: %s)\n",
               infoPath.c_str(), strerror(errno));
        }
      }
    }
  }

  return 0;
}


char const *RSScript::getCompilerErrorMessage() {
  if (mStatus != ScriptStatus::Compiled) {
    mErrorCode = BCC_INVALID_OPERATION;
    return NULL;
  }

  return mCompiled->getCompilerErrorMessage();
}


void *RSScript::lookup(const char *name) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->lookup(name);
    }

    case ScriptStatus::Cached: {
      return mCached->lookup(name);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
      return NULL;
    }
  }
}


size_t RSScript::getExportVarCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportVarCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getExportVarCount();
    }

    default: {
      return 0;
    }
  }
}


size_t RSScript::getExportFuncCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportFuncCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getExportFuncCount();
    }

    default: {
      return 0;
    }
  }
}


size_t RSScript::getExportForEachCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportForEachCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getExportForEachCount();
    }

    default: {
      return 0;
    }
  }
}


size_t RSScript::getPragmaCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getPragmaCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getPragmaCount();
    }

    default: {
      return 0;
    }
  }
}


size_t RSScript::getFuncCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getFuncCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getFuncCount();
    }

    default: {
      return 0;
    }
  }
}


size_t RSScript::getObjectSlotCount() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getObjectSlotCount();
    }

    case ScriptStatus::Cached: {
      return mCached->getObjectSlotCount();
    }

    default: {
      return 0;
    }
  }
}


void RSScript::getExportVarList(size_t varListSize, void **varList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                           \
      m##STATUS->getExportVarList(varListSize, varList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void RSScript::getExportVarNameList(std::vector<std::string> &varList) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportVarNameList(varList);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void RSScript::getExportFuncList(size_t funcListSize, void **funcList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                              \
      m##STATUS->getExportFuncList(funcListSize, funcList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void RSScript::getExportFuncNameList(std::vector<std::string> &funcList) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportFuncNameList(funcList);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void RSScript::getExportForEachList(size_t funcListSize, void **funcList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                                 \
      m##STATUS->getExportForEachList(funcListSize, funcList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void RSScript::getExportForEachNameList(std::vector<std::string> &forEachList) {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getExportForEachNameList(forEachList);
    }

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}

void RSScript::getPragmaList(size_t pragmaListSize,
                           char const **keyList,
                           char const **valueList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                                      \
      m##STATUS->getPragmaList(pragmaListSize, keyList, valueList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void RSScript::getFuncInfoList(size_t funcInfoListSize,
                             FuncInfo *funcInfoList) {
  switch (mStatus) {
#define DELEGATE(STATUS) \
    case ScriptStatus::STATUS:                                    \
      m##STATUS->getFuncInfoList(funcInfoListSize, funcInfoList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


void RSScript::getObjectSlotList(size_t objectSlotListSize,
                               uint32_t *objectSlotList) {
  switch (mStatus) {
#define DELEGATE(STATUS)     \
    case ScriptStatus::STATUS:                                          \
      m##STATUS->getObjectSlotList(objectSlotListSize, objectSlotList); \
      break;

    DELEGATE(Cached);

    DELEGATE(Compiled);
#undef DELEGATE

    default: {
      mErrorCode = BCC_INVALID_OPERATION;
    }
  }
}


int RSScript::registerSymbolCallback(BCCSymbolLookupFn pFn, void *pContext) {
  mpExtSymbolLookupFn = pFn;
  mpExtSymbolLookupFnContext = pContext;

  if (mStatus != ScriptStatus::Unknown) {
    mErrorCode = BCC_INVALID_OPERATION;
    ALOGE("Invalid operation: %s\n", __func__);
    return 1;
  }
  return 0;
}

bool RSScript::isCacheable() const {
  if (getBooleanProp("debug.bcc.nocache")) {
    // Android system environment property: Disables the cache mechanism by
    // setting "debug.bcc.nocache".  So we will not load the cache file any
    // way.
    return false;
  }

  if (mCacheDir.empty() || mCacheName.empty()) {
    // The application developer has not specified the cachePath, so
    // we don't know where to open the cache file.
    return false;
  }

  return true;
}

size_t RSScript::getELFSize() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getELFSize();
    }

    case ScriptStatus::Cached: {
      return mCached->getELFSize();
    }

    default: {
      return 0;
    }
  }
}

const char *RSScript::getELF() const {
  switch (mStatus) {
    case ScriptStatus::Compiled: {
      return mCompiled->getELF();
    }

    case ScriptStatus::Cached: {
      return mCached->getELF();
    }

    default: {
      return NULL;
    }
  }
}

} // namespace bcc
