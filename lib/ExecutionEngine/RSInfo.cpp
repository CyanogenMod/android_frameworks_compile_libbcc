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

//#define LOG_NDEBUG 0
#include "RSInfo.h"

#include <cstring>
#include <new>

#include "FileBase.h"
#include "DebugHelper.h"
#include "Sha1Helper.h"

using namespace bcc;

const char RSInfo::LibBCCPath[] = "/system/lib/libbcc.so";
const char RSInfo::LibRSPath[] = "/system/lib/libRS.so";
uint8_t RSInfo::LibBCCSHA1[20];
uint8_t RSInfo::LibRSSHA1[20];

void RSInfo::LoadBuiltInSHA1Information() {
  static bool loaded = false;

  if (loaded) {
    return;
  }

  // Read SHA-1 checksum of libbcc from hard-coded patch
  // /system/lib/libbcc.so.sha1.
  readSHA1(LibBCCSHA1, 20, "/system/lib/libbcc.so.sha1");

  // Calculate the SHA-1 checksum of libRS.so.
  calcFileSHA1(LibRSSHA1, LibRSPath);

  loaded = true;

  return;
}

android::String8 RSInfo::GetPath(const FileBase &pFile) {
  android::String8 result(pFile.getName().c_str());
  result.append(".info");
  return result;
}

#define PRINT_DEPENDENCY(PREFIX, N, X) \
        ALOGV("\t" PREFIX "Source name: %s, "                                 \
                          "SHA-1: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"   \
                                 "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",  \
              (N), (X)[ 0], (X)[ 1], (X)[ 2], (X)[ 3], (X)[ 4], (X)[ 5],      \
                   (X)[ 6], (X)[ 7], (X)[ 8], (X)[ 9], (X)[10], (X)[11],      \
                   (X)[12], (X)[13], (X)[14], (X)[15], (X)[16], (X)[17],      \
                   (X)[18], (X)[19]);

bool RSInfo::CheckDependency(const RSInfo &pInfo,
                             const char *pInputFilename,
                             const RSScript::SourceDependencyListTy &pDeps) {
  // Built-in dependencies are libbcc.so and libRS.so.
  static const unsigned NumBuiltInDependencies = 2;

  LoadBuiltInSHA1Information();

  if (pInfo.mDependencyTable.size() != (pDeps.size() + NumBuiltInDependencies)) {
    ALOGD("Number of dependencies recorded mismatch (%lu v.s. %lu) in %s!",
          static_cast<unsigned long>(pInfo.mDependencyTable.size()),
          static_cast<unsigned long>(pDeps.size()), pInputFilename);
    return false;
  } else {
    // Built-in dependencies always go first.
    const std::pair<const char *, const uint8_t *> &cache_libbcc_dep =
        pInfo.mDependencyTable[0];
    const std::pair<const char *, const uint8_t *> &cache_libRS_dep =
        pInfo.mDependencyTable[1];

    // Check libbcc.so.
    if (::memcmp(cache_libbcc_dep.second, LibBCCSHA1, 20) != 0) {
        ALOGD("Cache %s is dirty due to %s has been updated.", pInputFilename,
              LibBCCPath);
        PRINT_DEPENDENCY("current - ", LibBCCPath, LibBCCSHA1);
        PRINT_DEPENDENCY("cache - ", cache_libbcc_dep.first,
                                     cache_libbcc_dep.second);
        return false;
    }

    // Check libRS.so.
    if (::memcmp(cache_libRS_dep.second, LibRSSHA1, 20) != 0) {
        ALOGD("Cache %s is dirty due to %s has been updated.", pInputFilename,
              LibRSPath);
        PRINT_DEPENDENCY("current - ", LibRSPath, LibRSSHA1);
        PRINT_DEPENDENCY("cache - ", cache_libRS_dep.first,
                                     cache_libRS_dep.second);
        return false;
    }

    for (unsigned i = 0; i < pDeps.size(); i++) {
      const RSScript::SourceDependency &in_dep = *(pDeps[i]);
      const std::pair<const char *, const uint8_t *> &cache_dep =
          pInfo.mDependencyTable[i + NumBuiltInDependencies];

      if ((::strncmp(in_dep.getSourceName().c_str(),
                     cache_dep.first,
                     in_dep.getSourceName().length()) != 0) ||
          (::memcmp(in_dep.getSHA1Checksum(), cache_dep.second, 20) != 0)) {
        ALOGD("Cache %s is dirty due to the source it dependends on has been "
              "changed:", pInputFilename);
        PRINT_DEPENDENCY("given - ", in_dep.getSourceName().c_str(),
                                     in_dep.getSHA1Checksum());
        PRINT_DEPENDENCY("cache - ", cache_dep.first, cache_dep.second);
        return false;
      }
    }
  }

  return true;
}

