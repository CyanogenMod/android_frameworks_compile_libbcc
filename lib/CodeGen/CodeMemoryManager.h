//===-- CodeMemoryManager.h - CodeMemoryManager Class -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See external/llvm/LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CodeMemoryManager class.
//
//===----------------------------------------------------------------------===//

#ifndef BCC_CODEMEMORYMANAGER_H
#define BCC_CODEMEMORYMANAGER_H

#include "ExecutionEngine/Compiler.h"

#include "llvm/ExecutionEngine/JITMemoryManager.h"

#include <bcc/bcc_assert.h>

#include <map>
#include <utility>

#include <stddef.h>
#include <stdint.h>


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

  extern const unsigned int MaxCodeSize;
  extern const unsigned int MaxGOTSize;
  extern const unsigned int MaxGlobalVarSize;


  class CodeMemoryManager : public llvm::JITMemoryManager {
  private:
    typedef std::map<const llvm::Function*,
                     std::pair<void * /* start address */,
                               void * /* end address */> > FunctionMapTy;


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
    char *mpCodeMem;
    char *mpGVMem;

    // GOT Base
    uint8_t *mpGOTBase;

    FunctionMapTy mFunctionMap;


  public:
    CodeMemoryManager();

    virtual ~CodeMemoryManager();

    uint8_t *getCodeMemBase() const {
      return reinterpret_cast<uint8_t*>(mpCodeMem);
    }

    // setMemoryWritable - When code generation is in progress, the code pages
    //                     may need permissions changed.
    virtual void setMemoryWritable();

    // When code generation is done and we're ready to start execution, the
    // code pages may need permissions changed.
    virtual void setMemoryExecutable();

    // Setting this flag to true makes the memory manager garbage values over
    // freed memory.  This is useful for testing and debugging, and is to be
    // turned on by default in debug mode.
    virtual void setPoisonMemory(bool poison);


    // Global Offset Table Management

    // If the current table requires a Global Offset Table, this method is
    // invoked to allocate it.  This method is required to set HasGOT to true.
    virtual void AllocateGOT();

    // If this is managing a Global Offset Table, this method should return a
    // pointer to its base.
    virtual uint8_t *getGOTBase() const {
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
    virtual uint8_t *startFunctionBody(const llvm::Function *F,
                                       uintptr_t &ActualSize);

    // This method is called by the JIT to allocate space for a function stub
    // (used to handle limited branch displacements) while it is JIT compiling a
    // function. For example, if foo calls bar, and if bar either needs to be
    // lazily compiled or is a native function that exists too far away from the
    // call site to work, this method will be used to make a thunk for it. The
    // stub should be "close" to the current function body, but should not be
    // included in the 'actualsize' returned by startFunctionBody.
    virtual uint8_t *allocateStub(const llvm::GlobalValue *F,
                                  unsigned StubSize,
                                  unsigned Alignment) {
      return allocateSGMemory(StubSize, Alignment);
    }

    // This method is called when the JIT is done codegen'ing the specified
    // function. At this point we know the size of the JIT compiled function.
    // This passes in FunctionStart (which was returned by the startFunctionBody
    // method) and FunctionEnd which is a pointer to the actual end of the
    // function. This method should mark the space allocated and remember where
    // it is in case the client wants to deallocate it.
    virtual void endFunctionBody(const llvm::Function *F,
                                 uint8_t *FunctionStart,
                                 uint8_t *FunctionEnd);

    // Allocate a (function code) memory block of the given size. This method
    // cannot be called between calls to startFunctionBody and endFunctionBody.
    virtual uint8_t *allocateSpace(intptr_t Size, unsigned Alignment);

    // Allocate memory for a global variable.
    virtual uint8_t *allocateGlobal(uintptr_t Size, unsigned Alignment);

    // Free the specified function body. The argument must be the return value
    // from a call to startFunctionBody() that hasn't been deallocated yet. This
    // is never called when the JIT is currently emitting a function.
    virtual void deallocateFunctionBody(void *Body);

    // When we finished JITing the function, if exception handling is set, we
    // emit the exception table.
    virtual uint8_t *startExceptionTable(const llvm::Function *F,
                                         uintptr_t &ActualSize) {
      bccAssert(false &&
                "Exception is not allowed in our language specification");
      return NULL;
    }

    // This method is called when the JIT is done emitting the exception table.
    virtual void endExceptionTable(const llvm::Function *F, uint8_t *TableStart,
                                   uint8_t *TableEnd, uint8_t *FrameRegister) {
      bccAssert(false &&
                "Exception is not allowed in our language specification");
    }

    // Free the specified exception table's memory. The argument must be the
    // return value from a call to startExceptionTable() that hasn't been
    // deallocated yet. This is never called when the JIT is currently emitting
    // an exception table.
    virtual void deallocateExceptionTable(void *ET) {
      bccAssert(false &&
                "Exception is not allowed in our language specification");
    }

    // Below are the methods we create
    void reset();


  private:
    intptr_t getFreeCodeMemSize() const {
      return mCurSGMemIdx - mCurFuncMemIdx;
    }

    uint8_t *allocateSGMemory(uintptr_t Size,
                              unsigned Alignment = 1 /* no alignment */);

    uintptr_t getFreeGVMemSize() const {
      return MaxGlobalVarSize - mCurGVMemIdx;
    }

    uint8_t *getGVMemBase() const {
      return reinterpret_cast<uint8_t*>(mpGVMem);
    }

  };

} // namespace bcc

#endif  // BCC_CODEMEMORYMANAGER_H
