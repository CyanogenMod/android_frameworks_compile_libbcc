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
#   define TARGET_TRIPLE_STRING "armv7-none-linux-gnueabi"
#elif defined(DEFAULT_X86_CODEGEN)
#   define TARGET_TRIPLE_STRING "i686-unknown-linux"
#elif defined(DEFAULT_X64_CODEGEN)
#   define TARGET_TRIPLE_STRING "x86_64-unknown-linux"
#endif

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
#   define ARM_USE_VFP
#endif

#include "Compiler.h"

#include "ContextManager.h"

#include "llvm/ADT/StringRef.h"

#include "llvm/Analysis/Passes.h"

#include "llvm/Bitcode/ReaderWriter.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"

#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/TargetSelect.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"

#include "llvm/GlobalValue.h"
#include "llvm/Linker.h"
#include "llvm/LLVMContext.h"
#include "llvm/Metadata.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Value.h"

#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cutils/properties.h>

#include <sha1.h>

#include <string>
#include <vector>

namespace {

#define TEMP_FAILURE_RETRY1(exp) ({        \
    typeof (exp) _rc;                      \
    do {                                   \
        _rc = (exp);                       \
    } while (_rc == -1 && errno == EINTR); \
    _rc; })


int sysWriteFully(int fd, const void* buf, size_t count, const char* logMsg) {
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

inline uint32_t statModifyTime(char const *filepath) {
  struct stat st;

  if (stat(filepath, &st) < 0) {
    LOGE("Unable to stat \'%s\', with reason: %s\n", filepath, strerror(errno));
    return 0;
  }

  return static_cast<uint32_t>(st.st_mtime);
}

static char const libRSPath[] = "/system/lib/libRS.so";

static char const libBccPath[] = "/system/lib/libbcc.so";

} // namespace anonymous


