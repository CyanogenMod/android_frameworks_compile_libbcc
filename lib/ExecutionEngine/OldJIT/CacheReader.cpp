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

#include "CacheReader.h"

#include "ContextManager.h"
#include "DebugHelper.h"
#include "FileHandle.h"
#include "ScriptCached.h"

#include <bcc/bcc_cache.h>

#include <llvm/ADT/OwningPtr.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <utility>
#include <vector>

#include <new>

#include <stdlib.h>
#include <string.h>

using namespace std;


namespace bcc {

CacheReader::~CacheReader() {
  if (mpHeader) { free(mpHeader); }
  if (mpCachedDependTable) { free(mpCachedDependTable); }
  if (mpPragmaList) { free(mpPragmaList); }
  if (mpFuncTable) { free(mpFuncTable); }
}

ScriptCached *CacheReader::readCacheFile(FileHandle *objFile,
                                         FileHandle *infoFile,
                                         Script *S) {
  // Check file handle
  if (!objFile || objFile->getFD() < 0 ||
      !infoFile || infoFile->getFD() < 0) {
    return NULL;
  }

  mObjFile = objFile;
  mInfoFile = infoFile;

  // Allocate ScriptCached object
  mpResult.reset(new (nothrow) ScriptCached(S));

  if (!mpResult) {
    LOGE("Unable to allocate ScriptCached object.\n");
    return NULL;
  }

  bool result = checkFileSize()
             && readHeader()
             && checkHeader()
             && checkMachineIntType()
             && checkSectionOffsetAndSize()
             && readStringPool()
             && checkStringPool()
             && readDependencyTable()
             && checkDependency()
             && readExportVarList()
             && readExportFuncList()
             && readPragmaList()
             && readFuncTable()
             && readObjectSlotList()
             && readContext()
             && checkContext()
             //&& readRelocationTable()
             //&& relocate()
             ;

  return result ? mpResult.take() : NULL;
}


bool CacheReader::checkFileSize() {
  struct stat stfile;

  if (fstat(mInfoFile->getFD(), &stfile) < 0) {
    LOGE("Unable to stat metadata information file.\n");
    return false;
  }

  mInfoFileSize = stfile.st_size;

  if (mInfoFileSize < (off_t)sizeof(OBCC_Header)) {
    LOGE("Metadata information file is too small to be correct.\n");
    return false;
  }

  if (fstat(mObjFile->getFD(), &stfile) < 0) {
    LOGE("Unable to stat executable file.\n");
    return false;
  }

  if (stfile.st_size < (off_t)ContextManager::ContextSize) {
    LOGE("Executable file is too small to be correct.\n");
    return false;
  }

  return true;
}


bool CacheReader::readHeader() {
  if (mInfoFile->seek(0, SEEK_SET) != 0) {
    LOGE("Unable to seek to 0. (reason: %s)\n", strerror(errno));
    return false;
  }

  mpHeader = (OBCC_Header *)malloc(sizeof(OBCC_Header));
  if (!mpHeader) {
    LOGE("Unable to allocate for cache header.\n");
    return false;
  }

  if (mInfoFile->read((char *)mpHeader, sizeof(OBCC_Header)) !=
      (ssize_t)sizeof(OBCC_Header)) {
    LOGE("Unable to read cache header.\n");
    return false;
  }

  // Dirty hack for libRS.
  // TODO(all): This should be removed in the future.
  if (mpHeader->libRS_threadable) {
    mpResult->mLibRSThreadable = true;
  }

  return true;
}


bool CacheReader::checkHeader() {
  if (memcmp(mpHeader->magic, OBCC_MAGIC, 4) != 0) {
    LOGE("Bad magic word\n");
    return false;
  }

  if (memcmp(mpHeader->version, OBCC_VERSION, 4) != 0) {
    mpHeader->version[4 - 1] = '\0'; // ensure c-style string terminated
    LOGI("Cache file format version mismatch: now %s cached %s\n",
         OBCC_VERSION, mpHeader->version);
    return false;
  }
  return true;
}


bool CacheReader::checkMachineIntType() {
  uint32_t number = 0x00000001;

  bool isLittleEndian = (*reinterpret_cast<char *>(&number) == 1);
  if ((isLittleEndian && mpHeader->endianness != 'e') ||
      (!isLittleEndian && mpHeader->endianness != 'E')) {
    LOGE("Machine endianness mismatch.\n");
    return false;
  }

  if ((unsigned int)mpHeader->sizeof_off_t != sizeof(off_t) ||
      (unsigned int)mpHeader->sizeof_size_t != sizeof(size_t) ||
      (unsigned int)mpHeader->sizeof_ptr_t != sizeof(void *)) {
    LOGE("Machine integer size mismatch.\n");
    return false;
  }

  return true;
}


bool CacheReader::checkSectionOffsetAndSize() {
#define CHECK_SECTION_OFFSET(NAME)                                          \
  do {                                                                      \
    off_t offset = mpHeader-> NAME##_offset;                                \
    off_t size = (off_t)mpHeader-> NAME##_size;                             \
                                                                            \
    if (mInfoFileSize < offset || mInfoFileSize < offset + size) {          \
      LOGE(#NAME " section overflow.\n");                                   \
      return false;                                                         \
    }                                                                       \
                                                                            \
    if (offset % sizeof(int) != 0) {                                        \
      LOGE(#NAME " offset must aligned to %d.\n", (int)sizeof(int));        \
      return false;                                                         \
    }                                                                       \
                                                                            \
    if (size < static_cast<off_t>(sizeof(size_t))) {                        \
      LOGE(#NAME " size is too small to be correct.\n");                    \
      return false;                                                         \
    }                                                                       \
  } while (0)

  CHECK_SECTION_OFFSET(str_pool);
  CHECK_SECTION_OFFSET(depend_tab);
  //CHECK_SECTION_OFFSET(reloc_tab);
  CHECK_SECTION_OFFSET(export_var_list);
  CHECK_SECTION_OFFSET(export_func_list);
  CHECK_SECTION_OFFSET(pragma_list);

#undef CHECK_SECTION_OFFSET

  // TODO(logan): Move this to some where else.
  long pagesize = sysconf(_SC_PAGESIZE);
  if ((uintptr_t)mpHeader->context_cached_addr % pagesize != 0) {
    LOGE("cached address is not aligned to pagesize.\n");
    return false;
  }

  return true;
}


#define CACHE_READER_READ_SECTION(TYPE, AUTO_MANAGED_HOLDER, NAME)          \
  TYPE *NAME##_raw = (TYPE *)malloc(mpHeader->NAME##_size);                 \
                                                                            \
  if (!NAME##_raw) {                                                        \
    LOGE("Unable to allocate for " #NAME "\n");                             \
    return false;                                                           \
  }                                                                         \
                                                                            \
  /* We have to ensure that some one will deallocate NAME##_raw */          \
  AUTO_MANAGED_HOLDER = NAME##_raw;                                         \
                                                                            \
  if (mInfoFile->seek(mpHeader->NAME##_offset, SEEK_SET) == -1) {           \
    LOGE("Unable to seek to " #NAME " section\n");                          \
    return false;                                                           \
  }                                                                         \
                                                                            \
  if (mInfoFile->read(reinterpret_cast<char *>(NAME##_raw),                 \
                  mpHeader->NAME##_size) != (ssize_t)mpHeader->NAME##_size) \
  {                                                                         \
    LOGE("Unable to read " #NAME ".\n");                                    \
    return false;                                                           \
  }


bool CacheReader::readStringPool() {
  CACHE_READER_READ_SECTION(OBCC_StringPool,
                            mpResult->mpStringPoolRaw, str_pool);

  char *str_base = reinterpret_cast<char *>(str_pool_raw);

  vector<char const *> &pool = mpResult->mStringPool;
  for (size_t i = 0; i < str_pool_raw->count; ++i) {
    char *str = str_base + str_pool_raw->list[i].offset;
    pool.push_back(str);
  }

  return true;
}


bool CacheReader::checkStringPool() {
  OBCC_StringPool *poolR = mpResult->mpStringPoolRaw;
  vector<char const *> &pool = mpResult->mStringPool;

  // Ensure that every c-style string is ended with '\0'
  for (size_t i = 0; i < poolR->count; ++i) {
    if (pool[i][poolR->list[i].length] != '\0') {
      LOGE("The %lu-th string does not end with '\\0'.\n", (unsigned long)i);
      return false;
    }
  }

  return true;
}


bool CacheReader::readDependencyTable() {
  CACHE_READER_READ_SECTION(OBCC_DependencyTable, mpCachedDependTable,
                            depend_tab);
  return true;
}


bool CacheReader::checkDependency() {
  if (mDependencies.size() != mpCachedDependTable->count) {
    LOGE("Dependencies count mismatch. (%lu vs %lu)\n",
         (unsigned long)mDependencies.size(),
         (unsigned long)mpCachedDependTable->count);
    return false;
  }

  vector<char const *> &strPool = mpResult->mStringPool;
  map<string, pair<uint32_t, unsigned char const *> >::iterator dep;

  dep = mDependencies.begin();
  for (size_t i = 0; i < mpCachedDependTable->count; ++i, ++dep) {
    string const &depName = dep->first;
    uint32_t depType = dep->second.first;
    unsigned char const *depSHA1 = dep->second.second;

    OBCC_Dependency *depCached =&mpCachedDependTable->table[i];
    char const *depCachedName = strPool[depCached->res_name_strp_index];
    uint32_t depCachedType = depCached->res_type;
    unsigned char const *depCachedSHA1 = depCached->sha1;

    if (depName != depCachedName) {
      LOGE("Cache dependency name mismatch:\n");
      LOGE("  given:  %s\n", depName.c_str());
      LOGE("  cached: %s\n", depCachedName);

      return false;
    }

    if (memcmp(depSHA1, depCachedSHA1, 20) != 0) {
      LOGE("Cache dependency %s sha1 mismatch:\n", depCachedName);

#define PRINT_SHA1(PREFIX, X, POSTFIX) \
      LOGE(PREFIX "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x" \
                  "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x" POSTFIX, \
           X[0], X[1], X[2], X[3], X[4], X[5], X[6], X[7], X[8], X[9], \
           X[10],X[11],X[12],X[13],X[14],X[15],X[16],X[17],X[18],X[19]);

      PRINT_SHA1("  given:  ", depSHA1, "\n");
      PRINT_SHA1("  cached: ", depCachedSHA1, "\n");

#undef PRINT_SHA1

      return false;
    }

    if (depType != depCachedType) {
      LOGE("Cache dependency %s resource type mismatch.\n", depCachedName);
      return false;
    }
  }

  return true;
}

bool CacheReader::readExportVarList() {
  CACHE_READER_READ_SECTION(OBCC_ExportVarList,
                            mpResult->mpExportVars, export_var_list);
  return true;
}


bool CacheReader::readExportFuncList() {
  CACHE_READER_READ_SECTION(OBCC_ExportFuncList,
                            mpResult->mpExportFuncs, export_func_list);
  return true;
}


bool CacheReader::readPragmaList() {
  CACHE_READER_READ_SECTION(OBCC_PragmaList, mpPragmaList, pragma_list);

  vector<char const *> const &strPool = mpResult->mStringPool;
  ScriptCached::PragmaList &pragmas = mpResult->mPragmas;

  for (size_t i = 0; i < pragma_list_raw->count; ++i) {
    OBCC_Pragma *pragma = &pragma_list_raw->list[i];
    pragmas.push_back(make_pair(strPool[pragma->key_strp_index],
                                strPool[pragma->value_strp_index]));
  }

  return true;
}


bool CacheReader::readObjectSlotList() {
  CACHE_READER_READ_SECTION(OBCC_ObjectSlotList,
                            mpResult->mpObjectSlotList, object_slot_list);
  return true;
}


bool CacheReader::readFuncTable() {
  CACHE_READER_READ_SECTION(OBCC_FuncTable, mpFuncTable, func_table);

  vector<char const *> &strPool = mpResult->mStringPool;
  ScriptCached::FuncTable &table = mpResult->mFunctions;
  for (size_t i = 0; i < func_table_raw->count; ++i) {
    OBCC_FuncInfo *func = &func_table_raw->table[i];
    table.insert(make_pair(strPool[func->name_strp_index],
                           make_pair(func->cached_addr, func->size)));
  }

  return true;
}

#undef CACHE_READER_READ_SECTION


bool CacheReader::readContext() {
  mpResult->mContext =
    ContextManager::get().allocateContext(mpHeader->context_cached_addr,
                                          mObjFile->getFD(), 0);

  if (!mpResult->mContext) {
    // Unable to allocate at cached address.  Give up.
    mIsContextSlotNotAvail = true;
    return false;

    // TODO(logan): If relocation is fixed, we should try to allocate the
    // code in different location, and relocate the context.
  }

  return true;
}


bool CacheReader::checkContext() {
  uint32_t sum = mpHeader->context_parity_checksum;
  uint32_t *ptr = reinterpret_cast<uint32_t *>(mpResult->mContext);

  for (size_t i = 0; i < ContextManager::ContextSize / sizeof(uint32_t); ++i) {
    sum ^= *ptr++;
  }

  if (sum != 0) {
    LOGE("Checksum check failed\n");
    return false;
  }

  LOGI("Passed checksum even parity verification.\n");
  return true;
}


bool CacheReader::readRelocationTable() {
  // TODO(logan): Not finished.
  return true;
}


bool CacheReader::relocate() {
  // TODO(logan): Not finished.
  return true;
}


} // namespace bcc
