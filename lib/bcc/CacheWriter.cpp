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

#include "CacheWriter.h"

#include "ContextManager.h"
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

bool CacheWriter::writeCacheFile(FileHandle *file, Script *S,
                                 uint32_t libRS_threadable) {
  if (!file || file->getFD() < 0) {
    return false;
  }

  mFile = file;
  mpOwner = S;

  bool result = prepareHeader(libRS_threadable)
             && prepareDependencyTable()
             && prepareFuncTable()
             && preparePragmaList()
             //&& prepareRelocationTable()
             && prepareStringPool()
             && prepareExportVarList()
             && prepareExportFuncList()
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
  ssize_t funcCount = 0;

  mpOwner->getFunctions(&funcCount, 0, NULL);

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
  vector<char *> funcNameList(funcCount);
  mpOwner->getFunctions(0, funcCount, &*funcNameList.begin());

  for (int i = 0; i < funcCount; ++i) {
    char *funcName = funcNameList[i];
    size_t funcNameLen = strlen(funcName);

    void *funcAddr = NULL;
    ssize_t funcBinarySize = 0;
    mpOwner->getFunctionBinary(funcName, &funcAddr, &funcBinarySize);

    OBCC_FuncInfo *funcInfo = &tab->table[i];
    funcInfo->name_strp_index = addString(funcName, funcNameLen);
    funcInfo->cached_addr = funcAddr;
    funcInfo->size = static_cast<size_t>(funcBinarySize);
  }

  return true;
}