namespace bcc {

//////////////////////////////////////////////////////////////////////////////
// BCC Compiler Static Variables
//////////////////////////////////////////////////////////////////////////////

bool Compiler::GlobalInitialized = false;

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


//////////////////////////////////////////////////////////////////////////////
// Compiler
//////////////////////////////////////////////////////////////////////////////

void Compiler::GlobalInitialization() {
  if (GlobalInitialized)
    return;

  LOGI("LIBBCC BUILD: %s %s\n", __DATE__, __TIME__);

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
  CodeGenOptLevel = llvm::CodeGenOpt::Aggressive;

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
}


void Compiler::LLVMErrorHandler(void *UserData, const std::string &Message) {
  std::string *Error = static_cast<std::string*>(UserData);
  Error->assign(Message);
  LOGE("%s", Message.c_str());
  exit(1);
}


CodeMemoryManager *Compiler::createCodeMemoryManager() {
  mCodeMemMgr.reset(new CodeMemoryManager());
  return mCodeMemMgr.get();
}


CodeEmitter *Compiler::createCodeEmitter() {
  mCodeEmitter.reset(new CodeEmitter(mCodeMemMgr.get()));
  return mCodeEmitter.get();
}


Compiler::Compiler()
  : mResId(-1),
    mUseCache(false),
    mCacheNew(false),
    mCacheFd(-1),
    mCacheMapAddr(NULL),
    mCacheHdr(NULL),
    mCacheSize(0),
    mCacheDiff(0),
    mCacheLoadFailed(false),
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


static bool getProp(const char *str) {
    char buf[PROPERTY_VALUE_MAX];
    property_get(str, buf, "0");
    return 0 != strcmp(buf, "0");
}

// Compiler::readBC
// Parameters:
//   resName: NULL means don't use cache.
//   Note: If "cache-hit but bccLoadBinary fails for some reason", still
//         pass in the resName.
//         Rationale: So we may have future cache-hit.
//
int Compiler::readBC(const char *bitcode,
                     size_t bitcodeSize,
                     long bitcodeFileModTime,
                     long bitcodeFileCRC32,
                     const BCCchar *resName,
                     const BCCchar *cacheDir) {
  GlobalInitialization();

  this->props.mNoCache = getProp("debug.bcc.nocache");
  if (this->props.mNoCache) {
    resName = NULL;
  }

  // Compute the SHA1 hash of the input bitcode.  So that we can use the hash
  // to decide rather to update the cache or not.
  computeSourceSHA1(bitcode, bitcodeSize);

  if (resName && !mCacheLoadFailed) {
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

    mCacheFd = openCacheFile(resName, cacheDir, true /* createIfMissing */);
    if (mCacheFd >= 0 && !mCacheNew) {  // Just use cache file
      return -mCacheFd - 1;
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


int Compiler::linkBC(const char *bitcode, size_t bitcodeSize) {
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
int Compiler::loadCacheFile() {
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

  // Part 1. Deal with the non-codedata section first
  {
    // Read cached file and perform quick integrity check

    off_t heuristicCodeOffset = mCacheSize - MaxCodeSize - MaxGlobalVarSize;
    LOGW("@loadCacheFile: mCacheSize=%x, heuristicCodeOffset=%llx",
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
    LOGW("mCacheHdr->cachedCodeDataAddr=%x", mCacheHdr->cachedCodeDataAddr);
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
                     mCacheHdr->exportPragmasSize) {
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

    long pagesize = sysconf(_SC_PAGESIZE);
    if (mCacheHdr->codeOffset % pagesize != 0) {
      LOGE("code offset must aligned to pagesize\n");
      goto bail;
    }
  }

  // Part 2. Deal with the codedata section
  {
    long pagesize = sysconf(_SC_PAGESIZE);

    if (mCacheHdr->cachedCodeDataAddr % pagesize == 0) {
      char *addr = reinterpret_cast<char *>(mCacheHdr->cachedCodeDataAddr);

      // Try to allocate context (i.e., mmap) at cached address directly.
      mCodeDataAddr = allocateContext(addr, mCacheFd, mCacheHdr->codeOffset);
      // LOGI("mCodeDataAddr=%x", mCodeDataAddr);

      if (!mCodeDataAddr) {
        // Unable to allocate at cached address.  Give up.
        flock(mCacheFd, LOCK_UN);
        goto bail;
      }

      // Above: Already checked some cache-hit conditions:
      //        mCodeDataAddr && mCodeDataAddr != MAP_FAILED
      // Next: Check cache-hit conditions when USE_RELOCATE == false:
      //       mCodeDataAddr == addr
      //       (When USE_RELOCATE == true, we still don't need to relocate
      //        if mCodeDataAddr == addr. But if mCodeDataAddr != addr,
      //        that means "addr" is taken by previous mmap and we need
      //        to relocate the cache file content to mcodeDataAddr.)

#if !USE_RELOCATE
      if (mCodeDataAddr != addr) {
        flock(mCacheFd, LOCK_UN);
        goto bail;
      }
#else     // USE_RELOCATE == true

#endif    // End of #if #else !USE_RELOCATE

      // Check the checksum of code and data
      {
        uint32_t sum = mCacheHdr->checksum;
        uint32_t *ptr = (uint32_t *)mCodeDataAddr;

        for (size_t i = 0; i < BCC_CONTEXT_SIZE / sizeof(uint32_t); ++i) {
          sum ^= *ptr++;
        }

        if (sum != 0) {
          LOGE("Checksum check failed\n");
          goto bail;
        }

        LOGI("Passed checksum even parity verification.\n");
      }

      flock(mCacheFd, LOCK_UN);
      return 0; // loadCacheFile succeed!
    }  // End of "if(mCacheHdr->cachedCodeDataAddr % pagesize == 0)"
  }  // End of Part 2. Deal with the codedata section

// Execution will reach here only if (mCacheHdr->cachedCodeDataAddr % pagesize != 0)

#if !USE_RELOCATE
  // Note: If Android.mk set USE_RELOCATE to false, we are not allowed to
  // relocate to the new mCodeDataAddr. That is, we got no
  // choice but give up this cache-hit case: go ahead and recompile the code

  flock(mCacheFd, LOCK_UN);
  goto bail;

#else

  // Note: Currently, relocation code is not working.  Give up now.
  flock(mCacheFd, LOCK_UN);
  goto bail;

  // The following code is not working. Don't use them.
  // Rewrite them asap.
#if 0
  {
    // Try to allocate at arbitary address.  And perform relocation.
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

    // Relocate
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

    return 0; // Success!
  }
#endif  // End of #if 0

#endif  // End of #if #else USE_RELOCATE

bail:
  if (mCacheMapAddr) {
    free(mCacheMapAddr);
  }

  if (mCodeDataAddr) {
    deallocateContext(mCodeDataAddr);
  }

  mCacheMapAddr = NULL;
  mCacheHdr = NULL;
  mCodeDataAddr = NULL;

giveup:
  if (mCacheFd >= 0) {
    close(mCacheFd);
    mCacheFd = -1;
  }

  mUseCache = false;
  mCacheLoadFailed = true;

  return 1;
}

// interace for bccCompileBC()
int Compiler::compile() {
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

    for (std::vector<std::string>::const_iterator
         I = Features.begin(), E = Features.end(); I != E; I++) {
      F.AddFeature(*I);
    }

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
       I != E; I++) {
    if (!I->isDeclaration()) {
      CodeGenPasses->run(*I);
    }
  }

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
               I != E; I++) {
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
    if (mUseCache && mCacheFd >= 0 && mCacheNew) {
      genCacheFile();
      //LOGI("DONE generating cache file");  //sliao
      flock(mCacheFd, LOCK_UN);
    }

    return false;
  }

  // LOGE(getErrorMessage());
  return true;
}


// interface for bccGetScriptLabel()
void *Compiler::lookup(const char *name) {
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
void Compiler::getExportVars(BCCsizei *actualVarCount,
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
        *vars = (BCCvoid *)((char *)(*cachedVars) + mCacheDiff);
        vars++;
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
    for (ExportVarList::const_iterator
         I = mExportVars.begin(), E = mExportVars.end(); I != E; I++) {
      *vars++ = *I;
    }
  }
}


// Interface for bccGetExportFuncs()
void Compiler::getExportFuncs(BCCsizei *actualFuncCount,
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
        *funcs = (BCCvoid *)((char *)(*cachedFuncs) + mCacheDiff);
        funcs++;
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
    for (ExportFuncList::const_iterator
         I = mExportFuncs.begin(), E = mExportFuncs.end(); I != E; I++) {
      *funcs++ = *I;
    }
  }
}


// Interface for bccGetPragmas()
void Compiler::getPragmas(BCCsizei *actualStringCount,
                          BCCsizei maxStringCount,
                          BCCchar **strings) {
  int stringCount;

  if (mUseCache && mCacheFd >= 0 && !mCacheNew) {
    stringCount = static_cast<int>(mCacheHdr->exportPragmasCount) * 2;

    if (actualStringCount)
      *actualStringCount = stringCount;

    if (stringCount > maxStringCount)
      stringCount = maxStringCount;

    if (strings) {
      char *pragmaTab = mCacheMapAddr + mCacheHdr->exportPragmasOffset;

      oBCCPragmaEntry *cachedPragmaEntries = (oBCCPragmaEntry *)pragmaTab;

      for (int i = 0; stringCount >= 2; stringCount -= 2, i++) {
        *strings++ = pragmaTab + cachedPragmaEntries[i].pragmaNameOffset;
        *strings++ = pragmaTab + cachedPragmaEntries[i].pragmaValueOffset;
      }
    }

    return;
  }

  stringCount = mPragmas.size() * 2;

  if (actualStringCount)
    *actualStringCount = stringCount;
  if (stringCount > maxStringCount)
    stringCount = maxStringCount;
  if (strings) {
    size_t i = 0;
    for (PragmaList::const_iterator it = mPragmas.begin();
         stringCount >= 2; stringCount -= 2, it++, ++i) {
      *strings++ = const_cast<BCCchar*>(it->first.c_str());
      *strings++ = const_cast<BCCchar*>(it->second.c_str());
    }
  }

  return;
}


// Interface for bccGetFunctions()
void Compiler::getFunctions(BCCsizei *actualFunctionCount,
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
void Compiler::getFunctionBinary(BCCchar *function,
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


Compiler::~Compiler() {
  if (!mCodeMemMgr.get()) {
    // mCodeDataAddr and mCacheMapAddr are from loadCacheFile and not
    // managed by CodeMemoryManager.
    LOGI("~Compiler(): mCodeDataAddr = %p\n", mCodeDataAddr); //sliao
    if (mCodeDataAddr) {
      deallocateContext(mCodeDataAddr);
    }

    if (mCacheMapAddr) {
      free(mCacheMapAddr);
    }

    mCodeDataAddr = 0;
    mCacheMapAddr = 0;
  }

  delete mModule;
  delete mContext;

  // llvm::llvm_shutdown();
}


void Compiler::computeSourceSHA1(char const *bitcode, size_t bitcodeSize) {
  SHA1_CTX hashContext;

  SHA1Init(&hashContext);
  SHA1Update(&hashContext,
             reinterpret_cast<const unsigned char *>(bitcode),
             static_cast<unsigned long>(bitcodeSize));
  SHA1Final(mSourceSHA1, &hashContext);

}


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

// Note: loadCacheFile() and genCacheFile() go hand in hand
void Compiler::genCacheFile() {
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
  hdr->sourceWhen = 0; // TODO(sliao)
  hdr->rslibWhen = 0; // TODO(sliao)
  hdr->libRSWhen = statModifyTime(libRSPath);
  hdr->libbccWhen = statModifyTime(libBccPath);

  // Copy the hash checksum
  memcpy(hdr->sourceSHA1, mSourceSHA1, 20);

  // Current Memory Address (Saved for Recalculation)
  hdr->cachedCodeDataAddr = reinterpret_cast<uint32_t>(mCodeDataAddr);
  hdr->rootAddr = reinterpret_cast<uint32_t>(lookup("root"));
  hdr->initAddr = reinterpret_cast<uint32_t>(lookup("init"));

  // Check libRS isThreadable
  if (!mCodeEmitter) {
    hdr->libRSThreadable = 0;
  } else {
    hdr->libRSThreadable =
      (uint32_t) mCodeEmitter->mpSymbolLookupFn(mpSymbolLookupContext,
                                                "__isThreadable");
  }

  // Relocation Table Offset and Entry Count
  hdr->relocOffset = sizeof(oBCCHeader);
  hdr->relocCount = mCodeEmitter->getCachingRelocations().size();

  offset += hdr->relocCount * sizeof(oBCCRelocEntry);

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
  hdr->exportPragmasCount = mPragmas.size();
  hdr->exportPragmasSize = hdr->exportPragmasCount * sizeof(oBCCPragmaEntry);

  offset += hdr->exportPragmasCount * sizeof(oBCCPragmaEntry);

  for (PragmaList::const_iterator
       I = mPragmas.begin(), E = mPragmas.end(); I != E; ++I) {
    offset += I->first.size() + 1;
    offset += I->second.size() + 1;
    hdr->exportPragmasSize += I->first.size() + I->second.size() + 2;
  }

  // Code Offset and Size

  {  // Always pad to the page boundary for now
    long pagesize = sysconf(_SC_PAGESIZE);

    if (offset % pagesize > 0) {
      codeOffsetNeedPadding = true;
      offset += pagesize - (offset % pagesize);
    }
  }

  hdr->codeOffset = offset;
  hdr->codeSize = MaxCodeSize;

  offset += hdr->codeSize;

  // Data (Global Variable) Offset and Size
  hdr->dataOffset = offset;
  hdr->dataSize = MaxGlobalVarSize;

  offset += hdr->dataSize;

  // Checksum
#if 1
  {
    // Note: This is an simple checksum implementation that are using xor
    // to calculate even parity (for code and data only).

    uint32_t sum = 0;
    uint32_t *ptr = (uint32_t *)mCodeDataAddr;

    for (size_t i = 0; i < BCC_CONTEXT_SIZE / sizeof(uint32_t); ++i) {
      sum ^= *ptr++;
    }

    hdr->checksum = sum;
  }
#else
  hdr->checksum = 0; // Set Field checksum. TODO(all)
#endif

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


  // Write Export Pragmas Table
  {
    uint32_t pragmaEntryOffset =
      hdr->exportPragmasCount * sizeof(oBCCPragmaEntry);

    for (PragmaList::const_iterator
         I = mPragmas.begin(), E = mPragmas.end(); I != E; ++I) {
      oBCCPragmaEntry entry;

      entry.pragmaNameOffset = pragmaEntryOffset;
      entry.pragmaNameSize = I->first.size();
      pragmaEntryOffset += entry.pragmaNameSize + 1;

      entry.pragmaValueOffset = pragmaEntryOffset;
      entry.pragmaValueSize = I->second.size();
      pragmaEntryOffset += entry.pragmaValueSize + 1;

      sysWriteFully(mCacheFd, (char *)&entry, sizeof(oBCCPragmaEntry),
                    "Write export pragma entry");
    }

    for (PragmaList::const_iterator
         I = mPragmas.begin(), E = mPragmas.end(); I != E; ++I) {
      sysWriteFully(mCacheFd, I->first.c_str(), I->first.size() + 1,
                    "Write export pragma name string");
      sysWriteFully(mCacheFd, I->second.c_str(), I->second.size() + 1,
                    "Write export pragma value string");
    }
  }

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
int Compiler::openCacheFile(const BCCchar *resName,
                            const BCCchar *cacheDir,
                            bool createIfMissing) {
  int fd, cc;
  struct stat fdStat, fileStat;
  bool readOnly = false;

  char *cacheFileName = genCacheFileName(cacheDir, resName, ".oBCC");

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
    long sourceWhen = 0;
    uint32_t rslibWhen = 0;
    uint32_t libRSWhen = statModifyTime(libRSPath);
    uint32_t libbccWhen = statModifyTime(libBccPath);
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

// Input: cacheDir
// Input: resName
// Input: extName
//
// Note: cacheFile = resName + extName
//
// Output: Returns cachePath == cacheDir + cacheFile
char *Compiler::genCacheFileName(const char *cacheDir,
                                 const char *resName,
                                 const char *extName) {
  char cachePath[512];
  char cacheFile[sizeof(cachePath)];
  const size_t kBufLen = sizeof(cachePath) - 1;

  cacheFile[0] = '\0';
  // Note: resName today is usually something like
  //       "/com.android.fountain:raw/fountain"
  if (resName[0] != '/') {
    // Get the absolute path of the raw/***.bc file.

    // Generate the absolute path.  This doesn't do everything it
    // should, e.g. if resName is "./out/whatever" it doesn't crunch
    // the leading "./" out because this if-block is not triggered,
    // but it'll make do.
    //
    if (getcwd(cacheFile, kBufLen) == NULL) {
      LOGE("Can't get CWD while opening raw/***.bc file\n");
      return NULL;
    }
    // Append "/" at the end of cacheFile so far.
    strncat(cacheFile, "/", kBufLen);
  }

  // cacheFile = resName + extName
  //
  strncat(cacheFile, resName, kBufLen);
  if (extName != NULL) {
    // TODO(srhines): strncat() is a bit dangerous
    strncat(cacheFile, extName, kBufLen);
  }

  // Turn the path into a flat filename by replacing
  // any slashes after the first one with '@' characters.
  char *cp = cacheFile + 1;
  while (*cp != '\0') {
    if (*cp == '/') {
      *cp = '@';
    }
    cp++;
  }

  // Tack on the file name for the actual cache file path.
  strncpy(cachePath, cacheDir, kBufLen);
  strncat(cachePath, cacheFile, kBufLen);

  LOGV("Cache file for '%s' '%s' is '%s'\n", resName, extName, cachePath);
  return strdup(cachePath);
}

/*
 * Read the oBCC header, verify it, then read the dependent section
 * and verify that data as well.
 *
 * On successful return, the file will be seeked immediately past the
 * oBCC header.
 */
bool Compiler::checkHeaderAndDependencies(int fd,
                                          long sourceWhen,
                                          uint32_t rslibWhen,
                                          uint32_t libRSWhen,
                                          uint32_t libbccWhen) {
  oBCCHeader optHdr;

  // The header is guaranteed to be at the start of the cached file.
  // Seek to the start position.
  if (lseek(fd, 0, SEEK_SET) != 0) {
    LOGE("bcc: failed to seek to start of file: %s\n", strerror(errno));
    return false;
  }

  // Read and do trivial verification on the bcc header.  The header is
  // always in host byte order.
  ssize_t nread = read(fd, &optHdr, sizeof(optHdr));
  if (nread < 0) {
    LOGE("bcc: failed reading bcc header: %s\n", strerror(errno));
    return false;
  } else if (nread != sizeof(optHdr)) {
    LOGE("bcc: failed reading bcc header (got %d of %zd)\n",
         (int) nread, sizeof(optHdr));
    return false;
  }

  uint8_t const *magic = optHdr.magic;
  if (memcmp(magic, OBCC_MAGIC, 4) != 0) {
    /* not an oBCC file, or previous attempt was interrupted */
    LOGD("bcc: incorrect opt magic number (0x%02x %02x %02x %02x)\n",
         magic[0], magic[1], magic[2], magic[3]);
    return false;
  }

  uint8_t const *magicVer = optHdr.magicVersion;
  if (memcmp(magicVer, OBCC_MAGIC_VERS, 4) != 0) {
    LOGW("bcc: stale oBCC version (0x%02x %02x %02x %02x)\n",
         magicVer[0], magicVer[1], magicVer[2], magicVer[3]);
    return false;
  }

  // Check the file dependencies
  uint32_t val;

  val = optHdr.rslibWhen;
  if (val && (val != rslibWhen)) {
    LOGI("bcc: rslib file mod time mismatch (%08x vs %08x)\n",
         val, rslibWhen);
    return false;
  }

  val = optHdr.libRSWhen;
  if (val && (val != libRSWhen)) {
    LOGI("bcc: libRS file mod time mismatch (%08x vs %08x)\n",
         val, libRSWhen);
    return false;
  }

  val = optHdr.libbccWhen;
  if (val && (val != libbccWhen)) {
    LOGI("bcc: libbcc file mod time mismatch (%08x vs %08x)\n",
         val, libbccWhen);
    return false;
  }

  if (memcmp(optHdr.sourceSHA1, mSourceSHA1, 20) != 0) {
    LOGE("Bitcode SHA1 mismatch. Cache is outdated.\n");

#define PRINT_SHA1SUM(PREFIX, POSTFIX, D) \
    LOGE(PREFIX "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x" \
                "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x" POSTFIX, \
         D[0], D[1], D[2], D[3], D[4], D[5], D[6], D[7], D[8], D[9], \
         D[10], D[11], D[12], D[13], D[14], D[15], D[16], D[17], D[18], D[19]);

    PRINT_SHA1SUM("Note: Bitcode sha1sum: ", "\n", mSourceSHA1);
    PRINT_SHA1SUM("Note: Cached sha1sum:  ", "\n", optHdr.sourceSHA1);

#undef PRINT_SHA1SUM

    return false;
  }

  // Check the cache file has __isThreadable or not.  If it is set,
  // then we have to call mpSymbolLookupFn for __clearThreadable.
  if (optHdr.libRSThreadable && mpSymbolLookupFn) {
    mpSymbolLookupFn(mpSymbolLookupContext, "__clearThreadable");
  }

  return true;
}

}  // namespace bcc
