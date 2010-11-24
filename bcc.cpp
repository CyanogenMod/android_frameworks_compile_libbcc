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

// Bitcode compiler (bcc) for Android:
//    This is an eager-compilation JIT running on Android.

// BCC_CODE_ADDR == Beginning of mmap region to generate EXE onto and
//                  to load EXE from disk cache
// Note: The static variable Compiler::BccCodeAddr = BCC_CODE_ADDR
// I.e., Compiler::BccCodeAddr is used as "Compiler::UseCache"
//
#if (USE_CACHE)
#   define BCC_CODE_ADDR 0x7e000000
#else
#   define BCC_CODE_ADDR 0
#endif

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

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cutils/hashmap.h>
#include <utils/StopWatch.h>

#if defined(__arm__)
#   define DEFAULT_ARM_CODEGEN
#   define PROVIDE_ARM_CODEGEN
#elif defined(__i386__)
#   define DEFAULT_X86_CODEGEN
#   define PROVIDE_X86_CODEGEN
#elif defined(__x86_64__)
#   define DEFAULT_X64_CODEGEN
#   define PROVIDE_X64_CODEGEN
#endif

#if defined(FORCE_ARM_CODEGEN)
#   define DEFAULT_ARM_CODEGEN
#   undef DEFAULT_X86_CODEGEN
#   undef DEFAULT_X64_CODEGEN
#   define PROVIDE_ARM_CODEGEN
#   undef PROVIDE_X86_CODEGEN
#   undef PROVIDE_X64_CODEGEN
#elif defined(FORCE_X86_CODEGEN)
#   undef DEFAULT_ARM_CODEGEN
#   define DEFAULT_X86_CODEGEN
#   undef DEFAULT_X64_CODEGEN
#   undef PROVIDE_ARM_CODEGEN
#   define PROVIDE_X86_CODEGEN
#   undef PROVIDE_X64_CODEGEN
#elif defined(FORCE_X64_CODEGEN)
#   undef DEFAULT_ARM_CODEGEN
#   undef DEFAULT_X86_CODEGEN
#   define DEFAULT_X64_CODEGEN
#   undef PROVIDE_ARM_CODEGEN
#   undef PROVIDE_X86_CODEGEN
#   define PROVIDE_X64_CODEGEN
#endif

#if defined(DEFAULT_ARM_CODEGEN)
#   define TARGET_TRIPLE_STRING    "armv7-none-linux-gnueabi"
#elif defined(DEFAULT_X86_CODEGEN)
#   define TARGET_TRIPLE_STRING    "i686-unknown-linux"
#elif defined(DEFAULT_X64_CODEGEN)
#   define  TARGET_TRIPLE_STRING   "x86_64-unknown-linux"
#endif

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
#   define ARM_USE_VFP
#endif

#include <bcc/bcc.h>
#include "bcc_runtime.h"

#define LOG_API(...) do {} while (0)
// #define LOG_API(...) fprintf (stderr, __VA_ARGS__)

#define LOG_STACK(...) do {} while (0)
// #define LOG_STACK(...) fprintf (stderr, __VA_ARGS__)

// #define PROVIDE_TRACE_CODEGEN

#if defined(USE_DISASSEMBLER)
#   include "llvm/MC/MCInst.h"
#   include "llvm/MC/MCAsmInfo.h"
#   include "llvm/MC/MCInstPrinter.h"
#   include "llvm/MC/MCDisassembler.h"
// If you want the disassemble results written to file, define this:
#   define USE_DISASSEMBLER_FILE
#endif

#include <set>
#include <map>
#include <list>
#include <cmath>
#include <string>
#include <cstring>
#include <algorithm>  // for std::reverse

// VMCore
#include "llvm/Use.h"
#include "llvm/User.h"
#include "llvm/Linker.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/Instruction.h"
#include "llvm/PassManager.h"
#include "llvm/LLVMContext.h"
#include "llvm/GlobalValue.h"
#include "llvm/Instructions.h"
#include "llvm/OperandTraits.h"
#include "llvm/TypeSymbolTable.h"

// System
#include "llvm/System/Host.h"

// ADT
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallString.h"

// Target
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetJITInfo.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/SubtargetFeature.h"

// Support
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MemoryObject.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Support/FormattedStream.h"

// Bitcode
#include "llvm/Bitcode/ReaderWriter.h"

// CodeGen
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/JITCodeEmitter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/MachineRelocation.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineCodeEmitter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"

// ExecutionEngine
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITMemoryManager.h"

extern "C" void LLVMInitializeARMDisassembler();

// For caching
struct oBCCHeader {
  uint8_t magic[4];             // includes version number
  uint8_t magicVersion[4];

  uint32_t sourceWhen;
  uint32_t rslibWhen;
  uint32_t libRSWhen;
  uint32_t libbccWhen;

  uint32_t cachedCodeDataAddr;
  uint32_t rootAddr;
  uint32_t initAddr;

  uint32_t relocOffset;         // offset of reloc table.
  uint32_t relocCount;
  uint32_t exportVarsOffset;    // offset of export var table
  uint32_t exportVarsCount;
  uint32_t exportFuncsOffset;   // offset of export func table
  uint32_t exportFuncsCount;
  uint32_t exportPragmasOffset; // offset of export pragma table
  uint32_t exportPragmasCount;

  uint32_t codeOffset;          // offset of code: 64-bit alignment
  uint32_t codeSize;
  uint32_t dataOffset;          // offset of data section
  uint32_t dataSize;

  //  uint32_t flags;           // some info flags
  uint32_t checksum;            // adler32 checksum covering deps/opt
};

struct oBCCRelocEntry {
  uint32_t relocType;           // target instruction relocation type
  uint32_t relocOffset;         // offset of hole (holeAddr - codeAddr)
  uint32_t cachedResultAddr;    // address resolved at compile time

  oBCCRelocEntry(uint32_t ty, uintptr_t off, void *addr)
    : relocType(ty),
      relocOffset(static_cast<uint32_t>(off)),
      cachedResultAddr(reinterpret_cast<uint32_t>(addr)) {
  }
};

/* oBCCHeader Offset Table */
#define k_magic                 offsetof(oBCCHeader, magic)
#define k_magicVersion          offsetof(oBCCHeader, magicVersion)
#define k_sourceWhen            offsetof(oBCCHeader, sourceWhen)
#define k_rslibWhen             offsetof(oBCCHeader, rslibWhen)
#define k_libRSWhen             offsetof(oBCCHeader, libRSWhen)
#define k_libbccWhen            offsetof(oBCCHeader, libbccWhen)
#define k_cachedCodeDataAddr    offsetof(oBCCHeader, cachedCodeDataAddr)
#define k_rootAddr              offsetof(oBCCHeader, rootAddr)
#define k_initAddr              offsetof(oBCCHeader, initAddr)
#define k_relocOffset           offsetof(oBCCHeader, relocOffset)
#define k_relocCount            offsetof(oBCCHeader, relocCount)
#define k_exportVarsOffset      offsetof(oBCCHeader, exportVarsOffset)
#define k_exportVarsCount       offsetof(oBCCHeader, exportVarsCount)
#define k_exportFuncsOffset     offsetof(oBCCHeader, exportFuncsOffset)
#define k_exportFuncsCount      offsetof(oBCCHeader, exportFuncsCount)
#define k_exportPragmasOffset   offsetof(oBCCHeader, exportPragmasOffset)
#define k_exportPragmasCount    offsetof(oBCCHeader, exportPragmasCount)
#define k_codeOffset            offsetof(oBCCHeader, codeOffset)
#define k_codeSize              offsetof(oBCCHeader, codeSize)
#define k_dataOffset            offsetof(oBCCHeader, dataOffset)
#define k_dataSize              offsetof(oBCCHeader, dataSize)
#define k_checksum              offsetof(oBCCHeader, checksum)

/* oBCC file magic number */
#define OBCC_MAGIC       "bcc\n"
/* version, encoded in 4 bytes of ASCII */
#define OBCC_MAGIC_VERS  "001\0"

#define TEMP_FAILURE_RETRY1(exp) ({         \
    typeof (exp) _rc;                      \
    do {                                   \
        _rc = (exp);                       \
    } while (_rc == -1 && errno == EINTR); \
    _rc; })

static int sysWriteFully(int fd, const void* buf, size_t count, const char* logMsg)
{
    while (count != 0) {
        ssize_t actual = TEMP_FAILURE_RETRY1(write(fd, buf, count));
        if (actual < 0) {
            int err = errno;
            LOGE("%s: write failed: %s\n", logMsg, strerror(err));
            return err;
        } else if (actual != (ssize_t) count) {
            LOGD("%s: partial write (will retry): (%d of %zd)\n",
                logMsg, (int) actual, count);
            buf = (const void*) (((const uint8_t*) buf) + actual);
        }
        count -= actual;
    }

    return 0;
}

//
// Compilation class that suits Android's needs.
// (Support: no argument passed, ...)
//
namespace bcc {

class Compiler {
  // This part is designed to be orthogonal to those exported bcc*() functions
  // implementation and internal struct BCCscript.

  //////////////////////////////////////////////////////////////////////////////
  // The variable section below (e.g., Triple, CodeGenOptLevel)
  // is initialized in GlobalInitialization()
  //
  static bool GlobalInitialized;
  static char *BccCodeAddr;

  // If given, this will be the name of the target triple to compile for.
  // If not given, the initial values defined in this file will be used.
  static std::string Triple;

  static llvm::CodeGenOpt::Level CodeGenOptLevel;

  // End of section of GlobalInitializing variables
  //////////////////////////////////////////////////////////////////////////////

  // If given, the name of the target CPU to generate code for.
  static std::string CPU;

  // The list of target specific features to enable or disable -- this should
  // be a list of strings starting with '+' (enable) or '-' (disable).
  static std::vector<std::string> Features;

  struct Runtime {
      const char *mName;
      void *mPtr;
  };
  static struct Runtime Runtimes[];

  static void GlobalInitialization() {
    if (GlobalInitialized)
      return;

    // if (!llvm::llvm_is_multithreaded())
    //   llvm::llvm_start_multithreaded();

    // Set Triple, CPU and Features here
    Triple = TARGET_TRIPLE_STRING;

    // TODO(sliao): NEON for JIT
    // Features.push_back("+neon");
    // Features.push_back("+vmlx");
    // Features.push_back("+neonfp");
    Features.push_back("+vfp3");
    Features.push_back("+d16");

#if defined(DEFAULT_ARM_CODEGEN) || defined(PROVIDE_ARM_CODEGEN)
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
#if defined(USE_DISASSEMBLER)
    LLVMInitializeARMDisassembler();
    LLVMInitializeARMAsmPrinter();
#endif
#endif

#if defined(DEFAULT_X86_CODEGEN) || defined(PROVIDE_X86_CODEGEN)
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
#if defined(USE_DISASSEMBLER)
    LLVMInitializeX86Disassembler();
    LLVMInitializeX86AsmPrinter();
#endif
#endif

#if defined(DEFAULT_X64_CODEGEN) || defined(PROVIDE_X64_CODEGEN)
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
#if defined(USE_DISASSEMBLER)
    LLVMInitializeX86Disassembler();
    LLVMInitializeX86AsmPrinter();
#endif
#endif

     // -O0: llvm::CodeGenOpt::None
     // -O1: llvm::CodeGenOpt::Less
     // -O2: llvm::CodeGenOpt::Default
     // -O3: llvm::CodeGenOpt::Aggressive
    CodeGenOptLevel = llvm::CodeGenOpt::None;

    // Below are the global settings to LLVM

    // Disable frame pointer elimination optimization
    llvm::NoFramePointerElim = false;

     // Use hardfloat ABI
     //
     // TODO(all): Need to detect the CPU capability and decide whether to use
     // softfp. To use softfp, change following 2 lines to
     //
     // llvm::FloatABIType = llvm::FloatABI::Soft;
     // llvm::UseSoftFloat = true;
     //
    llvm::FloatABIType = llvm::FloatABI::Soft;
    llvm::UseSoftFloat = false;

    // BCC needs all unknown symbols resolved at JIT/compilation time.
    // So we don't need any dynamic relocation model.
    llvm::TargetMachine::setRelocationModel(llvm::Reloc::Static);

#if defined(DEFAULT_X64_CODEGEN)
    // Data address in X86_64 architecture may reside in a far-away place
    llvm::TargetMachine::setCodeModel(llvm::CodeModel::Medium);
#else
    // This is set for the linker (specify how large of the virtual addresses
    // we can access for all unknown symbols.)
    llvm::TargetMachine::setCodeModel(llvm::CodeModel::Small);
#endif

    // Register the scheduler
    llvm::RegisterScheduler::setDefault(llvm::createDefaultScheduler);

    // Register allocation policy:
    //  createFastRegisterAllocator: fast but bad quality
    //  createLinearScanRegisterAllocator: not so fast but good quality
    llvm::RegisterRegAlloc::setDefault
        ((CodeGenOptLevel == llvm::CodeGenOpt::None) ?
         llvm::createFastRegisterAllocator :
         llvm::createLinearScanRegisterAllocator);

    GlobalInitialized = true;
    return;
  }

  static void LLVMErrorHandler(void *UserData, const std::string &Message) {
    std::string *Error = static_cast<std::string*>(UserData);
    Error->assign(Message);
    LOGE("%s", Message.c_str());
    exit(1);
  }

  static const llvm::StringRef PragmaMetadataName;
  static const llvm::StringRef ExportVarMetadataName;
  static const llvm::StringRef ExportFuncMetadataName;

 private:
  std::string mError;

  inline bool hasError() const {
    return !mError.empty();
  }
  inline void setError(const char *Error) {
    mError.assign(Error);  // Copying
    return;
  }
  inline void setError(const std::string &Error) {
    mError = Error;
    return;
  }

  bool mUseCache;       // Set by readBC()
  bool mCacheNew;         // Set by readBC()
  int mCacheFd;           // Set by readBC()
  char *mCacheMapAddr;    // Set by loadCacheFile() if mCacheNew is false
  oBCCHeader *mCacheHdr;  // Set by loadCacheFile()
  size_t mCacheSize;      // Set by loadCacheFile()
  ptrdiff_t mCacheDiff;   // Set by loadCacheFile()
  char *mCodeDataAddr;    // Set by CodeMemoryManager if mCacheNew is true.
                          // Used by genCacheFile() for dumping

  typedef std::list< std::pair<std::string, std::string> > PragmaList;
  PragmaList mPragmas;

  typedef std::list<void*> ExportVarList;
  ExportVarList mExportVars;

  typedef std::list<void*> ExportFuncList;
  ExportFuncList mExportFuncs;

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
  static const unsigned int MaxCodeSize = 128 * 1024;
  // 1 KiB for global offset table (GOT)
  static const unsigned int MaxGOTSize = 1 * 1024;
  // 128 KiB for global variable
  static const unsigned int MaxGlobalVarSize = 128 * 1024;

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

    typedef std::map<const llvm::Function*, pair<void* /* start address */,
                                                 void* /* end address */>
                     > FunctionMapTy;
    FunctionMapTy mFunctionMap;

    inline intptr_t getFreeCodeMemSize() const {
      return mCurSGMemIdx - mCurFuncMemIdx;
    }

