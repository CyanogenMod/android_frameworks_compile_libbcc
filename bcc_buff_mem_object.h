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

#ifndef BCC_BUFF_MEM_OBJECT_H
#define BCC_BUFF_MEM_OBJECT_H

#include "llvm/Support/MemoryObject.h"

namespace bcc {

  class BufferMemoryObject : public llvm::MemoryObject {
  private:
    const uint8_t *mBytes;
    uint64_t mLength;

  public:
    BufferMemoryObject(const uint8_t *Bytes, uint64_t Length)
      : mBytes(Bytes), mLength(Length) {
    }

    virtual uint64_t getBase() const { return 0; }
    virtual uint64_t getExtent() const { return mLength; }

    virtual int readByte(uint64_t Addr, uint8_t *Byte) const {
      if (Addr > getExtent())
        return -1;
      *Byte = mBytes[Addr];
      return 0;
    }
  };

} // end namespace bcc

#endif // BCC_BUFF_MEM_OBJ_H
