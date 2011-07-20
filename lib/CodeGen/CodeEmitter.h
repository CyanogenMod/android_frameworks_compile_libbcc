//===-- CodeEmitter.h - CodeEmitter Class -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See external/llvm/LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CodeEmitter class.
//
//===----------------------------------------------------------------------===//

#ifndef BCC_CODEEMITTER_H
#define BCC_CODEEMITTER_H

#include <bcc/bcc.h>
#include <bcc/bcc_assert.h>
#include <bcc/bcc_cache.h>
#include "ExecutionEngine/bcc_internal.h"

#include "Config.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineRelocation.h"
#include "llvm/CodeGen/JITCodeEmitter.h"
#include "llvm/Support/ValueHandle.h"

#include <map>
#include <vector>
#include <set>

#include <stdint.h>

namespace llvm {
  class Constant;
  class GenericValue;
  class GlobalVariable;
  class GlobalValue;
  class Function;
  class MachineBasicBlock;
  class MachineFunction;
  class MachineJumpTableInfo;
  class MachineModuleInfo;
  class MCSymbol;
  class Target;
  class TargetData;
  class TargetJITInfo;
  class TargetMachine;
  class Type;
}

namespace bcc {
  class CodeMemoryManager;
  class ScriptCompiled;

  class CodeEmitter : public llvm::JITCodeEmitter {
  private:
    typedef llvm::DenseMap<const llvm::GlobalValue *, void *>
      GlobalAddressMapTy;

    typedef llvm::DenseMap<const llvm::Function *, void*>
      FunctionToLazyStubMapTy;

    typedef std::map<llvm::AssertingVH<llvm::GlobalValue>, void *>
      GlobalToIndirectSymMapTy;

  public:
    typedef GlobalAddressMapTy::const_iterator global_addresses_const_iterator;


  private:
    ScriptCompiled *mpResult;

    CodeMemoryManager *mpMemMgr;

    llvm::TargetMachine *mpTargetMachine;

    // The JITInfo for the target we are compiling to
    const llvm::Target *mpTarget;

    llvm::TargetJITInfo *mpTJI;

    const llvm::TargetData *mpTD;


    FuncInfo *mpCurEmitFunction;

    GlobalAddressMapTy mGlobalAddressMap;

    // This vector is a mapping from MBB ID's to their address. It is filled in
    // by the StartMachineBasicBlock callback and queried by the
    // getMachineBasicBlockAddress callback.
    std::vector<uintptr_t> mMBBLocations;

    // The constant pool for the current function.
    llvm::MachineConstantPool *mpConstantPool;

    // A pointer to the first entry in the constant pool.
    void *mpConstantPoolBase;

    // Addresses of individual constant pool entries.
    llvm::SmallVector<uintptr_t, 8> mConstPoolAddresses;

    // The jump tables for the current function.
    llvm::MachineJumpTableInfo *mpJumpTable;

    // A pointer to the first entry in the jump table.
    void *mpJumpTableBase;

    // When outputting a function stub in the context of some other function, we
    // save BufferBegin/BufferEnd/CurBufferPtr here.
    uint8_t *mpSavedBufferBegin, *mpSavedBufferEnd, *mpSavedCurBufferPtr;

    // These are the relocations that the function needs, as emitted.
    std::vector<llvm::MachineRelocation> mRelocations;

#if 0
    std::vector<oBCCRelocEntry> mCachingRelocations;
#endif

    // This vector is a mapping from Label ID's to their address.
    llvm::DenseMap<llvm::MCSymbol*, uintptr_t> mLabelLocations;

    // Machine module info for exception informations
    llvm::MachineModuleInfo *mpMMI;


    FunctionToLazyStubMapTy mFunctionToLazyStubMap;

    std::set<const llvm::Function*> PendingFunctions;

    GlobalToIndirectSymMapTy GlobalToIndirectSymMap;

