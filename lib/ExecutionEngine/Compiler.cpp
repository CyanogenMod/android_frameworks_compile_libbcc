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

#include "Compiler.h"

#include "Config.h"

#if USE_OLD_JIT
#include "OldJIT/ContextManager.h"
#endif

#if USE_DISASSEMBLER
#include "Disassembler/Disassembler.h"
#endif

#include "DebugHelper.h"
#include "FileHandle.h"
#include "Runtime.h"
#include "ScriptCompiled.h"
#include "Sha1Helper.h"

#if USE_MCJIT
#include "librsloader.h"
#endif

#include "llvm/ADT/StringRef.h"

#include "llvm/Analysis/Passes.h"

#include "llvm/Bitcode/ReaderWriter.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"

#include "llvm/MC/SubtargetFeature.h"

#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"

#include "llvm/Type.h"
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

#include <string.h>

#include <algorithm>
#include <iterator>
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

// Name of metadata node where pragma info resides (should be synced with
// slang.cpp)
const llvm::StringRef Compiler::PragmaMetadataName = "#pragma";

// Name of metadata node where exported variable names reside (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportVarMetadataName = "#rs_export_var";

// Name of metadata node where exported function names reside (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportFuncMetadataName = "#rs_export_func";

// Name of metadata node where RS object slot info resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ObjectSlotMetadataName = "#rs_object_slots";

//////////////////////////////////////////////////////////////////////////////
// Compiler
//////////////////////////////////////////////////////////////////////////////

void Compiler::GlobalInitialization() {
  if (GlobalInitialized)
    return;
  // if (!llvm::llvm_is_multithreaded())
  //   llvm::llvm_start_multithreaded();

  // Set Triple, CPU and Features here
  Triple = TARGET_TRIPLE_STRING;

#if defined(DEFAULT_ARM_CODEGEN)

#if defined(ARCH_ARM_HAVE_VFP) && __ARM_ARCH__ >= 7
  Features.push_back("+vfp3");
#if !defined(ARCH_ARM_HAVE_VFP_D32)
  Features.push_back("+d16");
#endif
#elif defined(ARCH_ARM_HAVE_VFP)
  Features.push_back("+vfp2");
#endif

  // NOTE: Currently, we have to turn off the support for NEON explicitly.
  // Since the ARMCodeEmitter.cpp is not ready for JITing NEON
  // instructions.

  // FIXME: Re-enable NEON when ARMCodeEmitter supports NEON.
#define USE_ARM_NEON 0
#if USE_ARM_NEON
  Features.push_back("+neon");
  Features.push_back("+neonfp");
#else
  Features.push_back("-neon");
  Features.push_back("-neonfp");
#endif // USE_ARM_NEON
#endif // DEFAULT_ARM_CODEGEN

#if defined(PROVIDE_ARM_CODEGEN)
  LLVMInitializeARMAsmPrinter();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTarget();
#endif

#if defined(PROVIDE_X86_CODEGEN)
  LLVMInitializeX86AsmPrinter();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
#endif

#if USE_DISASSEMBLER
  InitializeDisassembler();
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

  // Register the scheduler
  llvm::RegisterScheduler::setDefault(llvm::createDefaultScheduler);

  // Register allocation policy:
  //  createFastRegisterAllocator: fast but bad quality
  //  createLinearScanRegisterAllocator: not so fast but good quality
  llvm::RegisterRegAlloc::setDefault
    ((CodeGenOptLevel == llvm::CodeGenOpt::None) ?
     llvm::createFastRegisterAllocator :
     llvm::createLinearScanRegisterAllocator);

#if USE_CACHE
  // Read in SHA1 checksum of libbcc and libRS.
  readSHA1(sha1LibBCC_SHA1, sizeof(sha1LibBCC_SHA1), pathLibBCC_SHA1);

  calcFileSHA1(sha1LibRS, pathLibRS);
#endif

  GlobalInitialized = true;
}


