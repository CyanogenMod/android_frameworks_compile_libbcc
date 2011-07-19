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

#ifndef BCC_CACHEWRITER_H
#define BCC_CACHEWRITER_H

#include <bcc/bcc_cache.h>

#include "FileHandle.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bcc {
  class Script;

  class CacheWriter {
  private:
    Script *mpOwner;

    FileHandle *mObjFile;
    FileHandle *mInfoFile;

    std::vector<std::pair<char const *, size_t> > mStringPool;

    std::map<std::string,
             std::pair<uint32_t, unsigned char const *> > mDependencies;

    OBCC_Header *mpHeaderSection;
    OBCC_StringPool *mpStringPoolSection;
    OBCC_DependencyTable *mpDependencyTableSection;
    //OBCC_RelocationTable *mpRelocationTableSection;
    OBCC_ExportVarList *mpExportVarListSection;
    OBCC_ExportFuncList *mpExportFuncListSection;
    OBCC_PragmaList *mpPragmaListSection;
    OBCC_FuncTable *mpFuncTableSection;
    OBCC_ObjectSlotList *mpObjectSlotSection;

  public:
    CacheWriter()
      : mpHeaderSection(NULL), mpStringPoolSection(NULL),
        mpDependencyTableSection(NULL), mpExportVarListSection(NULL),
        mpExportFuncListSection(NULL), mpPragmaListSection(NULL),
        mpFuncTableSection(NULL), mpObjectSlotSection(NULL) {
    }

    ~CacheWriter();

    bool writeCacheFile(FileHandle *objFile,
                        FileHandle *infoFile,
                        Script *S,
                        uint32_t libRS_threadable);

    void addDependency(OBCC_ResourceType resType,
                       std::string const &resName,
                       unsigned char const *sha1) {
      mDependencies.insert(std::make_pair(resName,
                           std::make_pair((uint32_t)resType, sha1)));
    }

  private:
    bool prepareHeader(uint32_t libRS_threadable);
    bool prepareStringPool();
    bool prepareDependencyTable();
    bool prepareRelocationTable();
    bool prepareExportVarList();
    bool prepareExportFuncList();
    bool preparePragmaList();
    bool prepareFuncTable();
    bool prepareObjectSlotList();

    bool writeAll();

    bool calcSectionOffset();
    bool calcContextChecksum();

    size_t addString(char const *str, size_t size) {
      mStringPool.push_back(std::make_pair(str, size));
      return mStringPool.size() - 1;
    }

  };

} // namespace bcc

#endif // BCC_CACHEWRITER_H