    uint8_t *allocateSGMemory(uintptr_t Size,
                              unsigned Alignment = 1 /* no alignment */) {
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

    inline uintptr_t getFreeGVMemSize() const {
      return MaxGlobalVarSize - mCurGVMemIdx;
    }
    inline uint8_t *getGVMemBase() const {
      return reinterpret_cast<uint8_t*>(mpGVMem);
    }

   public:
    CodeMemoryManager() : mpCodeMem(NULL), mpGVMem(NULL), mpGOTBase(NULL) {
      reset();
      std::string ErrMsg;

      if (Compiler::BccCodeAddr) {  // Try to use BccCodeAddr
        mpCodeMem = mmap(reinterpret_cast<void*>(Compiler::BccCodeAddr),
                         MaxCodeSize + MaxGlobalVarSize,
                         PROT_READ | PROT_EXEC | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_FIXED,
                         -1, 0);

        if (mpCodeMem == MAP_FAILED) {
          LOGE("Mmap mpCodeMem at %p failed with reason: %s.\n",
               reinterpret_cast<void *>(BCC_CODE_ADDR), strerror(errno));
          LOGE("Retry to mmap mpCodeMem at arbitary address\n");
          // TODO(sliao): Future: Should we retry at
          // BccCodeAddr + MaxCodeSize + MaxGlobalVarSize?

        } else {
          Compiler::BccCodeAddr += MaxCodeSize + MaxGlobalVarSize;
        }
      }

      if (!Compiler::BccCodeAddr || mpCodeMem == MAP_FAILED) {
        // If no BccCodeAddr specified, or we can't allocate
        // mpCodeMem in previous mmap, then allocate them in arbitary
        // location and rely on relocation.
        // Note: Will incur time overhead in relocating when reloading from disk

        mpCodeMem = mmap(NULL,
                         MaxCodeSize + MaxGlobalVarSize,
                         PROT_READ | PROT_EXEC | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON,
                         -1, 0);

        if (mpCodeMem == MAP_FAILED) {
          LOGE("Unable to mmap mpCodeMem with reason: %s.\n", strerror(errno));
          llvm::report_fatal_error("Failed to allocate memory for emitting "
                                   "codes\n" + ErrMsg);
        }
      }

      // Set global variable pool
      mpGVMem = (void *) ((char *)mpCodeMem + MaxCodeSize);

      return;
    }

    inline uint8_t *getCodeMemBase() const {
      return reinterpret_cast<uint8_t*>(mpCodeMem);
    }

    // setMemoryWritable - When code generation is in progress, the code pages
    //                     may need permissions changed.
    void setMemoryWritable() {
      ::mprotect(mpCodeMem, MaxCodeSize, PROT_READ | PROT_WRITE | PROT_EXEC);
      return;
    }

     // When code generation is done and we're ready to start execution, the
     // code pages may need permissions changed.
    void setMemoryExecutable() {
      ::mprotect(mpCodeMem, MaxCodeSize, PROT_READ | PROT_EXEC);
      return;
    }

    // Setting this flag to true makes the memory manager garbage values over
    // freed memory.  This is useful for testing and debugging, and is to be
    // turned on by default in debug mode.
    void setPoisonMemory(bool poison) {
      // no effect
      return;
    }

    // Global Offset Table Management

    // If the current table requires a Global Offset Table, this method is
    // invoked to allocate it.  This method is required to set HasGOT to true.
    void AllocateGOT() {
      assert(mpGOTBase != NULL && "Cannot allocate the GOT multiple times");
      mpGOTBase = allocateSGMemory(MaxGOTSize);
      HasGOT = true;
      return;
    }

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
    uint8_t *startFunctionBody(const llvm::Function *F, uintptr_t &ActualSize) {
      intptr_t FreeMemSize = getFreeCodeMemSize();
      if ((FreeMemSize < 0) ||
          (static_cast<uintptr_t>(FreeMemSize) < ActualSize))
        // The code size excesses our limit
        return NULL;

      ActualSize = getFreeCodeMemSize();
      return (getCodeMemBase() + mCurFuncMemIdx);
    }

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
    uint8_t *allocateSpace(intptr_t Size, unsigned Alignment) {
      if (getFreeCodeMemSize() < Size)
        // The code size excesses our limit
        return NULL;

      if (Alignment == 0)
        Alignment = 1;

      uint8_t *result = getCodeMemBase() + mCurFuncMemIdx;
      result = (uint8_t*) (((intptr_t) result + Alignment - 1) &
                           ~(intptr_t) (Alignment - 1));

      mCurFuncMemIdx = (result + Size) - getCodeMemBase();

      return result;
    }

    // Allocate memory for a global variable.
    uint8_t *allocateGlobal(uintptr_t Size, unsigned Alignment) {
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
    void deallocateFunctionBody(void *Body) {
      // linear search
      uint8_t *FunctionStart = NULL, *FunctionEnd = NULL;
      for (FunctionMapTy::iterator I = mFunctionMap.begin(),
              E = mFunctionMap.end();
           I != E;
           I++)
        if (I->second.first == Body) {
          FunctionStart = reinterpret_cast<uint8_t*>(I->second.first);
          FunctionEnd = reinterpret_cast<uint8_t*>(I->second.second);
          break;
        }

      assert((FunctionStart == NULL) && "Memory is never allocated!");

      // free the memory
      intptr_t SizeNeedMove = (getCodeMemBase() + mCurFuncMemIdx) - FunctionEnd;

      assert(SizeNeedMove >= 0 &&
             "Internal error: CodeMemoryManager::mCurFuncMemIdx may not"
             " be correctly calculated!");

      if (SizeNeedMove > 0)
        // there's data behind deallocating function
        ::memmove(FunctionStart, FunctionEnd, SizeNeedMove);
      mCurFuncMemIdx -= (FunctionEnd - FunctionStart);

      return;
    }

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
      return;
    }

    // Free the specified exception table's memory. The argument must be the
    // return value from a call to startExceptionTable() that hasn't been
    // deallocated yet. This is never called when the JIT is currently emitting
    // an exception table.
    void deallocateExceptionTable(void *ET) {
      assert(false && "Exception is not allowed in our language specification");
      return;
    }

    // Below are the methods we create
    void reset() {
      mpGOTBase = NULL;
      HasGOT = false;

      mCurFuncMemIdx = 0;
      mCurSGMemIdx = MaxCodeSize - 1;
      mCurGVMemIdx = 0;

      mFunctionMap.clear();

      return;
    }

    ~CodeMemoryManager() {
      if (mpCodeMem != NULL && mpCodeMem != MAP_FAILED)
        munmap(mpCodeMem, MaxCodeSize + MaxGlobalVarSize);
      return;
    }
  };
  // End of class CodeMemoryManager
  //////////////////////////////////////////////////////////////////////////////

