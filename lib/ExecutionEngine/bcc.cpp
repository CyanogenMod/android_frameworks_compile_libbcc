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

// Bitcode compiler (bcc) for Android:
//    This is an eager-compilation JIT running on Android.

#include <bcc/bcc.h>

#include <string>

#include <utils/StopWatch.h>

#include "Config.h"

#include <bcc/bcc_mccache.h>
#include "bcc_internal.h"

#include "BCCContext.h"
#include "Compiler.h"
#include "DebugHelper.h"
#include "RSScript.h"
#include "Sha1Helper.h"
#include "Source.h"

using namespace bcc;

namespace llvm {
  class Module;
}

static bool bccBuildStampPrinted = false;

static void bccPrintBuildStamp() {
  if (!bccBuildStampPrinted) {
    ALOGI("LIBBCC build time: %s", bccGetBuildTime());
    ALOGI("LIBBCC build revision: %s", bccGetBuildRev());
    bccBuildStampPrinted = true;
  }
}

extern "C" BCCScriptRef bccCreateScript() {
  BCC_FUNC_LOGGER();
  bccPrintBuildStamp();
  // FIXME: This is a workaround for this API: use global BCC context and
  //        create an empty source to create a Script object.
  BCCContext *context = BCCContext::GetOrCreateGlobalContext();
  if (context == NULL) {
    return NULL;
  }

  Source *source = Source::CreateEmpty(*context, "empty");
  return wrap(new RSScript(*source));
}


extern "C" void bccDisposeScript(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  delete unwrap(script);
}


extern "C" int bccRegisterSymbolCallback(BCCScriptRef script,
                                         BCCSymbolLookupFn pFn,
                                         void *pContext) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->registerSymbolCallback(pFn, pContext);
}


extern "C" int bccGetError(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getError();
}

static bool helper_add_source(RSScript *pScript,
                              char const *pName,
                              char const *pBitcode,
                              size_t pBitcodeSize,
                              unsigned long pFlags,
                              bool pIsLink) {
  bool need_dependency_check = !(pFlags & BCC_SKIP_DEP_SHA1);
  if (!pName && need_dependency_check) {
    pFlags |= BCC_SKIP_DEP_SHA1;

    ALOGW("It is required to give resName for sha1 dependency check.\n");
    ALOGW("Sha1sum dependency check will be skipped.\n");
    ALOGW("Set BCC_SKIP_DEP_SHA1 for flags to suppress this warning.\n");
  }

  BCCContext *context = BCCContext::GetOrCreateGlobalContext();
  if (context == NULL) {
    return false;
  }

  Source *source = Source::CreateFromBuffer(*context, pName,
                                            pBitcode, pBitcodeSize);
  if (source == NULL) {
    return false;
  }

  if (need_dependency_check) {
    uint8_t sha1[20];
    calcSHA1(sha1, pBitcode, pBitcodeSize);
    if (!pScript->addSourceDependency(BCC_APK_RESOURCE, pName, sha1)) {
      return false;
    }
  }

  return ((pIsLink) ? pScript->mergeSource(*source) : pScript->reset(*source));
}

static bool helper_add_source(RSScript *pScript,
                              llvm::Module *pModule,
                              bool pIsLink) {
  if (pModule == NULL)
    return false;

  BCCContext *context = BCCContext::GetOrCreateGlobalContext();
  if (context == NULL) {
    return false;
  }

  if (pModule == NULL) {
    ALOGE("Cannot add null module to script!");
    return false;
  }

  Source *source = Source::CreateFromModule(*context, *pModule, true);
  if (source == NULL) {
    return false;
  }

  return ((pIsLink) ? pScript->mergeSource(*source) : pScript->reset(*source));
}

static bool helper_add_source(RSScript *pScript,
                              char const *pPath,
                              unsigned long pFlags,
                              bool pIsLink) {
  bool need_dependency_check = !(pFlags & BCC_SKIP_DEP_SHA1);
  BCCContext *context = BCCContext::GetOrCreateGlobalContext();
  if (context == NULL) {
    return false;
  }

  Source *source = Source::CreateFromFile(*context, pPath);
  if (source == NULL) {
    return false;
  }

  if (need_dependency_check) {
    uint8_t sha1[20];
    calcFileSHA1(sha1, pPath);
    if (!pScript->addSourceDependency(BCC_APK_RESOURCE, pPath, sha1)) {
      return false;
    }
  }

  return ((pIsLink) ? pScript->mergeSource(*source) : pScript->reset(*source));
}

extern "C" int bccReadBC(BCCScriptRef script,
                         char const *resName,
                         char const *bitcode,
                         size_t bitcodeSize,
                         unsigned long flags) {
  BCC_FUNC_LOGGER();
  return (helper_add_source(unwrap(script), resName,
                            bitcode, bitcodeSize,
                            flags, /* pIsLink */false) == false);
}


