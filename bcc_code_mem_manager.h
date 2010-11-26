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

#ifndef BCC_CODE_MEM_MANAGER_H
#define BCC_CODE_MEM_MANAGER_H

#include "llvm/ExecutionEngine/JITMemoryManager.h"

#include <map>
#include <utility>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>


#define BCC_MMAP_IMG_BEGIN 0x7e000000
#define BCC_MMAP_IMG_COUNT 5

#define BCC_MMAP_IMG_CODE_SIZE (128 * 1024)
#define BCC_MMAP_IMG_DATA_SIZE (128 * 1024)
#define BCC_MMAP_IMG_SIZE (BCC_MMAP_IMG_CODE_SIZE + BCC_MMAP_IMG_DATA_SIZE)


// Design of caching EXE:
// ======================
// 1. Each process will have virtual address available starting at 0x7e00000.
//    E.g., Books and Youtube all have its own 0x7e00000. Next, we should
//    minimize the chance of needing to do relocation INSIDE an app too.
//
// 2. Each process will have ONE class static variable called BccCodeAddr.
//    I.e., even though the Compiler class will have multiple Compiler objects,
//    e.g, one object for carousel.rs and the other for pageturn.rs,
//    both Compiler objects will share 1 static variable called BccCodeAddr.
//
// Key observation: Every app (process) initiates, say 3, scripts (which
// correspond to 3 Compiler objects) in the same order, usually.
//
// So, we should mmap to, e.g., 0x7e00000, 0x7e40000, 0x7e80000 for the 3
// scripts, respectively. Each time, BccCodeAddr should be updated after
// JITTing a script. BTW, in ~Compiler(), BccCodeAddr should NOT be
// decremented back by CodeDataSize. I.e., for 3 scripts: A, B, C,
// even if it's A -> B -> ~B -> C -> ~C -> B -> C ... no relocation will
// ever be needed.)
//
// If we are lucky, then we don't need relocation ever, since next time the
// application gets run, the 3 scripts are likely created in the SAME order.
//
//
// End-to-end algorithm on when to caching and when to JIT:
// ========================================================
// Prologue:
// ---------
// Assertion: bccReadBC() is always called and is before bccCompileBC(),
// bccLoadBinary(), ...
//
// Key variable definitions: Normally,
//  Compiler::BccCodeAddr: non-zero if (USE_CACHE)
//  | (Stricter, because currently relocation doesn't work. So mUseCache only
//  |  when BccCodeAddr is nonzero.)
//  V
//  mUseCache: In addition to (USE_CACHE), resName is non-zero
//  Note: mUseCache will be set to false later on whenever we find that caching
//        won't work. E.g., when mCodeDataAddr != mCacheHdr->cachedCodeDataAddr.
//        This is because currently relocation doesn't work.
//  | (Stricter, initially)
//  V
//  mCacheFd: In addition, >= 0 if openCacheFile() returns >= 0
//  | (Stricter)
//  V
//  mCacheNew: In addition, mCacheFd's size is 0, so need to call genCacheFile()
//             at the end of compile()
//
//
// Main algorithm:
// ---------------
// #if !USE_RELOCATE
// Case 1. ReadBC() doesn't detect a cache file:
//   compile(), which calls genCacheFile() at the end.
//   Note: mCacheNew will guard the invocation of genCacheFile()
// Case 2. ReadBC() find a cache file
//   loadCacheFile(). But if loadCacheFile() failed, should go to Case 1.
// #endif


namespace llvm {
  // Forward Declaration
  class Function;
  class GlobalValue;
};


namespace bcc {

  //////////////////////////////////////////////////////////////////////////////
  // Memory manager for the code reside in memory
  //
  // The memory for our code emitter is very simple and is conforming to the
  // design decisions of Android RenderScript's Exection Environment:
  //   The code, data, and symbol sizes are limited (currently 100KB.)
  //
  // It's very different from typical compiler, which has no limitation
  // on the code size. How does code emitter know the size of the code
  // it is about to emit? It does not know beforehand. We want to solve
  // this without complicating the code emitter too much.
  //
  // We solve this by pre-allocating a certain amount of memory,
  // and then start the code emission. Once the buffer overflows, the emitter
  // simply discards all the subsequent emission but still has a counter
  // on how many bytes have been emitted.
  //
  // So once the whole emission is done, if there's a buffer overflow,
  // it re-allocates the buffer with enough size (based on the
  //  counter from previous emission) and re-emit again.

  // 128 KiB for code
  static const unsigned int MaxCodeSize = BCC_MMAP_IMG_CODE_SIZE;
  // 1 KiB for global offset table (GOT)
  static const unsigned int MaxGOTSize = 1 * 1024;
  // 128 KiB for global variable
  static const unsigned int MaxGlobalVarSize = BCC_MMAP_IMG_DATA_SIZE;


