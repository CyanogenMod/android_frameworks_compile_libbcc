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

#ifndef __ANDROID_BCINFO_BITCODEWRAPPER_H__
#define __ANDROID_BCINFO_BITCODEWRAPPER_H__

#include <cstddef>
#include <stdint.h>

namespace bcinfo {

struct BCWrapperHeader {
  uint32_t Magic;
  uint32_t Version;
  uint32_t BitcodeOffset;
  uint32_t BitcodeSize;
  uint32_t HeaderVersion;
  uint32_t TargetAPI;
};

enum BCFileType {
  BC_NOT_BC = 0,
  BC_WRAPPER = 1,
  BC_RAW = 2
};

class BitcodeWrapper {
 private:
  enum BCFileType mFileType;
  const char *mBitcode;
  const char *mBitcodeEnd;
  size_t mBitcodeSize;

  struct BCWrapperHeader mBCHeader;

 public:
  /**
   * Reads wrapper information from \p bitcode.
   *
   * \param bitcode - input bitcode string.
   * \param bitcodeSize - length of \p bitcode string (in bytes).
   */
  BitcodeWrapper(const char *bitcode, size_t bitcodeSize);

  ~BitcodeWrapper();

  /**
   * Attempt to unwrap the target bitcode.
   *
   * \return true on success and false if an error occurred.
   */
  bool unwrap();

  /**
   * \return type of bitcode file.
   */
  enum BCFileType getBCFileType() const {
    return mFileType;
  }

  /**
   * \return header version of bitcode wrapper. This can only be 0 currently.
   */
  uint32_t getHeaderVersion() const {
    return mBCHeader.HeaderVersion;
  }

  /**
   * \return target API version of this script.
   */
  uint32_t getTargetAPI() const {
    return mBCHeader.TargetAPI;
  }
};

}  // namespace bcinfo

#endif  // __ANDROID_BCINFO_BITCODEWRAPPER_H__
