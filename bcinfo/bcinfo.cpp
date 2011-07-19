/*
 * Copyright 2011, The Android Open Source Project
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

#include <bcinfo/bcinfo.h>

#define LOG_TAG "bcinfo"
#include <cutils/log.h>

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cstdlib>

// Name of metadata node where pragma info resides (should be synced with
// slang.cpp)
const llvm::StringRef PragmaMetadataName = "#pragma";

// Name of metadata node where exported variable names reside (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef ExportVarMetadataName = "#rs_export_var";

// Name of metadata node where exported function names reside (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef ExportFuncMetadataName = "#rs_export_func";

// Name of metadata node where RS object slot info resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef ObjectSlotMetadataName = "#rs_object_slots";

static bool populateObjectSlotMetadata(struct BCScriptMetadata *md,
    const llvm::NamedMDNode *ObjectSlotMetadata) {
  if (!md) {
    return false;
  }

  if (!ObjectSlotMetadata) {
    return true;
  }

  md->objectSlotCount = ObjectSlotMetadata->getNumOperands();

  if (!md->objectSlotCount) {
    return true;
  }

  md->objectSlotList = (uint32_t*) calloc(md->objectSlotCount,
                                          sizeof(*md->objectSlotList));
  if (!md->objectSlotList) {
    return false;
  }

  for (size_t i = 0; i < md->objectSlotCount; i++) {
    llvm::MDNode *ObjectSlot = ObjectSlotMetadata->getOperand(i);
    if (ObjectSlot != NULL && ObjectSlot->getNumOperands() == 1) {
      llvm::Value *SlotMDS = ObjectSlot->getOperand(0);
      if (SlotMDS->getValueID() == llvm::Value::MDStringVal) {
        llvm::StringRef Slot =
            static_cast<llvm::MDString*>(SlotMDS)->getString();
        uint32_t USlot = 0;
        if (Slot.getAsInteger(10, USlot)) {
          LOGE("Non-integer object slot value '%s'", Slot.str().c_str());
          return false;
        }
        md->objectSlotList[i] = USlot;
      }
    }
  }

  return true;
}


static bool createStringFromValue(const char **mem, llvm::Value *v) {
  if (v->getValueID() != llvm::Value::MDStringVal) {
    return true;
  }

  llvm::StringRef ref = static_cast<llvm::MDString*>(v)->getString();

  char *c = (char *) malloc(ref.size() + 1);
  if (!c) {
    return false;
  }

  memcpy(c, ref.data(), ref.size());
  c[ref.size()] = '\0';

  *mem = const_cast<const char*>(c);

  return true;
}


static bool populatePragmaMetadata(struct BCScriptMetadata *md,
    const llvm::NamedMDNode *PragmaMetadata) {
  if (!md) {
    return false;
  }

  if (!PragmaMetadata) {
    return true;
  }

  md->pragmaCount = PragmaMetadata->getNumOperands();
  if (!md->pragmaCount) {
    return true;
  }

  md->pragmaKeyList = (const char**) calloc(md->pragmaCount,
                                            sizeof(*md->pragmaKeyList));
  if (!md->pragmaKeyList) {
    return false;
  }
  md->pragmaValueList = (const char**) calloc(md->pragmaCount,
                                              sizeof(*md->pragmaValueList));
  if (!md->pragmaValueList) {
    return false;
  }

  for (size_t i = 0; i < md->pragmaCount; i++) {
    llvm::MDNode *Pragma = PragmaMetadata->getOperand(i);
    if (Pragma != NULL && Pragma->getNumOperands() == 2) {
      llvm::Value *PragmaKeyMDS = Pragma->getOperand(0);
      if (!createStringFromValue(&md->pragmaKeyList[i], PragmaKeyMDS)) {
        return false;
      }
      llvm::Value *PragmaValueMDS = Pragma->getOperand(1);
      if (!createStringFromValue(&md->pragmaValueList[i], PragmaValueMDS)) {
        return false;
      }
    }
  }

  return true;
}


extern "C" struct BCScriptMetadata *bcinfoGetScriptMetadata(
    const char *bitcode, size_t bitcodeSize, unsigned int flags) {
  struct BCScriptMetadata *md = NULL;
  std::string error;

  if (!bitcode || !bitcodeSize) {
    LOGE("Invalid/empty bitcode");
    return NULL;
  }

  if (flags != 0) {
    LOGE("flags must be zero for this version");
    return NULL;
  }

  llvm::LLVMContext *mContext = new llvm::LLVMContext();
  llvm::OwningPtr<llvm::MemoryBuffer> MEM(
    llvm::MemoryBuffer::getMemBuffer(
      llvm::StringRef(bitcode, bitcodeSize)));
  llvm::Module *module = NULL;

  md = (struct BCScriptMetadata*) calloc(1, sizeof(*md));
  if (!md) {
    LOGE("Failed to allocate memory for BCScriptMetadata");
    return NULL;
  }

  module = llvm::ParseBitcodeFile(MEM.get(), *mContext, &error);
  if (!module) {
    LOGE("Could not parse bitcode file");
    free(md);
    return NULL;
  }

  const llvm::NamedMDNode *ExportVarMetadata =
      module->getNamedMetadata(ExportVarMetadataName);
  const llvm::NamedMDNode *ExportFuncMetadata =
      module->getNamedMetadata(ExportFuncMetadataName);
  const llvm::NamedMDNode *PragmaMetadata =
      module->getNamedMetadata(PragmaMetadataName);
  const llvm::NamedMDNode *ObjectSlotMetadata =
      module->getNamedMetadata(ObjectSlotMetadataName);

  if (ExportVarMetadata) {
    md->exportVarCount = ExportVarMetadata->getNumOperands();
  }

  if (ExportFuncMetadata) {
    md->exportFuncCount = ExportFuncMetadata->getNumOperands();
  }

  if (!populatePragmaMetadata(md, PragmaMetadata)) {
    LOGE("Could not populate pragma metadata");
    bcinfoReleaseScriptMetadata(&md);
    return NULL;
  }

  if (!populateObjectSlotMetadata(md, ObjectSlotMetadata)) {
    LOGE("Could not populate object slot metadata");
    bcinfoReleaseScriptMetadata(&md);
    return NULL;
  }

  LOGV("exportVarCount: %d", md->exportVarCount);
  LOGV("exportFuncCount: %d", md->exportFuncCount);
  LOGV("pragmaCount: %d", md->pragmaCount);
  LOGV("objectSlotCount: %d", md->objectSlotCount);

  return md;
}


extern "C" void bcinfoReleaseScriptMetadata(struct BCScriptMetadata **pmd) {
  if (pmd && *pmd) {
    struct BCScriptMetadata *md = *pmd;

    if (md->pragmaCount > 0) {
      for (size_t i = 0; i < md->pragmaCount; i++) {
        if (md->pragmaKeyList) {
          free(const_cast<char*>(md->pragmaKeyList[i]));
        }
        if (md->pragmaValueList) {
          free(const_cast<char*>(md->pragmaValueList[i]));
        }
      }
    }
    free(md->pragmaKeyList);
    free(md->pragmaValueList);
    free(md->objectSlotList);
    memset(md, 0, sizeof(*md));

    free(md);
    *pmd = NULL;
  }
  return;
}

