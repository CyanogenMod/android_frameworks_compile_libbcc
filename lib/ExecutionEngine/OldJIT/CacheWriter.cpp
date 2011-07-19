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

#include "CacheWriter.h"

#include "ContextManager.h"
#include "DebugHelper.h"
#include "FileHandle.h"
#include "Script.h"

#include <map>
#include <string>
#include <vector>
#include <utility>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

namespace bcc {

CacheWriter::~CacheWriter() {
#define CHECK_AND_FREE(VAR) if (VAR) { free(VAR); }

  CHECK_AND_FREE(mpHeaderSection);
  CHECK_AND_FREE(mpStringPoolSection);
  CHECK_AND_FREE(mpDependencyTableSection);
  //CHECK_AND_FREE(mpRelocationTableSection);
  CHECK_AND_FREE(mpExportVarListSection);
  CHECK_AND_FREE(mpExportFuncListSection);
  CHECK_AND_FREE(mpPragmaListSection);
  CHECK_AND_FREE(mpFuncTableSection);
  CHECK_AND_FREE(mpObjectSlotSection);

#undef CHECK_AND_FREE
}

bool CacheWriter::writeCacheFile(FileHandle *objFile,
                                 FileHandle *infoFile,
                                 Script *S,
                                 uint32_t libRS_threadable) {
  if (!objFile || objFile->getFD() < 0 ||
      !infoFile || infoFile->getFD() < 0) {
    return false;
  }

  mObjFile = objFile;
  mInfoFile = infoFile;
  mpOwner = S;

  bool result = prepareHeader(libRS_threadable)
             && prepareDependencyTable()
             && prepareFuncTable()
             && preparePragmaList()
             //&& prepareRelocationTable()
             && prepareStringPool()
             && prepareExportVarList()
             && prepareExportFuncList()
             && prepareObjectSlotList()
             && calcSectionOffset()
             && calcContextChecksum()
             && writeAll()
             ;

  return result;
}


bool CacheWriter::prepareHeader(uint32_t libRS_threadable) {
  OBCC_Header *header = (OBCC_Header *)malloc(sizeof(OBCC_Header));

  if (!header) {
    LOGE("Unable to allocate for header.\n");
    return false;
  }

  mpHeaderSection = header;

  // Initialize
  memset(header, '\0', sizeof(OBCC_Header));

  // Magic word and version
  memcpy(header->magic, OBCC_MAGIC, 4);
  memcpy(header->version, OBCC_VERSION, 4);

  // Machine Integer Type
  uint32_t number = 0x00000001;
  header->endianness = (*reinterpret_cast<char *>(&number) == 1) ? 'e' : 'E';
  header->sizeof_off_t = sizeof(off_t);
  header->sizeof_size_t = sizeof(size_t);
  header->sizeof_ptr_t = sizeof(void *);

  // Context
  header->context_cached_addr = mpOwner->getContext();

  // libRS is threadable dirty hack
  // TODO: This should be removed in the future
  header->libRS_threadable = libRS_threadable;

  return true;
}


bool CacheWriter::prepareDependencyTable() {
  size_t tableSize = sizeof(OBCC_DependencyTable) +
                     sizeof(OBCC_Dependency) * mDependencies.size();

  OBCC_DependencyTable *tab = (OBCC_DependencyTable *)malloc(tableSize);

  if (!tab) {
    LOGE("Unable to allocate for dependency table section.\n");
    return false;
  }

  mpDependencyTableSection = tab;
  mpHeaderSection->depend_tab_size = tableSize;

  tab->count = mDependencies.size();

  size_t i = 0;
  for (map<string, pair<uint32_t, unsigned char const *> >::iterator
       I = mDependencies.begin(), E = mDependencies.end(); I != E; ++I, ++i) {
    OBCC_Dependency *dep = &tab->table[i];

    dep->res_name_strp_index = addString(I->first.c_str(), I->first.size());
    dep->res_type = I->second.first;
    memcpy(dep->sha1, I->second.second, 20);
  }

  return true;
}


bool CacheWriter::prepareFuncTable() {
  size_t funcCount = mpOwner->getFuncCount();

  size_t tableSize = sizeof(OBCC_FuncTable) +
                     sizeof(OBCC_FuncInfo) * funcCount;

  OBCC_FuncTable *tab = (OBCC_FuncTable *)malloc(tableSize);

  if (!tab) {
    LOGE("Unable to allocate for function table section.\n");
    return false;
  }

  mpFuncTableSection = tab;
  mpHeaderSection->func_table_size = tableSize;

  tab->count = static_cast<size_t>(funcCount);

  // Get the function informations
  vector<FuncInfo> funcInfoList(funcCount);
  mpOwner->getFuncInfoList(funcCount, &*funcInfoList.begin());

  for (size_t i = 0; i < funcCount; ++i) {
    FuncInfo *info = &funcInfoList[i];
    OBCC_FuncInfo *outputInfo = &tab->table[i];

    outputInfo->name_strp_index = addString(info->name, strlen(info->name));
    outputInfo->cached_addr = info->addr;
    outputInfo->size = info->size;
  }

  return true;
}


bool CacheWriter::preparePragmaList() {
  size_t pragmaCount = mpOwner->getPragmaCount();

  size_t listSize = sizeof(OBCC_PragmaList) +
                    sizeof(OBCC_Pragma) * pragmaCount;

  OBCC_PragmaList *list = (OBCC_PragmaList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for pragma list\n");
    return false;
  }

  mpPragmaListSection = list;
  mpHeaderSection->pragma_list_size = listSize;

  list->count = pragmaCount;

  vector<char const *> keyList(pragmaCount);
  vector<char const *> valueList(pragmaCount);
  mpOwner->getPragmaList(pragmaCount, &*keyList.begin(), &*valueList.begin());

  for (size_t i = 0; i < pragmaCount; ++i) {
    char const *key = keyList[i];
    char const *value = valueList[i];

    size_t keyLen = strlen(key);
    size_t valueLen = strlen(value);

    OBCC_Pragma *pragma = &list->list[i];
    pragma->key_strp_index = addString(key, keyLen);
    pragma->value_strp_index = addString(value, valueLen);
  }

  return true;
}


bool CacheWriter::prepareRelocationTable() {
  // TODO(logan): Implement relocation table cache write.
  return false;
}


bool CacheWriter::prepareStringPool() {
  // Calculate string pool size
  size_t size = sizeof(OBCC_StringPool) +
                sizeof(OBCC_String) * mStringPool.size();

  off_t strOffset = size;

  for (size_t i = 0; i < mStringPool.size(); ++i) {
    size += mStringPool[i].second + 1;
  }

  // Create string pool
  OBCC_StringPool *pool = (OBCC_StringPool *)malloc(size);

  if (!pool) {
    LOGE("Unable to allocate string pool.\n");
    return false;
  }

  mpStringPoolSection = pool;
  mpHeaderSection->str_pool_size = size;

  pool->count = mStringPool.size();

  char *strPtr = reinterpret_cast<char *>(pool) + strOffset;

  for (size_t i = 0; i < mStringPool.size(); ++i) {
    OBCC_String *str = &pool->list[i];

    str->length = mStringPool[i].second;
    str->offset = strOffset;
    memcpy(strPtr, mStringPool[i].first, str->length);

    strPtr += str->length;
    *strPtr++ = '\0';

    strOffset += str->length + 1;
  }

  return true;
}


bool CacheWriter::prepareExportVarList() {
  size_t varCount = mpOwner->getExportVarCount();
  size_t listSize = sizeof(OBCC_ExportVarList) + sizeof(void *) * varCount;

  OBCC_ExportVarList *list = (OBCC_ExportVarList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for export variable list\n");
    return false;
  }

  mpExportVarListSection = list;
  mpHeaderSection->export_var_list_size = listSize;

  list->count = static_cast<size_t>(varCount);

  mpOwner->getExportVarList(varCount, list->cached_addr_list);
  return true;
}


bool CacheWriter::prepareExportFuncList() {
  size_t funcCount = mpOwner->getExportFuncCount();
  size_t listSize = sizeof(OBCC_ExportFuncList) + sizeof(void *) * funcCount;

  OBCC_ExportFuncList *list = (OBCC_ExportFuncList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for export function list\n");
    return false;
  }

  mpExportFuncListSection = list;
  mpHeaderSection->export_func_list_size = listSize;

  list->count = static_cast<size_t>(funcCount);

  mpOwner->getExportFuncList(funcCount, list->cached_addr_list);
  return true;
}


bool CacheWriter::prepareObjectSlotList() {
  size_t objectSlotCount = mpOwner->getObjectSlotCount();

  size_t listSize = sizeof(OBCC_ObjectSlotList) +
                    sizeof(uint32_t) * objectSlotCount;

  OBCC_ObjectSlotList *list = (OBCC_ObjectSlotList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for object slot list\n");
    return false;
  }

  mpObjectSlotSection = list;
  mpHeaderSection->object_slot_list_size = listSize;

  list->count = objectSlotCount;

  mpOwner->getObjectSlotList(objectSlotCount, list->object_slot_list);
  return true;
}


bool CacheWriter::calcSectionOffset() {
  size_t offset = sizeof(OBCC_Header);

#define OFFSET_INCREASE(NAME)                                               \
  do {                                                                      \
    /* Align to a word */                                                   \
    size_t rem = offset % sizeof(int);                                      \
    if (rem > 0) {                                                          \
      offset += sizeof(int) - rem;                                          \
    }                                                                       \
                                                                            \
    /* Save the offset and increase it */                                   \
    mpHeaderSection->NAME##_offset = offset;                                \
    offset += mpHeaderSection->NAME##_size;                                 \
  } while (0)

  OFFSET_INCREASE(str_pool);
  OFFSET_INCREASE(depend_tab);
  //OFFSET_INCREASE(reloc_tab);
  OFFSET_INCREASE(export_var_list);
  OFFSET_INCREASE(export_func_list);
  OFFSET_INCREASE(pragma_list);
  OFFSET_INCREASE(func_table);
  OFFSET_INCREASE(object_slot_list);

#undef OFFSET_INCREASE
  return true;
}


bool CacheWriter::calcContextChecksum() {
  uint32_t sum = 0;
  uint32_t *ptr = reinterpret_cast<uint32_t *>(mpOwner->getContext());

  for (size_t i = 0; i < ContextManager::ContextSize / sizeof(uint32_t); ++i) {
    sum ^= *ptr++;
  }

  mpHeaderSection->context_parity_checksum = sum;
  return true;
}


bool CacheWriter::writeAll() {
#define WRITE_SECTION(NAME, OFFSET, SIZE, SECTION)                          \
  do {                                                                      \
    if (mInfoFile->seek(OFFSET, SEEK_SET) == -1) {                          \
      LOGE("Unable to seek to " #NAME " section for writing.\n");           \
      return false;                                                         \
    }                                                                       \
                                                                            \
    if (mInfoFile->write(reinterpret_cast<char *>(SECTION), (SIZE)) !=      \
        static_cast<ssize_t>(SIZE)) {                                       \
      LOGE("Unable to write " #NAME " section to cache file.\n");           \
      return false;                                                         \
    }                                                                       \
  } while (0)

#define WRITE_SECTION_SIMPLE(NAME, SECTION)                                 \
  WRITE_SECTION(NAME,                                                       \
                mpHeaderSection->NAME##_offset,                             \
                mpHeaderSection->NAME##_size,                               \
                SECTION)

  WRITE_SECTION(header, 0, sizeof(OBCC_Header), mpHeaderSection);

  WRITE_SECTION_SIMPLE(str_pool, mpStringPoolSection);
  WRITE_SECTION_SIMPLE(depend_tab, mpDependencyTableSection);
  //WRITE_SECTION_SIMPLE(reloc_tab, mpRelocationTableSection);
  WRITE_SECTION_SIMPLE(export_var_list, mpExportVarListSection);
  WRITE_SECTION_SIMPLE(export_func_list, mpExportFuncListSection);
  WRITE_SECTION_SIMPLE(pragma_list, mpPragmaListSection);
  WRITE_SECTION_SIMPLE(func_table, mpFuncTableSection);
  WRITE_SECTION_SIMPLE(object_slot_list, mpObjectSlotSection);

#undef WRITE_SECTION_SIMPLE
#undef WRITE_SECTION


  // Write Context to Executable File
  char const *context = (char const *)mpOwner->getContext();
  size_t context_size = ContextManager::ContextSize;
  if (mObjFile->write(context, context_size) != (ssize_t)context_size) {
    LOGE("Unable to write context image to executable file\n");
    return false;
  }

  return true;
}


} // namespace bcc
