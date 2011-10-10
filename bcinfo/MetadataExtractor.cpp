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

#include "bcinfo/MetadataExtractor.h"

#define LOG_TAG "bcinfo"
#include <cutils/log.h>

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cstdlib>

namespace bcinfo {

// Name of metadata node where pragma info resides (should be synced with
// slang.cpp)
static const llvm::StringRef PragmaMetadataName = "#pragma";

// Name of metadata node where exported variable names reside (should be
// synced with slang_rs_metadata.h)
static const llvm::StringRef ExportVarMetadataName = "#rs_export_var";

// Name of metadata node where exported function names reside (should be
// synced with slang_rs_metadata.h)
static const llvm::StringRef ExportFuncMetadataName = "#rs_export_func";

// Name of metadata node where exported ForEach signature information resides
// (should be synced with slang_rs_metadata.h)
static const llvm::StringRef ExportForEachMetadataName = "#rs_export_foreach";

// Name of metadata node where RS object slot info resides (should be
// synced with slang_rs_metadata.h)
static const llvm::StringRef ObjectSlotMetadataName = "#rs_object_slots";


MetadataExtractor::MetadataExtractor(const char *bitcode, size_t bitcodeSize)
    : mBitcode(bitcode), mBitcodeSize(bitcodeSize), mExportVarCount(0),
      mExportFuncCount(0), mExportForEachSignatureCount(0),
      mExportForEachSignatureList(NULL), mPragmaCount(0), mPragmaKeyList(NULL),
      mPragmaValueList(NULL), mObjectSlotCount(0), mObjectSlotList(NULL) {
}


MetadataExtractor::~MetadataExtractor() {
  delete [] mExportForEachSignatureList;
  mExportForEachSignatureList = NULL;

  if (mPragmaCount > 0) {
    for (size_t i = 0; i < mPragmaCount; i++) {
      if (mPragmaKeyList) {
        delete [] mPragmaKeyList[i];
        mPragmaKeyList[i] = NULL;
      }
      if (mPragmaValueList) {
        delete [] mPragmaValueList[i];
        mPragmaValueList[i] = NULL;
      }
    }
  }
  delete [] mPragmaKeyList;
  mPragmaKeyList = NULL;
  delete [] mPragmaValueList;
  mPragmaValueList = NULL;

  delete [] mObjectSlotList;
  mObjectSlotList = NULL;

  return;
}


bool MetadataExtractor::populateObjectSlotMetadata(
    const llvm::NamedMDNode *ObjectSlotMetadata) {
  if (!ObjectSlotMetadata) {
    return true;
  }

  mObjectSlotCount = ObjectSlotMetadata->getNumOperands();

  if (!mObjectSlotCount) {
    return true;
  }

  uint32_t *TmpSlotList = new uint32_t[mObjectSlotCount];
  memset(TmpSlotList, 0, mObjectSlotCount * sizeof(*TmpSlotList));

  for (size_t i = 0; i < mObjectSlotCount; i++) {
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
        TmpSlotList[i] = USlot;
      }
    }
  }

  mObjectSlotList = TmpSlotList;

  return true;
}


static const char *createStringFromValue(llvm::Value *v) {
  if (v->getValueID() != llvm::Value::MDStringVal) {
    return NULL;
  }

  llvm::StringRef ref = static_cast<llvm::MDString*>(v)->getString();

  char *c = new char[ref.size() + 1];
  memcpy(c, ref.data(), ref.size());
  c[ref.size()] = '\0';

  return c;
}


void MetadataExtractor::populatePragmaMetadata(
    const llvm::NamedMDNode *PragmaMetadata) {
  if (!PragmaMetadata) {
    return;
  }

  mPragmaCount = PragmaMetadata->getNumOperands();
  if (!mPragmaCount) {
    return;
  }

  mPragmaKeyList = new const char*[mPragmaCount];
  mPragmaValueList = new const char*[mPragmaCount];

  for (size_t i = 0; i < mPragmaCount; i++) {
    llvm::MDNode *Pragma = PragmaMetadata->getOperand(i);
    if (Pragma != NULL && Pragma->getNumOperands() == 2) {
      llvm::Value *PragmaKeyMDS = Pragma->getOperand(0);
      mPragmaKeyList[i] = createStringFromValue(PragmaKeyMDS);
      llvm::Value *PragmaValueMDS = Pragma->getOperand(1);
      mPragmaValueList[i] = createStringFromValue(PragmaValueMDS);
    }
  }

  return;
}


