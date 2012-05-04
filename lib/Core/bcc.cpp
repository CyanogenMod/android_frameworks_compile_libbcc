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

#include "bcc/bcc.h"

#include <llvm/Support/CodeGen.h>

#include <utils/StopWatch.h>

#include <bcinfo/BitcodeWrapper.h>

#include "bcc/Config/BuildInfo.h"
#include "bcc/RenderScript/RSExecutable.h"
#include "bcc/RenderScript/RSScript.h"
#include "bcc/Source.h"
#include "bcc/Support/DebugHelper.h"
#include "bcc/Support/Initialization.h"
#include "bcc/Support/Sha1Helper.h"

#include "bcc_internal.h"

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
  init::Initialize();
  RSScriptContext *rsctx = new (std::nothrow) RSScriptContext();
  if (rsctx != NULL) {
    rsctx->script = NULL;
    rsctx->result = NULL;
  }
  return wrap(rsctx);
}


extern "C" void bccDisposeScript(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  RSScriptContext *rsctx = unwrap(script);
  if (rsctx != NULL) {
    delete rsctx->script;
    delete rsctx->result;
  }
  delete rsctx;
}


extern "C" int bccRegisterSymbolCallback(BCCScriptRef script,
                                         BCCSymbolLookupFn pFn,
                                         void *pContext) {
  BCC_FUNC_LOGGER();
  unwrap(script)->driver.setRSRuntimeLookupFunction(pFn);
  unwrap(script)->driver.setRSRuntimeLookupContext(pContext);
  return BCC_NO_ERROR;
}


extern "C" int bccGetError(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return BCC_DEPRECATED_API;
}

static bool helper_add_source(RSScriptContext *pCtx,
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
    ALOGW("Set BCC_SKIP_DEP_SHA1 for flags to surpress this warning.\n");
  }

  Source *source = Source::CreateFromBuffer(pCtx->context, pName,
                                            pBitcode, pBitcodeSize);
  if (source == NULL) {
    return false;
  }

  if (pCtx->script == NULL) {
    pCtx->script = new (std::nothrow) RSScript(*source);
    if (pCtx->script == NULL) {
      ALOGE("Out of memory during script creation.");
      return false;
    }
  } else {
    bool result;
    if (pIsLink) {
      result = pCtx->script->mergeSource(*source);
    } else {
      result = pCtx->script->reset(*source);
    }
    if (!result) {
      return false;
    } else {
      bcinfo::BitcodeWrapper wrapper(pBitcode, pBitcodeSize);
      pCtx->script->setCompilerVersion(wrapper.getCompilerVersion());
      pCtx->script->setOptimizationLevel(
          static_cast<RSScript::OptimizationLevel>(
              wrapper.getOptimizationLevel()));
    }
  }

  if (need_dependency_check) {
    uint8_t sha1[20];
    calcSHA1(sha1, pBitcode, pBitcodeSize);
    if (!pCtx->script->addSourceDependency(pName, sha1)) {
      return false;
    }
  }

  return true;
}

static bool helper_add_source(RSScriptContext *pCtx,
                              llvm::Module *pModule,
                              bool pIsLink) {
  if (pModule == NULL) {
    ALOGE("Cannot add null module to script!");
    return false;
  }

  Source *source = Source::CreateFromModule(pCtx->context, *pModule, true);
  if (source == NULL) {
    return false;
  }

  if (pCtx->script == NULL) {
    pCtx->script = new (std::nothrow) RSScript(*source);
    if (pCtx->script == NULL) {
      ALOGE("Out of memory during script creation.");
      return false;
    }
  } else {
    bool result;
    if (pIsLink) {
      result = pCtx->script->mergeSource(*source);
    } else {
      result = pCtx->script->reset(*source);
    }
    if (!result) {
      return false;
    }
  }

  return true;
}

static bool helper_add_source(RSScriptContext *pCtx,
                              char const *pPath,
                              unsigned long pFlags,
                              bool pIsLink) {
  bool need_dependency_check = !(pFlags & BCC_SKIP_DEP_SHA1);

  Source *source = Source::CreateFromFile(pCtx->context, pPath);
  if (source == NULL) {
    return false;
  }

  if (pCtx->script == NULL) {
    pCtx->script = new (std::nothrow) RSScript(*source);
    if (pCtx->script == NULL) {
      ALOGE("Out of memory during script creation.");
      return false;
    }
  } else {
    bool result;
    if (pIsLink) {
      result = pCtx->script->mergeSource(*source);
    } else {
      result = pCtx->script->reset(*source);
    }
    if (!result) {
      return false;
    }
  }

  if (need_dependency_check) {
    uint8_t sha1[20];
    calcFileSHA1(sha1, pPath);
    if (!pCtx->script->addSourceDependency(pPath, sha1)) {
      return false;
    }
  }

  return true;
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
  return /* BCC_DEPRECATED_API */;
}