  // The memory manager for code emitter
  llvm::OwningPtr<CodeMemoryManager> mCodeMemMgr;
  CodeMemoryManager *createCodeMemoryManager() {
    mCodeMemMgr.reset(new CodeMemoryManager());
    return mCodeMemMgr.get();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Code emitter
  class CodeEmitter : public llvm::JITCodeEmitter {
   public:
    typedef llvm::DenseMap<const llvm::GlobalValue*, void*> GlobalAddressMapTy;
    typedef GlobalAddressMapTy::const_iterator global_addresses_const_iterator;

    GlobalAddressMapTy mGlobalAddressMap;

   private:
    CodeMemoryManager *mpMemMgr;

    // The JITInfo for the target we are compiling to
    const llvm::Target *mpTarget;

    llvm::TargetJITInfo *mpTJI;

    const llvm::TargetData *mpTD;

    class EmittedFunctionCode {
     public:
      // Beginning of the function's allocation.
      void *FunctionBody;

      // The address the function's code actually starts at.
      void *Code;

      // The size of the function code
      int Size;

      EmittedFunctionCode() : FunctionBody(NULL), Code(NULL) { return; }
    };
    EmittedFunctionCode *mpCurEmitFunction;

    typedef std::map<const std::string,
                     EmittedFunctionCode*> EmittedFunctionsMapTy;
    EmittedFunctionsMapTy mEmittedFunctions;

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

    std::vector<oBCCRelocEntry> mCachingRelocations;

    // This vector is a mapping from Label ID's to their address.
    llvm::DenseMap<llvm::MCSymbol*, uintptr_t> mLabelLocations;

    // Machine module info for exception informations
    llvm::MachineModuleInfo *mpMMI;

    // Replace an existing mapping for GV with a new address. This updates both
    // maps as required. If Addr is null, the entry for the global is removed
    // from the mappings.
    void *UpdateGlobalMapping(const llvm::GlobalValue *GV, void *Addr) {
      if (Addr == NULL) {
        // Removing mapping
        GlobalAddressMapTy::iterator I = mGlobalAddressMap.find(GV);
        void *OldVal;

        if (I == mGlobalAddressMap.end()) {
          OldVal = NULL;
        } else {
          OldVal = I->second;
          mGlobalAddressMap.erase(I);
        }

        return OldVal;
      }

      void *&CurVal = mGlobalAddressMap[GV];
      void *OldVal = CurVal;

      CurVal = Addr;

      return OldVal;
    }

    // Tell the execution engine that the specified global is at the specified
    // location. This is used internally as functions are JIT'd and as global
    // variables are laid out in memory.
    void AddGlobalMapping(const llvm::GlobalValue *GV, void *Addr) {
      void *&CurVal = mGlobalAddressMap[GV];
      assert((CurVal == 0 || Addr == 0) &&
             "GlobalMapping already established!");
      CurVal = Addr;
      return;
    }

    // This returns the address of the specified global value if it is has
    // already been codegen'd, otherwise it returns null.
    void *GetPointerToGlobalIfAvailable(const llvm::GlobalValue *GV) {
      GlobalAddressMapTy::iterator I = mGlobalAddressMap.find(GV);
      return ((I != mGlobalAddressMap.end()) ? I->second : NULL);
    }

    unsigned int GetConstantPoolSizeInBytes(llvm::MachineConstantPool *MCP) {
      const std::vector<llvm::MachineConstantPoolEntry> &Constants =
          MCP->getConstants();

      if (Constants.empty())
        return 0;

      unsigned int Size = 0;
      for (int i = 0, e = Constants.size(); i != e; i++) {
        llvm::MachineConstantPoolEntry CPE = Constants[i];
        unsigned int AlignMask = CPE.getAlignment() - 1;
        Size = (Size + AlignMask) & ~AlignMask;
        const llvm::Type *Ty = CPE.getType();
        Size += mpTD->getTypeAllocSize(Ty);
      }

      return Size;
    }

    // This function converts a Constant* into a GenericValue. The interesting
    // part is if C is a ConstantExpr.
    void GetConstantValue(const llvm::Constant *C, llvm::GenericValue &Result) {
      if (C->getValueID() == llvm::Value::UndefValueVal)
        return;
      else if (C->getValueID() == llvm::Value::ConstantExprVal) {
        const llvm::ConstantExpr *CE = (llvm::ConstantExpr*) C;
        const llvm::Constant *Op0 = CE->getOperand(0);

        switch (CE->getOpcode()) {
          case llvm::Instruction::GetElementPtr: {
            // Compute the index
            llvm::SmallVector<llvm::Value*, 8> Indices(CE->op_begin() + 1,
                                                       CE->op_end());
            uint64_t Offset = mpTD->getIndexedOffset(Op0->getType(),
                                                     &Indices[0],
                                                     Indices.size());

            GetConstantValue(Op0, Result);
            Result.PointerVal =
                static_cast<uint8_t*>(Result.PointerVal) + Offset;

            return;
          }
          case llvm::Instruction::Trunc: {
            uint32_t BitWidth =
                llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

            GetConstantValue(Op0, Result);
            Result.IntVal = Result.IntVal.trunc(BitWidth);

            return;
          }
          case llvm::Instruction::ZExt: {
            uint32_t BitWidth =
                llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

            GetConstantValue(Op0, Result);
            Result.IntVal = Result.IntVal.zext(BitWidth);

            return;
          }
          case llvm::Instruction::SExt: {
            uint32_t BitWidth =
                llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

            GetConstantValue(Op0, Result);
            Result.IntVal = Result.IntVal.sext(BitWidth);

            return;
          }
          case llvm::Instruction::FPTrunc: {
            // TODO(all): fixme: long double
            GetConstantValue(Op0, Result);
            Result.FloatVal = static_cast<float>(Result.DoubleVal);
            return;
          }
          case llvm::Instruction::FPExt: {
            // TODO(all): fixme: long double
            GetConstantValue(Op0, Result);
            Result.DoubleVal = static_cast<double>(Result.FloatVal);
            return;
          }
          case llvm::Instruction::UIToFP: {
            GetConstantValue(Op0, Result);
            if (CE->getType()->isFloatTy())
              Result.FloatVal =
                  static_cast<float>(Result.IntVal.roundToDouble());
            else if (CE->getType()->isDoubleTy())
              Result.DoubleVal = Result.IntVal.roundToDouble();
            else if (CE->getType()->isX86_FP80Ty()) {
              const uint64_t zero[] = { 0, 0 };
              llvm::APFloat apf(llvm::APInt(80, 2, zero));
              apf.convertFromAPInt(Result.IntVal,
                                   false,
                                   llvm::APFloat::rmNearestTiesToEven);
              Result.IntVal = apf.bitcastToAPInt();
            }
            return;
          }
          case llvm::Instruction::SIToFP: {
            GetConstantValue(Op0, Result);
            if (CE->getType()->isFloatTy())
              Result.FloatVal =
                  static_cast<float>(Result.IntVal.signedRoundToDouble());
            else if (CE->getType()->isDoubleTy())
              Result.DoubleVal = Result.IntVal.signedRoundToDouble();
            else if (CE->getType()->isX86_FP80Ty()) {
              const uint64_t zero[] = { 0, 0 };
              llvm::APFloat apf = llvm::APFloat(llvm::APInt(80, 2, zero));
              apf.convertFromAPInt(Result.IntVal,
                                   true,
                                   llvm::APFloat::rmNearestTiesToEven);
              Result.IntVal = apf.bitcastToAPInt();
            }
            return;
          }
          // double->APInt conversion handles sign
          case llvm::Instruction::FPToUI:
          case llvm::Instruction::FPToSI: {
            uint32_t BitWidth =
                llvm::cast<llvm::IntegerType>(CE->getType())->getBitWidth();

            GetConstantValue(Op0, Result);
            if (Op0->getType()->isFloatTy())
              Result.IntVal =
               llvm::APIntOps::RoundFloatToAPInt(Result.FloatVal, BitWidth);
            else if (Op0->getType()->isDoubleTy())
              Result.IntVal =
                  llvm::APIntOps::RoundDoubleToAPInt(Result.DoubleVal,
                                                     BitWidth);
            else if (Op0->getType()->isX86_FP80Ty()) {
              llvm::APFloat apf = llvm::APFloat(Result.IntVal);
              uint64_t V;
              bool Ignored;
              apf.convertToInteger(&V,
                                   BitWidth,
                                   CE->getOpcode() == llvm::Instruction::FPToSI,
                                   llvm::APFloat::rmTowardZero,
                                   &Ignored);
              Result.IntVal = V;  // endian?
            }
            return;
          }
          case llvm::Instruction::PtrToInt: {
            uint32_t PtrWidth = mpTD->getPointerSizeInBits();

            GetConstantValue(Op0, Result);
            Result.IntVal = llvm::APInt(PtrWidth, uintptr_t
                                        (Result.PointerVal));

            return;
          }
          case llvm::Instruction::IntToPtr: {
            uint32_t PtrWidth = mpTD->getPointerSizeInBits();

            GetConstantValue(Op0, Result);
            if (PtrWidth != Result.IntVal.getBitWidth())
              Result.IntVal = Result.IntVal.zextOrTrunc(PtrWidth);
            assert(Result.IntVal.getBitWidth() <= 64 && "Bad pointer width");

            Result.PointerVal =
                llvm::PointerTy(
                    static_cast<uintptr_t>(Result.IntVal.getZExtValue()));

            return;
          }
          case llvm::Instruction::BitCast: {
            GetConstantValue(Op0, Result);
            const llvm::Type *DestTy = CE->getType();

            switch (Op0->getType()->getTypeID()) {
              case llvm::Type::IntegerTyID: {
                assert(DestTy->isFloatingPointTy() && "invalid bitcast");
                if (DestTy->isFloatTy())
                  Result.FloatVal = Result.IntVal.bitsToFloat();
                else if (DestTy->isDoubleTy())
                  Result.DoubleVal = Result.IntVal.bitsToDouble();
                break;
              }
              case llvm::Type::FloatTyID: {
                assert(DestTy->isIntegerTy(32) && "Invalid bitcast");
                Result.IntVal.floatToBits(Result.FloatVal);
                break;
              }
              case llvm::Type::DoubleTyID: {
                assert(DestTy->isIntegerTy(64) && "Invalid bitcast");
                Result.IntVal.doubleToBits(Result.DoubleVal);
                break;
              }
              case llvm::Type::PointerTyID: {
                assert(DestTy->isPointerTy() && "Invalid bitcast");
                break;  // getConstantValue(Op0) above already converted it
              }
              default: {
                llvm_unreachable("Invalid bitcast operand");
              }
            }
            return;
          }
          case llvm::Instruction::Add:
          case llvm::Instruction::FAdd:
          case llvm::Instruction::Sub:
          case llvm::Instruction::FSub:
          case llvm::Instruction::Mul:
          case llvm::Instruction::FMul:
          case llvm::Instruction::UDiv:
          case llvm::Instruction::SDiv:
          case llvm::Instruction::URem:
          case llvm::Instruction::SRem:
          case llvm::Instruction::And:
          case llvm::Instruction::Or:
          case llvm::Instruction::Xor: {
            llvm::GenericValue LHS, RHS;
            GetConstantValue(Op0, LHS);
            GetConstantValue(CE->getOperand(1), RHS);

            switch (Op0->getType()->getTypeID()) {
              case llvm::Type::IntegerTyID: {
                switch (CE->getOpcode()) {
                  case llvm::Instruction::Add: {
                    Result.IntVal = LHS.IntVal + RHS.IntVal;
                    break;
                  }
                  case llvm::Instruction::Sub: {
                    Result.IntVal = LHS.IntVal - RHS.IntVal;
                    break;
                  }
                  case llvm::Instruction::Mul: {
                    Result.IntVal = LHS.IntVal * RHS.IntVal;
                    break;
                  }
                  case llvm::Instruction::UDiv: {
                    Result.IntVal = LHS.IntVal.udiv(RHS.IntVal);
                    break;
                  }
                  case llvm::Instruction::SDiv: {
                    Result.IntVal = LHS.IntVal.sdiv(RHS.IntVal);
                    break;
                  }
                  case llvm::Instruction::URem: {
                    Result.IntVal = LHS.IntVal.urem(RHS.IntVal);
                    break;
                  }
                  case llvm::Instruction::SRem: {
                    Result.IntVal = LHS.IntVal.srem(RHS.IntVal);
                    break;
                  }
                  case llvm::Instruction::And: {
                    Result.IntVal = LHS.IntVal & RHS.IntVal;
                    break;
                  }
                  case llvm::Instruction::Or: {
                    Result.IntVal = LHS.IntVal | RHS.IntVal;
                    break;
                  }
                  case llvm::Instruction::Xor: {
                    Result.IntVal = LHS.IntVal ^ RHS.IntVal;
                    break;
                  }
                  default: {
                    llvm_unreachable("Invalid integer opcode");
                  }
                }
                break;
              }
              case llvm::Type::FloatTyID: {
                switch (CE->getOpcode()) {
                  case llvm::Instruction::FAdd: {
                    Result.FloatVal = LHS.FloatVal + RHS.FloatVal;
                    break;
                  }
                  case llvm::Instruction::FSub: {
                    Result.FloatVal = LHS.FloatVal - RHS.FloatVal;
                    break;
                  }
                  case llvm::Instruction::FMul: {
                    Result.FloatVal = LHS.FloatVal * RHS.FloatVal;
                    break;
                  }
                  case llvm::Instruction::FDiv: {
                    Result.FloatVal = LHS.FloatVal / RHS.FloatVal;
                    break;
                  }
                  case llvm::Instruction::FRem: {
                    Result.FloatVal = ::fmodf(LHS.FloatVal, RHS.FloatVal);
                    break;
                  }
                  default: {
                    llvm_unreachable("Invalid float opcode");
                  }
                }
                break;
              }
              case llvm::Type::DoubleTyID: {
                switch (CE->getOpcode()) {
                  case llvm::Instruction::FAdd: {
                    Result.DoubleVal = LHS.DoubleVal + RHS.DoubleVal;
                    break;
                  }
                  case llvm::Instruction::FSub: {
                    Result.DoubleVal = LHS.DoubleVal - RHS.DoubleVal;
                    break;
                  }
                  case llvm::Instruction::FMul: {
                    Result.DoubleVal = LHS.DoubleVal * RHS.DoubleVal;
                    break;
                  }
                  case llvm::Instruction::FDiv: {
                    Result.DoubleVal = LHS.DoubleVal / RHS.DoubleVal;
                    break;
                  }
                  case llvm::Instruction::FRem: {
                    Result.DoubleVal = ::fmod(LHS.DoubleVal, RHS.DoubleVal);
                    break;
                  }
                  default: {
                    llvm_unreachable("Invalid double opcode");
                  }
                }
                break;
              }
              case llvm::Type::X86_FP80TyID:
              case llvm::Type::PPC_FP128TyID:
              case llvm::Type::FP128TyID: {
                llvm::APFloat apfLHS = llvm::APFloat(LHS.IntVal);
                switch (CE->getOpcode()) {
                  case llvm::Instruction::FAdd: {
                    apfLHS.add(llvm::APFloat(RHS.IntVal),
                               llvm::APFloat::rmNearestTiesToEven);
                    break;
                  }
                  case llvm::Instruction::FSub: {
                    apfLHS.subtract(llvm::APFloat(RHS.IntVal),
                                    llvm::APFloat::rmNearestTiesToEven);
                    break;
                  }
                  case llvm::Instruction::FMul: {
                    apfLHS.multiply(llvm::APFloat(RHS.IntVal),
                                    llvm::APFloat::rmNearestTiesToEven);
                    break;
                  }
                  case llvm::Instruction::FDiv: {
                    apfLHS.divide(llvm::APFloat(RHS.IntVal),
                                  llvm::APFloat::rmNearestTiesToEven);
                    break;
                  }
                  case llvm::Instruction::FRem: {
                    apfLHS.mod(llvm::APFloat(RHS.IntVal),
                               llvm::APFloat::rmNearestTiesToEven);
                    break;
                  }
                  default: {
                    llvm_unreachable("Invalid long double opcode");
                  }
                }
                Result.IntVal = apfLHS.bitcastToAPInt();
                break;
              }
              default: {
                llvm_unreachable("Bad add type!");
              }
            }  // End switch (Op0->getType()->getTypeID())
            return;
          }
          default: {
            break;
          }
        }   // End switch (CE->getOpcode())

        std::string msg;
        llvm::raw_string_ostream Msg(msg);
        Msg << "ConstantExpr not handled: " << *CE;
        llvm::report_fatal_error(Msg.str());
      }  // C->getValueID() == llvm::Value::ConstantExprVal

      switch (C->getType()->getTypeID()) {
        case llvm::Type::FloatTyID: {
          Result.FloatVal =
              llvm::cast<llvm::ConstantFP>(C)->getValueAPF().convertToFloat();
          break;
        }
        case llvm::Type::DoubleTyID: {
          Result.DoubleVal =
              llvm::cast<llvm::ConstantFP>(C)->getValueAPF().convertToDouble();
          break;
        }
        case llvm::Type::X86_FP80TyID:
        case llvm::Type::FP128TyID:
        case llvm::Type::PPC_FP128TyID: {
          Result.IntVal =
              llvm::cast<llvm::ConstantFP>(C)->getValueAPF().bitcastToAPInt();
          break;
        }
        case llvm::Type::IntegerTyID: {
          Result.IntVal =
              llvm::cast<llvm::ConstantInt>(C)->getValue();
          break;
        }
        case llvm::Type::PointerTyID: {
          switch (C->getValueID()) {
            case llvm::Value::ConstantPointerNullVal: {
              Result.PointerVal = NULL;
              break;
            }
            case llvm::Value::FunctionVal: {
              const llvm::Function *F = static_cast<const llvm::Function*>(C);
              Result.PointerVal =
                  GetPointerToFunctionOrStub(const_cast<llvm::Function*>(F));
              break;
            }
            case llvm::Value::GlobalVariableVal: {
              const llvm::GlobalVariable *GV =
                  static_cast<const llvm::GlobalVariable*>(C);
              Result.PointerVal =
                GetOrEmitGlobalVariable(const_cast<llvm::GlobalVariable*>(GV));
              break;
            }
            case llvm::Value::BlockAddressVal: {
              assert(false && "JIT does not support address-of-label yet!");
            }
            default: {
              llvm_unreachable("Unknown constant pointer type!");
            }
          }
          break;
        }
        default: {
          std::string msg;
          llvm::raw_string_ostream Msg(msg);
          Msg << "ERROR: Constant unimplemented for type: " << *C->getType();
          llvm::report_fatal_error(Msg.str());
          break;
        }
      }
      return;
    }

    // Stores the data in @Val of type @Ty at address @Addr.
    void StoreValueToMemory(const llvm::GenericValue &Val, void *Addr,
                            const llvm::Type *Ty) {
      const unsigned int StoreBytes = mpTD->getTypeStoreSize(Ty);

      switch (Ty->getTypeID()) {
        case llvm::Type::IntegerTyID: {
          const llvm::APInt &IntVal = Val.IntVal;
          assert(((IntVal.getBitWidth() + 7) / 8 >= StoreBytes) &&
              "Integer too small!");

          const uint8_t *Src =
            reinterpret_cast<const uint8_t*>(IntVal.getRawData());

          if (llvm::sys::isLittleEndianHost()) {
            // Little-endian host - the source is ordered from LSB to MSB.
            // Order the destination from LSB to MSB: Do a straight copy.
            memcpy(Addr, Src, StoreBytes);
          } else {
            // Big-endian host - the source is an array of 64 bit words
            // ordered from LSW to MSW.
            //
            // Each word is ordered from MSB to LSB.
            //
            // Order the destination from MSB to LSB:
            //  Reverse the word order, but not the bytes in a word.
            unsigned int i = StoreBytes;
            while (i > sizeof(uint64_t)) {
              i -= sizeof(uint64_t);
              ::memcpy(reinterpret_cast<uint8_t*>(Addr) + i,
                  Src,
                  sizeof(uint64_t));
              Src += sizeof(uint64_t);
            }
            ::memcpy(Addr, Src + sizeof(uint64_t) - i, i);
          }
          break;
        }
        case llvm::Type::FloatTyID: {
          *reinterpret_cast<float*>(Addr) = Val.FloatVal;
          break;
        }
        case llvm::Type::DoubleTyID: {
          *reinterpret_cast<double*>(Addr) = Val.DoubleVal;
          break;
        }
        case llvm::Type::X86_FP80TyID: {
          memcpy(Addr, Val.IntVal.getRawData(), 10);
          break;
        }
        case llvm::Type::PointerTyID: {
          // Ensure 64 bit target pointers are fully initialized on 32 bit
          // hosts.
          if (StoreBytes != sizeof(llvm::PointerTy))
            memset(Addr, 0, StoreBytes);
          *((llvm::PointerTy*) Addr) = Val.PointerVal;
          break;
        }
        default: {
          break;
        }
      }

      if (llvm::sys::isLittleEndianHost() != mpTD->isLittleEndian())
        std::reverse(reinterpret_cast<uint8_t*>(Addr),
            reinterpret_cast<uint8_t*>(Addr) + StoreBytes);

      return;
    }

    // Recursive function to apply a @Constant value into the specified memory
    // location @Addr.
    void InitializeConstantToMemory(const llvm::Constant *C, void *Addr) {
      switch (C->getValueID()) {
        case llvm::Value::UndefValueVal: {
          // Nothing to do
          break;
        }
        case llvm::Value::ConstantVectorVal: {
          // dynamic cast may hurt performance
          const llvm::ConstantVector *CP = (llvm::ConstantVector*) C;

          unsigned int ElementSize = mpTD->getTypeAllocSize
            (CP->getType()->getElementType());

          for (int i = 0, e = CP->getNumOperands(); i != e;i++)
            InitializeConstantToMemory(
                CP->getOperand(i),
                reinterpret_cast<uint8_t*>(Addr) + i * ElementSize);
          break;
        }
        case llvm::Value::ConstantAggregateZeroVal: {
          memset(Addr, 0, (size_t) mpTD->getTypeAllocSize(C->getType()));
          break;
        }
        case llvm::Value::ConstantArrayVal: {
          const llvm::ConstantArray *CPA = (llvm::ConstantArray*) C;
          unsigned int ElementSize = mpTD->getTypeAllocSize
            (CPA->getType()->getElementType());

          for (int i = 0, e = CPA->getNumOperands(); i != e; i++)
            InitializeConstantToMemory(
                CPA->getOperand(i),
                reinterpret_cast<uint8_t*>(Addr) + i * ElementSize);
          break;
        }
        case llvm::Value::ConstantStructVal: {
          const llvm::ConstantStruct *CPS =
              static_cast<const llvm::ConstantStruct*>(C);
          const llvm::StructLayout *SL = mpTD->getStructLayout
            (llvm::cast<llvm::StructType>(CPS->getType()));

          for (int i = 0, e = CPS->getNumOperands(); i != e; i++)
            InitializeConstantToMemory(
                CPS->getOperand(i),
                reinterpret_cast<uint8_t*>(Addr) + SL->getElementOffset(i));
          break;
        }
        default: {
          if (C->getType()->isFirstClassType()) {
            llvm::GenericValue Val;
            GetConstantValue(C, Val);
            StoreValueToMemory(Val, Addr, C->getType());
          } else {
            llvm_unreachable("Unknown constant type to initialize memory "
                             "with!");
          }
          break;
        }
      }
      return;
    }

    void emitConstantPool(llvm::MachineConstantPool *MCP) {
      if (mpTJI->hasCustomConstantPool())
        return;

      // Constant pool address resolution is handled by the target itself in ARM
      // (TargetJITInfo::hasCustomConstantPool() returns true).
#if !defined(PROVIDE_ARM_CODEGEN)
      const std::vector<llvm::MachineConstantPoolEntry> &Constants =
        MCP->getConstants();

      if (Constants.empty())
        return;

      unsigned Size = GetConstantPoolSizeInBytes(MCP);
      unsigned Align = MCP->getConstantPoolAlignment();

      mpConstantPoolBase = allocateSpace(Size, Align);
      mpConstantPool = MCP;

      if (mpConstantPoolBase == NULL)
        return;  // out of memory

      unsigned Offset = 0;
      for (int i = 0, e = Constants.size(); i != e; i++) {
        llvm::MachineConstantPoolEntry CPE = Constants[i];
        unsigned AlignMask = CPE.getAlignment() - 1;
        Offset = (Offset + AlignMask) & ~AlignMask;

        uintptr_t CAddr = (uintptr_t) mpConstantPoolBase + Offset;
        mConstPoolAddresses.push_back(CAddr);

        if (CPE.isMachineConstantPoolEntry())
          llvm::report_fatal_error
            ("Initialize memory with machine specific constant pool"
             " entry has not been implemented!");

        InitializeConstantToMemory(CPE.Val.ConstVal, (void*) CAddr);

        const llvm::Type *Ty = CPE.Val.ConstVal->getType();
        Offset += mpTD->getTypeAllocSize(Ty);
      }
#endif
      return;
    }

    void initJumpTableInfo(llvm::MachineJumpTableInfo *MJTI) {
      if (mpTJI->hasCustomJumpTables())
        return;

      const std::vector<llvm::MachineJumpTableEntry> &JT =
        MJTI->getJumpTables();
      if (JT.empty())
        return;

      unsigned NumEntries = 0;
      for (int i = 0, e = JT.size(); i != e; i++)
        NumEntries += JT[i].MBBs.size();

      unsigned EntrySize = MJTI->getEntrySize(*mpTD);

      mpJumpTable = MJTI;
      mpJumpTableBase = allocateSpace(NumEntries * EntrySize,
          MJTI->getEntryAlignment(*mpTD));

      return;
    }

    void emitJumpTableInfo(llvm::MachineJumpTableInfo *MJTI) {
      if (mpTJI->hasCustomJumpTables())
        return;

      const std::vector<llvm::MachineJumpTableEntry> &JT =
        MJTI->getJumpTables();
      if (JT.empty() || mpJumpTableBase == 0)
        return;

      assert(llvm::TargetMachine::getRelocationModel() == llvm::Reloc::Static &&
             (MJTI->getEntrySize(*mpTD) == sizeof(mpTD /* a pointer type */)) &&
             "Cross JIT'ing?");

      // For each jump table, map each target in the jump table to the
      // address of an emitted MachineBasicBlock.
      intptr_t *SlotPtr = reinterpret_cast<intptr_t*>(mpJumpTableBase);
      for (int i = 0, ie = JT.size(); i != ie; i++) {
        const std::vector<llvm::MachineBasicBlock*> &MBBs = JT[i].MBBs;
        // Store the address of the basic block for this jump table slot in the
        // memory we allocated for the jump table in 'initJumpTableInfo'
        for (int j = 0, je = MBBs.size(); j != je; j++)
          *SlotPtr++ = getMachineBasicBlockAddress(MBBs[j]);
      }
    }

    void *GetPointerToGlobal(llvm::GlobalValue *V, void *Reference,
        bool MayNeedFarStub) {
      switch (V->getValueID()) {
        case llvm::Value::FunctionVal: {
          llvm::Function *F = (llvm::Function*) V;

          // If we have code, go ahead and return that.
          if (void *ResultPtr = GetPointerToGlobalIfAvailable(F))
            return ResultPtr;

          if (void *FnStub = GetLazyFunctionStubIfAvailable(F))
            // Return the function stub if it's already created.
            // We do this first so that:
            //   we're returning the same address for the function as any
            //   previous call.
            //
            // TODO(llvm.org): Yes, this is wrong. The lazy stub isn't
            //                 guaranteed to be close enough to call.
            return FnStub;

          // If we know the target can handle arbitrary-distance calls, try to
          //  return a direct pointer.
          if (!MayNeedFarStub) {
            //
            // x86_64 architecture may encounter the bug:
            //   http://llvm.org/bugs/show_bug.cgi?id=5201
            // which generate instruction "call" instead of "callq".
            //
            // And once the real address of stub is greater than 64-bit
            // long, the replacement will truncate to 32-bit resulting a
            // serious problem.
#if !defined(__x86_64__)
            // If this is an external function pointer, we can force the JIT
            // to 'compile' it, which really just adds it to the map.
            if (F->isDeclaration() || F->hasAvailableExternallyLinkage()) {
              return GetPointerToFunction(F, /* AbortOnFailure = */false);
              // Changing to false because wanting to allow later calls to
              // mpTJI->relocate() without aborting. For caching purpose
            }
#endif
          }

          // Otherwise, we may need a to emit a stub, and, conservatively, we
          // always do so.
          return GetLazyFunctionStub(F);
          break;
        }
        case llvm::Value::GlobalVariableVal: {
          return GetOrEmitGlobalVariable((llvm::GlobalVariable*) V);
          break;
        }
        case llvm::Value::GlobalAliasVal: {
          llvm::GlobalAlias *GA = (llvm::GlobalAlias*) V;
          const llvm::GlobalValue *GV = GA->resolveAliasedGlobal(false);

          switch (GV->getValueID()) {
            case llvm::Value::FunctionVal: {
              // TODO(all): is there's any possibility that the function is not
              // code-gen'd?
              return GetPointerToFunction(
                  static_cast<const llvm::Function*>(GV),
                  /* AbortOnFailure = */false);
              // Changing to false because wanting to allow later calls to
              // mpTJI->relocate() without aborting. For caching purpose
              break;
            }
            case llvm::Value::GlobalVariableVal: {
              if (void *P = mGlobalAddressMap[GV])
                return P;

              llvm::GlobalVariable *GVar = (llvm::GlobalVariable*) GV;
              EmitGlobalVariable(GVar);

              return mGlobalAddressMap[GV];
              break;
            }
            case llvm::Value::GlobalAliasVal: {
              assert(false && "Alias should be resolved ultimately!");
            }
          }
          break;
        }
        default: {
          break;
        }
      }
      llvm_unreachable("Unknown type of global value!");
    }

    // If the specified function has been code-gen'd, return a pointer to the
    // function. If not, compile it, or use a stub to implement lazy compilation
    // if available.
    void *GetPointerToFunctionOrStub(llvm::Function *F) {
      // If we have already code generated the function, just return the
      // address.
      if (void *Addr = GetPointerToGlobalIfAvailable(F))
        return Addr;

      // Get a stub if the target supports it.
      return GetLazyFunctionStub(F);
    }

    typedef llvm::DenseMap<const llvm::Function*,
                           void*> FunctionToLazyStubMapTy;
    FunctionToLazyStubMapTy mFunctionToLazyStubMap;

    void *GetLazyFunctionStubIfAvailable(llvm::Function *F) {
      return mFunctionToLazyStubMap.lookup(F);
    }

    std::set<const llvm::Function*> PendingFunctions;
    void *GetLazyFunctionStub(llvm::Function *F) {
      // If we already have a lazy stub for this function, recycle it.
      void *&Stub = mFunctionToLazyStubMap[F];
      if (Stub)
        return Stub;

      // In any cases, we should NOT resolve function at runtime (though we are
      // able to). We resolve this right now.
      void *Actual = NULL;
      if (F->isDeclaration() || F->hasAvailableExternallyLinkage()) {
        Actual = GetPointerToFunction(F, /* AbortOnFailure = */false);
        // Changing to false because wanting to allow later calls to
        // mpTJI->relocate() without aborting. For caching purpose
      }

      // Codegen a new stub, calling the actual address of the external
      // function, if it was resolved.
      llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
      startGVStub(F, SL.Size, SL.Alignment);
      Stub = mpTJI->emitFunctionStub(F, Actual, *this);
      finishGVStub();

      // We really want the address of the stub in the GlobalAddressMap for the
      // JIT, not the address of the external function.
      UpdateGlobalMapping(F, Stub);

      if (!Actual)
        PendingFunctions.insert(F);
      else
        Disassemble(F->getName(), reinterpret_cast<uint8_t*>(Stub),
                    SL.Size, true);

      return Stub;
    }

    void *GetPointerToFunction(const llvm::Function *F, bool AbortOnFailure) {
      void *Addr = GetPointerToGlobalIfAvailable(F);
      if (Addr)
        return Addr;

      assert((F->isDeclaration() || F->hasAvailableExternallyLinkage()) &&
             "Internal error: only external defined function routes here!");

      // Handle the failure resolution by ourselves.
      Addr = GetPointerToNamedSymbol(F->getName().str().c_str(),
                                     /* AbortOnFailure = */ false);

      // If we resolved the symbol to a null address (eg. a weak external)
      // return a null pointer let the application handle it.
      if (Addr == NULL) {
        if (AbortOnFailure)
          llvm::report_fatal_error("Could not resolve external function "
                                   "address: " + F->getName());
        else
          return NULL;
      }

      AddGlobalMapping(F, Addr);

      return Addr;
    }

    void *GetPointerToNamedSymbol(const std::string &Name,
                                  bool AbortOnFailure) {
      if (void *Addr = FindRuntimeFunction(Name.c_str()))
        return Addr;

      if (mpSymbolLookupFn)
        if (void *Addr = mpSymbolLookupFn(mpSymbolLookupContext, Name.c_str()))
          return Addr;

      if (AbortOnFailure)
        llvm::report_fatal_error("Program used external symbol '" + Name +
                                "' which could not be resolved!");

      return NULL;
    }

    // Return the address of the specified global variable, possibly emitting it
    // to memory if needed. This is used by the Emitter.
    void *GetOrEmitGlobalVariable(const llvm::GlobalVariable *GV) {
      void *Ptr = GetPointerToGlobalIfAvailable(GV);
      if (Ptr)
        return Ptr;

      if (GV->isDeclaration() || GV->hasAvailableExternallyLinkage()) {
        // If the global is external, just remember the address.
        Ptr = GetPointerToNamedSymbol(GV->getName().str(), true);
        AddGlobalMapping(GV, Ptr);
      } else {
        // If the global hasn't been emitted to memory yet, allocate space and
        // emit it into memory.
        Ptr = GetMemoryForGV(GV);
        AddGlobalMapping(GV, Ptr);
        EmitGlobalVariable(GV);
      }

      return Ptr;
    }

    // This method abstracts memory allocation of global variable so that the
    // JIT can allocate thread local variables depending on the target.
    void *GetMemoryForGV(const llvm::GlobalVariable *GV) {
      void *Ptr;

      const llvm::Type *GlobalType = GV->getType()->getElementType();
      size_t S = mpTD->getTypeAllocSize(GlobalType);
      size_t A = mpTD->getPreferredAlignment(GV);

      if (GV->isThreadLocal()) {
        // We can support TLS by
        //
        //  Ptr = TJI.allocateThreadLocalMemory(S);
        //
        // But I tend not to.
        // (should we disable this in the front-end (i.e., slang)?).
        llvm::report_fatal_error
            ("Compilation of Thread Local Storage (TLS) is disabled!");

      } else if (mpTJI->allocateSeparateGVMemory()) {
        if (A <= 8) {
          Ptr = malloc(S);
        } else {
          // Allocate (S + A) bytes of memory, then use an aligned pointer
          // within that space.
          Ptr = malloc(S + A);
          unsigned int MisAligned = ((intptr_t) Ptr & (A - 1));
          Ptr = reinterpret_cast<uint8_t*>(Ptr) +
                    (MisAligned ? (A - MisAligned) : 0);
        }
      } else {
        Ptr = allocateGlobal(S, A);
      }

      return Ptr;
    }

    void EmitGlobalVariable(const llvm::GlobalVariable *GV) {
      void *GA = GetPointerToGlobalIfAvailable(GV);

      if (GV->isThreadLocal())
        llvm::report_fatal_error
            ("We don't support Thread Local Storage (TLS)!");

      if (GA == NULL) {
        // If it's not already specified, allocate memory for the global.
        GA = GetMemoryForGV(GV);
        AddGlobalMapping(GV, GA);
      }

      InitializeConstantToMemory(GV->getInitializer(), GA);

      // You can do some statistics on global variable here.
      return;
    }

    typedef std::map<llvm::AssertingVH<llvm::GlobalValue>, void*
                     > GlobalToIndirectSymMapTy;
    GlobalToIndirectSymMapTy GlobalToIndirectSymMap;

    void *GetPointerToGVIndirectSym(llvm::GlobalValue *V, void *Reference) {
      // Make sure GV is emitted first, and create a stub containing the fully
      // resolved address.
      void *GVAddress = GetPointerToGlobal(V, Reference, false);

      // If we already have a stub for this global variable, recycle it.
      void *&IndirectSym = GlobalToIndirectSymMap[V];
      // Otherwise, codegen a new indirect symbol.
      if (!IndirectSym)
        IndirectSym = mpTJI->emitGlobalValueIndirectSym(V, GVAddress, *this);

      return IndirectSym;
    }

    // This is the equivalent of FunctionToLazyStubMap for external functions.
    //
    // TODO(llvm.org): Of course, external functions don't need a lazy stub.
    //                 It's actually here to make it more likely that far calls
    //                 succeed, but no single stub can guarantee that. I'll
    //                 remove this in a subsequent checkin when I actually fix
    //                 far calls.
    std::map<void*, void*> ExternalFnToStubMap;

    // Return a stub for the function at the specified address.
    void *GetExternalFunctionStub(void *FnAddr) {
      void *&Stub = ExternalFnToStubMap[FnAddr];
      if (Stub)
        return Stub;

      llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
      startGVStub(0, SL.Size, SL.Alignment);
      Stub = mpTJI->emitFunctionStub(0, FnAddr, *this);
      finishGVStub();

      return Stub;
    }

#if defined(USE_DISASSEMBLER)
    const llvm::MCAsmInfo *mpAsmInfo;
    const llvm::MCDisassembler *mpDisassmbler;
    llvm::MCInstPrinter *mpIP;

    class BufferMemoryObject : public llvm::MemoryObject {
    private:
      const uint8_t *mBytes;
      uint64_t mLength;

    public:
      BufferMemoryObject(const uint8_t *Bytes, uint64_t Length) :
        mBytes(Bytes), mLength(Length) { }

      uint64_t getBase() const { return 0; }
      uint64_t getExtent() const { return mLength; }

      int readByte(uint64_t Addr, uint8_t *Byte) const {
        if (Addr > getExtent())
          return -1;
        *Byte = mBytes[Addr];
        return 0;
      }
    };

   public:
    void Disassemble(const llvm::StringRef &Name, uint8_t *Start,
                     size_t Length, bool IsStub) {
      llvm::raw_fd_ostream *OS;
#if defined(USE_DISASSEMBLER_FILE)
      std::string ErrorInfo;
      OS = new llvm::raw_fd_ostream("/data/local/tmp/out.S",
                                    ErrorInfo,
                                    llvm::raw_fd_ostream::F_Append);
      if (!ErrorInfo.empty()) {    // some errors occurred
        // LOGE("Error in creating disassembly file");
        delete OS;
        return;
      }
#else
      OS = &llvm::outs();
#endif
      *OS << "JIT: Disassembled code: " << Name << ((IsStub) ? " (stub)" : "")
          << "\n";

      if (mpAsmInfo == NULL)
        mpAsmInfo = mpTarget->createAsmInfo(Triple);
      if (mpDisassmbler == NULL)
        mpDisassmbler = mpTarget->createMCDisassembler();
      if (mpIP == NULL)
        mpIP = mpTarget->createMCInstPrinter(mpAsmInfo->getAssemblerDialect(),
                                             *mpAsmInfo);

      const BufferMemoryObject *BufferMObj = new BufferMemoryObject(Start,
                                                                    Length);
      uint64_t Size;
      uint64_t Index;

      for (Index = 0; Index < Length; Index += Size) {
        llvm::MCInst Inst;

        if (mpDisassmbler->getInstruction(Inst, Size, *BufferMObj, Index,
              /* REMOVED */ llvm::nulls())) {
          (*OS).indent(4)
               .write("0x", 2)
               .write_hex((uint32_t) Start + Index)
               .write(':');
          mpIP->printInst(&Inst, *OS);
          *OS << "\n";
        } else {
          if (Size == 0)
            Size = 1;  // skip illegible bytes
        }
      }

      *OS << "\n";
      delete BufferMObj;

#if  defined(USE_DISASSEMBLER_FILE)
      // If you want the disassemble results write to file, uncomment this.
      OS->close();
      delete OS;
#endif
      return;
    }
#else
    inline void Disassemble(const std::string &Name, uint8_t *Start,
                            size_t Length, bool IsStub) {
      return;
    }
#endif  // defined(USE_DISASSEMBLER)

   private:
    // Resolver to undefined symbol in CodeEmitter
    BCCSymbolLookupFn mpSymbolLookupFn;
    void *mpSymbolLookupContext;

   public:
    // Will take the ownership of @MemMgr
    explicit CodeEmitter(CodeMemoryManager *pMemMgr)
        : mpMemMgr(pMemMgr),
          mpTarget(NULL),
          mpTJI(NULL),
          mpTD(NULL),
          mpCurEmitFunction(NULL),
          mpConstantPool(NULL),
          mpJumpTable(NULL),
          mpMMI(NULL),
#if defined(USE_DISASSEMBLER)
          mpAsmInfo(NULL),
          mpDisassmbler(NULL),
          mpIP(NULL),
#endif
          mpSymbolLookupFn(NULL),
          mpSymbolLookupContext(NULL) {
      return;
    }

    inline global_addresses_const_iterator global_address_begin() const {
      return mGlobalAddressMap.begin();
    }
    inline global_addresses_const_iterator global_address_end() const {
      return mGlobalAddressMap.end();
    }

    std::vector<oBCCRelocEntry> const &getCachingRelocations() const {
      return mCachingRelocations;
    }

    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      mpSymbolLookupFn = pFn;
      mpSymbolLookupContext = pContext;
      return;
    }

    void setTargetMachine(llvm::TargetMachine &TM) {
      // Set Target
      mpTarget = &TM.getTarget();
      // Set TargetJITInfo
      mpTJI = TM.getJITInfo();
      // set TargetData
      mpTD = TM.getTargetData();

      assert(!mpTJI->needsGOT() && "We don't support GOT needed target!");

      return;
    }

    // This callback is invoked when the specified function is about to be code
    // generated.  This initializes the BufferBegin/End/Ptr fields.
    void startFunction(llvm::MachineFunction &F) {
      uintptr_t ActualSize = 0;

      mpMemMgr->setMemoryWritable();

      // BufferBegin, BufferEnd and CurBufferPtr are all inherited from class
      // MachineCodeEmitter, which is the super class of the class
      // JITCodeEmitter.
      //
      // BufferBegin/BufferEnd - Pointers to the start and end of the memory
      //                         allocated for this code buffer.
      //
      // CurBufferPtr - Pointer to the next byte of memory to fill when emitting
      //                code. This is guranteed to be in the range
      //                [BufferBegin, BufferEnd].  If this pointer is at
      //                BufferEnd, it will never move due to code emission, and
      //                all code emission requests will be ignored (this is the
      //                buffer overflow condition).
      BufferBegin = CurBufferPtr =
          mpMemMgr->startFunctionBody(F.getFunction(), ActualSize);
      BufferEnd = BufferBegin + ActualSize;

      if (mpCurEmitFunction == NULL)
        mpCurEmitFunction = new EmittedFunctionCode();
      mpCurEmitFunction->FunctionBody = BufferBegin;

      // Ensure the constant pool/jump table info is at least 4-byte aligned.
      emitAlignment(16);

      emitConstantPool(F.getConstantPool());
      if (llvm::MachineJumpTableInfo *MJTI = F.getJumpTableInfo())
        initJumpTableInfo(MJTI);

      // About to start emitting the machine code for the function.
      emitAlignment(std::max(F.getFunction()->getAlignment(), 8U));

      UpdateGlobalMapping(F.getFunction(), CurBufferPtr);

      mpCurEmitFunction->Code = CurBufferPtr;

      mMBBLocations.clear();

      return;
    }

    // This callback is invoked when the specified function has finished code
    // generation. If a buffer overflow has occurred, this method returns true
    // (the callee is required to try again).
    bool finishFunction(llvm::MachineFunction &F) {
      if (CurBufferPtr == BufferEnd) {
        // No enough memory
        mpMemMgr->endFunctionBody(F.getFunction(), BufferBegin, CurBufferPtr);
        return false;
      }

      if (llvm::MachineJumpTableInfo *MJTI = F.getJumpTableInfo())
        emitJumpTableInfo(MJTI);

      // FnStart is the start of the text, not the start of the constant pool
      // and other per-function data.
      uint8_t *FnStart =
          reinterpret_cast<uint8_t*>(
              GetPointerToGlobalIfAvailable(F.getFunction()));

      // FnEnd is the end of the function's machine code.
      uint8_t *FnEnd = CurBufferPtr;

      if (!mRelocations.empty()) {
        ptrdiff_t BufferOffset = BufferBegin - mpMemMgr->getCodeMemBase();

        // Resolve the relocations to concrete pointers.
        for (int i = 0, e = mRelocations.size(); i != e; i++) {
          llvm::MachineRelocation &MR = mRelocations[i];
          void *ResultPtr = NULL;

          if (!MR.letTargetResolve()) {
            if (MR.isExternalSymbol()) {
              ResultPtr = GetPointerToNamedSymbol(MR.getExternalSymbol(), true);

              if (MR.mayNeedFarStub()) {
                ResultPtr = GetExternalFunctionStub(ResultPtr);
              }

            } else if (MR.isGlobalValue()) {
              ResultPtr = GetPointerToGlobal(MR.getGlobalValue(),
                                             BufferBegin
                                               + MR.getMachineCodeOffset(),
                                             MR.mayNeedFarStub());
            } else if (MR.isIndirectSymbol()) {
              ResultPtr =
                  GetPointerToGVIndirectSym(
                      MR.getGlobalValue(),
                      BufferBegin + MR.getMachineCodeOffset());
            } else if (MR.isBasicBlock()) {
              ResultPtr =
                  (void*) getMachineBasicBlockAddress(MR.getBasicBlock());
            } else if (MR.isConstantPoolIndex()) {
              ResultPtr =
                 (void*) getConstantPoolEntryAddress(MR.getConstantPoolIndex());
            } else {
              assert(MR.isJumpTableIndex() && "Unknown type of relocation");
              ResultPtr =
                  (void*) getJumpTableEntryAddress(MR.getJumpTableIndex());
            }

            if (!MR.isExternalSymbol() || MR.mayNeedFarStub()) {
              // TODO(logan): Cache external symbol relocation entry.
              // Currently, we are not caching them.  But since Android
              // system is using prelink, it is not a problem.

              // Cache the relocation result address
              mCachingRelocations.push_back(
                oBCCRelocEntry(MR.getRelocationType(),
                               MR.getMachineCodeOffset() + BufferOffset,
                               ResultPtr));
            }

            MR.setResultPointer(ResultPtr);
          }
        }

        mpTJI->relocate(BufferBegin, &mRelocations[0], mRelocations.size(),
                        mpMemMgr->getGOTBase());
      }

      mpMemMgr->endFunctionBody(F.getFunction(), BufferBegin, CurBufferPtr);
      // CurBufferPtr may have moved beyond FnEnd, due to memory allocation for
      // global variables that were referenced in the relocations.
      if (CurBufferPtr == BufferEnd)
        return false;

      // Now that we've succeeded in emitting the function.
      mpCurEmitFunction->Size = CurBufferPtr - BufferBegin;
      BufferBegin = CurBufferPtr = 0;

      if (F.getFunction()->hasName())
        mEmittedFunctions[F.getFunction()->getNameStr()] = mpCurEmitFunction;
      mpCurEmitFunction = NULL;

      mRelocations.clear();
      mConstPoolAddresses.clear();

      if (mpMMI)
        mpMMI->EndFunction();

      updateFunctionStub(F.getFunction());

      // Mark code region readable and executable if it's not so already.
      mpMemMgr->setMemoryExecutable();

      Disassemble(F.getFunction()->getName(), FnStart, FnEnd - FnStart, false);

      return false;
    }

    void startGVStub(const llvm::GlobalValue *GV, unsigned StubSize,
                     unsigned Alignment) {
      mpSavedBufferBegin = BufferBegin;
      mpSavedBufferEnd = BufferEnd;
      mpSavedCurBufferPtr = CurBufferPtr;

      BufferBegin = CurBufferPtr = mpMemMgr->allocateStub(GV, StubSize,
                                                          Alignment);
      BufferEnd = BufferBegin + StubSize + 1;

      return;
    }

    void startGVStub(void *Buffer, unsigned StubSize) {
      mpSavedBufferBegin = BufferBegin;
      mpSavedBufferEnd = BufferEnd;
      mpSavedCurBufferPtr = CurBufferPtr;

      BufferBegin = CurBufferPtr = reinterpret_cast<uint8_t *>(Buffer);
      BufferEnd = BufferBegin + StubSize + 1;

      return;
    }

    void finishGVStub() {
      assert(CurBufferPtr != BufferEnd && "Stub overflowed allocated space.");

      // restore
      BufferBegin = mpSavedBufferBegin;
      BufferEnd = mpSavedBufferEnd;
      CurBufferPtr = mpSavedCurBufferPtr;

      return;
    }

    // Allocates and fills storage for an indirect GlobalValue, and returns the
    // address.
    void *allocIndirectGV(const llvm::GlobalValue *GV,
                          const uint8_t *Buffer, size_t Size,
                          unsigned Alignment) {
      uint8_t *IndGV = mpMemMgr->allocateStub(GV, Size, Alignment);
      memcpy(IndGV, Buffer, Size);
      return IndGV;
    }

    // Emits a label
    void emitLabel(llvm::MCSymbol *Label) {
      mLabelLocations[Label] = getCurrentPCValue();
      return;
    }

    // Allocate memory for a global. Unlike allocateSpace, this method does not
    // allocate memory in the current output buffer, because a global may live
    // longer than the current function.
    void *allocateGlobal(uintptr_t Size, unsigned Alignment) {
      // Delegate this call through the memory manager.
      return mpMemMgr->allocateGlobal(Size, Alignment);
    }

    // This should be called by the target when a new basic block is about to be
    // emitted. This way the MCE knows where the start of the block is, and can
    // implement getMachineBasicBlockAddress.
    void StartMachineBasicBlock(llvm::MachineBasicBlock *MBB) {
      if (mMBBLocations.size() <= (unsigned) MBB->getNumber())
        mMBBLocations.resize((MBB->getNumber() + 1) * 2);
      mMBBLocations[MBB->getNumber()] = getCurrentPCValue();
      return;
    }

    // Whenever a relocatable address is needed, it should be noted with this
    // interface.
    void addRelocation(const llvm::MachineRelocation &MR) {
      mRelocations.push_back(MR);
      return;
    }

    // Return the address of the @Index entry in the constant pool that was
    // last emitted with the emitConstantPool method.
    uintptr_t getConstantPoolEntryAddress(unsigned Index) const {
      assert(Index < mpConstantPool->getConstants().size() &&
             "Invalid constant pool index!");
      return mConstPoolAddresses[Index];
    }

    // Return the address of the jump table with index @Index in the function
    // that last called initJumpTableInfo.
    uintptr_t getJumpTableEntryAddress(unsigned Index) const {
      const std::vector<llvm::MachineJumpTableEntry> &JT =
          mpJumpTable->getJumpTables();

      assert((Index < JT.size()) && "Invalid jump table index!");

      unsigned int Offset = 0;
      unsigned int EntrySize = mpJumpTable->getEntrySize(*mpTD);

      for (unsigned i = 0; i < Index; i++)
        Offset += JT[i].MBBs.size();
      Offset *= EntrySize;

      return (uintptr_t)(reinterpret_cast<uint8_t*>(mpJumpTableBase) + Offset);
    }

    // Return the address of the specified MachineBasicBlock, only usable after
    // the label for the MBB has been emitted.
    uintptr_t getMachineBasicBlockAddress(llvm::MachineBasicBlock *MBB) const {
      assert(mMBBLocations.size() > (unsigned) MBB->getNumber() &&
             mMBBLocations[MBB->getNumber()] &&
             "MBB not emitted!");
      return mMBBLocations[MBB->getNumber()];
    }

    // Return the address of the specified LabelID, only usable after the
    // LabelID has been emitted.
    uintptr_t getLabelAddress(llvm::MCSymbol *Label) const {
      assert(mLabelLocations.count(Label) && "Label not emitted!");
      return mLabelLocations.find(Label)->second;
    }

    // Specifies the MachineModuleInfo object. This is used for exception
    // handling purposes.
    void setModuleInfo(llvm::MachineModuleInfo *Info) {
      mpMMI = Info;
      return;
    }

    void updateFunctionStub(const llvm::Function *F) {
      // Get the empty stub we generated earlier.
      void *Stub;
      std::set<const llvm::Function*>::iterator I = PendingFunctions.find(F);
      if (I != PendingFunctions.end())
        Stub = mFunctionToLazyStubMap[F];
      else
        return;

      void *Addr = GetPointerToGlobalIfAvailable(F);

      assert(Addr != Stub &&
             "Function must have non-stub address to be updated.");

      // Tell the target jit info to rewrite the stub at the specified address,
      // rather than creating a new one.
      llvm::TargetJITInfo::StubLayout SL = mpTJI->getStubLayout();
      startGVStub(Stub, SL.Size);
      mpTJI->emitFunctionStub(F, Addr, *this);
      finishGVStub();

      Disassemble(F->getName(), reinterpret_cast<uint8_t*>(Stub),
                  SL.Size, true);

      PendingFunctions.erase(I);

      return;
    }

    // Once you finish the compilation on a translation unit, you can call this
    // function to recycle the memory (which is used at compilation time and not
    // needed for runtime).
    //
    //  NOTE: You should not call this funtion until the code-gen passes for a
    //        given module is done. Otherwise, the results is undefined and may
    //        cause the system crash!
    void releaseUnnecessary() {
      mMBBLocations.clear();
      mLabelLocations.clear();
      mGlobalAddressMap.clear();
      mFunctionToLazyStubMap.clear();
      GlobalToIndirectSymMap.clear();
      ExternalFnToStubMap.clear();
      PendingFunctions.clear();

      return;
    }

    void reset() {
      releaseUnnecessary();

      mpSymbolLookupFn = NULL;
      mpSymbolLookupContext = NULL;

      mpTJI = NULL;
      mpTD = NULL;

      for (EmittedFunctionsMapTy::iterator I = mEmittedFunctions.begin(),
              E = mEmittedFunctions.end();
           I != E;
           I++)
        if (I->second != NULL)
          delete I->second;
      mEmittedFunctions.clear();

      mpMemMgr->reset();

      return;
    }

    void *lookup(const char *Name) {
      return lookup( llvm::StringRef(Name) );
    }

    void *lookup(const llvm::StringRef &Name) {
      EmittedFunctionsMapTy::const_iterator I =
          mEmittedFunctions.find(Name.str());
      if (I == mEmittedFunctions.end())
        return NULL;
      else
        return I->second->Code;
    }

    void getFunctionNames(BCCsizei *actualFunctionCount,
                          BCCsizei maxFunctionCount,
                          BCCchar **functions) {
      int functionCount = mEmittedFunctions.size();

      if (actualFunctionCount)
        *actualFunctionCount = functionCount;
      if (functionCount > maxFunctionCount)
        functionCount = maxFunctionCount;
      if (functions)
        for (EmittedFunctionsMapTy::const_iterator
                I = mEmittedFunctions.begin(), E = mEmittedFunctions.end();
             (I != E) && (functionCount > 0);
             I++, functionCount--)
          *functions++ = const_cast<BCCchar*>(I->first.c_str());

      return;
    }

    void getFunctionBinary(BCCchar *label,
                           BCCvoid **base,
                           BCCsizei *length) {
      EmittedFunctionsMapTy::const_iterator I = mEmittedFunctions.find(label);
      if (I == mEmittedFunctions.end()) {
        *base = NULL;
        *length = 0;
      } else {
        *base = I->second->Code;
        *length = I->second->Size;
      }
      return;
    }

    ~CodeEmitter() {
       delete mpMemMgr;
#if defined(USE_DISASSEMBLER)
      delete mpAsmInfo;
      delete mpDisassmbler;
      delete mpIP;
#endif
      return;
    }
  };
  // End of Class CodeEmitter
  //////////////////////////////////////////////////////////////////////////////

