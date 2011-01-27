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

#ifndef BCC_CACHEREADER_H
#define BCC_CACHEREADER_H

#include "ScriptCached.h"

#include <llvm/ADT/OwningPtr.h>

#include <map>
#include <string>
#include <utility>

#include <stddef.h>
#include <stdint.h>

struct OBCC_Header;

namespace bcc {
  class FileHandle;
  class Script;

  class CacheReader {
  private:
    FileHandle *mFile;
    off_t mFileSize;

    OBCC_Header *mpHeader;
    OBCC_DependencyTable *mpCachedDependTable;
    OBCC_PragmaList *mpPragmaList;
    OBCC_FuncTable *mpFuncTable;

    llvm::OwningPtr<ScriptCached> mpResult;

    std::map<std::string,
             std::pair<uint32_t, unsigned char const *> > mDependencies;

  public:
    CacheReader()
      : mFile(NULL), mFileSize(0), mpHeader(NULL),
        mpCachedDependTable(NULL), mpPragmaList(NULL), mpFuncTable(NULL) {
    }

    ~CacheReader();

    void addDependency(OBCC_ResourceType resType,
                       std::string const &resName,
                       unsigned char const *sha1) {
      mDependencies.insert(std::make_pair(resName,
                           std::make_pair((uint32_t)resType, sha1)));
    }

    ScriptCached *readCacheFile(FileHandle *file, Script *s);

  private:
    bool readHeader();
    bool readStringPool();
    bool readDependencyTable();
    bool readExportVarList();
    bool readExportFuncList();
    bool readPragmaList();
    bool readFuncTable();
    bool readObjectSlotList();
    bool readContext();
    bool readRelocationTable();

    bool checkFileSize();
    bool checkHeader();
    bool checkMachineIntType();
    bool checkSectionOffsetAndSize();
    bool checkStringPool();
    bool checkDependency();
    bool checkContext();

    bool relocate();
  };

} // namespace bcc

#endif // BCC_CACHEREADER_H