RSInfo::RSInfo(size_t pStringPoolSize) : mStringPool(NULL) {
  ::memset(&mHeader, 0, sizeof(mHeader));

  ::memcpy(mHeader.magic, RSINFO_MAGIC, sizeof(mHeader.magic));
  ::memcpy(mHeader.version, RSINFO_VERSION, sizeof(mHeader.version));

  mHeader.headerSize = sizeof(mHeader);

  mHeader.dependencyTable.itemSize = sizeof(rsinfo::DependencyTableItem);
  mHeader.pragmaList.itemSize = sizeof(rsinfo::PragmaItem);
  mHeader.objectSlotList.itemSize = sizeof(rsinfo::ObjectSlotItem);
  mHeader.exportVarNameList.itemSize = sizeof(rsinfo::ExportVarNameItem);
  mHeader.exportFuncNameList.itemSize = sizeof(rsinfo::ExportFuncNameItem);
  mHeader.exportForeachFuncList.itemSize = sizeof(rsinfo::ExportForeachFuncItem);

  if (pStringPoolSize > 0) {
    mHeader.strPoolSize = pStringPoolSize;
    mStringPool = new (std::nothrow) char [ mHeader.strPoolSize ];
    if (mStringPool == NULL) {
      ALOGE("Out of memory when allocate memory for string pool in RSInfo "
            "constructor (size: %u)!", mHeader.strPoolSize);
    }
  }
}

RSInfo::~RSInfo() {
  delete [] mStringPool;
}

bool RSInfo::layout(off_t initial_offset) {
  mHeader.dependencyTable.offset = initial_offset +
                                   mHeader.headerSize +
                                   mHeader.strPoolSize;
  mHeader.dependencyTable.count = mDependencyTable.size();

#define AFTER(_list) ((_list).offset + (_list).itemSize * (_list).count)
  mHeader.pragmaList.offset = AFTER(mHeader.dependencyTable);
  mHeader.pragmaList.count = mPragmas.size();

  mHeader.objectSlotList.offset = AFTER(mHeader.pragmaList);
  mHeader.objectSlotList.count = mObjectSlots.size();

  mHeader.exportVarNameList.offset = AFTER(mHeader.objectSlotList);
  mHeader.exportVarNameList.count = mExportVarNames.size();

  mHeader.exportFuncNameList.offset = AFTER(mHeader.exportVarNameList);
  mHeader.exportFuncNameList.count = mExportFuncNames.size();

  mHeader.exportForeachFuncList.offset = AFTER(mHeader.exportFuncNameList);
  mHeader.exportForeachFuncList.count = mExportForeachFuncs.size();
#undef AFTER

  return true;
}

