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

#include "bcinfo/BitcodeTranslator.h"

#include "BitReader_2_7/BitReader_2_7.h"

#define LOG_TAG "bcinfo"
#include <cutils/log.h>

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cstdlib>

namespace bcinfo {

/**
 * Define minimum and maximum target API versions. These correspond to the
 * same API levels used by the standard Android SDK.
 *
 * 11 - Honeycomb
 * 12 - Honeycomb MR1
 * 13 - Honeycomb MR2
 * 14 - Ice Cream Sandwich
 */
static const unsigned int kMinimumAPIVersion = 11;
static const unsigned int kMaximumAPIVersion = BCINFO_API_VERSION;
static const unsigned int kCurrentAPIVersion = 10000;

/**
 * The minimum version which does not require translation (i.e. is already
 * compatible with LLVM's default bitcode reader).
 */
static const unsigned int kMinimumUntranslatedVersion = 14;


BitcodeTranslator::BitcodeTranslator(const char *bitcode, size_t bitcodeSize,
                                     unsigned int version)
    : mBitcode(bitcode), mBitcodeSize(bitcodeSize), mTranslatedBitcode(NULL),
      mTranslatedBitcodeSize(0), mVersion(version) {
  return;
}


BitcodeTranslator::~BitcodeTranslator() {
  if (mVersion < kMinimumUntranslatedVersion) {
    // We didn't actually do a translation in the alternate case, so deleting
    // the bitcode would be improper.
    delete [] mTranslatedBitcode;
  }
  mTranslatedBitcode = NULL;
  return;
}


bool BitcodeTranslator::translate() {
  if (!mBitcode || !mBitcodeSize) {
    LOGE("Invalid/empty bitcode");
    return false;
  }

  if ((mVersion != kCurrentAPIVersion) &&
      ((mVersion < kMinimumAPIVersion) ||
       (mVersion > kMaximumAPIVersion))) {
    LOGE("Invalid API version: %u is out of range ('%u' - '%u')", mVersion,
         kMinimumAPIVersion, kMaximumAPIVersion);
    return false;
  }

  // We currently don't need to transcode any API version higher than 14 or
  // the current API version (i.e. 10000)
  if (mVersion >= kMinimumUntranslatedVersion) {
    mTranslatedBitcode = mBitcode;
    mTranslatedBitcodeSize = mBitcodeSize;
    return true;
  }

  // Do the actual transcoding by invoking a 2.7-era bitcode reader that can
  // then write the bitcode back out in a more modern (acceptable) version.
  llvm::OwningPtr<llvm::LLVMContext> mContext(new llvm::LLVMContext());
  llvm::OwningPtr<llvm::MemoryBuffer> MEM(
    llvm::MemoryBuffer::getMemBuffer(
      llvm::StringRef(mBitcode, mBitcodeSize)));
  std::string error;

  // Module ownership is handled by the context, so we don't need to free it.
  llvm::Module *module =
      llvm_2_7::ParseBitcodeFile(MEM.get(), *mContext, &error);
  if (!module) {
    LOGE("Could not parse bitcode file");
    LOGE("%s", error.c_str());
    return false;
  }

  std::vector<unsigned char> Buffer;
  llvm::BitstreamWriter Stream(Buffer);
  Buffer.reserve(mBitcodeSize);
  llvm::WriteBitcodeToStream(module, Stream);

  char *c = new char[Buffer.size()];
  memcpy(c, &Buffer.front(), Buffer.size());

  mTranslatedBitcode = c;
  mTranslatedBitcodeSize = Buffer.size();

  return true;
}

}  // namespace bcinfo

