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

#include <stddef.h>
#include <string.h>


namespace {
  static bool ContextSlotTaken[BCC_CONTEXT_SLOT_COUNT];
} // namespace anonymous


namespace bcc {

char *allocateContext() {
  // Try to allocate context on the managed context slot.
  for (size_t i = 0; i < BCC_CONTEXT_SLOT_COUNT; ++i) {
    if (ContextSlotTaken[i]) {
      continue;
    }

    // Take the context slot.  (No matter we can mmap or not)
    ContextSlotTaken[i] = true;

    void *addr = BCC_CONTEXT_FIXED_ADDR + BCC_CONTEXT_SIZE * i;

    // Try to mmap
    void *result = mmap(addr, BCC_CONTEXT_SIZE,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);

    if (result && result != MAP_FAILED) { // Allocated successfully
      return (char *)result;
    }

    LOGE("Unable to mmap at %p.  Retry ...\n", addr);
  }

  // No slot available, allocate at arbitary address.

  void *result = mmap(0, BCC_CONTEXT_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANON, -1, 0);

  if (!result || result == MAP_FAILED) {
    LOGE("Unable to mmap.  (reason: %s)\n", strerror(errno));
    return NULL;
  }

  return (char *)result;
}


char *allocateContext(char *addr, int imageFd, off_t imageOffset) {
  // This function should only allocate context when address is an context
  // slot address.  And the image offset is aligned to the pagesize.

  if (imageFd < 0) {
    // Invalid file descriptor.
    return NULL;
  }

  unsigned long pagesize = (unsigned long)sysconf(_SC_PAGESIZE);

  if (imageOffset % pagesize > 0) {
    // Image is not aligned in the cache file.
    return NULL;
  }

  if (addr < BCC_CONTEXT_FIXED_ADDR) {
    // If the suggest address is not at the context slot, then return
    // NULL as error.
    return NULL;
  }

  size_t offset = (size_t)(addr - BCC_CONTEXT_FIXED_ADDR);
  size_t slot = offset / BCC_CONTEXT_SIZE;

  if (offset % BCC_CONTEXT_SIZE != 0 || slot >= BCC_CONTEXT_SLOT_COUNT) {
    // Invalid Slot Address (Not aligned or overflow)
    return NULL;
  }
  // LOGI("BEFORE slot[%d] checking", slot);
  if (ContextSlotTaken[slot]) {
    // Slot has been taken.
    return NULL;
  }
  // LOGI("AFTER slot checking");

  ContextSlotTaken[slot] = true;

  // LOGI("addr=%x, imageFd=%d, imageOffset=%x", addr, imageFd, imageOffset);
  void *result = mmap(addr, BCC_CONTEXT_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE, imageFd, imageOffset);

  return (result && result != MAP_FAILED) ? (char *)result : NULL;
}


void deallocateContext(char *addr) {
  if (!addr) {
    return;
  }

  // Unmap
  if (munmap(addr, BCC_CONTEXT_SIZE) < 0) {
    LOGE("Unable to unmap addr %p (reason: %s)\n", addr, strerror(errno));
    return;
  }

  // If the address is one of the context slot, then mark such slot
  // freely available as well.
  if (addr >= BCC_CONTEXT_FIXED_ADDR) {
    size_t offset = (size_t)(addr - BCC_CONTEXT_FIXED_ADDR);
    size_t slot = offset / BCC_CONTEXT_SIZE;

    if (offset % BCC_CONTEXT_SIZE == 0 && slot < BCC_CONTEXT_SLOT_COUNT) {
      // Give the context slot back.
      ContextSlotTaken[slot] = false;
    }
  }
}

} // namespace bcc