    std::map<void*, void*> ExternalFnToStubMap;

  public:
    // Resolver to undefined symbol in CodeEmitter
    BCCSymbolLookupFn mpSymbolLookupFn;
    void *mpSymbolLookupContext;

    // Will take the ownership of @MemMgr
    explicit CodeEmitter(ScriptCompiled *result, CodeMemoryManager *pMemMgr);

    virtual ~CodeEmitter();

    global_addresses_const_iterator global_address_begin() const {
      return mGlobalAddressMap.begin();
    }

    global_addresses_const_iterator global_address_end() const {
      return mGlobalAddressMap.end();
    }

#if 0
    std::vector<oBCCRelocEntry> const &getCachingRelocations() const {
      return mCachingRelocations;
    }
#endif

    void registerSymbolCallback(BCCSymbolLookupFn pFn, void *pContext) {
      mpSymbolLookupFn = pFn;
      mpSymbolLookupContext = pContext;
    }

    void setTargetMachine(llvm::TargetMachine &TM);

    // This callback is invoked when the specified function is about to be code
    // generated.  This initializes the BufferBegin/End/Ptr fields.
    virtual void startFunction(llvm::MachineFunction &F);

    // This callback is invoked when the specified function has finished code
    // generation. If a buffer overflow has occurred, this method returns true
    // (the callee is required to try again).
    virtual bool finishFunction(llvm::MachineFunction &F);

    // Allocates and fills storage for an indirect GlobalValue, and returns the
    // address.
    virtual void *allocIndirectGV(const llvm::GlobalValue *GV,
                                  const uint8_t *Buffer, size_t Size,
                                  unsigned Alignment);

    // Emits a label
    virtual void emitLabel(llvm::MCSymbol *Label) {
      mLabelLocations[Label] = getCurrentPCValue();
    }

    // Allocate memory for a global. Unlike allocateSpace, this method does not
    // allocate memory in the current output buffer, because a global may live
    // longer than the current function.
    virtual void *allocateGlobal(uintptr_t Size, unsigned Alignment);

    // This should be called by the target when a new basic block is about to be
    // emitted. This way the MCE knows where the start of the block is, and can
    // implement getMachineBasicBlockAddress.
    virtual void StartMachineBasicBlock(llvm::MachineBasicBlock *MBB);

    // Whenever a relocatable address is needed, it should be noted with this
    // interface.
    virtual void addRelocation(const llvm::MachineRelocation &MR) {
      mRelocations.push_back(MR);
    }

    // Return the address of the @Index entry in the constant pool that was
    // last emitted with the emitConstantPool method.
    virtual uintptr_t getConstantPoolEntryAddress(unsigned Index) const {
      bccAssert(Index < mpConstantPool->getConstants().size() &&
                "Invalid constant pool index!");
      return mConstPoolAddresses[Index];
    }

    // Return the address of the jump table with index @Index in the function
    // that last called initJumpTableInfo.
    virtual uintptr_t getJumpTableEntryAddress(unsigned Index) const;

    // Return the address of the specified MachineBasicBlock, only usable after
    // the label for the MBB has been emitted.
    virtual uintptr_t getMachineBasicBlockAddress(
                                        llvm::MachineBasicBlock *MBB) const;

    // Return the address of the specified LabelID, only usable after the
    // LabelID has been emitted.
    virtual uintptr_t getLabelAddress(llvm::MCSymbol *Label) const {
      bccAssert(mLabelLocations.count(Label) && "Label not emitted!");
      return mLabelLocations.find(Label)->second;
    }

    // Specifies the MachineModuleInfo object. This is used for exception
    // handling purposes.
    virtual void setModuleInfo(llvm::MachineModuleInfo *Info) {
      mpMMI = Info;
    }

    void releaseUnnecessary();

    void reset();

  private:
    void startGVStub(const llvm::GlobalValue *GV, unsigned StubSize,
                     unsigned Alignment);