extern "C" int bccPrepareRelocatable(BCCScriptRef script,
                                     char const *objPath,
                                     bccRelocModelEnum RelocModel,
                                     unsigned long flags) {
  BCC_FUNC_LOGGER();
  return BCC_DEPRECATED_API;
}


extern "C" int bccPrepareSharedObject(BCCScriptRef script,
                                      char const *objPath,
                                      char const *dsoPath,
                                      unsigned long flags) {
  BCC_FUNC_LOGGER();
  return BCC_DEPRECATED_API;
}


extern "C" int bccPrepareExecutable(BCCScriptRef script,
                                    char const *cacheDir,
                                    char const *cacheName,
                                    unsigned long flags) {
  BCC_FUNC_LOGGER();

  android::StopWatch compileTimer("bcc: PrepareExecutable time");

  RSScriptContext *rsctx = unwrap(script);

  if (rsctx->script == NULL) {
    return 1;
  }

  // Construct the output path.
  std::string output_path(cacheDir);
  if (!output_path.empty() && (*output_path.rbegin() != '/')) {
    output_path.append(1, '/');
  }
  output_path.append(cacheName);
  output_path.append(".o");

  // Make sure the result container is clean.
  if (rsctx->result != NULL) {
    delete rsctx->result;
    rsctx->result = NULL;
  }

  rsctx->result = rsctx->driver.build(*rsctx->script, output_path);

  return (rsctx->result == NULL);
}


extern "C" void *bccGetFuncAddr(BCCScriptRef script, char const *funcname) {
  BCC_FUNC_LOGGER();

  RSScriptContext *rsctx = unwrap(script);

  void *addr = NULL;
  if (rsctx->result != NULL) {
    addr = rsctx->result->getSymbolAddress(funcname);
  }

#if DEBUG_BCC_REFLECT
  ALOGD("Function Address: %s --> %p\n", funcname, addr);
#endif

  return addr;
}


extern "C" void bccGetExportVarList(BCCScriptRef script,
                                    size_t varListSize,
                                    void **varList) {
  BCC_FUNC_LOGGER();

  const RSScriptContext *rsctx = unwrap(script);
  if (varList && rsctx->result) {
    const android::Vector<void *> &export_var_addrs =
        rsctx->result->getExportVarAddrs();
    size_t count = export_var_addrs.size();

    if (count > varListSize) {
      count = varListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      varList[i] = export_var_addrs[i];
    }

#if DEBUG_BCC_REFLECT
    ALOGD("ExportVarCount = %lu\n",
          static_cast<unsigned long>(export_var_addrs.size()));

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportVarList[%lu] = %p\n", static_cast<unsigned long>(i),
            varList[i]);
    }
#endif
  }
}


extern "C" void bccGetExportFuncList(BCCScriptRef script,
                                     size_t funcListSize,
                                     void **funcList) {
  BCC_FUNC_LOGGER();

  const RSScriptContext *rsctx = unwrap(script);
  if (funcList && rsctx->result) {
    const android::Vector<void *> &export_func_addrs =
        rsctx->result->getExportFuncAddrs();
    size_t count = export_func_addrs.size();

    if (count > funcListSize) {
      count = funcListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      funcList[i] = export_func_addrs[i];
    }

#if DEBUG_BCC_REFLECT
    ALOGD("ExportFuncCount = %lu\n",
          static_cast<unsigned long>(export_var_addrs.size()));

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportFuncList[%lu] = %p\n", static_cast<unsigned long>(i),
            varList[i]);
    }
#endif
  }
}


extern "C" void bccGetExportForEachList(BCCScriptRef script,
                                        size_t forEachListSize,
                                        void **forEachList) {
  BCC_FUNC_LOGGER();

  const RSScriptContext *rsctx = unwrap(script);
  if (forEachList && rsctx->result) {
    const android::Vector<void *> &export_foreach_func_addrs =
        rsctx->result->getExportForeachFuncAddrs();
    size_t count = export_foreach_func_addrs.size();

    if (count > forEachListSize) {
      count = forEachListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      forEachList[i] = export_foreach_func_addrs[i];
    }

#if DEBUG_BCC_REFLECT
    ALOGD("ExportForEachCount = %lu\n",
          static_cast<unsigned long>(export_foreach_func_addrs.size()));

    for (size_t i = 0; i < count; ++i) {
      ALOGD("ExportForEachList[%lu] = %p\n", static_cast<unsigned long>(i),
            forEachList[i]);
    }
#endif
  }
}

extern "C" char const *bccGetBuildTime() {
  return BuildInfo::GetBuildTime();
}

extern "C" char const *bccGetBuildRev() {
  return BuildInfo::GetBuildRev();
}

extern "C" char const *bccGetBuildSHA1() {
  return BuildInfo::GetBuildSourceBlob();
}
