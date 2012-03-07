/*
 * Copyright 2010-2012, The Android Open Source Project
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
#include "CompilerOption.h"

#if USE_MCJIT
#include "librsloader.h"
#endif

#include "Transforms/BCCTransforms.h"

#include "llvm/ADT/StringRef.h"

#include "llvm/Analysis/Passes.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"

#include "llvm/MC/MCContext.h"
#include "llvm/MC/SubtargetFeature.h"

#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Constants.h"
#include "llvm/GlobalValue.h"
#include "llvm/Linker.h"
#include "llvm/LLVMContext.h"
#include "llvm/Metadata.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Type.h"
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

extern char* gDebugDumpDirectory;

namespace bcc {

//////////////////////////////////////////////////////////////////////////////
// BCC Compiler Static Variables
//////////////////////////////////////////////////////////////////////////////

bool Compiler::GlobalInitialized = false;


#if !defined(__HOST__)
  #define TARGET_TRIPLE_STRING  DEFAULT_TARGET_TRIPLE_STRING
#else
// In host TARGET_TRIPLE_STRING is a variable to allow cross-compilation.
  #if defined(__cplusplus)
    extern "C" {
  #endif
      char *TARGET_TRIPLE_STRING = (char*)DEFAULT_TARGET_TRIPLE_STRING;
  #if defined(__cplusplus)
    };
  #endif
#endif

// Code generation optimization level for the compiler
llvm::CodeGenOpt::Level Compiler::CodeGenOptLevel;

std::string Compiler::Triple;
llvm::Triple::ArchType Compiler::ArchType;

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

// Name of metadata node where exported ForEach name information resides
// (should be synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportForEachNameMetadataName =
    "#rs_export_foreach_name";

// Name of metadata node where exported ForEach signature information resides
// (should be synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ExportForEachMetadataName =
    "#rs_export_foreach";

// Name of metadata node where RS object slot info resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef Compiler::ObjectSlotMetadataName = "#rs_object_slots";

// Name of metadata node where RS optimization level resides (should be
// synced with slang_rs_metadata.h)
const llvm::StringRef OptimizationLevelMetadataName = "#optimization_level";



//////////////////////////////////////////////////////////////////////////////
// Compiler
//////////////////////////////////////////////////////////////////////////////

void Compiler::GlobalInitialization() {
  if (GlobalInitialized) {
    return;
  }

#if defined(PROVIDE_ARM_CODEGEN)
  LLVMInitializeARMAsmPrinter();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTarget();
#endif

#if defined(PROVIDE_MIPS_CODEGEN)
  LLVMInitializeMipsAsmPrinter();
  LLVMInitializeMipsTargetMC();
  LLVMInitializeMipsTargetInfo();
  LLVMInitializeMipsTarget();
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

  // if (!llvm::llvm_is_multithreaded())
  //   llvm::llvm_start_multithreaded();

  // Set Triple, CPU and Features here
  Triple = TARGET_TRIPLE_STRING;

  // Determine ArchType
#if defined(__HOST__)
  {
    std::string Err;
    llvm::Target const *Target = llvm::TargetRegistry::lookupTarget(Triple, Err);
    if (Target != NULL) {
      ArchType = llvm::Triple::getArchTypeForLLVMName(Target->getName());
    } else {
      ArchType = llvm::Triple::UnknownArch;
      ALOGE("%s", Err.c_str());
    }
  }
#elif defined(DEFAULT_ARM_CODEGEN)
  ArchType = llvm::Triple::arm;
#elif defined(DEFAULT_MIPS_CODEGEN)
  ArchType = llvm::Triple::mipsel;
#elif defined(DEFAULT_X86_CODEGEN)
  ArchType = llvm::Triple::x86;
#elif defined(DEFAULT_X86_64_CODEGEN)
  ArchType = llvm::Triple::x86_64;
#else
  ArchType = llvm::Triple::UnknownArch;
#endif

  if ((ArchType == llvm::Triple::arm) || (ArchType == llvm::Triple::thumb)) {
#  if defined(ARCH_ARM_HAVE_VFP)
    Features.push_back("+vfp3");
#  if !defined(ARCH_ARM_HAVE_VFP_D32)
    Features.push_back("+d16");
#  endif
#  endif

#  if defined(ARCH_ARM_HAVE_NEON)
    Features.push_back("+neon");
    Features.push_back("+neonfp");
#  else
    Features.push_back("-neon");
    Features.push_back("-neonfp");
#  endif

#  if defined(DISABLE_ARCH_ARM_HAVE_NEON)
    Features.push_back("-neon");
    Features.push_back("-neonfp");
#  endif
  }

  // Register the scheduler
  llvm::RegisterScheduler::setDefault(llvm::createDefaultScheduler);

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
  ALOGE("%s", Message.c_str());
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
    mModule(NULL),
    mHasLinked(false) /* Turn off linker */ {
  llvm::remove_fatal_error_handler();
  llvm::install_fatal_error_handler(LLVMErrorHandler, &mError);
  return;
}