  // The CodeEmitter
  llvm::OwningPtr<CodeEmitter> mCodeEmitter;
  CodeEmitter *createCodeEmitter() {
    mCodeEmitter.reset(new CodeEmitter(mCodeMemMgr.take()));
    return mCodeEmitter.get();
  }

  BCCSymbolLookupFn mpSymbolLookupFn;
  void *mpSymbolLookupContext;

  llvm::LLVMContext *mContext;
  llvm::Module *mModule;

  bool mHasLinked;

 public:
  Compiler()
      : mUseCache(false),
        mCacheNew(false),
        mCacheFd(-1),
        mCacheMapAddr(NULL),
        mCacheHdr(NULL),
        mCacheSize(0),
        mCacheDiff(0),
        mCodeDataAddr(NULL),
        mpSymbolLookupFn(NULL),
        mpSymbolLookupContext(NULL),
        mContext(NULL),
        mModule(NULL),
        mHasLinked(false) /* Turn off linker */ {
    llvm::remove_fatal_error_handler();
    llvm::install_fatal_error_handler(LLVMErrorHandler, &mError);
    mContext = new llvm::LLVMContext();
    return;
  }

  // interface for BCCscript::registerSymbolCallback()
  void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
    mpSymbolLookupFn = pFn;
    mpSymbolLookupContext = pContext;
    return;
  }