void Compiler::LLVMErrorHandler(void *UserData, const std::string &Message) {
  std::string *Error = static_cast<std::string*>(UserData);
  Error->assign(Message);
  LOGE("%s", Message.c_str());
  exit(1);
}


#if USE_OLD_JIT
CodeMemoryManager *Compiler::createCodeMemoryManager() {
  mCodeMemMgr.reset(new CodeMemoryManager());
  return mCodeMemMgr.get();
}
#endif


#if USE_OLD_JIT
CodeEmitter *Compiler::createCodeEmitter() {
  mCodeEmitter.reset(new CodeEmitter(mpResult, mCodeMemMgr.get()));
  return mCodeEmitter.get();
}
#endif


Compiler::Compiler(ScriptCompiled *result)
  : mpResult(result),
#if USE_MCJIT
    mRSExecutable(NULL),
#endif
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


llvm::Module *Compiler::parseBitcodeFile(llvm::MemoryBuffer *MEM) {
  llvm::Module *result = llvm::ParseBitcodeFile(MEM, *mContext, &mError);

  if (!result) {
    LOGE("Unable to ParseBitcodeFile: %s\n", mError.c_str());
    return NULL;
  }

  return result;
}


int Compiler::linkModule(llvm::Module *moduleWith) {
  if (llvm::Linker::LinkModules(mModule, moduleWith,
                                llvm::Linker::DestroySource,
                                &mError) != 0) {
    return hasError();
  }

  // Everything for linking should be settled down here with no error occurs
  mHasLinked = true;
  return hasError();
}