  class CodeMemoryManager : public llvm::JITMemoryManager {
  private:
    //
    // Our memory layout is as follows:
    //
    //  The direction of arrows (-> and <-) shows memory's growth direction
    //  when more space is needed.
    //
    // @mpCodeMem:
    //  +--------------------------------------------------------------+
    //  | Function Memory ... ->                <- ...        Stub/GOT |
    //  +--------------------------------------------------------------+
    //  |<------------------ Total: @MaxCodeSize KiB ----------------->|
    //
    //  Where size of GOT is @MaxGOTSize KiB.
    //
    // @mpGVMem:
    //  +--------------------------------------------------------------+
    //  | Global variable ... ->                                       |
    //  +--------------------------------------------------------------+
    //  |<--------------- Total: @MaxGlobalVarSize KiB --------------->|
    //
    //
    // @mCurFuncMemIdx: The current index (starting from 0) of the last byte
    //                    of function code's memory usage
    // @mCurSGMemIdx: The current index (starting from tail) of the last byte
    //                    of stub/GOT's memory usage
    // @mCurGVMemIdx: The current index (starting from tail) of the last byte
    //                    of global variable's memory usage
    //
    uintptr_t mCurFuncMemIdx;
    uintptr_t mCurSGMemIdx;
    uintptr_t mCurGVMemIdx;
    void *mpCodeMem;
    void *mpGVMem;

    // GOT Base
    uint8_t *mpGOTBase;

    typedef std::map<const llvm::Function*,
                     std::pair<void * /* start address */,
                               void * /* end address */> > FunctionMapTy;

    FunctionMapTy mFunctionMap;

    inline intptr_t getFreeCodeMemSize() const {
      return mCurSGMemIdx - mCurFuncMemIdx;
    }

    uint8_t *allocateSGMemory(uintptr_t Size,
                              unsigned Alignment = 1 /* no alignment */);

    inline uintptr_t getFreeGVMemSize() const {
      return MaxGlobalVarSize - mCurGVMemIdx;
    }
    inline uint8_t *getGVMemBase() const {
      return reinterpret_cast<uint8_t*>(mpGVMem);
    }

  public:

    CodeMemoryManager();
    ~CodeMemoryManager();

    inline uint8_t *getCodeMemBase() const {
      return reinterpret_cast<uint8_t*>(mpCodeMem);
    }

    // setMemoryWritable - When code generation is in progress, the code pages
    //                     may need permissions changed.
    void setMemoryWritable();

    // When code generation is done and we're ready to start execution, the
    // code pages may need permissions changed.
    void setMemoryExecutable();

    // Setting this flag to true makes the memory manager garbage values over
    // freed memory.  This is useful for testing and debugging, and is to be
    // turned on by default in debug mode.
    void setPoisonMemory(bool poison);


    // Global Offset Table Management

    // If the current table requires a Global Offset Table, this method is
    // invoked to allocate it.  This method is required to set HasGOT to true.
    void AllocateGOT();

    // If this is managing a Global Offset Table, this method should return a
    // pointer to its base.
    uint8_t *getGOTBase() const {
      return mpGOTBase;
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
    uint8_t *startFunctionBody(const llvm::Function *F, uintptr_t &ActualSize);

    // This method is called by the JIT to allocate space for a function stub
    // (used to handle limited branch displacements) while it is JIT compiling a
    // function. For example, if foo calls bar, and if bar either needs to be
    // lazily compiled or is a native function that exists too far away from the
    // call site to work, this method will be used to make a thunk for it. The
    // stub should be "close" to the current function body, but should not be
    // included in the 'actualsize' returned by startFunctionBody.
    uint8_t *allocateStub(const llvm::GlobalValue *F, unsigned StubSize,
                          unsigned Alignment) {
      return allocateSGMemory(StubSize, Alignment);
    }

    // This method is called when the JIT is done codegen'ing the specified
    // function. At this point we know the size of the JIT compiled function.
    // This passes in FunctionStart (which was returned by the startFunctionBody
    // method) and FunctionEnd which is a pointer to the actual end of the
    // function. This method should mark the space allocated and remember where
    // it is in case the client wants to deallocate it.
    void endFunctionBody(const llvm::Function *F, uint8_t *FunctionStart,
                         uint8_t *FunctionEnd);

    // Allocate a (function code) memory block of the given size. This method
    // cannot be called between calls to startFunctionBody and endFunctionBody.
    uint8_t *allocateSpace(intptr_t Size, unsigned Alignment);

    // Allocate memory for a global variable.
    uint8_t *allocateGlobal(uintptr_t Size, unsigned Alignment);

    // Free the specified function body. The argument must be the return value
    // from a call to startFunctionBody() that hasn't been deallocated yet. This
    // is never called when the JIT is currently emitting a function.
    void deallocateFunctionBody(void *Body);

    // When we finished JITing the function, if exception handling is set, we
    // emit the exception table.
    uint8_t *startExceptionTable(const llvm::Function *F,
                                 uintptr_t &ActualSize) {
      assert(false && "Exception is not allowed in our language specification");
      return NULL;
    }

    // This method is called when the JIT is done emitting the exception table.
    void endExceptionTable(const llvm::Function *F, uint8_t *TableStart,
                           uint8_t *TableEnd, uint8_t *FrameRegister) {
      assert(false && "Exception is not allowed in our language specification");
    }

    // Free the specified exception table's memory. The argument must be the
    // return value from a call to startExceptionTable() that hasn't been
    // deallocated yet. This is never called when the JIT is currently emitting
    // an exception table.
    void deallocateExceptionTable(void *ET) {
      assert(false && "Exception is not allowed in our language specification");
    }

    // Below are the methods we create
    void reset();

  };

}  // namespace bcc

#endif  // BCC_CODE_MEM_MANAGER_H