extern "C" int bccReadModule(BCCScriptRef script,
                             char const *resName /* deprecated */,
                             LLVMModuleRef module,
                             unsigned long flags) {
  BCC_FUNC_LOGGER();
  return (helper_add_source(unwrap(script), unwrap(module),
                            /* pIsLink */false) == false);
}


extern "C" int bccReadFile(BCCScriptRef script,
                           char const *path,
                           unsigned long flags) {
  BCC_FUNC_LOGGER();
  return (helper_add_source(unwrap(script), path,
                            flags, /* pIsLink */false) == false);
}


extern "C" int bccLinkBC(BCCScriptRef script,
                         char const *resName,
                         char const *bitcode,
                         size_t bitcodeSize,
                         unsigned long flags) {
  BCC_FUNC_LOGGER();
  return (helper_add_source(unwrap(script), resName,
                            bitcode, bitcodeSize,
                            flags, /* pIsLink */true) == false);
}


extern "C" int bccLinkFile(BCCScriptRef script,
                           char const *path,
                           unsigned long flags) {
  BCC_FUNC_LOGGER();
  return (helper_add_source(unwrap(script), path,
                            flags, /* pIsLink */true) == false);
}


extern "C" void bccMarkExternalSymbol(BCCScriptRef script, char const *name) {
  BCC_FUNC_LOGGER();
  unwrap(script)->markExternalSymbol(name);
}


extern "C" int bccPrepareRelocatable(BCCScriptRef script,
                                     char const *objPath,
                                     bccRelocModelEnum RelocModel,
                                     unsigned long flags) {
  BCC_FUNC_LOGGER();
  llvm::Reloc::Model RM;

  switch (RelocModel) {
    case bccRelocDefault: {
      RM = llvm::Reloc::Default;
      break;
    }
    case bccRelocStatic: {
      RM = llvm::Reloc::Static;
      break;
    }
    case bccRelocPIC: {
      RM = llvm::Reloc::PIC_;
      break;
    }
    case bccRelocDynamicNoPIC: {
      RM = llvm::Reloc::DynamicNoPIC;
      break;
    }
    default: {
      ALOGE("Unrecognized relocation model for bccPrepareObject!");
      return BCC_INVALID_VALUE;
    }
  }

  return unwrap(script)->prepareRelocatable(objPath, RM, flags);
}


extern "C" int bccPrepareSharedObject(BCCScriptRef script,
                                      char const *objPath,
                                      char const *dsoPath,
                                      unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->prepareSharedObject(objPath, dsoPath, flags);
}


extern "C" int bccPrepareExecutable(BCCScriptRef script,
                                    char const *cacheDir,
                                    char const *cacheName,
                                    unsigned long flags) {
  BCC_FUNC_LOGGER();

  android::StopWatch compileTimer("bcc: PrepareExecutable time");

  return unwrap(script)->prepareExecutable(cacheDir, cacheName, flags);
}


extern "C" void *bccGetFuncAddr(BCCScriptRef script, char const *funcname) {
  BCC_FUNC_LOGGER();

  void *addr = unwrap(script)->lookup(funcname);

#if DEBUG_BCC_REFLECT
  ALOGD("Function Address: %s --> %p\n", funcname, addr);
#endif

  return addr;
}


extern "C" void bccGetExportVarList(BCCScriptRef script,
                                    size_t varListSize,
                                    void **varList) {
  BCC_FUNC_LOGGER();

  if (varList) {
    unwrap(script)->getExportVarList(varListSize, varList);

#if DEBUG_BCC_REFLECT
    size_t count = unwrap(script)->getExportVarCount();
    ALOGD("ExportVarCount = %lu\n", (unsigned long)count);

    if (count > varListSize) {
      count = varListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportVarList[%lu] = %p\n", (unsigned long)i, varList[i]);
    }
#endif
  }
}


extern "C" void bccGetExportFuncList(BCCScriptRef script,
                                     size_t funcListSize,
                                     void **funcList) {
  BCC_FUNC_LOGGER();

  if (funcList) {
    unwrap(script)->getExportFuncList(funcListSize, funcList);

#if DEBUG_BCC_REFLECT
    size_t count = unwrap(script)->getExportFuncCount();
    ALOGD("ExportFuncCount = %lu\n", (unsigned long)count);

    if (count > funcListSize) {
      count = funcListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportFuncList[%lu] = %p\n", (unsigned long)i, funcList[i]);
    }
#endif
  }
}


extern "C" void bccGetExportForEachList(BCCScriptRef script,
                                        size_t forEachListSize,
                                        void **forEachList) {
  BCC_FUNC_LOGGER();

  if (forEachList) {
    unwrap(script)->getExportForEachList(forEachListSize, forEachList);

#if DEBUG_BCC_REFLECT
    size_t count = unwrap(script)->getExportForEachCount();
    ALOGD("ExportForEachCount = %lu\n", (unsigned long)count);

    if (count > forEachListSize) {
      count = forEachListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportForEachList[%lu] = %p\n", (unsigned long)i, forEachList[i]);
    }
#endif
  }
}