int Compiler::linkModule(llvm::Module *moduleWith) {
  if (llvm::Linker::LinkModules(mModule, moduleWith,
                                llvm::Linker::PreserveSource,
                                &mError) != 0) {
    return hasError();
  }

  // Everything for linking should be settled down here with no error occurs
  mHasLinked = true;
  return hasError();
}


int Compiler::compile(const CompilerOption &option) {
  llvm::Target const *Target = NULL;
  llvm::TargetData *TD = NULL;
  llvm::TargetMachine *TM = NULL;

  std::string FeaturesStr;

  if (mModule == NULL)  // No module was loaded
    return 0;

  llvm::NamedMDNode const *PragmaMetadata;
  llvm::NamedMDNode const *ExportVarMetadata;
  llvm::NamedMDNode const *ExportFuncMetadata;
  llvm::NamedMDNode const *ExportForEachNameMetadata;
  llvm::NamedMDNode const *ExportForEachMetadata;
  llvm::NamedMDNode const *ObjectSlotMetadata;

  std::vector<std::string> ForEachNameList;
  std::vector<std::string> ForEachExpandList;
  std::vector<uint32_t> forEachSigList;

  llvm::NamedMDNode const *OptimizationLevelMetadata =
    mModule->getNamedMetadata(OptimizationLevelMetadataName);

  // Default to maximum optimization in the absence of named metadata node
  int OptimizationLevel = 3;
  if (OptimizationLevelMetadata) {
    llvm::ConstantInt* OL = llvm::dyn_cast<llvm::ConstantInt>(
      OptimizationLevelMetadata->getOperand(0)->getOperand(0));
    OptimizationLevel = OL->getZExtValue();
  }

  if (OptimizationLevel == 0) {
    CodeGenOptLevel = llvm::CodeGenOpt::None;
  } else if (OptimizationLevel == 1) {
    CodeGenOptLevel = llvm::CodeGenOpt::Less;
  } else if (OptimizationLevel == 2) {
    CodeGenOptLevel = llvm::CodeGenOpt::Default;
  } else if (OptimizationLevel == 3) {
    CodeGenOptLevel = llvm::CodeGenOpt::Aggressive;
  }

  // not the best place for this, but we need to set the register allocation
  // policy after we read the optimization_level metadata from the bitcode

  // Register allocation policy:
  //  createFastRegisterAllocator: fast but bad quality
  //  createLinearScanRegisterAllocator: not so fast but good quality
  llvm::RegisterRegAlloc::setDefault
    ((CodeGenOptLevel == llvm::CodeGenOpt::None) ?
     llvm::createFastRegisterAllocator :
     llvm::createGreedyRegisterAllocator);

  // Find LLVM Target
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

  // Create LLVM Target Machine
  TM = Target->createTargetMachine(Triple, CPU, FeaturesStr,
                                   option.TargetOpt,
                                   option.RelocModelOpt,
                                   option.CodeModelOpt);

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
  ExportForEachNameMetadata =
      mModule->getNamedMetadata(ExportForEachNameMetadataName);
  ExportForEachMetadata =
      mModule->getNamedMetadata(ExportForEachMetadataName);
  PragmaMetadata = mModule->getNamedMetadata(PragmaMetadataName);
  ObjectSlotMetadata = mModule->getNamedMetadata(ObjectSlotMetadataName);

  if (ExportForEachNameMetadata) {
    for (int i = 0, e = ExportForEachNameMetadata->getNumOperands();
         i != e;
         i++) {
      llvm::MDNode *ExportForEach = ExportForEachNameMetadata->getOperand(i);
      if (ExportForEach != NULL && ExportForEach->getNumOperands() > 0) {
        llvm::Value *ExportForEachNameMDS = ExportForEach->getOperand(0);
        if (ExportForEachNameMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef ExportForEachName =
            static_cast<llvm::MDString*>(ExportForEachNameMDS)->getString();
          ForEachNameList.push_back(ExportForEachName.str());
          std::string ExpandName = ExportForEachName.str() + ".expand";
          ForEachExpandList.push_back(ExpandName);
        }
      }
    }
  }

  if (ExportForEachMetadata) {
    for (int i = 0, e = ExportForEachMetadata->getNumOperands(); i != e; i++) {
      llvm::MDNode *SigNode = ExportForEachMetadata->getOperand(i);
      if (SigNode != NULL && SigNode->getNumOperands() == 1) {
        llvm::Value *SigVal = SigNode->getOperand(0);
        if (SigVal->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef SigString =
              static_cast<llvm::MDString*>(SigVal)->getString();
          uint32_t Signature = 0;
          if (SigString.getAsInteger(10, Signature)) {
            ALOGE("Non-integer signature value '%s'", SigString.str().c_str());
            goto on_bcc_compile_error;
          }
          forEachSigList.push_back(Signature);
        }
      }
    }
  }

  runInternalPasses(ForEachNameList, forEachSigList);

  // Perform link-time optimization if we have multiple modules
  if (mHasLinked) {
    runLTO(new llvm::TargetData(*TD), ExportVarMetadata, ExportFuncMetadata,
           ForEachExpandList, CodeGenOptLevel);
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

  if (!option.LoadAfterCompile)
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

  rsloaderUpdateSectionHeaders(mRSExecutable,
    (unsigned char*) mEmittedELFExecutable.begin());

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
          ALOGD("runMCCodeGen(): Exported Var: %s @ %p\n", ExportVarName.str().c_str(),
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
          ALOGD("runMCCodeGen(): Exported Func: %s @ %p\n", ExportFuncName.str().c_str(),
               funcList.back());
#endif
        }
      }
    }
  }

  if (ExportForEachNameMetadata) {
    ScriptCompiled::ExportForEachList &forEachList = mpResult->mExportForEach;
    std::vector<std::string> &ForEachNameList = mpResult->mExportForEachName;

    for (int i = 0, e = ExportForEachNameMetadata->getNumOperands();
         i != e;
         i++) {
      llvm::MDNode *ExportForEach = ExportForEachNameMetadata->getOperand(i);
      if (ExportForEach != NULL && ExportForEach->getNumOperands() > 0) {
        llvm::Value *ExportForEachNameMDS = ExportForEach->getOperand(0);
        if (ExportForEachNameMDS->getValueID() == llvm::Value::MDStringVal) {
          llvm::StringRef ExportForEachName =
            static_cast<llvm::MDString*>(ExportForEachNameMDS)->getString();
          std::string Name = ExportForEachName.str() + ".expand";

          forEachList.push_back(
              rsloaderGetSymbolAddress(mRSExecutable, Name.c_str()));
          ForEachNameList.push_back(Name);
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
          ALOGD("compile(): Pragma: %s -> %s\n",
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
          ALOGD("compile(): RefCount Slot: %s @ %u\n", Slot.str().c_str(), USlot);
#endif
        }
      }
    }
  }

on_bcc_compile_error:
  // ALOGE("on_bcc_compiler_error");
  if (TD) {
    delete TD;
  }

  if (TM) {
    delete TM;
  }

  if (mError.empty()) {
    return 0;
  }

  // ALOGE(getErrorMessage());
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
              ALOGD("runCodeGen(): Exported VAR: %s @ %p\n", ExportVarName.str().c_str(), I->second);
#endif
              break;
            }
          }
          if (I != mCodeEmitter->global_address_end())
            continue;  // found