int Compiler::compile(bool compileOnly) {
  llvm::Target const *Target = NULL;
  llvm::TargetData *TD = NULL;
  llvm::TargetMachine *TM = NULL;

  std::string FeaturesStr;

  llvm::NamedMDNode const *PragmaMetadata;
  llvm::NamedMDNode const *ExportVarMetadata;
  llvm::NamedMDNode const *ExportFuncMetadata;
  llvm::NamedMDNode const *ObjectSlotMetadata;

  if (mModule == NULL)  // No module was loaded
    return 0;

  // Create TargetMachine
  Target = llvm::TargetRegistry::lookupTarget(Triple, mError);
  if (hasError())
    goto on_bcc_compile_error;

  if (!CPU.empty() || !Features.empty()) {
    llvm::SubtargetFeatures F;

    for (std::vector<std::string>::const_iterator
         I = Features.begin(), E = Features.end(); I != E; I++) {
      F.AddFeature(*I);
    }

    FeaturesStr = F.getString();
  }

#if defined(DEFAULT_X86_64_CODEGEN)
  // Data address in X86_64 architecture may reside in a far-away place
  TM = Target->createTargetMachine(Triple, CPU, FeaturesStr,
                                   llvm::Reloc::Static,
                                   llvm::CodeModel::Medium);
#else
  // This is set for the linker (specify how large of the virtual addresses
  // we can access for all unknown symbols.)
  TM = Target->createTargetMachine(Triple, CPU, FeaturesStr,
                                   llvm::Reloc::Static,
                                   llvm::CodeModel::Small);
#endif
  if (TM == NULL) {
    setError("Failed to create target machine implementation for the"
             " specified triple '" + Triple + "'");
    goto on_bcc_compile_error;
  }

  // Get target data from Module
  TD = new llvm::TargetData(mModule);

  // Load named metadata
  ExportVarMetadata = mModule->getNamedMetadata(ExportVarMetadataName);
  ExportFuncMetadata = mModule->getNamedMetadata(ExportFuncMetadataName);
  PragmaMetadata = mModule->getNamedMetadata(PragmaMetadataName);
  ObjectSlotMetadata = mModule->getNamedMetadata(ObjectSlotMetadataName);

  // Perform link-time optimization if we have multiple modules
  if (mHasLinked) {
    runLTO(new llvm::TargetData(*TD), ExportVarMetadata, ExportFuncMetadata);
  }

  // Perform code generation
#if USE_OLD_JIT
  if (runCodeGen(new llvm::TargetData(*TD), TM,
                 ExportVarMetadata, ExportFuncMetadata) != 0) {
    goto on_bcc_compile_error;
  }
#endif

#if USE_MCJIT
  if (runMCCodeGen(new llvm::TargetData(*TD), TM) != 0) {
    goto on_bcc_compile_error;
  }

  if (compileOnly)
    return 0;

  // Load the ELF Object
  mRSExecutable =
    rsloaderCreateExec((unsigned char *)&*mEmittedELFExecutable.begin(),
                       mEmittedELFExecutable.size(),
                       &resolveSymbolAdapter, this);

  if (!mRSExecutable) {
    setError("Fail to load emitted ELF relocatable file");
    goto on_bcc_compile_error;
  }

  if (ExportVarMetadata) {
    ScriptCompiled::ExportVarList &varList = mpResult->mExportVars;
    std::vector<std::string> &varNameList = mpResult->mExportVarsName;

    for (int i = 0, e = ExportVarMetadata->getNumOperands(); i != e; i++) {
      llvm::MDNode *ExportVar = ExportVarMetadata->getOperand(i);
      if (ExportVar != NULL && ExportVar->getNumOperands() > 1) {
        llvm::Value *ExportVarNameMDS = ExportVar->getOperand(0);
        if (ExportVarNameMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef ExportVarName =
            static_cast<llvm::MDString*>(ExportVarNameMDS)->getString();

          varList.push_back(
            rsloaderGetSymbolAddress(mRSExecutable,
                                     ExportVarName.str().c_str()));
          varNameList.push_back(ExportVarName.str());
#if DEBUG_MCJIT_REFLECT
          LOGD("runMCCodeGen(): Exported Var: %s @ %p\n", ExportVarName.str().c_str(),
               varList.back());
#endif
          continue;
        }
      }

      varList.push_back(NULL);
    }
  }

  if (ExportFuncMetadata) {
    ScriptCompiled::ExportFuncList &funcList = mpResult->mExportFuncs;
    std::vector<std::string> &funcNameList = mpResult->mExportFuncsName;

    for (int i = 0, e = ExportFuncMetadata->getNumOperands(); i != e; i++) {
      llvm::MDNode *ExportFunc = ExportFuncMetadata->getOperand(i);
      if (ExportFunc != NULL && ExportFunc->getNumOperands() > 0) {
        llvm::Value *ExportFuncNameMDS = ExportFunc->getOperand(0);
        if (ExportFuncNameMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef ExportFuncName =
            static_cast<llvm::MDString*>(ExportFuncNameMDS)->getString();

          funcList.push_back(
            rsloaderGetSymbolAddress(mRSExecutable,
                                     ExportFuncName.str().c_str()));
          funcNameList.push_back(ExportFuncName.str());
#if DEBUG_MCJIT_RELECT
          LOGD("runMCCodeGen(): Exported Func: %s @ %p\n", ExportFuncName.str().c_str(),
               funcList.back());
#endif
        }
      }
    }
  }

#if DEBUG_MCJIT_DISASSEMBLER
  {
    // Get MC codegen emitted function name list
    size_t func_list_size = rsloaderGetFuncCount(mRSExecutable);
    std::vector<char const *> func_list(func_list_size, NULL);
    rsloaderGetFuncNameList(mRSExecutable, func_list_size, &*func_list.begin());

    // Disassemble each function
    for (size_t i = 0; i < func_list_size; ++i) {
      void *func = rsloaderGetSymbolAddress(mRSExecutable, func_list[i]);
      if (func) {
        size_t size = rsloaderGetSymbolSize(mRSExecutable, func_list[i]);
        Disassemble(DEBUG_MCJIT_DISASSEMBLER_FILE,
                    Target, TM, func_list[i], (unsigned char const *)func, size);
      }
    }
  }
#endif
#endif

  // Read pragma information from the metadata node of the module.
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
#if DEBUG_BCC_REFLECT
          LOGD("compile(): Pragma: %s -> %s\n",
               pragmaList.back().first.c_str(),
               pragmaList.back().second.c_str());
#endif
        }
      }
    }
  }

  if (ObjectSlotMetadata) {
    ScriptCompiled::ObjectSlotList &objectSlotList = mpResult->mObjectSlots;

    for (int i = 0, e = ObjectSlotMetadata->getNumOperands(); i != e; i++) {
      llvm::MDNode *ObjectSlot = ObjectSlotMetadata->getOperand(i);
      if (ObjectSlot != NULL &&
          ObjectSlot->getNumOperands() == 1) {
        llvm::Value *SlotMDS = ObjectSlot->getOperand(0);
        if (SlotMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef Slot =
              static_cast<llvm::MDString*>(SlotMDS)->getString();
          uint32_t USlot = 0;
          if (Slot.getAsInteger(10, USlot)) {
            setError("Non-integer object slot value '" + Slot.str() + "'");
            goto on_bcc_compile_error;
          }
          objectSlotList.push_back(USlot);
#if DEBUG_BCC_REFLECT
          LOGD("compile(): RefCount Slot: %s @ %u\n", Slot.str().c_str(), USlot);
#endif
        }
      }
    }
  }

