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

#include "CacheReader.h"

#include "ContextManager.h"
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

#include <string.h>

using namespace std;


namespace bcc {

ScriptCached *CacheReader::readCacheFile(FileHandle *file) {
  // Check file handle
  if (!file || file->getFD() < 0) {
    return NULL;
  }

  // Allocate ScriptCached object
  mResult.reset(new (nothrow) ScriptCached(mpOwner));

  if (!mResult) {
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
             && readContext()
             && checkContext()
             //&& readRelocationTable()
             //&& relocate()
             ;


  // TODO(logan): This is the hack for libRS.  Should be turned on
  // before caching is ready to go.
#if 0
  // Check the cache file has __isThreadable or not.  If it is set,
  // then we have to call mpSymbolLookupFn for __clearThreadable.
  if (mHeader->libRSThreadable && mpSymbolLookupFn) {
    mpSymbolLookupFn(mpSymbolLookupContext, "__clearThreadable");
  }
#endif

  return result ? mResult.take() : NULL;
}


bool CacheReader::checkFileSize() {
  struct stat stfile;
  if (fstat(mFile->getFD(), &stfile) < 0) {
    LOGE("Unable to stat cache file.\n");
    return false;
  }

  mFileSize = stfile.st_size;

  if (mFileSize < (off_t)sizeof(OBCC_Header) ||
      mFileSize < (off_t)BCC_CONTEXT_SIZE) {
    LOGE("Cache file is too small to be correct.\n");
    return false;
  }

  return true;
}


bool CacheReader::readHeader() {
  if (mFile->seek(0, SEEK_SET) != 0) {
    LOGE("Unable to seek to 0. (reason: %s)\n", strerror(errno));
    return false;
  }

  mHeader = (OBCC_Header *)malloc(sizeof(OBCC_Header));
  if (!mHeader) {
    LOGE("Unable to allocate for cache header.\n");
    return false;
  }

  if (mFile->read(reinterpret_cast<char *>(mHeader), sizeof(OBCC_Header)) !=
      (ssize_t)sizeof(OBCC_Header)) {
    LOGE("Unable to read cache header.\n");
    return false;
  }

  return true;
}


bool CacheReader::checkHeader() {
  if (memcmp(mHeader->magic, OBCC_MAGIC, 4) != 0) {
    LOGE("Bad magic word\n");
    return false;
  }

  if (memcmp(mHeader->version, OBCC_VERSION, 4) != 0) {
    LOGE("Bad oBCC version 0x%08x\n",
         *reinterpret_cast<uint32_t *>(mHeader->version));
    return false;
  }

  return true;
}


bool CacheReader::checkMachineIntType() {
  uint32_t number = 0x00000001;

  bool isLittleEndian = (*reinterpret_cast<char *>(&number) == 1);
  if ((isLittleEndian && mHeader->endianness != 'e') ||
      (!isLittleEndian && mHeader->endianness != 'E')) {
    LOGE("Machine endianness mismatch.\n");
    return false;
  }

  if ((unsigned int)mHeader->sizeof_off_t != sizeof(off_t) ||
      (unsigned int)mHeader->sizeof_size_t != sizeof(size_t) ||
      (unsigned int)mHeader->sizeof_ptr_t != sizeof(void *)) {
    LOGE("Machine integer size mismatch.\n");
    return false;
  }

  return true;
}


bool CacheReader::checkSectionOffsetAndSize() {
#define CHECK_SECTION_OFFSET(NAME)                                          \
  do {                                                                      \
    off_t offset = mHeader-> NAME##_offset;                                 \
    off_t size = (off_t)mHeader-> NAME##_size;                              \
                                                                            \
    if (mFileSize < offset || mFileSize < offset + size) {                  \
      LOGE(#NAME " section overflow.\n");                                   \
      return false;                                                         \
    }                                                                       \
                                                                            \
    if (offset % sizeof(int) != 0) {                                        \
      LOGE(#NAME " offset must aligned to %d.\n", sizeof(int));             \
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
  CHECK_SECTION_OFFSET(reloc_tab);
  CHECK_SECTION_OFFSET(export_var_list);
  CHECK_SECTION_OFFSET(export_func_list);
  CHECK_SECTION_OFFSET(pragma_list);

#undef CHECK_SECTION_OFFSET

  if (mFileSize < mHeader->context_offset ||
      mFileSize < mHeader->context_offset + BCC_CONTEXT_SIZE) {
    LOGE("context section overflow.\n");
    return false;
  }

  long pagesize = sysconf(_SC_PAGESIZE);
  if (mHeader->context_offset % pagesize != 0) {
    LOGE("context offset must aligned to pagesize.\n");
    return false;
  }

  // TODO(logan): Move this to some where else.
  if ((uintptr_t)mHeader->context_cached_addr % pagesize != 0) {
    LOGE("cached address is not aligned to pagesize.\n");
    return false;
  }

  return true;
}


bool CacheReader::readStringPool() {
  OBCC_StringPool *poolR = (OBCC_StringPool *)malloc(mHeader->str_pool_size);

  if (!poolR) {
    LOGE("Unable to allocate string pool.\n");
    return false;
  }

  mResult->mpStringPoolRaw = poolR; // Managed by mResult from now on.

  if (mFile->read(reinterpret_cast<char *>(poolR), mHeader->str_pool_size) !=
      (ssize_t)mHeader->str_pool_size) {
    LOGE("Unable to read string pool.\n");
    return false;
  }

  vector<char const *> &pool = mResult->mStringPool;

  for (size_t i = 0; i < poolR->count; ++i) {
    char *str = reinterpret_cast<char *>(poolR) + poolR->list[i].offset;
    pool.push_back(str);
  }

  return true;
}


bool CacheReader::checkStringPool() {
  OBCC_StringPool *poolR = mResult->mpStringPoolRaw;
  vector<char const *> &pool = mResult->mStringPool;

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
  // TODO(logan): Not finished.
  return true;
}


bool CacheReader::checkDependency() {
  // TODO(logan): Not finished.
  return true;
}

bool CacheReader::readExportVarList() {
  char *varList = (char *)malloc(mHeader->export_var_list_size);

  if (!varList) {
    LOGE("Unable to allocate exported variable list.\n");
    return false;
  }

  mResult->mpExportVars = reinterpret_cast<OBCC_ExportVarList *>(varList);

  if (mFile->seek(mHeader->export_var_list_offset, SEEK_SET) == -1) {
    LOGE("Unable to seek to exported variable list section.\n");
    return false;
  }

  if (mFile->read(varList, mHeader->export_var_list_size) !=
      (ssize_t)mHeader->export_var_list_size) {
    LOGE("Unable to read exported variable list.\n");
    return false;
  }

  return true;
}


bool CacheReader::readExportFuncList() {
  char *funcList = (char *)malloc(mHeader->export_func_list_size);

  if (!funcList) {
    LOGE("Unable to allocate exported function list.\n");
    return false;
  }

  mResult->mpExportFuncs = reinterpret_cast<OBCC_ExportFuncList *>(funcList);

  if (mFile->seek(mHeader->export_func_list_offset, SEEK_SET) == -1) {
    LOGE("Unable to seek to exported function list section.\n");
    return false;
  }

  if (mFile->read(funcList, mHeader->export_func_list_size) !=
      (ssize_t)mHeader->export_func_list_size) {
    LOGE("Unable to read exported function list.\n");
    return false;
  }

  return true;
}


bool CacheReader::readPragmaList() {
  OBCC_PragmaList *pragmaListRaw =
    (OBCC_PragmaList *)malloc(mHeader->pragma_list_size);

  if (!pragmaListRaw) {
    LOGE("Unable to allocate pragma list.\n");
    return false;
  }

  if (mFile->seek(mHeader->pragma_list_offset, SEEK_SET) == -1) {
    LOGE("Unable to seek to pragma list section.\n");
    return false;
  }

  if (mFile->read(reinterpret_cast<char *>(pragmaListRaw),
                  mHeader->pragma_list_size) !=
                              (ssize_t)mHeader->pragma_list_size) {
    LOGE("Unable to read pragma list.\n");
    return false;
  }

  vector<char const *> const &strPool = mResult->mStringPool;
  ScriptCached::PragmaList &pragmas = mResult->mPragmas;

  for (size_t i = 0; i < pragmaListRaw->count; ++i) {
    OBCC_Pragma *pragma = &pragmaListRaw->list[i];
    pragmas.push_back(make_pair(strPool[pragma->key_strp_index],
                                strPool[pragma->value_strp_index]));
  }

  return true;
}


bool CacheReader::readFuncTable() {
  return false;
}


bool CacheReader::readContext() {
  mResult->mContext = allocateContext(mHeader->context_cached_addr,
                                      mFile->getFD(),
                                      mHeader->context_offset);

  if (!mResult->mContext) {
    // Unable to allocate at cached address.  Give up.
    return false;

    // TODO(logan): If relocation is fixed, we should try to allocate the
    // code in different location, and relocate the context.
  }

  return true;
}


bool CacheReader::checkContext() {
  uint32_t sum = mHeader->context_parity_checksum;
  uint32_t *ptr = reinterpret_cast<uint32_t *>(mResult->mContext);

  for (size_t i = 0; i < BCC_CONTEXT_SIZE / sizeof(uint32_t); ++i) {
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