#if DEBUG_BCC_REFLECT
          ALOGD("runCodeGen(): Exported VAR: %s @ %p\n",
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
          ALOGD("runCodeGen(): Exported Func: %s @ %p\n", ExportFuncName.str().c_str(),
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
  llvm::MCContext *Ctx = NULL;
  if (TM->addPassesToEmitMC(MCCodeGenPasses, Ctx, OutSVOS, false)) {
    setError("Fail to add passes to emit file");
    return 1;
  }

  MCCodeGenPasses.run(*mModule);
  OutSVOS.flush();
  return 0;
}
#endif // USE_MCJIT

int Compiler::runInternalPasses(std::vector<std::string>& Names,
                                std::vector<uint32_t>& Signatures) {
  llvm::PassManager BCCPasses;

  // Expand ForEach on CPU path to reduce launch overhead.
  BCCPasses.add(createForEachExpandPass(Names, Signatures));

  BCCPasses.run(*mModule);

  return 0;
}

int Compiler::runLTO(llvm::TargetData *TD,
                     llvm::NamedMDNode const *ExportVarMetadata,
                     llvm::NamedMDNode const *ExportFuncMetadata,
                     std::vector<std::string>& ForEachExpandList,
                     llvm::CodeGenOpt::Level OptimizationLevel) {
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

  for (int i = 0, e = ForEachExpandList.size(); i != e; i++) {
    ExportSymbols.push_back(ForEachExpandList[i].c_str());
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

  llvm::PassManager LTOPasses;

  // Add TargetData to LTO passes
  LTOPasses.add(TD);

  // We now create passes list performing LTO. These are copied from
  // (including comments) llvm::createStandardLTOPasses().
  // Only a subset of these LTO passes are enabled in optimization level 0
  // as they interfere with interactive debugging.
  // FIXME: figure out which passes (if any) makes sense for levels 1 and 2

  if (OptimizationLevel != llvm::CodeGenOpt::None) {
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

  } else {
    LTOPasses.add(llvm::createInternalizePass(ExportSymbols));
    LTOPasses.add(llvm::createGlobalOptimizerPass());
    LTOPasses.add(llvm::createConstantMergePass());
  }

  LTOPasses.run(*mModule);

#if ANDROID_ENGINEERING_BUILD
  if (0 != gDebugDumpDirectory) {
    std::string errs;
    std::string Filename(gDebugDumpDirectory);
    Filename += "/post-lto-module.ll";
    llvm::raw_fd_ostream FS(Filename.c_str(), errs);
    mModule->print(FS, 0);
    FS.close();
  }
#endif

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

  ALOGE("Unable to resolve symbol: %s\n", name);
  return NULL;
}
#endif


Compiler::~Compiler() {
#if USE_MCJIT
  rsloaderDisposeExec(mRSExecutable);
#endif

  // llvm::llvm_shutdown();
}


}  // namespace bcc