on_bcc_compile_error:
  // LOGE("on_bcc_compiler_error");
  if (TD) {
    delete TD;
  }

  if (TM) {
    delete TM;
  }

  if (mError.empty()) {
    return 0;
  }

  // LOGE(getErrorMessage());
  return 1;
}


#if USE_OLD_JIT
int Compiler::runCodeGen(llvm::TargetData *TD, llvm::TargetMachine *TM,
                         llvm::NamedMDNode const *ExportVarMetadata,
                         llvm::NamedMDNode const *ExportFuncMetadata) {
  // Create memory manager for creation of code emitter later.
  if (!mCodeMemMgr.get() && !createCodeMemoryManager()) {
    setError("Failed to startup memory management for further compilation");
    return 1;
  }

  mpResult->mContext = (char *) (mCodeMemMgr.get()->getCodeMemBase());

  // Create code emitter
  if (!mCodeEmitter.get()) {
    if (!createCodeEmitter()) {
      setError("Failed to create machine code emitter for compilation");
      return 1;
    }
  } else {
    // Reuse the code emitter
    mCodeEmitter->reset();
  }

  mCodeEmitter->setTargetMachine(*TM);
  mCodeEmitter->registerSymbolCallback(mpSymbolLookupFn,
                                       mpSymbolLookupContext);

  // Create code-gen pass to run the code emitter
  llvm::OwningPtr<llvm::FunctionPassManager> CodeGenPasses(
    new llvm::FunctionPassManager(mModule));

  // Add TargetData to code generation pass manager
  CodeGenPasses->add(TD);

  // Add code emit passes
  if (TM->addPassesToEmitMachineCode(*CodeGenPasses,
                                     *mCodeEmitter,
                                     CodeGenOptLevel)) {
    setError("The machine code emission is not supported on '" + Triple + "'");
    return 1;
  }

  // Run the code emitter on every non-declaration function in the module
  CodeGenPasses->doInitialization();
  for (llvm::Module::iterator
       I = mModule->begin(), E = mModule->end(); I != E; I++) {
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
#if DEBUG_BCC_REFLECT
              LOGD("runCodeGen(): Exported VAR: %s @ %p\n", ExportVarName.str().c_str(), I->second);
#endif
              break;
            }
          }
          if (I != mCodeEmitter->global_address_end())
            continue;  // found