  int readModule(llvm::Module *module) {
    GlobalInitialization();
    mModule = module;
    return hasError();
  }

  int readBC(const char *bitcode,
                 size_t bitcodeSize,
                 const BCCchar *resName) {
    GlobalInitialization();

    if (Compiler::BccCodeAddr /* USE_CACHE */ && resName) {
      // Turn on mUseCache mode iff
      // 1. Has resName
      // and, assuming USE_RELOCATE is false:
      // 2. Later running code doesn't violate the following condition:
      //    mCodeDataAddr (set in loadCacheFile()) ==
      //        mCacheHdr->cachedCodeDataAddr
      //
      //    BTW, this condition is achievable only when in the earlier
      //    cache-generating run,
      //      mpCodeMem == BccCodeAddr - MaxCodeSize - MaxGlobalVarSize,
      //      which means the mmap'ed is in the reserved area,
      //
      //    Note: Upon violation, mUseCache will be set back to false.
      mUseCache = true;

      mCacheFd = openCacheFile(resName, true /* createIfMissing */);
      if (mCacheFd >= 0 && !mCacheNew) {  // Just use cache file
        return -mCacheFd;
      }
    }

    llvm::OwningPtr<llvm::MemoryBuffer> MEM;

    if (bitcode == NULL || bitcodeSize <= 0)
      return 0;

    // Package input to object MemoryBuffer
    MEM.reset(llvm::MemoryBuffer::getMemBuffer(
                llvm::StringRef(bitcode, bitcodeSize)));

    if (MEM.get() == NULL) {
      setError("Error reading input program bitcode into memory");
      return hasError();
    }

    // Read the input Bitcode as a Module
    mModule = llvm::ParseBitcodeFile(MEM.get(), *mContext, &mError);
    MEM.reset();
    return hasError();
  }

