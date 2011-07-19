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

#ifndef BCC_CONTEXTMANAGER_H
#define BCC_CONTEXTMANAGER_H

#include <Config.h>

#include <llvm/Support/Mutex.h>

#include <unistd.h>
#include <stddef.h>


namespace bcc {

  class ContextManager {
  public:
    // Starting address of context slot address space
    static char * const ContextFixedAddr;

    // Number of the context slots
    static size_t const ContextSlotCount = BCC_CONTEXT_SLOT_COUNT_;

    // Context size
    static size_t const ContextCodeSize = BCC_CONTEXT_CODE_SIZE_;
    static size_t const ContextDataSize = BCC_CONTEXT_DATA_SIZE_;
    static size_t const ContextSize = ContextCodeSize + ContextDataSize;

  private:
    // Context manager singleton
    static ContextManager TheContextManager;

  private:
    // Mutex lock for context slot occupation table
    mutable llvm::sys::Mutex mContextSlotOccupiedLock;

    // Context slot occupation table
    bool mContextSlotOccupied[ContextSlotCount];

    ContextManager();

  public:
    static ContextManager &get() {
      return TheContextManager;
    }

    char *allocateContext();
    char *allocateContext(char *addr, int imageFd, off_t imageOffset);
    void deallocateContext(char *addr);

    bool isManagingContext(char *addr) const;

  private:
    static ssize_t getSlotIndexFromAddress(char *addr);

  };

} // namespace bcc

#endif // BCC_CONTEXTMANAGER_H
