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

#include "bcinfo/BitcodeWrapper.h"

#define LOG_TAG "bcinfo"
#include <cutils/log.h>

#include "llvm/Bitcode/ReaderWriter.h"

#include <cstdlib>
#include <cstring>

namespace bcinfo {

BitcodeWrapper::BitcodeWrapper(const char *bitcode, size_t bitcodeSize)
    : mFileType(BC_NOT_BC), mBitcode(bitcode),
      mBitcodeEnd(bitcode + bitcodeSize - 1), mBitcodeSize(bitcodeSize) {
  memset(&mBCHeader, 0, sizeof(mBCHeader));
}


BitcodeWrapper::~BitcodeWrapper() {
  return;
}


bool BitcodeWrapper::unwrap() {
  if (!mBitcode || !mBitcodeSize) {
    LOGE("Invalid/empty bitcode");
    return false;
  }

  if (llvm::isBitcodeWrapper((const unsigned char*) mBitcode,
                             (const unsigned char*) mBitcodeEnd)) {
    if (mBitcodeSize < sizeof(mBCHeader)) {
      LOGE("Invalid bitcode size");
      return false;
    }

    mFileType = BC_WRAPPER;
    memcpy(&mBCHeader, mBitcode, sizeof(mBCHeader));
    return true;
  } else if (llvm::isRawBitcode((const unsigned char*) mBitcode,
                                (const unsigned char*) mBitcodeEnd)) {
    mFileType = BC_RAW;
    return true;
  } else {
    LOGE("Not bitcode");
    mFileType = BC_NOT_BC;
    return false;
  }
}

}  // namespace bcinfo