  int linkBC(const char *bitcode, size_t bitcodeSize) {
    llvm::OwningPtr<llvm::MemoryBuffer> MEM;

    if (bitcode == NULL || bitcodeSize <= 0)
      return 0;

    if (mModule == NULL) {
      setError("No module presents for linking");
      return hasError();
    }

    MEM.reset(llvm::MemoryBuffer::getMemBuffer(
                llvm::StringRef(bitcode, bitcodeSize)));

    if (MEM.get() == NULL) {
      setError("Error reading input library bitcode into memory");
      return hasError();
    }

    llvm::OwningPtr<llvm::Module> Lib(llvm::ParseBitcodeFile(MEM.get(),
                                                             *mContext,
                                                             &mError));
    if (Lib.get() == NULL)
      return hasError();

    if (llvm::Linker::LinkModules(mModule, Lib.take(), &mError))
      return hasError();

    // Everything for linking should be settled down here with no error occurs
    mHasLinked = true;
    return hasError();
  }

  // interface for bccLoadBinary()
  int loadCacheFile() {
    // Check File Descriptor
    if (mCacheFd < 0) {
      LOGE("loading cache from invalid mCacheFd = %d\n", (int)mCacheFd);
      goto giveup;
    }

    // Check File Size
    struct stat statCacheFd;
    if (fstat(mCacheFd, &statCacheFd) < 0) {
      LOGE("unable to stat mCacheFd = %d\n", (int)mCacheFd);
      goto giveup;
    }

    mCacheSize = statCacheFd.st_size;

    if (mCacheSize < sizeof(oBCCHeader) ||
        mCacheSize <= MaxCodeSize + MaxGlobalVarSize) {
      LOGE("mCacheFd %d is too small to be correct\n", (int)mCacheFd);
      goto giveup;
    }

    if (lseek(mCacheFd, 0, SEEK_SET) != 0) {
      LOGE("Unable to seek to 0: %s\n", strerror(errno));
      goto giveup;
    }

    // Read File Content
    {
      // Part 1. Deal with the non-codedata section first
      off_t heuristicCodeOffset = mCacheSize - MaxCodeSize - MaxGlobalVarSize;
      LOGW("TODO(sliao)@loadCacheFile: mCacheSize=%x, heuristicCodeOffset=%llx",
           (unsigned int)mCacheSize,
           (unsigned long long int)heuristicCodeOffset);

      mCacheMapAddr = (char *)malloc(heuristicCodeOffset);
      if (!mCacheMapAddr) {
          flock(mCacheFd, LOCK_UN);
          LOGE("allocation failed.\n");
          goto bail;
      }

      size_t nread = TEMP_FAILURE_RETRY1(read(mCacheFd, mCacheMapAddr,
                                              heuristicCodeOffset));
      if (nread != (size_t)heuristicCodeOffset) {
        LOGE("read(mCacheFd) failed\n");
        goto bail;
      }

      mCacheHdr = reinterpret_cast<oBCCHeader *>(mCacheMapAddr);
      // Sanity check
      if (mCacheHdr->codeOffset != (uint32_t)heuristicCodeOffset) {
        LOGE("assertion failed: heuristic code offset is not correct.\n");
        goto bail;
      }
      LOGW("TODO(sliao): mCacheHdr->cachedCodeDataAddr=%x", mCacheHdr->cachedCodeDataAddr);
      LOGW("mCacheHdr->rootAddr=%x", mCacheHdr->rootAddr);
      LOGW("mCacheHdr->initAddr=%x", mCacheHdr->initAddr);
      LOGW("mCacheHdr->codeOffset=%x", mCacheHdr->codeOffset);
      LOGW("mCacheHdr->codeSize=%x", mCacheHdr->codeSize);

      // Verify the Cache File
      if (memcmp(mCacheHdr->magic, OBCC_MAGIC, 4) != 0) {
        LOGE("bad magic word\n");
        goto bail;
      }

      if (memcmp(mCacheHdr->magicVersion, OBCC_MAGIC_VERS, 4) != 0) {
        LOGE("bad oBCC version 0x%08x\n",
             *reinterpret_cast<uint32_t *>(mCacheHdr->magicVersion));
        goto bail;
      }

      if (mCacheSize < mCacheHdr->relocOffset +
          mCacheHdr->relocCount * sizeof(oBCCRelocEntry)) {
        LOGE("relocate table overflow\n");
        goto bail;
      }

      if (mCacheSize < mCacheHdr->exportVarsOffset +
          mCacheHdr->exportVarsCount * sizeof(uint32_t)) {
        LOGE("export variables table overflow\n");
        goto bail;
      }

      if (mCacheSize < mCacheHdr->exportFuncsOffset +
          mCacheHdr->exportFuncsCount * sizeof(uint32_t)) {
        LOGE("export functions table overflow\n");
        goto bail;
      }

      if (mCacheSize < mCacheHdr->exportPragmasOffset +
          mCacheHdr->exportPragmasCount * sizeof(uint32_t)) {
        LOGE("export pragmas table overflow\n");
        goto bail;
      }

      if (mCacheSize < mCacheHdr->codeOffset + mCacheHdr->codeSize) {
        LOGE("code cache overflow\n");
        goto bail;
      }

      if (mCacheSize < mCacheHdr->dataOffset + mCacheHdr->dataSize) {
        LOGE("data (global variable) cache overflow\n");
        goto bail;
      }

      // Part 2. Deal with the codedata section
      mCodeDataAddr = (char *) mmap(reinterpret_cast<void*>(BCC_CODE_ADDR),
                                    MaxCodeSize + MaxGlobalVarSize,
                                    PROT_READ | PROT_EXEC | PROT_WRITE,
                                    MAP_PRIVATE | MAP_FIXED,
                                    mCacheFd, heuristicCodeOffset);
      if (mCodeDataAddr != MAP_FAILED &&
          mCodeDataAddr ==
          reinterpret_cast<char *>(mCacheHdr->cachedCodeDataAddr)) {
        // relocate is avoidable


        flock(mCacheFd, LOCK_UN);
      } else {
#if (USE_RELOCATE)
        mCacheMapAddr = (char *) mmap(0,
                                      mCacheSize,
                                      PROT_READ | PROT_EXEC | PROT_WRITE,
                                      MAP_PRIVATE,
                                      mCacheFd,
                                      0);
        if (mCacheMapAddr == MAP_FAILED) {
          LOGE("unable to mmap .oBBC cache: %s\n", strerror(errno));
          flock(mCacheFd, LOCK_UN);
          goto giveup;
        }

        flock(mCacheFd, LOCK_UN);
        mCodeDataAddr = mCacheMapAddr + mCacheHdr->codeOffset;
#else
        // TODO(sliao): XXX: Call Compile();
        flock(mCacheFd, LOCK_UN);
#endif
      }
    }

#if (USE_RELOCATE)
    // Relocate
    {
      mCacheDiff = mCodeDataAddr -
                   reinterpret_cast<char *>(mCacheHdr->cachedCodeDataAddr);

      if (mCacheDiff) {  // To relocate
        if (mCacheHdr->rootAddr) {
          mCacheHdr->rootAddr += mCacheDiff;
        }

        if (mCacheHdr->initAddr) {
          mCacheHdr->initAddr += mCacheDiff;
        }

        oBCCRelocEntry *cachedRelocTable =
            reinterpret_cast<oBCCRelocEntry *>(mCacheMapAddr +
                                               mCacheHdr->relocOffset);

        std::vector<llvm::MachineRelocation> relocations;

        // Read in the relocs
        for (size_t i = 0; i < mCacheHdr->relocCount; i++) {
          oBCCRelocEntry *entry = &cachedRelocTable[i];

          llvm::MachineRelocation reloc =
              llvm::MachineRelocation::getGV((uintptr_t)entry->relocOffset,
                                             (unsigned)entry->relocType, 0, 0);

          reloc.setResultPointer(
              reinterpret_cast<char *>(entry->cachedResultAddr) + mCacheDiff);

          relocations.push_back(reloc);
        }

        // Rewrite machine code using llvm::TargetJITInfo relocate
        {
          llvm::TargetMachine *TM = NULL;
          const llvm::Target *Target;
          std::string FeaturesStr;

          // Create TargetMachine
          Target = llvm::TargetRegistry::lookupTarget(Triple, mError);
          if (hasError())
            goto bail;

          if (!CPU.empty() || !Features.empty()) {
            llvm::SubtargetFeatures F;
            F.setCPU(CPU);
            for (std::vector<std::string>::const_iterator I = Features.begin(),
                     E = Features.end(); I != E; I++)
              F.AddFeature(*I);
            FeaturesStr = F.getString();
          }

          TM = Target->createTargetMachine(Triple, FeaturesStr);
          if (TM == NULL) {
            setError("Failed to create target machine implementation for the"
                     " specified triple '" + Triple + "'");
            goto bail;
          }

          TM->getJITInfo()->relocate(mCodeDataAddr,
                                     &relocations[0], relocations.size(),
                                     (unsigned char *)mCodeDataAddr+MaxCodeSize);

          if (mCodeEmitter.get()) {
            mCodeEmitter->Disassemble(llvm::StringRef("cache"),
                                      reinterpret_cast<uint8_t*>(mCodeDataAddr),
                                      2 * 1024 /*MaxCodeSize*/,
                                      false);
          }

          delete TM;
        }
      }  // End of if (mCacheDiff)
    }
#else
    // TODO(sliao): XXX: Call Compile();
#endif  // End of USE_RELOCATE
    return 0;

 bail:
    if (mCacheMapAddr) {
      free(mCacheMapAddr);
      mCacheMapAddr = 0;
    }

    //    if (BccCodeAddrTaken) {
    if (munmap(mCodeDataAddr, MaxCodeSize + MaxGlobalVarSize) != 0) {
      LOGE("munmap failed: %s\n", strerror(errno));
    }
    mCodeDataAddr = 0;
    //}

 giveup:
    return 1;
  }

  // interace for bccCompileBC()
  int compile() {
    llvm::TargetData *TD = NULL;

    llvm::TargetMachine *TM = NULL;
    const llvm::Target *Target;
    std::string FeaturesStr;

    llvm::FunctionPassManager *CodeGenPasses = NULL;

    const llvm::NamedMDNode *PragmaMetadata;
    const llvm::NamedMDNode *ExportVarMetadata;
    const llvm::NamedMDNode *ExportFuncMetadata;

    if (mModule == NULL)  // No module was loaded
      return 0;

    // Create TargetMachine
    Target = llvm::TargetRegistry::lookupTarget(Triple, mError);
    if (hasError())
      goto on_bcc_compile_error;

    if (!CPU.empty() || !Features.empty()) {
      llvm::SubtargetFeatures F;
      F.setCPU(CPU);
      for (std::vector<std::string>::const_iterator I = Features.begin(),
              E = Features.end();
           I != E;
           I++)
        F.AddFeature(*I);
      FeaturesStr = F.getString();
    }

    TM = Target->createTargetMachine(Triple, FeaturesStr);
    if (TM == NULL) {
      setError("Failed to create target machine implementation for the"
               " specified triple '" + Triple + "'");
      goto on_bcc_compile_error;
    }

    // Create memory manager for creation of code emitter later.
    if (!mCodeMemMgr.get() && !createCodeMemoryManager()) {
      setError("Failed to startup memory management for further compilation");
      goto on_bcc_compile_error;
    }
    mCodeDataAddr = (char *) (mCodeMemMgr.get()->getCodeMemBase());

    // Create code emitter
    if (!mCodeEmitter.get()) {
      if (!createCodeEmitter()) {
        setError("Failed to create machine code emitter to complete"
                 " the compilation");
        goto on_bcc_compile_error;
      }
    } else {
      // Reuse the code emitter
      mCodeEmitter->reset();
    }

    mCodeEmitter->setTargetMachine(*TM);
    mCodeEmitter->registerSymbolCallback(mpSymbolLookupFn,
                                         mpSymbolLookupContext);

    // Get target data from Module
    TD = new llvm::TargetData(mModule);

    // Load named metadata
    ExportVarMetadata = mModule->getNamedMetadata(ExportVarMetadataName);
    ExportFuncMetadata = mModule->getNamedMetadata(ExportFuncMetadataName);
    PragmaMetadata = mModule->getNamedMetadata(PragmaMetadataName);

    // Create LTO passes and run them on the mModule
    if (mHasLinked) {
      llvm::TimePassesIsEnabled = true;  // TODO(all)
      llvm::PassManager LTOPasses;
      LTOPasses.add(new llvm::TargetData(*TD));

      std::vector<const char*> ExportSymbols;

      // A workaround for getting export variable and function name. Will refine
      // it soon.
      if (ExportVarMetadata) {
        for (int i = 0, e = ExportVarMetadata->getNumOperands(); i != e; i++) {
          llvm::MDNode *ExportVar = ExportVarMetadata->getOperand(i);
          if (ExportVar != NULL && ExportVar->getNumOperands() > 1) {
            llvm::Value *ExportVarNameMDS = ExportVar->getOperand(0);
            if (ExportVarNameMDS->getValueID() == llvm::Value::MDStringVal) {
              llvm::StringRef ExportVarName =
                  static_cast<llvm::MDString*>(ExportVarNameMDS)->getString();
              ExportSymbols.push_back(ExportVarName.data());
            }
          }
        }
      }

      if (ExportFuncMetadata) {
        for (int i = 0, e = ExportFuncMetadata->getNumOperands(); i != e; i++) {
          llvm::MDNode *ExportFunc = ExportFuncMetadata->getOperand(i);
          if (ExportFunc != NULL && ExportFunc->getNumOperands() > 0) {
            llvm::Value *ExportFuncNameMDS = ExportFunc->getOperand(0);
            if (ExportFuncNameMDS->getValueID() == llvm::Value::MDStringVal) {
              llvm::StringRef ExportFuncName =
                  static_cast<llvm::MDString*>(ExportFuncNameMDS)->getString();
              ExportSymbols.push_back(ExportFuncName.data());
            }
          }
        }
      }
      // root() and init() are born to be exported
      ExportSymbols.push_back("root");
      ExportSymbols.push_back("init");

      // We now create passes list performing LTO. These are copied from
      // (including comments) llvm::createStandardLTOPasses().

      // Internalize all other symbols not listed in ExportSymbols
      LTOPasses.add(llvm::createInternalizePass(ExportSymbols));

      // Propagate constants at call sites into the functions they call. This
      // opens opportunities for globalopt (and inlining) by substituting
      // function pointers passed as arguments to direct uses of functions.
      LTOPasses.add(llvm::createIPSCCPPass());

      // Now that we internalized some globals, see if we can hack on them!
      LTOPasses.add(llvm::createGlobalOptimizerPass());

      // Linking modules together can lead to duplicated global constants, only
      // keep one copy of each constant...
      LTOPasses.add(llvm::createConstantMergePass());

      // Remove unused arguments from functions...
      LTOPasses.add(llvm::createDeadArgEliminationPass());

      // Reduce the code after globalopt and ipsccp. Both can open up
      // significant simplification opportunities, and both can propagate
      // functions through function pointers. When this happens, we often have
      // to resolve varargs calls, etc, so let instcombine do this.
      LTOPasses.add(llvm::createInstructionCombiningPass());

      // Inline small functions
      LTOPasses.add(llvm::createFunctionInliningPass());

      // Remove dead EH info.
      LTOPasses.add(llvm::createPruneEHPass());

      // Internalize the globals again after inlining
      LTOPasses.add(llvm::createGlobalOptimizerPass());

      // Remove dead functions.
      LTOPasses.add(llvm::createGlobalDCEPass());

      // If we didn't decide to inline a function, check to see if we can
      // transform it to pass arguments by value instead of by reference.
      LTOPasses.add(llvm::createArgumentPromotionPass());

      // The IPO passes may leave cruft around.  Clean up after them.
      LTOPasses.add(llvm::createInstructionCombiningPass());
      LTOPasses.add(llvm::createJumpThreadingPass());

      // Break up allocas
      LTOPasses.add(llvm::createScalarReplAggregatesPass());

      // Run a few AA driven optimizations here and now, to cleanup the code.
      LTOPasses.add(llvm::createFunctionAttrsPass());  // Add nocapture.
      LTOPasses.add(llvm::createGlobalsModRefPass());  // IP alias analysis.

      // Hoist loop invariants.
      LTOPasses.add(llvm::createLICMPass());

      // Remove redundancies.
      LTOPasses.add(llvm::createGVNPass());

      // Remove dead memcpys.
      LTOPasses.add(llvm::createMemCpyOptPass());

      // Nuke dead stores.
      LTOPasses.add(llvm::createDeadStoreEliminationPass());

      // Cleanup and simplify the code after the scalar optimizations.
      LTOPasses.add(llvm::createInstructionCombiningPass());

      LTOPasses.add(llvm::createJumpThreadingPass());

      // Delete basic blocks, which optimization passes may have killed.
      LTOPasses.add(llvm::createCFGSimplificationPass());

      // Now that we have optimized the program, discard unreachable functions.
      LTOPasses.add(llvm::createGlobalDCEPass());

      LTOPasses.run(*mModule);
    }

    // Create code-gen pass to run the code emitter
    CodeGenPasses = new llvm::FunctionPassManager(mModule);
    CodeGenPasses->add(TD);  // Will take the ownership of TD

    if (TM->addPassesToEmitMachineCode(*CodeGenPasses,
                                       *mCodeEmitter,
                                       CodeGenOptLevel)) {
      setError("The machine code emission is not supported by BCC on target '"
               + Triple + "'");
      goto on_bcc_compile_error;
    }

    // Run the pass (the code emitter) on every non-declaration function in the
    // module
    CodeGenPasses->doInitialization();
    for (llvm::Module::iterator I = mModule->begin(), E = mModule->end();
         I != E;
         I++)
      if (!I->isDeclaration())
        CodeGenPasses->run(*I);

    CodeGenPasses->doFinalization();

    // Copy the global address mapping from code emitter and remapping
    if (ExportVarMetadata) {
      for (int i = 0, e = ExportVarMetadata->getNumOperands(); i != e; i++) {
        llvm::MDNode *ExportVar = ExportVarMetadata->getOperand(i);
        if (ExportVar != NULL && ExportVar->getNumOperands() > 1) {
          llvm::Value *ExportVarNameMDS = ExportVar->getOperand(0);
          if (ExportVarNameMDS->getValueID() == llvm::Value::MDStringVal) {
            llvm::StringRef ExportVarName =
                static_cast<llvm::MDString*>(ExportVarNameMDS)->getString();
            CodeEmitter::global_addresses_const_iterator I, E;
            for (I = mCodeEmitter->global_address_begin(),
                    E = mCodeEmitter->global_address_end();
                 I != E;
                 I++) {
              if (I->first->getValueID() != llvm::Value::GlobalVariableVal)
                continue;
              if (ExportVarName == I->first->getName()) {
                mExportVars.push_back(I->second);
                break;
              }
            }
            if (I != mCodeEmitter->global_address_end())
              continue;  // found
          }
        }
        // if reaching here, we know the global variable record in metadata is
        // not found. So we make an empty slot
        mExportVars.push_back(NULL);
      }
      assert((mExportVars.size() == ExportVarMetadata->getNumOperands()) &&
              "Number of slots doesn't match the number of export variables!");
    }

    if (ExportFuncMetadata) {
      for (int i = 0, e = ExportFuncMetadata->getNumOperands(); i != e; i++) {
        llvm::MDNode *ExportFunc = ExportFuncMetadata->getOperand(i);
        if (ExportFunc != NULL && ExportFunc->getNumOperands() > 0) {
          llvm::Value *ExportFuncNameMDS = ExportFunc->getOperand(0);
          if (ExportFuncNameMDS->getValueID() == llvm::Value::MDStringVal) {
            llvm::StringRef ExportFuncName =
                static_cast<llvm::MDString*>(ExportFuncNameMDS)->getString();
            mExportFuncs.push_back(mCodeEmitter->lookup(ExportFuncName));
          }
        }
      }
    }

    // Tell code emitter now can release the memory using during the JIT since
    // we have done the code emission
    mCodeEmitter->releaseUnnecessary();

    // Finally, read pragma information from the metadata node of the @Module if
    // any.
    if (PragmaMetadata)
      for (int i = 0, e = PragmaMetadata->getNumOperands(); i != e; i++) {
        llvm::MDNode *Pragma = PragmaMetadata->getOperand(i);
        if (Pragma != NULL &&
           Pragma->getNumOperands() == 2 /* should have exactly 2 operands */) {
          llvm::Value *PragmaNameMDS = Pragma->getOperand(0);
          llvm::Value *PragmaValueMDS = Pragma->getOperand(1);

          if ((PragmaNameMDS->getValueID() == llvm::Value::MDStringVal) &&
             (PragmaValueMDS->getValueID() == llvm::Value::MDStringVal)) {
            llvm::StringRef PragmaName =
                static_cast<llvm::MDString*>(PragmaNameMDS)->getString();
            llvm::StringRef PragmaValue =
                static_cast<llvm::MDString*>(PragmaValueMDS)->getString();

            mPragmas.push_back(
                std::make_pair(std::string(PragmaName.data(),
                                           PragmaName.size()),
                               std::string(PragmaValue.data(),
                                           PragmaValue.size())));
          }
        }
      }

 on_bcc_compile_error:
    // LOGE("on_bcc_compiler_error");
    if (CodeGenPasses) {
      delete CodeGenPasses;
    } else if (TD) {
      delete TD;
    }
    if (TM)
      delete TM;

    if (mError.empty()) {
#if !USE_RELOCATE
      if (mUseCache && mCacheFd >= 0 && mCacheNew) {
        genCacheFile();
        flock(mCacheFd, LOCK_UN);
      }
#else
      // TODO(sliao)
#endif
      return false;
    }

    // LOGE(getErrorMessage());
    return true;
  }