    void startGVStub(void *Buffer, unsigned StubSize);

    void finishGVStub();

    // Replace an existing mapping for GV with a new address. This updates both
    // maps as required. If Addr is null, the entry for the global is removed
    // from the mappings.
    void *UpdateGlobalMapping(const llvm::GlobalValue *GV, void *Addr);

    // Tell the execution engine that the specified global is at the specified
    // location. This is used internally as functions are JIT'd and as global
    // variables are laid out in memory.
    void AddGlobalMapping(const llvm::GlobalValue *GV, void *Addr) {
       void *&CurVal = mGlobalAddressMap[GV];
       assert((CurVal == 0 || Addr == 0) && "GlobalMapping already established!");
       CurVal = Addr;
    }

    // This returns the address of the specified global value if it is has
    // already been codegen'd, otherwise it returns null.
    void *GetPointerToGlobalIfAvailable(const llvm::GlobalValue *GV) {
      GlobalAddressMapTy::iterator I = mGlobalAddressMap.find(GV);
      return ((I != mGlobalAddressMap.end()) ? I->second : NULL);
    }

    unsigned int GetConstantPoolSizeInBytes(llvm::MachineConstantPool *MCP);

    // This function converts a Constant* into a GenericValue. The interesting
    // part is if C is a ConstantExpr.
    void GetConstantValue(const llvm::Constant *C, llvm::GenericValue &Result);

    // Stores the data in @Val of type @Ty at address @Addr.
    void StoreValueToMemory(const llvm::GenericValue &Val, void *Addr,
                            llvm::Type *Ty);

    // Recursive function to apply a @Constant value into the specified memory
    // location @Addr.
    void InitializeConstantToMemory(const llvm::Constant *C, void *Addr);

    void emitConstantPool(llvm::MachineConstantPool *MCP);

    void initJumpTableInfo(llvm::MachineJumpTableInfo *MJTI);

    void emitJumpTableInfo(llvm::MachineJumpTableInfo *MJTI);

    void *GetPointerToGlobal(llvm::GlobalValue *V,
                             void *Reference,
                             bool MayNeedFarStub);

    // If the specified function has been code-gen'd, return a pointer to the
    // function. If not, compile it, or use a stub to implement lazy compilation
    // if available.
    void *GetPointerToFunctionOrStub(llvm::Function *F);

    void *GetLazyFunctionStubIfAvailable(llvm::Function *F) {
      return mFunctionToLazyStubMap.lookup(F);
    }

    void *GetLazyFunctionStub(llvm::Function *F);

    void updateFunctionStub(const llvm::Function *F);

    void *GetPointerToFunction(const llvm::Function *F, bool AbortOnFailure);

    void *GetPointerToNamedSymbol(const std::string &Name,
                                  bool AbortOnFailure);

    // Return the address of the specified global variable, possibly emitting it
    // to memory if needed. This is used by the Emitter.
    void *GetOrEmitGlobalVariable(llvm::GlobalVariable *GV);

    // This method abstracts memory allocation of global variable so that the
    // JIT can allocate thread local variables depending on the target.
    void *GetMemoryForGV(llvm::GlobalVariable *GV);

    void EmitGlobalVariable(llvm::GlobalVariable *GV);

    void *GetPointerToGVIndirectSym(llvm::GlobalValue *V, void *Reference);

    // This is the equivalent of FunctionToLazyStubMap for external functions.
    //
    // TODO(llvm.org): Of course, external functions don't need a lazy stub.
    //                 It's actually here to make it more likely that far calls
    //                 succeed, but no single stub can guarantee that. I'll
    //                 remove this in a subsequent checkin when I actually fix
    //                 far calls.

    // Return a stub for the function at the specified address.
    void *GetExternalFunctionStub(void *FnAddr);

  };

} // namespace bcc

#endif // BCC_CODEEMITTER_H
