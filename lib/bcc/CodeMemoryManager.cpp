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

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "CodeMemoryManager.h"

#include "llvm/Support/ErrorHandling.h"

#include <errno.h>
#include <sys/mman.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <map>
#include <string>
#include <utility>


namespace bcc {


const unsigned int MaxCodeSize = BCC_MMAP_IMG_CODE_SIZE;
const unsigned int MaxGOTSize = 1 * 1024;
const unsigned int MaxGlobalVarSize = BCC_MMAP_IMG_DATA_SIZE;


CodeMemoryManager::CodeMemoryManager()
  : mpCodeMem(NULL), mpGVMem(NULL), mpGOTBase(NULL) {

  reset();
  std::string ErrMsg;

  // Try to use fixed address

  // Note: If we failed to allocate mpCodeMem at fixed address,
  // the caching mechanism has to either perform relocation or
  // give up.  If the caching mechanism gives up, then we have to
  // recompile the bitcode and wasting a lot of time.

  for (size_t i = 0; i < BCC_MMAP_IMG_COUNT; ++i) {
    if (Compiler::BccMmapImgAddrTaken[i]) {
      // The address BCC_MMAP_IMG_BEGIN + i * BCC_MMAP_IMG_SIZE has
      // been taken.
      continue;
    }

    // Occupy the mmap image address first (No matter allocation
    // success or not.  Keep occupying if succeed; otherwise,
    // keep occupying as a mark of failure.)
    Compiler::BccMmapImgAddrTaken[i] = true;

    void *currMmapImgAddr =
      reinterpret_cast<void *>(BCC_MMAP_IMG_BEGIN + i * BCC_MMAP_IMG_SIZE);

    mpCodeMem = mmap(currMmapImgAddr, BCC_MMAP_IMG_SIZE,
                     PROT_READ | PROT_EXEC | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON | MAP_FIXED,
                     -1, 0);

    if (mpCodeMem == MAP_FAILED) {
      LOGE("Mmap mpCodeMem at %p failed with reason: %s. Retrying ..\n",
           currMmapImgAddr, strerror(errno));
    } else {
      // Good, we have got one mmap image address.
      break;
    }
  }

  if (!mpCodeMem || mpCodeMem == MAP_FAILED) {
    LOGE("Try to allocate mpCodeMem at arbitary address.\n");

    mpCodeMem = mmap(NULL, BCC_MMAP_IMG_SIZE,
                     PROT_READ | PROT_EXEC | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON,
                     -1, 0);

    if (mpCodeMem == MAP_FAILED) {
      LOGE("Unable to mmap mpCodeMem with reason: %s.\n", strerror(errno));
      llvm::report_fatal_error("Failed to allocate memory for emitting "
                               "codes\n" + ErrMsg);
    }
  }

  LOGE("Mmap mpCodeMem at %p successfully.\n", mpCodeMem);

  // Set global variable pool
  mpGVMem = (void *) ((char *)mpCodeMem + MaxCodeSize);

  return;
}


CodeMemoryManager::~CodeMemoryManager() {
  if (mpCodeMem && mpCodeMem != MAP_FAILED) {
    munmap(mpCodeMem, BCC_MMAP_IMG_SIZE);

    // TODO(logan): Reset Compiler::BccMmapImgAddrTaken[i] to false, so
    // that the address can be reused.
  }

  mpCodeMem = 0;
  mpGVMem = 0;
}


uint8_t *CodeMemoryManager::allocateSGMemory(uintptr_t Size,
                                             unsigned Alignment) {

  intptr_t FreeMemSize = getFreeCodeMemSize();
  if ((FreeMemSize < 0) || (static_cast<uintptr_t>(FreeMemSize) < Size))
    // The code size excesses our limit
    return NULL;

  if (Alignment == 0)
    Alignment = 1;

  uint8_t *result = getCodeMemBase() + mCurSGMemIdx - Size;
  result = (uint8_t*) (((intptr_t) result) & ~(intptr_t) (Alignment - 1));

  mCurSGMemIdx = result - getCodeMemBase();

  return result;
}


// setMemoryWritable - When code generation is in progress, the code pages
//                     may need permissions changed.
void CodeMemoryManager::setMemoryWritable() {
  mprotect(mpCodeMem, MaxCodeSize, PROT_READ | PROT_WRITE | PROT_EXEC);
}


// When code generation is done and we're ready to start execution, the
// code pages may need permissions changed.
void CodeMemoryManager::setMemoryExecutable() {
  mprotect(mpCodeMem, MaxCodeSize, PROT_READ | PROT_EXEC);
}


// Setting this flag to true makes the memory manager garbage values over
// freed memory.  This is useful for testing and debugging, and is to be
// turned on by default in debug mode.
void CodeMemoryManager::setPoisonMemory(bool poison) {
  // no effect
}


// Global Offset Table Management

// If the current table requires a Global Offset Table, this method is
// invoked to allocate it.  This method is required to set HasGOT to true.
void CodeMemoryManager::AllocateGOT() {
  assert(mpGOTBase != NULL && "Cannot allocate the GOT multiple times");
  mpGOTBase = allocateSGMemory(MaxGOTSize);
  HasGOT = true;
}


// Main Allocation Functions

// When we start JITing a function, the JIT calls this method to allocate a
// block of free RWX memory, which returns a pointer to it. If the JIT wants
// to request a block of memory of at least a certain size, it passes that
// value as ActualSize, and this method returns a block with at least that
// much space. If the JIT doesn't know ahead of time how much space it will
// need to emit the function, it passes 0 for the ActualSize. In either
// case, this method is required to pass back the size of the allocated
// block through ActualSize. The JIT will be careful to not write more than
// the returned ActualSize bytes of memory.
uint8_t *CodeMemoryManager::startFunctionBody(const llvm::Function *F,
                                              uintptr_t &ActualSize) {
  intptr_t FreeMemSize = getFreeCodeMemSize();
  if ((FreeMemSize < 0) ||
      (static_cast<uintptr_t>(FreeMemSize) < ActualSize))
    // The code size excesses our limit
    return NULL;

  ActualSize = getFreeCodeMemSize();
  return (getCodeMemBase() + mCurFuncMemIdx);
}

// This method is called when the JIT is done codegen'ing the specified
// function. At this point we know the size of the JIT compiled function.
// This passes in FunctionStart (which was returned by the startFunctionBody
// method) and FunctionEnd which is a pointer to the actual end of the
// function. This method should mark the space allocated and remember where
// it is in case the client wants to deallocate it.
void CodeMemoryManager::endFunctionBody(const llvm::Function *F,
                                        uint8_t *FunctionStart,
                                        uint8_t *FunctionEnd) {
  assert(FunctionEnd > FunctionStart);
  assert(FunctionStart == (getCodeMemBase() + mCurFuncMemIdx) &&
         "Mismatched function start/end!");

  // Advance the pointer
  intptr_t FunctionCodeSize = FunctionEnd - FunctionStart;
  assert(FunctionCodeSize <= getFreeCodeMemSize() &&
         "Code size excess the limitation!");
  mCurFuncMemIdx += FunctionCodeSize;

  // Record there's a function in our memory start from @FunctionStart
  assert(mFunctionMap.find(F) == mFunctionMap.end() &&
         "Function already emitted!");
  mFunctionMap.insert(
      std::make_pair<const llvm::Function*, std::pair<void*, void*> >(
          F, std::make_pair(FunctionStart, FunctionEnd)));

  return;
}

// Allocate a (function code) memory block of the given size. This method
// cannot be called between calls to startFunctionBody and endFunctionBody.
uint8_t *CodeMemoryManager::allocateSpace(intptr_t Size, unsigned Alignment) {
  if (getFreeCodeMemSize() < Size) {
    // The code size excesses our limit
    return NULL;
  }

  if (Alignment == 0)
    Alignment = 1;

  uint8_t *result = getCodeMemBase() + mCurFuncMemIdx;
  result = (uint8_t*) (((intptr_t) result + Alignment - 1) &
                       ~(intptr_t) (Alignment - 1));

  mCurFuncMemIdx = (result + Size) - getCodeMemBase();

  return result;
}

// Allocate memory for a global variable.
uint8_t *CodeMemoryManager::allocateGlobal(uintptr_t Size, unsigned Alignment) {
  if (getFreeGVMemSize() < Size) {
    // The code size excesses our limit
    LOGE("No Global Memory");
    return NULL;
  }

  if (Alignment == 0)
    Alignment = 1;

  uint8_t *result = getGVMemBase() + mCurGVMemIdx;
  result = (uint8_t*) (((intptr_t) result + Alignment - 1) &
                       ~(intptr_t) (Alignment - 1));

  mCurGVMemIdx = (result + Size) - getGVMemBase();

  return result;
}

// Free the specified function body. The argument must be the return value
// from a call to startFunctionBody() that hasn't been deallocated yet. This
// is never called when the JIT is currently emitting a function.
void CodeMemoryManager::deallocateFunctionBody(void *Body) {
  // linear search
  uint8_t *FunctionStart = NULL, *FunctionEnd = NULL;
  for (FunctionMapTy::iterator I = mFunctionMap.begin(),
          E = mFunctionMap.end(); I != E; I++) {
    if (I->second.first == Body) {
      FunctionStart = reinterpret_cast<uint8_t*>(I->second.first);
      FunctionEnd = reinterpret_cast<uint8_t*>(I->second.second);
      break;
    }
  }

  assert((FunctionStart == NULL) && "Memory is never allocated!");

  // free the memory
  intptr_t SizeNeedMove = (getCodeMemBase() + mCurFuncMemIdx) - FunctionEnd;

  assert(SizeNeedMove >= 0 &&
         "Internal error: CodeMemoryManager::mCurFuncMemIdx may not"
         " be correctly calculated!");

  if (SizeNeedMove > 0) {
    // there's data behind deallocating function
    memmove(FunctionStart, FunctionEnd, SizeNeedMove);
  }

  mCurFuncMemIdx -= (FunctionEnd - FunctionStart);
}

// Below are the methods we create
void CodeMemoryManager::reset() {
  mpGOTBase = NULL;
  HasGOT = false;

  mCurFuncMemIdx = 0;
  mCurSGMemIdx = MaxCodeSize - 1;
  mCurGVMemIdx = 0;

  mFunctionMap.clear();
}

} // namespace bcc