  // interface for bccGetScriptInfoLog()
  char *getErrorMessage() {
    return const_cast<char*>(mError.c_str());
  }

  // interface for bccGetScriptLabel()
  void *lookup(const char *name) {
    void *addr = NULL;
    if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
      if (!strcmp(name, "root")) {
        addr = reinterpret_cast<void *>(mCacheHdr->rootAddr);
      } else if (!strcmp(name, "init")) {
        addr = reinterpret_cast<void *>(mCacheHdr->initAddr);
      }
      return addr;
    }

    if (mCodeEmitter.get())
      // Find function pointer
      addr = mCodeEmitter->lookup(name);
    return addr;
  }

  // Interface for bccGetExportVars()
  void getExportVars(BCCsizei *actualVarCount,
                     BCCsizei maxVarCount,
                     BCCvoid **vars) {
    int varCount;

    if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
      varCount = static_cast<int>(mCacheHdr->exportVarsCount);
      if (actualVarCount)
        *actualVarCount = varCount;
      if (varCount > maxVarCount)
        varCount = maxVarCount;
      if (vars) {
        uint32_t *cachedVars = (uint32_t *)(mCacheMapAddr +
                                            mCacheHdr->exportVarsOffset);

        for (int i = 0; i < varCount; i++) {
          *vars++ = (BCCvoid *)(reinterpret_cast<char *>(*cachedVars) +
                                mCacheDiff);
          cachedVars++;
        }
      }
      return;
    }

    varCount = mExportVars.size();
    if (actualVarCount)
      *actualVarCount = varCount;
    if (varCount > maxVarCount)
      varCount = maxVarCount;
    if (vars) {
      for (ExportVarList::const_iterator I = mExportVars.begin(),
              E = mExportVars.end();
           I != E;
           I++) {
        *vars++ = *I;
      }
    }

    return;
  }

  // Interface for bccGetExportFuncs()
  void getExportFuncs(BCCsizei *actualFuncCount,
                      BCCsizei maxFuncCount,
                      BCCvoid **funcs) {
    int funcCount;

    if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
      funcCount = static_cast<int>(mCacheHdr->exportFuncsCount);
      if (actualFuncCount)
        *actualFuncCount = funcCount;
      if (funcCount > maxFuncCount)
        funcCount = maxFuncCount;
      if (funcs) {
        uint32_t *cachedFuncs = (uint32_t *)(mCacheMapAddr +
                                             mCacheHdr->exportFuncsOffset);

        for (int i = 0; i < funcCount; i++) {
          *funcs++ = (BCCvoid *)(reinterpret_cast<char *>(*cachedFuncs) +
                                 mCacheDiff);
          cachedFuncs++;
        }
      }
      return;
    }

    funcCount = mExportFuncs.size();
    if (actualFuncCount)
      *actualFuncCount = funcCount;
    if (funcCount > maxFuncCount)
      funcCount = maxFuncCount;
    if (funcs) {
      for (ExportFuncList::const_iterator I = mExportFuncs.begin(),
              E = mExportFuncs.end();
           I != E;
           I++) {
        *funcs++ = *I;
      }
    }

    return;
  }

  // Interface for bccGetPragmas()
  void getPragmas(BCCsizei *actualStringCount,
                  BCCsizei maxStringCount,
                  BCCchar **strings) {
    int stringCount;
    if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
      if (actualStringCount)
        *actualStringCount = 0;  // XXX
      return;
    }

    stringCount = mPragmas.size() * 2;

    if (actualStringCount)
      *actualStringCount = stringCount;
    if (stringCount > maxStringCount)
      stringCount = maxStringCount;
    if (strings) {
      for (PragmaList::const_iterator it = mPragmas.begin();
           stringCount > 0;
           stringCount -= 2, it++) {
        *strings++ = const_cast<BCCchar*>(it->first.c_str());
        *strings++ = const_cast<BCCchar*>(it->second.c_str());
      }
    }

    return;
  }

  // Interface for bccGetFunctions()
  void getFunctions(BCCsizei *actualFunctionCount,
                 BCCsizei maxFunctionCount,
                 BCCchar **functions) {
    if (mCodeEmitter.get())
      mCodeEmitter->getFunctionNames(actualFunctionCount,
                                     maxFunctionCount,
                                     functions);
    else
      *actualFunctionCount = 0;

    return;
  }

  // Interface for bccGetFunctionBinary()
  void getFunctionBinary(BCCchar *function,
                         BCCvoid **base,
                         BCCsizei *length) {
    if (mCodeEmitter.get()) {
      mCodeEmitter->getFunctionBinary(function, base, length);
    } else {
      *base = NULL;
      *length = 0;
    }
    return;
  }

  inline const llvm::Module *getModule() const {
    return mModule;
  }

  ~Compiler() {
    if (!mCodeMemMgr.get()) {
      // mCodeDataAddr and mCacheMapAddr are from loadCacheFile and not
      // managed by CodeMemoryManager.

      if (mCodeDataAddr != 0 && mCodeDataAddr != MAP_FAILED) {
        if (munmap(mCodeDataAddr, MaxCodeSize + MaxGlobalVarSize) < 0) {
          LOGE("munmap failed while releasing mCodeDataAddr\n");
        }

        mCodeDataAddr = 0;
      }

      if (mCacheMapAddr) {
        free(mCacheMapAddr);
        mCacheMapAddr = 0;
      }
    }

    delete mModule;
    // llvm::llvm_shutdown();
    delete mContext;
    return;
  }

 private:
  // Note: loadCacheFile() and genCacheFile() go hand in hand
  void genCacheFile() {
    if (lseek(mCacheFd, 0, SEEK_SET) != 0) {
      LOGE("Unable to seek to 0: %s\n", strerror(errno));
      return;
    }

    bool codeOffsetNeedPadding = false;

    uint32_t offset = sizeof(oBCCHeader);

    // BCC Cache File Header
    oBCCHeader *hdr = (oBCCHeader *)malloc(sizeof(oBCCHeader));

    if (!hdr) {
      LOGE("Unable to allocate oBCCHeader.\n");
      return;
    }

    // Magic Words
    memcpy(hdr->magic, OBCC_MAGIC, 4);
    memcpy(hdr->magicVersion, OBCC_MAGIC_VERS, 4);

    // Timestamp
    hdr->sourceWhen = 0; // TODO(all)
    hdr->rslibWhen = 0; // TODO(all)
    hdr->libRSWhen = 0; // TODO(all)
    hdr->libbccWhen = 0; // TODO(all)

    // Current Memory Address (Saved for Recalculation)
    hdr->cachedCodeDataAddr = reinterpret_cast<uint32_t>(mCodeDataAddr);
    hdr->rootAddr = reinterpret_cast<uint32_t>(lookup("root"));
    hdr->initAddr = reinterpret_cast<uint32_t>(lookup("init"));

    // Relocation Table Offset and Entry Count
    hdr->relocOffset = sizeof(oBCCHeader);
    hdr->relocCount = mCodeEmitter->getCachingRelocations().size();

    offset += hdr->relocCount * (sizeof(oBCCRelocEntry));

    // Export Variable Table Offset and Entry Count
    hdr->exportVarsOffset = offset;
    hdr->exportVarsCount = mExportVars.size();

    offset += hdr->exportVarsCount * sizeof(uint32_t);

    // Export Function Table Offset and Entry Count
    hdr->exportFuncsOffset = offset;
    hdr->exportFuncsCount = mExportFuncs.size();

    offset += hdr->exportFuncsCount * sizeof(uint32_t);

    // Export Pragmas Table Offset and Entry Count
    hdr->exportPragmasOffset = offset;
    hdr->exportPragmasCount = 0; // TODO(all): mPragmas.size();

    offset += hdr->exportPragmasCount * sizeof(uint32_t);

    // Code Offset and Size

    //#ifdef BCC_CODE_ADDR
    {  // Always pad to the page boundary for now
      long pagesize = sysconf(_SC_PAGESIZE);

      if (offset % pagesize > 0) {
        codeOffsetNeedPadding = true;
        offset += pagesize - (offset % pagesize);
      }
    }
    /*#else
    if (offset & 0x07) { // Ensure that offset aligned to 64-bit (8 byte).
      codeOffsetNeedPadding = true;
      offset += 0x08 - (offset & 0x07);
    }
    #endif*/

    hdr->codeOffset = offset;
    hdr->codeSize = MaxCodeSize;

    offset += hdr->codeSize;

    // Data (Global Variable) Offset and Size
    hdr->dataOffset = offset;
    hdr->dataSize = MaxGlobalVarSize;

    offset += hdr->dataSize;

    // Checksum
    hdr->checksum = 0; // Set Field checksum. TODO(all)

    // Write Header
    sysWriteFully(mCacheFd, reinterpret_cast<char const *>(hdr),
                  sizeof(oBCCHeader), "Write oBCC header");

    // Write Relocation Entry Table
    {
      size_t allocSize = hdr->relocCount * sizeof(oBCCRelocEntry);

      oBCCRelocEntry const*records = &mCodeEmitter->getCachingRelocations()[0];

      sysWriteFully(mCacheFd, reinterpret_cast<char const *>(records),
                    allocSize, "Write Relocation Entries");
    }

    // Write Export Variables Table
    {
      uint32_t *record, *ptr;

      record = (uint32_t *)calloc(hdr->exportVarsCount, sizeof(uint32_t));
      ptr = record;

      if (!record) {
        goto bail;
      }

      for (ExportVarList::const_iterator I = mExportVars.begin(),
               E = mExportVars.end(); I != E; I++) {
        *ptr++ = reinterpret_cast<uint32_t>(*I);
      }

      sysWriteFully(mCacheFd, reinterpret_cast<char const *>(record),
                    hdr->exportVarsCount * sizeof(uint32_t),
                    "Write ExportVars");

      free(record);
    }

    // Write Export Functions Table
    {
      uint32_t *record, *ptr;

      record = (uint32_t *)calloc(hdr->exportFuncsCount, sizeof(uint32_t));
      ptr = record;

      if (!record) {
        goto bail;
      }

      for (ExportFuncList::const_iterator I = mExportFuncs.begin(),
               E = mExportFuncs.end(); I != E; I++) {
        *ptr++ = reinterpret_cast<uint32_t>(*I);
      }

      sysWriteFully(mCacheFd, reinterpret_cast<char const *>(record),
                    hdr->exportFuncsCount * sizeof(uint32_t),
                    "Write ExportFuncs");

      free(record);
    }


    // TODO(all): Write Export Pragmas Table
#if 0
#else
    // Note: As long as we have comment out export pragmas table code,
    // we have to seek the position to correct offset.

    lseek(mCacheFd, hdr->codeOffset, SEEK_SET);
#endif

    if (codeOffsetNeedPadding) {
      // requires additional padding
      lseek(mCacheFd, hdr->codeOffset, SEEK_SET);
    }

    // Write Generated Code and Global Variable
    sysWriteFully(mCacheFd, mCodeDataAddr, MaxCodeSize + MaxGlobalVarSize,
                  "Write code and global variable");

    goto close_return;

  bail:
    if (ftruncate(mCacheFd, 0) != 0) {
      LOGW("Warning: unable to truncate cache file: %s\n", strerror(errno));
    }

  close_return:
    free(hdr);
    close(mCacheFd);
    mCacheFd = -1;
    return;
  }

  // OpenCacheFile() returns fd of the cache file.
  // Input:
  //   BCCchar *resName: Used to genCacheFileName()
  //   bool createIfMissing: If false, turn off caching
  // Output:
  //   returns fd: If -1: Failed
  //   mCacheNew: If true, the returned fd is new. Otherwise, the fd is the
  //              cache file's file descriptor
  //              Note: openCacheFile() will check the cache file's validity,
  //              such as Magic number, sourceWhen... dependencies.
  int openCacheFile(const BCCchar *resName, bool createIfMissing) {
    int fd, cc;
    struct stat fdStat, fileStat;
    bool readOnly = false;

    char *cacheFileName = genCacheFileName(resName, ".oBCC");

    mCacheNew = false;

 retry:
    /*
     * Try to open the cache file.  If we've been asked to,
     * create it if it doesn't exist.
     */
    fd = createIfMissing ? open(cacheFileName, O_CREAT|O_RDWR, 0644) : -1;
    if (fd < 0) {
      fd = open(cacheFileName, O_RDONLY, 0);
      if (fd < 0) {
        if (createIfMissing) {
          LOGW("Can't open bcc-cache '%s': %s\n",
               cacheFileName, strerror(errno));
          mUseCache = false;
        }
        return fd;
      }
      readOnly = true;
    }

    /*
     * Grab an exclusive lock on the cache file.  If somebody else is
     * working on it, we'll block here until they complete.
     */
    LOGV("bcc: locking cache file %s (fd=%d, boot=%d)\n",
         cacheFileName, fd);

    cc = flock(fd, LOCK_EX | LOCK_NB);
    if (cc != 0) {
      LOGD("bcc: sleeping on flock(%s)\n", cacheFileName);
      cc = flock(fd, LOCK_EX);
    }

    if (cc != 0) {
      LOGE("Can't lock bcc cache '%s': %d\n", cacheFileName, cc);
      close(fd);
      return -1;
    }
    LOGV("bcc:  locked cache file\n");

    /*
     * Check to see if the fd we opened and locked matches the file in
     * the filesystem.  If they don't, then somebody else unlinked ours
     * and created a new file, and we need to use that one instead.  (If
     * we caught them between the unlink and the create, we'll get an
     * ENOENT from the file stat.)
     */
    cc = fstat(fd, &fdStat);
    if (cc != 0) {
      LOGE("Can't stat open file '%s'\n", cacheFileName);
      LOGV("bcc: unlocking cache file %s\n", cacheFileName);
      goto close_fail;
    }
    cc = stat(cacheFileName, &fileStat);
    if (cc != 0 ||
        fdStat.st_dev != fileStat.st_dev || fdStat.st_ino != fileStat.st_ino) {
      LOGD("bcc: our open cache file is stale; sleeping and retrying\n");
      LOGV("bcc: unlocking cache file %s\n", cacheFileName);
      flock(fd, LOCK_UN);
      close(fd);
      usleep(250 * 1000);     // if something is hosed, don't peg machine
      goto retry;
    }

    /*
     * We have the correct file open and locked.  If the file size is zero,
     * then it was just created by us, and we want to fill in some fields
     * in the "bcc" header and set "mCacheNew".  Otherwise, we want to
     * verify that the fields in the header match our expectations, and
     * reset the file if they don't.
     */
    if (fdStat.st_size == 0) {
      if (readOnly) {  // The device is readOnly --> close_fail
        LOGW("bcc: file has zero length and isn't writable\n");
        goto close_fail;
      }
      /*cc = createEmptyHeader(fd);
      if (cc != 0)
        goto close_fail;
      */
      mCacheNew = true;
      LOGV("bcc: successfully initialized new cache file\n");
    } else {
      // Calculate sourceWhen
      // XXX
      uint32_t sourceWhen = 0;
      uint32_t rslibWhen = 0;
      uint32_t libRSWhen = 0;
      uint32_t libbccWhen = 0;
      if (!checkHeaderAndDependencies(fd,
                                      sourceWhen,
                                      rslibWhen,
                                      libRSWhen,
                                      libbccWhen)) {
        // If checkHeaderAndDependencies returns 0: FAILED
        // Will truncate the file and retry to createIfMissing the file

        if (readOnly) {  // Shouldn't be readonly.
          /*
           * We could unlink and rewrite the file if we own it or
           * the "sticky" bit isn't set on the directory.  However,
           * we're not able to truncate it, which spoils things.  So,
           * give up now.
           */
          if (createIfMissing) {
            LOGW("Cached file %s is stale and not writable\n",
                 cacheFileName);
          }
          goto close_fail;
        }

        /*
         * If we truncate the existing file before unlinking it, any
         * process that has it mapped will fail when it tries to touch
         * the pages? Probably OK because we use MAP_PRIVATE.
         */
        LOGD("oBCC file is stale or bad; removing and retrying (%s)\n",
             cacheFileName);
        if (ftruncate(fd, 0) != 0) {
          LOGW("Warning: unable to truncate cache file '%s': %s\n",
               cacheFileName, strerror(errno));
          /* keep going */
        }
        if (unlink(cacheFileName) != 0) {
          LOGW("Warning: unable to remove cache file '%s': %d %s\n",
               cacheFileName, errno, strerror(errno));
          /* keep going; permission failure should probably be fatal */
        }
        LOGV("bcc: unlocking cache file %s\n", cacheFileName);
        flock(fd, LOCK_UN);
        close(fd);
        goto retry;
      } else {
        // Got cacheFile! Good to go.
        LOGV("Good cache file\n");
      }
    }

    assert(fd >= 0);
    return fd;

 close_fail:
    flock(fd, LOCK_UN);
    close(fd);
    return -1;
  }  // End of openCacheFile()

  char *genCacheFileName(const char *fileName, const char *subFileName) {
    char nameBuf[512];
    static const char kCachePath[] = "bcc-cache";
    char absoluteFile[sizeof(nameBuf)];
    const size_t kBufLen = sizeof(nameBuf) - 1;
    const char *dataRoot;
    char *cp;

    // Get the absolute path of the raw/***.bc file.
    absoluteFile[0] = '\0';
    if (fileName[0] != '/') {
      /*
       * Generate the absolute path.  This doesn't do everything it
       * should, e.g. if filename is "./out/whatever" it doesn't crunch
       * the leading "./" out, but it'll do.
       */
      if (getcwd(absoluteFile, kBufLen) == NULL) {
        LOGE("Can't get CWD while opening raw/***.bc file\n");
        return NULL;
      }
      // TODO(srhines): strncat() is a bit dangerous
      strncat(absoluteFile, "/", kBufLen);
    }
    strncat(absoluteFile, fileName, kBufLen);

    if (subFileName != NULL) {
      strncat(absoluteFile, "/", kBufLen);
      strncat(absoluteFile, subFileName, kBufLen);
    }

    /* Turn the path into a flat filename by replacing
     * any slashes after the first one with '@' characters.
     */
    cp = absoluteFile + 1;
    while (*cp != '\0') {
      if (*cp == '/') {
        *cp = '@';
      }
      cp++;
    }

    /* Build the name of the cache directory.
     */
    dataRoot = getenv("ANDROID_DATA");
    if (dataRoot == NULL)
      dataRoot = "/data";
    snprintf(nameBuf, kBufLen, "%s/%s", dataRoot, kCachePath);

    /* Tack on the file name for the actual cache file path.
     */
    strncat(nameBuf, absoluteFile, kBufLen);

    LOGV("Cache file for '%s' '%s' is '%s'\n", fileName, subFileName, nameBuf);
    return strdup(nameBuf);
  }

  /*
   * Read the oBCC header, verify it, then read the dependent section
   * and verify that data as well.
   *
   * On successful return, the file will be seeked immediately past the
   * oBCC header.
   */
  bool checkHeaderAndDependencies(int fd,
                                  uint32_t sourceWhen,
                                  uint32_t rslibWhen,
                                  uint32_t libRSWhen,
                                  uint32_t libbccWhen) {
    ssize_t actual;
    oBCCHeader optHdr;
    uint32_t val;
    uint8_t const *magic, *magicVer;

    /*
     * Start at the start.  The "bcc" header, when present, will always be
     * the first thing in the file.
     */
    if (lseek(fd, 0, SEEK_SET) != 0) {
      LOGE("bcc: failed to seek to start of file: %s\n", strerror(errno));
      goto bail;
    }

    /*
     * Read and do trivial verification on the bcc header.  The header is
     * always in host byte order.
     */
    actual = read(fd, &optHdr, sizeof(optHdr));
    if (actual < 0) {
      LOGE("bcc: failed reading bcc header: %s\n", strerror(errno));
      goto bail;
    } else if (actual != sizeof(optHdr)) {
      LOGE("bcc: failed reading bcc header (got %d of %zd)\n",
           (int) actual, sizeof(optHdr));
      goto bail;
    }

    magic = optHdr.magic;
    if (memcmp(magic, OBCC_MAGIC, 4) != 0) {
      /* not an oBCC file, or previous attempt was interrupted */
      LOGD("bcc: incorrect opt magic number (0x%02x %02x %02x %02x)\n",
           magic[0], magic[1], magic[2], magic[3]);
      goto bail;
    }

    magicVer = optHdr.magicVersion;
    if (memcmp(magic+4, OBCC_MAGIC_VERS, 4) != 0) {
      LOGW("bcc: stale oBCC version (0x%02x %02x %02x %02x)\n",
           magicVer[0], magicVer[1], magicVer[2], magicVer[3]);
      goto bail;
    }

    /*
     * Do the header flags match up with what we want?
     *
     * This is useful because it allows us to automatically regenerate
     * a file when settings change (e.g. verification is now mandatory),
     * but can cause difficulties if the thing we depend upon
     * were handled differently than the current options specify.
     *
     * So, for now, we essentially ignore "expectVerify" and "expectOpt"
     * by limiting the match mask.
     *
     * The only thing we really can't handle is incorrect byte-ordering.
     */

    val = optHdr.sourceWhen;
    if (val && (val != sourceWhen)) {
      LOGI("bcc: source file mod time mismatch (%08x vs %08x)\n",
           val, sourceWhen);
      goto bail;
    }
    val = optHdr.rslibWhen;
    if (val && (val != rslibWhen)) {
      LOGI("bcc: rslib file mod time mismatch (%08x vs %08x)\n",
           val, rslibWhen);
      goto bail;
    }
    val = optHdr.libRSWhen;
    if (val && (val != libRSWhen)) {
      LOGI("bcc: libRS file mod time mismatch (%08x vs %08x)\n",
           val, libRSWhen);
      goto bail;
    }
    val = optHdr.libbccWhen;
    if (val && (val != libbccWhen)) {
      LOGI("bcc: libbcc file mod time mismatch (%08x vs %08x)\n",
           val, libbccWhen);
      goto bail;
    }

    return true;

 bail:
    return false;
 }

};
// End of Class Compiler
////////////////////////////////////////////////////////////////////////////////


