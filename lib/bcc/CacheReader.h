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

struct OBCC_Header;

namespace bcc {
  class FileHandle;
  class Script;

  class CacheReader {
  private:
    Script *mpOwner;

    FileHandle *mFile;
    off_t mFileSize;

    OBCC_Header *mHeader;

    llvm::OwningPtr<ScriptCached> mResult;

    std::map<std::string, char const *> mDependency;

  public:
    CacheReader(Script *owner)
      : mpOwner(owner), mFile(NULL), mFileSize(0), mHeader(NULL) {
    }

    void addDependency(std::string const &resName, char const *sha1) {
      mDependency.insert(std::make_pair(resName, sha1));
    }

    ScriptCached *readCacheFile(FileHandle *file);

  private:
    bool readHeader();
    bool readStringPool();
    bool readDependencyTable();
    bool readExportVarList();
    bool readExportFuncList();
    bool readPragmaList();
    bool readFuncTable();
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