bool CacheWriter::preparePragmaList() {
  ssize_t stringCount;

  mpOwner->getPragmas(&stringCount, 0, NULL);

  size_t pragmaCount = static_cast<size_t>(stringCount) / 2;

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

  vector<char *> strings(stringCount);
  mpOwner->getPragmas(&stringCount, stringCount, &*strings.begin());

  for (size_t i = 0; i < pragmaCount; ++i) {
    char *key = strings[2 * i];
    size_t keyLen = strlen(key);

    char *value = strings[2 * i + 1];
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

  char *strPtr = reinterpret_cast<char *>(pool) + strOffset;

  for (size_t i = 0; i < mStringPool.size(); ++i) {
    OBCC_String *str = &pool->list[i];

    str->length = mStringPool[i].second;
    str->offset = strOffset;
    memcpy(strPtr, mStringPool[i].first, str->length);

    strPtr += str->length;
    *strPtr++ = '\0';

    strOffset += str->length;
  }

  return true;
}


bool CacheWriter::prepareExportVarList() {
  ssize_t varCount;

  mpOwner->getExportVars(&varCount, 0, NULL);

  size_t listSize = sizeof(OBCC_ExportVarList) + sizeof(void *) * varCount;

  OBCC_ExportVarList *list = (OBCC_ExportVarList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for export variable list\n");
    return false;
  }

  mpExportVarListSection = list;
  mpHeaderSection->export_var_list_size = listSize;

  list->count = static_cast<size_t>(varCount);

  mpOwner->getExportVars(&varCount, varCount, list->cached_addr_list);
  return true;
}


bool CacheWriter::prepareExportFuncList() {
  ssize_t funcCount;

  mpOwner->getExportFuncs(&funcCount, 0, NULL);

  size_t listSize = sizeof(OBCC_ExportFuncList) + sizeof(void *) * funcCount;

  OBCC_ExportFuncList *list = (OBCC_ExportFuncList *)malloc(listSize);

  if (!list) {
    LOGE("Unable to allocate for export function list\n");
    return false;
  }

  mpExportFuncListSection = list;
  mpHeaderSection->export_func_list_size = listSize;

  list->count = static_cast<size_t>(funcCount);

  mpOwner->getExportFuncs(&funcCount, funcCount, list->cached_addr_list);
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

#undef OFFSET_INCREASE

  // Context
  long pagesize = sysconf(_SC_PAGESIZE);
  size_t context_offset_rem = offset % pagesize;
  if (context_offset_rem) {
    offset += pagesize - context_offset_rem;
  }

  mpHeaderSection->context_offset = offset;
  return true;
}


bool CacheWriter::calcContextChecksum() {
  uint32_t sum = 0;
  uint32_t *ptr = reinterpret_cast<uint32_t *>(mpOwner->getContext());

  for (size_t i = 0; i < BCC_CONTEXT_SIZE / sizeof(uint32_t); ++i) {
    sum ^= *ptr++;
  }

  mpHeaderSection->context_parity_checksum = sum;
  return true;
}


bool CacheWriter::writeAll() {
#define WRITE_SECTION(NAME, OFFSET, SIZE, SECTION)                          \
  do {                                                                      \
    if (mFile->seek(OFFSET, SEEK_SET) == -1) {                              \
      LOGE("Unable to seek to " #NAME " section for writing.\n");           \
      return false;                                                         \
    }                                                                       \
                                                                            \
    if (mFile->write(reinterpret_cast<char *>(SECTION), (SIZE)) !=          \
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

  WRITE_SECTION(context, mpHeaderSection->context_offset, BCC_CONTEXT_SIZE,
                mpOwner->getContext());

#undef WRITE_SECTION_SIMPLE
#undef WRITE_SECTION

  return true;
}


#if 0
void Compiler::genCacheFile() {

  // Write Header
  sysWriteFully(mCacheFd, reinterpret_cast<char const *>(hdr),
                sizeof(oBCCHeader), "Write oBCC header");

  // Write Relocation Entry Table
  {
    size_t allocSize = hdr->relocCount * sizeof(oBCCRelocEntry);

    oBCCRelocEntry const*records = &mCodeEmitter->getCachingRelocations()[0];

    sysWriteFully(mCacheFd, reinterpret_cast<char const *>(records),
                  allocSize, "Write Relocation Entries");
  }

  // Write Export Variables Table
  {
    uint32_t *record, *ptr;

    record = (uint32_t *)calloc(hdr->exportVarsCount, sizeof(uint32_t));
    ptr = record;

    if (!record) {
      goto bail;
    }

    for (ScriptCompiled::ExportVarList::const_iterator
         I = mpResult->mExportVars.begin(),
         E = mpResult->mExportVars.end(); I != E; I++) {
      *ptr++ = reinterpret_cast<uint32_t>(*I);
    }

    sysWriteFully(mCacheFd, reinterpret_cast<char const *>(record),
                  hdr->exportVarsCount * sizeof(uint32_t),
                  "Write ExportVars");

    free(record);
  }

  // Write Export Functions Table
  {
    uint32_t *record, *ptr;

    record = (uint32_t *)calloc(hdr->exportFuncsCount, sizeof(uint32_t));
    ptr = record;

    if (!record) {
      goto bail;
    }

    for (ScriptCompiled::ExportFuncList::const_iterator
         I = mpResult->mExportFuncs.begin(),
         E = mpResult->mExportFuncs.end(); I != E; I++) {
      *ptr++ = reinterpret_cast<uint32_t>(*I);
    }

    sysWriteFully(mCacheFd, reinterpret_cast<char const *>(record),
                  hdr->exportFuncsCount * sizeof(uint32_t),
                  "Write ExportFuncs");

    free(record);
  }


  // Write Export Pragmas Table
  {
    uint32_t pragmaEntryOffset =
      hdr->exportPragmasCount * sizeof(oBCCPragmaEntry);

    for (ScriptCompiled::PragmaList::const_iterator
         I = mpResult->mPragmas.begin(),
         E = mpResult->mPragmas.end(); I != E; ++I) {
      oBCCPragmaEntry entry;

      entry.pragmaNameOffset = pragmaEntryOffset;
      entry.pragmaNameSize = I->first.size();
      pragmaEntryOffset += entry.pragmaNameSize + 1;

      entry.pragmaValueOffset = pragmaEntryOffset;
      entry.pragmaValueSize = I->second.size();
      pragmaEntryOffset += entry.pragmaValueSize + 1;

      sysWriteFully(mCacheFd, (char *)&entry, sizeof(oBCCPragmaEntry),
                    "Write export pragma entry");
    }

    for (ScriptCompiled::PragmaList::const_iterator
         I = mpResult->mPragmas.begin(),
         E = mpResult->mPragmas.end(); I != E; ++I) {
      sysWriteFully(mCacheFd, I->first.c_str(), I->first.size() + 1,
                    "Write export pragma name string");
      sysWriteFully(mCacheFd, I->second.c_str(), I->second.size() + 1,
                    "Write export pragma value string");
    }
  }

  if (codeOffsetNeedPadding) {
    // requires additional padding
    lseek(mCacheFd, hdr->codeOffset, SEEK_SET);
  }

  // Write Generated Code and Global Variable
  sysWriteFully(mCacheFd, mCodeDataAddr, MaxCodeSize + MaxGlobalVarSize,
                "Write code and global variable");

  goto close_return;

bail:
  if (ftruncate(mCacheFd, 0) != 0) {
    LOGW("Warning: unable to truncate cache file: %s\n", strerror(errno));
  }

close_return:
  free(hdr);
  close(mCacheFd);
  mCacheFd = -1;
}
#endif

} // namespace bcc