bool Compiler::GlobalInitialized = false;

char *Compiler::BccCodeAddr = BCC_CODE_ADDR;

// Code generation optimization level for the compiler
llvm::CodeGenOpt::Level Compiler::CodeGenOptLevel;

std::string Compiler::Triple;

std::string Compiler::CPU;

std::vector<std::string> Compiler::Features;

// The named of metadata node that pragma resides (should be synced with
// slang.cpp)
const llvm::StringRef Compiler::PragmaMetadataName = "#pragma";

// The named of metadata node that export variable name resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportVarMetadataName = "#rs_export_var";

// The named of metadata node that export function name resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportFuncMetadataName = "#rs_export_func";

struct BCCscript {
  //////////////////////////////////////////////////////////////////////////////
  // Part I. Compiler
  //////////////////////////////////////////////////////////////////////////////
  Compiler compiler;

  void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
    compiler.registerSymbolCallback(pFn, pContext);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part II. Logistics & Error handling
  //////////////////////////////////////////////////////////////////////////////
  BCCscript() {
    bccError = BCC_NO_ERROR;
  }

  ~BCCscript() {
  }

  void setError(BCCenum error) {
    if (bccError == BCC_NO_ERROR && error != BCC_NO_ERROR) {
      bccError = error;
    }
  }

  BCCenum getError() {
    BCCenum result = bccError;
    bccError = BCC_NO_ERROR;
    return result;
  }

  BCCenum bccError;
};


extern "C"
BCCscript *bccCreateScript() {
  return new BCCscript();
}

extern "C"
BCCenum bccGetError(BCCscript *script) {
  return script->getError();
}

extern "C"
void bccDeleteScript(BCCscript *script) {
  delete script;
}

extern "C"
void bccRegisterSymbolCallback(BCCscript *script,
                               BCCSymbolLookupFn pFn,
                               BCCvoid *pContext) {
  script->registerSymbolCallback(pFn, pContext);
}

extern "C"
int bccReadModule(BCCscript *script,
                     BCCvoid *module) {
  return script->compiler.readModule(reinterpret_cast<llvm::Module*>(module));
}

extern "C"
int bccReadBC(BCCscript *script,
              const BCCchar *bitcode,
              BCCint size,
              const BCCchar *resName) {
  return script->compiler.readBC(bitcode, size, resName);
}

extern "C"
void bccLinkBC(BCCscript *script,
                    const BCCchar *bitcode,
                    BCCint size) {
  script->compiler.linkBC(bitcode, size);
}

extern "C"
void bccLoadBinary(BCCscript *script) {
  int result = script->compiler.loadCacheFile();
  if (result)
    script->setError(BCC_INVALID_OPERATION);
}

extern "C"
void bccCompileBC(BCCscript *script) {
  {
#if defined(__arm__)
    android::StopWatch compileTimer("RenderScript compile time");
#endif
    int result = script->compiler.compile();
    if (result)
      script->setError(BCC_INVALID_OPERATION);
  }
}

extern "C"
void bccGetScriptInfoLog(BCCscript *script,
                         BCCsizei maxLength,
                         BCCsizei *length,
                         BCCchar *infoLog) {
  char *message = script->compiler.getErrorMessage();
  int messageLength = strlen(message) + 1;
  if (length)
    *length = messageLength;

  if (infoLog && maxLength > 0) {
    int trimmedLength = maxLength < messageLength ? maxLength : messageLength;
    memcpy(infoLog, message, trimmedLength);
    infoLog[trimmedLength] = 0;
  }
}

extern "C"
void bccGetScriptLabel(BCCscript *script,
                       const BCCchar  *name,
                       BCCvoid **address) {
  void *value = script->compiler.lookup(name);
  if (value)
    *address = value;
  else
    script->setError(BCC_INVALID_VALUE);
}

extern "C"
void bccGetExportVars(BCCscript *script,
                      BCCsizei *actualVarCount,
                      BCCsizei maxVarCount,
                      BCCvoid **vars) {
  script->compiler.getExportVars(actualVarCount, maxVarCount, vars);
}

extern "C"
void bccGetExportFuncs(BCCscript *script,
                       BCCsizei *actualFuncCount,
                       BCCsizei maxFuncCount,
                       BCCvoid **funcs) {
  script->compiler.getExportFuncs(actualFuncCount, maxFuncCount, funcs);
}

extern "C"
void bccGetPragmas(BCCscript *script,
                   BCCsizei *actualStringCount,
                   BCCsizei maxStringCount,
                   BCCchar **strings) {
  script->compiler.getPragmas(actualStringCount, maxStringCount, strings);
}

extern "C"
void bccGetFunctions(BCCscript *script,
                     BCCsizei *actualFunctionCount,
                     BCCsizei maxFunctionCount,
                     BCCchar **functions) {
  script->compiler.getFunctions(actualFunctionCount,
                                maxFunctionCount,
                                functions);
}

extern "C"
void bccGetFunctionBinary(BCCscript *script,
                          BCCchar *function,
                          BCCvoid **base,
                          BCCsizei *length) {
  script->compiler.getFunctionBinary(function, base, length);
}

struct BCCtype {
  const Compiler *compiler;
  const llvm::Type *t;
};

}  // namespace bcc