#if DEBUG_BCC_REFLECT
          LOGD("runCodeGen(): Exported VAR: %s @ %p\n",
               ExportVarName.str().c_str(), (void *)0);
#endif
        }
      }
      // if reaching here, we know the global variable record in metadata is
      // not found. So we make an empty slot
      varList.push_back(NULL);
    }

    bccAssert((varList.size() == ExportVarMetadata->getNumOperands()) &&
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
#if DEBUG_BCC_REFLECT
          LOGD("runCodeGen(): Exported Func: %s @ %p\n", ExportFuncName.str().c_str(),
               funcList.back());
#endif
        }
      }
    }
  }

  // Tell code emitter now can release the memory using during the JIT since
  // we have done the code emission
  mCodeEmitter->releaseUnnecessary();

  return 0;
}
#endif // USE_OLD_JIT


#if USE_MCJIT
int Compiler::runMCCodeGen(llvm::TargetData *TD, llvm::TargetMachine *TM) {
  // Decorate mEmittedELFExecutable with formatted ostream
  llvm::raw_svector_ostream OutSVOS(mEmittedELFExecutable);

  // Relax all machine instructions
  TM->setMCRelaxAll(/* RelaxAll= */ true);

  // Create MC code generation pass manager
  llvm::PassManager MCCodeGenPasses;

  // Add TargetData to MC code generation pass manager
  MCCodeGenPasses.add(TD);

  // Add MC code generation passes to pass manager
  llvm::MCContext *Ctx;
  if (TM->addPassesToEmitMC(MCCodeGenPasses, Ctx, OutSVOS,
                            CodeGenOptLevel, false)) {
    setError("Fail to add passes to emit file");
    return 1;
  }

  MCCodeGenPasses.run(*mModule);
  OutSVOS.flush();
  return 0;
}
#endif // USE_MCJIT


int Compiler::runLTO(llvm::TargetData *TD,
                     llvm::NamedMDNode const *ExportVarMetadata,
                     llvm::NamedMDNode const *ExportFuncMetadata) {
  llvm::PassManager LTOPasses;

  // Add TargetData to LTO passes
  LTOPasses.add(TD);

  // Collect All Exported Symbols
  std::vector<const char*> ExportSymbols;

  // Note: This is a workaround for getting export variable and function name.
  // We should refine it soon.
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

  // TODO(logan): Remove this after we have finished the
  // bccMarkExternalSymbol API.

  // root(), init(), and .rs.dtor() are born to be exported
  ExportSymbols.push_back("root");
  ExportSymbols.push_back("init");
  ExportSymbols.push_back(".rs.dtor");

  // User-defined exporting symbols
  std::vector<char const *> const &UserDefinedExternalSymbols =
    mpResult->getUserDefinedExternalSymbols();

  std::copy(UserDefinedExternalSymbols.begin(),
            UserDefinedExternalSymbols.end(),
            std::back_inserter(ExportSymbols));

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

  return 0;
}


#if USE_MCJIT
void *Compiler::getSymbolAddress(char const *name) {
  return rsloaderGetSymbolAddress(mRSExecutable, name);
}
#endif


#if USE_MCJIT
void *Compiler::resolveSymbolAdapter(void *context, char const *name) {
  Compiler *self = reinterpret_cast<Compiler *>(context);

  if (void *Addr = FindRuntimeFunction(name)) {
    return Addr;
  }

  if (self->mpSymbolLookupFn) {
    if (void *Addr = self->mpSymbolLookupFn(self->mpSymbolLookupContext, name)) {
      return Addr;
    }
  }

  LOGE("Unable to resolve symbol: %s\n", name);
  return NULL;
}
#endif


Compiler::~Compiler() {
  delete mModule;
  delete mContext;

#if USE_MCJIT
  rsloaderDisposeExec(mRSExecutable);
#endif

  // llvm::llvm_shutdown();
}


}  // namespace bcc