void RSInfo::dump() const {
  // Hide the codes to save the code size when debugging is disabled.
#if !LOG_NDEBUG

  // Dump header
  ALOGV("RSInfo Header:");
  ALOGV("\tIs threadable: %s", ((mHeader.isThreadable) ? "true" : "false"));
  ALOGV("\tHeader size: %u", mHeader.headerSize);
  ALOGV("\tString pool size: %u", mHeader.strPoolSize);

#define DUMP_LIST_HEADER(_name, _header) do { \
  ALOGV(_name ":"); \
  ALOGV("\toffset: %u", (_header).offset);  \
  ALOGV("\t# of item: %u", (_header).count);  \
  ALOGV("\tsize of each item: %u", (_header).itemSize); \
} while (false)
  DUMP_LIST_HEADER("Dependency table", mHeader.dependencyTable);
  for (DependencyTableTy::const_iterator dep_iter = mDependencyTable.begin(),
          dep_end = mDependencyTable.end(); dep_iter != dep_end; dep_iter++) {
    PRINT_DEPENDENCY("", dep_iter->first, dep_iter->second);
  }

  DUMP_LIST_HEADER("Pragma list", mHeader.pragmaList);
  for (PragmaListTy::const_iterator pragma_iter = mPragmas.begin(),
        pragma_end = mPragmas.end(); pragma_iter != pragma_end; pragma_iter++) {
    ALOGV("\tkey: %s, value: %s", pragma_iter->first, pragma_iter->second);
  }

  DUMP_LIST_HEADER("RS object slots", mHeader.objectSlotList);
  for (ObjectSlotListTy::const_iterator slot_iter = mObjectSlots.begin(),
          slot_end = mObjectSlots.end(); slot_iter != slot_end; slot_iter++) {
    ALOGV("slot: %u", *slot_iter);
  }

  DUMP_LIST_HEADER("RS export variables", mHeader.exportVarNameList);
  for (ExportVarNameListTy::const_iterator var_iter = mExportVarNames.begin(),
          var_end = mExportVarNames.end(); var_iter != var_end; var_iter++) {
    ALOGV("name: %s", *var_iter);
  }

  DUMP_LIST_HEADER("RS export functions", mHeader.exportFuncNameList);
  for (ExportFuncNameListTy::const_iterator func_iter = mExportFuncNames.begin(),
        func_end = mExportFuncNames.end(); func_iter != func_end; func_iter++) {
    ALOGV("name: %s", *func_iter);
  }

  DUMP_LIST_HEADER("RS foreach list", mHeader.exportForeachFuncList);
  for (ExportForeachFuncListTy::const_iterator
          foreach_iter = mExportForeachFuncs.begin(),
          foreach_end = mExportForeachFuncs.end(); foreach_iter != foreach_end;
          foreach_iter++) {
    ALOGV("name: %s, signature: %05x", foreach_iter->first,
                                       foreach_iter->second);
  }
#undef DUMP_LIST_HEADER

#endif // LOG_NDEBUG
  return;
}

const char *RSInfo::getStringFromPool(rsinfo::StringIndexTy pStrIdx) const {
  // String pool uses direct indexing. Ensure that the pStrIdx is within the
  // range.
  if (pStrIdx >= mHeader.strPoolSize) {
    ALOGE("String index #%u is out of range in string pool (size: %u)!",
          pStrIdx, mHeader.strPoolSize);
    return NULL;
  }
  return &mStringPool[ pStrIdx ];
}

rsinfo::StringIndexTy RSInfo::getStringIdxInPool(const char *pStr) const {
  // Assume we are on the flat memory architecture (i.e., the memory space is
  // continuous.)
  if ((mStringPool + mHeader.strPoolSize) < pStr) {
    ALOGE("String %s does not in the string pool!", pStr);
    return rsinfo::gInvalidStringIndex;
  }
  return (pStr - mStringPool);
}

enum RSInfo::FloatPrecision RSInfo::getFloatPrecisionRequirement() const {
  // Check to see if we have any FP precision-related pragmas.
  static const char relaxed_pragma[] = "rs_fp_relaxed";
  static const char imprecise_pragma[] = "rs_fp_imprecise";
  bool relaxed_pragma_seen = false;

  for (PragmaListTy::const_iterator pragma_iter = mPragmas.begin(),
           pragma_end = mPragmas.end(); pragma_iter != pragma_end;
       pragma_iter++) {
    const char *pragma_key = pragma_iter->first;
    if (::strcmp(pragma_key, relaxed_pragma) == 0) {
      relaxed_pragma_seen = true;
    } else if (::strcmp(pragma_key, imprecise_pragma) == 0) {
      if (relaxed_pragma_seen) {
        ALOGW("Multiple float precision pragmas specified!");
      }
      // Fast return when there's rs_fp_imprecise specified.
      return Imprecise;
    }
  }

  // Imprecise is selected over Relaxed precision.
  // In the absence of both, we stick to the default Full precision.
  if (relaxed_pragma_seen) {
    return Relaxed;
  } else {
    return Full;
  }
  // unreachable
}
