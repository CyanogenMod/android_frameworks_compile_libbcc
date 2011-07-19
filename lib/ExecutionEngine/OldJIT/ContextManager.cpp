/*
 * copyright 2010, the android open source project
 *
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/license-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "as is" basis,
 * without warranties or conditions of any kind, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 */

#include "ContextManager.h"

#include "DebugHelper.h"

#include <llvm/Support/Mutex.h>
#include <llvm/Support/MutexGuard.h>

#include <errno.h>
#include <sys/mman.h>
#include <utils/threads.h>

#include <stddef.h>
#include <string.h>


namespace bcc {

// Starting address for context slots
char * const ContextManager::ContextFixedAddr = BCC_CONTEXT_FIXED_ADDR_;

// ContextManager singleton object
ContextManager ContextManager::TheContextManager;


ContextManager::ContextManager() {
  // Initialize context slot occupation table to false
  for (size_t i = 0; i < ContextSlotCount; ++i) {
    mContextSlotOccupied[i] = false;
  }
}

char *ContextManager::allocateContext() {
  {
    // Acquire mContextSlotOccupiedLock
    llvm::MutexGuard Locked(mContextSlotOccupiedLock);

    // Try to allocate context on the managed context slot.
    for (size_t i = 0; i < ContextSlotCount; ++i) {
      if (mContextSlotOccupied[i]) {
        continue;
      }

      void *addr = ContextFixedAddr + ContextSize * i;
      void *result = mmap(addr, ContextSize,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANON, -1, 0);

      if (result == addr) {
        LOGI("Allocate bcc context. addr=%p\n", result);
        mContextSlotOccupied[i] = true;
        return static_cast<char *>(result);
      }

      if (result && result != MAP_FAILED) {
        LOGE("Unable to allocate. suggested=%p, result=%p\n", addr, result);
        munmap(result, ContextSize);
      }

      LOGE("Unable to allocate. addr=%p.  Retry ...\n", addr);
    }
    // Release mContextSlotOccupiedLock
  }

  // No slot available, allocate at arbitary address.
  void *result = mmap(0, ContextSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANON, -1, 0);

  if (!result || result == MAP_FAILED) {
    LOGE("Unable to mmap. (reason: %s)\n", strerror(errno));
    return NULL;
  }

  LOGI("Allocate bcc context. addr=%p\n", result);
  return static_cast<char *>(result);
}


char *ContextManager::allocateContext(char *addr,
                                      int imageFd, off_t imageOffset) {
  // This function should only allocate context when address is an context
  // slot address.  And the image offset is aligned to the pagesize.

  if (imageFd < 0) {
    LOGE("Invalid file descriptor for bcc context image\n");
    return NULL;
  }

  unsigned long pagesize = (unsigned long)sysconf(_SC_PAGESIZE);

  if (imageOffset % pagesize > 0) {
    LOGE("BCC context image offset is not aligned to page size\n");
    return NULL;
  }

  ssize_t slot = getSlotIndexFromAddress(addr);
  if (slot < 0) {
    LOGE("Suggested address is not a bcc context slot address\n");
    return NULL;
  }

  llvm::MutexGuard Locked(mContextSlotOccupiedLock);
  if (mContextSlotOccupied[slot]) {
    LOGW("Suggested bcc context slot has been occupied.\n");
    return NULL;
  }

  // LOGI("addr=%x, imageFd=%d, imageOffset=%x", addr, imageFd, imageOffset);
  void *result = mmap(addr, ContextSize,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE, imageFd, imageOffset);

  if (!result || result == MAP_FAILED) {
    LOGE("Unable to allocate. addr=%p\n", addr);
    return NULL;
  }

  if (result != addr) {
    LOGE("Unable to allocate at suggested=%p, result=%p\n", addr, result);
    munmap(result, ContextSize);
    return NULL;
  }

  LOGI("Allocate bcc context. addr=%p\n", addr);
  mContextSlotOccupied[slot] = true;
  return static_cast<char *>(result);
}


void ContextManager::deallocateContext(char *addr) {
  if (!addr) {
    return;
  }

  llvm::MutexGuard Locked(mContextSlotOccupiedLock);

  LOGI("Deallocate bcc context. addr=%p\n", addr);

  // Unmap
  if (munmap(addr, ContextSize) < 0) {
    LOGE("Unable to unmap. addr=%p (reason: %s)\n", addr, strerror(errno));
    return;
  }

  // If the address is one of the context slot, then mark such slot
  // freely available as well.
  ssize_t slot = getSlotIndexFromAddress(addr);
  if (slot >= 0) {
    // Give the context slot back.
    mContextSlotOccupied[slot] = false;
  }
}


bool ContextManager::isManagingContext(char *addr) const {
  ssize_t slot = getSlotIndexFromAddress(addr);

  if (slot < 0) {
    return false;
  }

  llvm::MutexGuard Locked(mContextSlotOccupiedLock);
  return mContextSlotOccupied[slot];
}


ssize_t ContextManager::getSlotIndexFromAddress(char *addr) {
  if (addr >= ContextFixedAddr) {
    size_t offset = (size_t)(addr - ContextFixedAddr);
    if (offset % ContextSize == 0) {
      size_t slot = offset / ContextSize;
      if (slot < ContextSlotCount) {
        return slot;
      }
    }
  }
  return -1;
}



} // namespace bcc
