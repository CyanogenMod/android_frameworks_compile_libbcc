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

#include "SourceInfo.h"

#if USE_CACHE
#if USE_OLD_JIT
#include "OldJIT/CacheReader.h"
#include "OldJIT/CacheWriter.h"
#endif
#if USE_MCJIT
#include "MCCacheWriter.h"
#include "MCCacheReader.h"
#endif
#endif

#include "DebugHelper.h"
#include "ScriptCompiled.h"
#include "Sha1Helper.h"

#include <bcc/bcc.h>
#include <bcc/bcc_cache.h>

#include <llvm/ADT/OwningPtr.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/system_error.h>

#include <stddef.h>
#include <string.h>

namespace bcc {


SourceInfo *SourceInfo::createFromBuffer(char const *resName,
                                         char const *bitcode,
                                         size_t bitcodeSize,
                                         unsigned long flags) {
  SourceInfo *result = new SourceInfo();

  if (!result) {
    return NULL;
  }

  result->type = SourceKind::Buffer;
  result->buffer.resName = resName;
  result->buffer.bitcode = bitcode;
  result->buffer.bitcodeSize = bitcodeSize;
  result->flags = flags;

#if USE_CACHE
  if (!resName && !(flags & BCC_SKIP_DEP_SHA1)) {
    result->flags |= BCC_SKIP_DEP_SHA1;

    LOGW("It is required to give resName for sha1 dependency check.\n");
    LOGW("Sha1sum dependency check will be skipped.\n");
    LOGW("Set BCC_SKIP_DEP_SHA1 for flags to surpress this warning.\n");
  }

  if (result->flags & BCC_SKIP_DEP_SHA1) {
    memset(result->sha1, '\0', 20);
  } else {
    calcSHA1(result->sha1, bitcode, bitcodeSize);
  }
#endif

  return result;
}


SourceInfo *SourceInfo::createFromFile(char const *path,
                                       unsigned long flags) {
  SourceInfo *result = new SourceInfo();

  if (!result) {
    return NULL;
  }

  result->type = SourceKind::File;
  result->file.path = path;
  result->flags = flags;

#if USE_CACHE
  memset(result->sha1, '\0', 20);

  if (!(result->flags & BCC_SKIP_DEP_SHA1)) {
    calcFileSHA1(result->sha1, path);
  }
#endif

  return result;
}


SourceInfo *SourceInfo::createFromModule(llvm::Module *module,
                                         unsigned long flags) {
  SourceInfo *result = new SourceInfo();

  if (!result) {
    return NULL;
  }

  result->type = SourceKind::Module;
  result->module.reset(module);
  result->flags = flags;

#if USE_CACHE
  if (! (flags & BCC_SKIP_DEP_SHA1)) {
    result->flags |= BCC_SKIP_DEP_SHA1;

    LOGW("Unable to calculate sha1sum for llvm::Module.\n");
    LOGW("Sha1sum dependency check will be skipped.\n");
    LOGW("Set BCC_SKIP_DEP_SHA1 for flags to surpress this warning.\n");
  }

  memset(result->sha1, '\0', 20);
#endif

  return result;
}


int SourceInfo::prepareModule(ScriptCompiled *SC) {
  switch (type) {
  case SourceKind::Buffer:
    {
      llvm::OwningPtr<llvm::MemoryBuffer> MEM(
        llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(buffer.bitcode, buffer.bitcodeSize)));

      if (!MEM.get()) {
        LOGE("Unable to MemoryBuffer::getMemBuffer(addr=%p, size=%lu)\n",
             buffer.bitcode, (unsigned long)buffer.bitcodeSize);
        return 1;
      }

      module.reset(SC->parseBitcodeFile(MEM.get()));
    }
    break;

  case SourceKind::File:
    {
      llvm::OwningPtr<llvm::MemoryBuffer> MEM;

      if (llvm::error_code ec = llvm::MemoryBuffer::getFile(file.path, MEM)) {
        LOGE("Unable to MemoryBuffer::getFile(path=%s)\n", file.path);
        return 1;
      }

      module.reset(SC->parseBitcodeFile(MEM.get()));
    }
    break;

  default:
    break;
  }

  return (module.get()) ? 0 : 1;
}


#if USE_CACHE
template <typename T> void SourceInfo::introDependency(T &checker) {
  if (flags & BCC_SKIP_DEP_SHA1) {
    return;
  }

  switch (type) {
  case SourceKind::Buffer:
    checker.addDependency(BCC_APK_RESOURCE, buffer.resName, sha1);
    break;

  case SourceKind::File:
    checker.addDependency(BCC_FILE_RESOURCE, file.path, sha1);
    break;

  default:
    break;
  }
}

#if USE_OLD_JIT
template void SourceInfo::introDependency<CacheReader>(CacheReader &);
template void SourceInfo::introDependency<CacheWriter>(CacheWriter &);
#endif

#if USE_MCJIT
template void SourceInfo::introDependency<MCCacheWriter>(MCCacheWriter &);
template void SourceInfo::introDependency<MCCacheReader>(MCCacheReader &);
#endif
#endif // USE_CACHE


} // namespace bcc
