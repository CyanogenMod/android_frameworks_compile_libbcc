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

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "ContextManager.h"

#include <errno.h>
#include <sys/mman.h>
#include <utils/threads.h>

#include <stddef.h>
#include <string.h>


namespace {
  static bool ContextSlotTaken[BCC_CONTEXT_SLOT_COUNT];
} // namespace anonymous


namespace bcc {

static android::Mutex gContextLock;


ssize_t getSlotIndexFromAddress(char *addr) {
  if (addr >= BCC_CONTEXT_FIXED_ADDR) {
    size_t offset = (size_t)(addr - BCC_CONTEXT_FIXED_ADDR);
    if (offset % BCC_CONTEXT_SIZE == 0) {
      size_t slot = offset / BCC_CONTEXT_SIZE;
      if (slot < BCC_CONTEXT_SLOT_COUNT) {
        return slot;
      }
    }
  }
  return -1;
}


char *allocateContext() {
  android::AutoMutex _l(gContextLock);

  // Try to allocate context on the managed context slot.
  for (size_t i = 0; i < BCC_CONTEXT_SLOT_COUNT; ++i) {
    if (ContextSlotTaken[i]) {
      continue;
    }

    // Try to mmap
    void *addr = BCC_CONTEXT_FIXED_ADDR + BCC_CONTEXT_SIZE * i;
    void *result = mmap(addr, BCC_CONTEXT_SIZE,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON, -1, 0);

    if (result == addr) {
      LOGI("Allocate bcc context. addr=%p\n", result);
      ContextSlotTaken[i] = true;
      return static_cast<char *>(result);
    }

    if (result && result != MAP_FAILED) {
      LOGE("Unable to allocate. suggested=%p, result=%p\n", addr, result);
      munmap(result, BCC_CONTEXT_SIZE);
    }

    LOGE("Unable to allocate. addr=%p.  Retry ...\n", addr);
  }

  // No slot available, allocate at arbitary address.

  void *result = mmap(0, BCC_CONTEXT_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANON, -1, 0);

  if (!result || result == MAP_FAILED) {
    LOGE("Unable to mmap. (reason: %s)\n", strerror(errno));
    return NULL;
  }

  LOGI("Allocate bcc context. addr=%p\n", result);
  return (char *)result;
}


char *allocateContext(char *addr, int imageFd, off_t imageOffset) {
  android::AutoMutex _l(gContextLock);

  // This function should only allocate context when address is an context
  // slot address.  And the image offset is aligned to the pagesize.

  if (imageFd < 0) {
    // Invalid file descriptor.
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

  if (ContextSlotTaken[slot]) {
    LOGW("Suggested bcc context slot has been occupied.\n");
    return NULL;
  }

  // LOGI("addr=%x, imageFd=%d, imageOffset=%x", addr, imageFd, imageOffset);
  void *result = mmap(addr, BCC_CONTEXT_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE, imageFd, imageOffset);

  if (!result || result == MAP_FAILED) {
    LOGE("Unable to allocate. addr=%p\n", addr);
    return NULL;
  }

  if (result != addr) {
    LOGE("Unable to allocate at suggested=%p, result=%p\n", addr, result);
    munmap(result, BCC_CONTEXT_SIZE);
    return NULL;
  }

  LOGI("Allocate bcc context. addr=%p\n", addr);
  ContextSlotTaken[slot] = true;
  return static_cast<char *>(result);
}


void deallocateContext(char *addr) {
  if (!addr) {
    return;
  }

  android::AutoMutex _l(gContextLock);

  LOGI("Deallocate bcc context. addr=%p\n", addr);

  // Unmap
  if (munmap(addr, BCC_CONTEXT_SIZE) < 0) {
    LOGE("Unable to unmap. addr=%p (reason: %s)\n", addr, strerror(errno));
    return;
  }

  // If the address is one of the context slot, then mark such slot
  // freely available as well.
  ssize_t slot = getSlotIndexFromAddress(addr);
  if (slot >= 0) {
    // Give the context slot back.
    ContextSlotTaken[slot] = false;
  }
}

} // namespace bcc
