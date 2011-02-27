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

#include <bcc/bcc.h>
#include "bcc_internal.h"

#include "Config.h"

#include "Compiler.h"
#include "DebugHelper.h"
#include "Script.h"

#include <utils/StopWatch.h>

using namespace bcc;

char const libbcc_build_time[24] = __DATE__ " " __TIME__;


namespace llvm {
  class Module;
}


extern "C" BCCScriptRef bccCreateScript() {
  BCC_FUNC_LOGGER();
  return wrap(new bcc::Script());
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

extern "C" int bccReadBC(BCCScriptRef script,
                         char const *resName,
                         char const *bitcode,
                         size_t bitcodeSize,
                         unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->addSourceBC(0, resName, bitcode, bitcodeSize, flags);
}


extern "C" int bccReadModule(BCCScriptRef script,
                             char const *resName /* deprecated */,
                             LLVMModuleRef module,
                             unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->addSourceModule(0, unwrap(module), flags);
}


extern "C" int bccReadFile(BCCScriptRef script,
                           char const *path,
                           unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->addSourceFile(0, path, flags);
}


extern "C" int bccLinkBC(BCCScriptRef script,
                         char const *resName,
                         char const *bitcode,
                         size_t bitcodeSize,
                         unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->addSourceBC(1, resName, bitcode, bitcodeSize, flags);
}


extern "C" int bccLinkFile(BCCScriptRef script,
                           char const *path,
                           unsigned long flags) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->addSourceFile(1, path, flags);
}


extern "C" int bccPrepareExecutable(BCCScriptRef script,
                                    char const *cachePath,
                                    unsigned long flags) {
  BCC_FUNC_LOGGER();

#if defined(__arm__)
  android::StopWatch compileTimer("bcc: PrepareExecutable time");
#endif

  return unwrap(script)->prepareExecutable(cachePath, flags);
}


extern "C" void *bccGetFuncAddr(BCCScriptRef script, char const *funcname) {
  BCC_FUNC_LOGGER();

  void *addr = unwrap(script)->lookup(funcname);

#if USE_DISASSEMBLER_FILE
  LOGD("Function Address: %s --> 0x%p\n", funcname, addr);
#endif

  return addr;
}


extern "C" size_t bccGetExportVarCount(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getExportVarCount();
}


extern "C" void bccGetExportVarList(BCCScriptRef script,
                                    size_t varListSize,
                                    void **varList) {
  BCC_FUNC_LOGGER();

  if (varList) {
    unwrap(script)->getExportVarList(varListSize, varList);

#if USE_DISASSEMBLER_FILE
    size_t count = unwrap(script)->getExportVarCount();
    LOGD("ExportVarCount = %lu\n", (unsigned long)count);

    if (count > varListSize) {
      count = varListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      LOGD("ExportVarList[%lu] = 0x%p\n", (unsigned long)i, varList[i]);
    }
#endif
  }
}


extern "C" size_t bccGetExportFuncCount(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getExportFuncCount();
}


extern "C" void bccGetExportFuncList(BCCScriptRef script,
                                     size_t funcListSize,
                                     void **funcList) {
  BCC_FUNC_LOGGER();

  if (funcList) {
    unwrap(script)->getExportFuncList(funcListSize, funcList);

#if USE_DISASSEMBLER_FILE
    size_t count = unwrap(script)->getExportFuncCount();
    LOGD("ExportFuncCount = %lu\n", (unsigned long)count);

    if (count > funcListSize) {
      count = funcListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      LOGD("ExportFuncList[%lu] = 0x%p\n", (unsigned long)i, funcList[i]);
    }
#endif
  }
}


extern "C" size_t bccGetPragmaCount(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getPragmaCount();
}


extern "C" void bccGetPragmaList(BCCScriptRef script,
                                 size_t pragmaListSize,
                                 const char **keyList,
                                 const char **valueList) {
  BCC_FUNC_LOGGER();
  unwrap(script)->getPragmaList(pragmaListSize, keyList, valueList);

#if USE_DISASSEMBLER_FILE
  if (keyList && valueList) {
    size_t count = unwrap(script)->getPragmaCount();
    LOGD("PragmaCount = %lu\n", (unsigned long)count);

    if (count > pragmaListSize) {
      count = pragmaListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      LOGD("Pragma[%lu] = (%s , %s)\n",
           (unsigned long)i, keyList[i], valueList[i]);
    }
  }
#endif
}


extern "C" size_t bccGetFuncCount(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getFuncCount();
}


extern "C" void bccGetFuncInfoList(BCCScriptRef script,
                                   size_t funcInfoListSize,
                                   BCCFuncInfo *funcInfoList) {
  BCC_FUNC_LOGGER();

  if (funcInfoList) {
    unwrap(script)->getFuncInfoList(funcInfoListSize, funcInfoList);
  }
}


extern "C" size_t bccGetObjectSlotCount(BCCScriptRef script) {
  BCC_FUNC_LOGGER();
  return unwrap(script)->getObjectSlotCount();
}


extern "C" void bccGetObjectSlotList(BCCScriptRef script,
                                     size_t objectSlotListSize,
                                     uint32_t *objectSlotList) {
  BCC_FUNC_LOGGER();

  if (objectSlotList) {
    unwrap(script)->getObjectSlotList(objectSlotListSize, objectSlotList);
#if USE_DISASSEMBLER_FILE
    size_t count = unwrap(script)->getObjectSlotCount();
    LOGD("ObjectSlotCount = %lu\n", (unsigned long)count);

    if (count > objectSlotListSize) {
      count = objectSlotListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      LOGD("ObjectSlotList[%lu] = %d\n", (unsigned long)i, objectSlotList[i]);
    }
#endif
  }
}