bool MetadataExtractor::populateForEachMetadata(
    const llvm::NamedMDNode *ExportForEachMetadata) {
  if (!ExportForEachMetadata) {
    // Handle legacy case for pre-ICS bitcode that doesn't contain a metadata
    // section for ForEach. We generate a full signature for a "root" function
    // which means that we need to set the bottom 5 bits in the mask.
    mExportForEachSignatureCount = 1;
    uint32_t *TmpSigList = new uint32_t[mExportForEachSignatureCount];
    TmpSigList[0] = 0x1f;
    mExportForEachSignatureList = TmpSigList;
    return true;
  }

  mExportForEachSignatureCount = ExportForEachMetadata->getNumOperands();
  if (!mExportForEachSignatureCount) {
    return true;
  }

  uint32_t *TmpSigList = new uint32_t[mExportForEachSignatureCount];

  for (size_t i = 0; i < mExportForEachSignatureCount; i++) {
    llvm::MDNode *SigNode = ExportForEachMetadata->getOperand(i);
    if (SigNode != NULL && SigNode->getNumOperands() == 1) {
      llvm::Value *SigVal = SigNode->getOperand(0);
      if (SigVal->getValueID() == llvm::Value::MDStringVal) {
        llvm::StringRef SigString =
            static_cast<llvm::MDString*>(SigVal)->getString();
        uint32_t Signature = 0;
        if (SigString.getAsInteger(10, Signature)) {
          LOGE("Non-integer signature value '%s'", SigString.str().c_str());
          return false;
        }
        TmpSigList[i] = Signature;
      }
    }
  }

  mExportForEachSignatureList = TmpSigList;

  return true;
}



bool MetadataExtractor::extract() {
  if (!mBitcode || !mBitcodeSize) {
    LOGE("Invalid/empty bitcode");
    return false;
  }

  llvm::OwningPtr<llvm::LLVMContext> mContext(new llvm::LLVMContext());
  llvm::OwningPtr<llvm::MemoryBuffer> MEM(
    llvm::MemoryBuffer::getMemBuffer(
      llvm::StringRef(mBitcode, mBitcodeSize)));
  std::string error;

  // Module ownership is handled by the context, so we don't need to free it.
  llvm::Module *module = llvm::ParseBitcodeFile(MEM.get(), *mContext, &error);
  if (!module) {
    LOGE("Could not parse bitcode file");
    LOGE("%s", error.c_str());
    return false;
  }

  const llvm::NamedMDNode *ExportVarMetadata =
      module->getNamedMetadata(ExportVarMetadataName);
  const llvm::NamedMDNode *ExportFuncMetadata =
      module->getNamedMetadata(ExportFuncMetadataName);
  const llvm::NamedMDNode *ExportForEachMetadata =
      module->getNamedMetadata(ExportForEachMetadataName);
  const llvm::NamedMDNode *PragmaMetadata =
      module->getNamedMetadata(PragmaMetadataName);
  const llvm::NamedMDNode *ObjectSlotMetadata =
      module->getNamedMetadata(ObjectSlotMetadataName);

  if (ExportVarMetadata) {
    mExportVarCount = ExportVarMetadata->getNumOperands();
  }

  if (ExportFuncMetadata) {
    mExportFuncCount = ExportFuncMetadata->getNumOperands();
  }

  if (!populateForEachMetadata(ExportForEachMetadata)) {
    LOGE("Could not populate ForEach signature metadata");
    return false;
  }

  populatePragmaMetadata(PragmaMetadata);

  if (!populateObjectSlotMetadata(ObjectSlotMetadata)) {
    LOGE("Could not populate object slot metadata");
    return false;
  }

  return true;
}

}  // namespace bcinfo

