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
#include "ScriptCompiled.h"

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

#include <sha1.h>

#include <string>
#include <vector>

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
  mCodeEmitter.reset(new CodeEmitter(mpResult, mCodeMemMgr.get()));
  return mCodeEmitter.get();
}


Compiler::Compiler(ScriptCompiled *result)
  : mpResult(result),
    mUseCache(false),
    mCacheNew(false),
    mCacheLoadFailed(false),
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

// Compiler::readBC
// Parameters:
//   resName: NULL means don't use cache.
//   Note: If "cache-hit but bccLoadBinary fails for some reason", still
//         pass in the resName.
//         Rationale: So we may have future cache-hit.
//
int Compiler::readBC(const char *bitcode,
                     size_t bitcodeSize,
                     const BCCchar *resName,
                     const BCCchar *cacheDir) {

  // Compute the SHA1 hash of the input bitcode.  So that we can use the hash
  // to decide rather to update the cache or not.
  computeSourceSHA1(bitcode, bitcodeSize);

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

  mpResult->mContext = (char *) (mCodeMemMgr.get()->getCodeMemBase());

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
    ScriptCompiled::ExportVarList &varList = mpResult->mExportVars;

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
              varList.push_back(I->second);
              break;
            }
          }
          if (I != mCodeEmitter->global_address_end())
            continue;  // found
        }
      }
      // if reaching here, we know the global variable record in metadata is
      // not found. So we make an empty slot
      varList.push_back(NULL);
    }

    assert((varList.size() == ExportVarMetadata->getNumOperands()) &&
           "Number of slots doesn't match the number of export variables!");
  }

  if (ExportFuncMetadata) {
    ScriptCompiled::ExportFuncList &funcList = mpResult->mExportFuncs;

    for (int i = 0, e = ExportFuncMetadata->getNumOperands(); i != e; i++) {
      llvm::MDNode *ExportFunc = ExportFuncMetadata->getOperand(i);
      if (ExportFunc != NULL && ExportFunc->getNumOperands() > 0) {
        llvm::Value *ExportFuncNameMDS = ExportFunc->getOperand(0);
        if (ExportFuncNameMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef ExportFuncName =
            static_cast<llvm::MDString*>(ExportFuncNameMDS)->getString();
          funcList.push_back(mpResult->lookup(ExportFuncName.str().c_str()));
        }
      }
    }
  }

  // Tell code emitter now can release the memory using during the JIT since
  // we have done the code emission
  mCodeEmitter->releaseUnnecessary();

  // Finally, read pragma information from the metadata node of the @Module if
  // any.
  if (PragmaMetadata) {
    ScriptCompiled::PragmaList &pragmaList = mpResult->mPragmas;

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

          pragmaList.push_back(
            std::make_pair(std::string(PragmaName.data(),
                                       PragmaName.size()),
                           std::string(PragmaValue.data(),
                                       PragmaValue.size())));
        }
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

#if 0
  if (mError.empty()) {
    if (mUseCache && mCacheFd >= 0 && mCacheNew) {
      genCacheFile();
      //LOGI("DONE generating cache file");  //sliao
      flock(mCacheFd, LOCK_UN);
    }

    return false;
  }
#endif

  // LOGE(getErrorMessage());
  return true;
}


Compiler::~Compiler() {
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

}  // namespace bcc
