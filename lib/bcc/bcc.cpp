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

#define LOG_TAG "bcc"
#include <cutils/log.h>

#include "Compiler.h"
#include "Script.h"

#include <bcc/bcc.h>

#include <utils/StopWatch.h>

char const libbcc_build_time[24] = __DATE__ " " __TIME__;

namespace bcc {
  class FuncLogger {
  private:
    char const *mFuncName;

  public:
    FuncLogger(char const *name) : mFuncName(name) {
      // LOGI("---> BEGIN: libbcc [ %s ]\n", name);
    }

    ~FuncLogger() {
      // LOGI("---> END: libbcc [ %s ]\n", mFuncName);
    }
  };

#define BCC_FUNC_LOGGER() bcc::FuncLogger XX__funcLogger(__FUNCTION__)
} // namespace bcc


namespace llvm {
  class Module;
}


extern "C" BCCscript *bccCreateScript() {
  BCC_FUNC_LOGGER();
  return new BCCscript();
}

extern "C" BCCenum bccGetError(BCCscript *script) {
  BCC_FUNC_LOGGER();
  return script->getError();
}

extern "C" void bccDeleteScript(BCCscript *script) {
  BCC_FUNC_LOGGER();
  delete script;
}

extern "C" void bccRegisterSymbolCallback(BCCscript *script,
                                          BCCSymbolLookupFn pFn,
                                          BCCvoid *pContext) {
  BCC_FUNC_LOGGER();
  script->registerSymbolCallback(pFn, pContext);
}

extern "C" int bccReadModule(BCCscript *script, BCCvoid *module) {
  BCC_FUNC_LOGGER();
  return script->readModule(reinterpret_cast<llvm::Module*>(module));
}

extern "C" int bccReadBC(BCCscript *script,
                         const BCCchar *bitcode,
                         BCCint bitcodeSize,
                         long __DONT_USE_PARAM_1,
                         long __DONT_USE_PARAM_2,
                         const BCCchar *resName,
                         const BCCchar *cacheDir) {
  BCC_FUNC_LOGGER();
  return script->readBC(bitcode, bitcodeSize, resName, cacheDir);
}

extern "C" int bccLinkBC(BCCscript *script,
                         const BCCchar *bitcode,
                         BCCint size) {
  BCC_FUNC_LOGGER();
  return script->linkBC(bitcode, size);
}

extern "C" int bccPrepareExecutable(BCCscript *script) {
  BCC_FUNC_LOGGER();
#if defined(__arm__)
  android::StopWatch compileTimer("bcc: PrepareExecutable time");
#endif

  int result = script->prepareExecutable();
  if (result)
    script->setError(BCC_INVALID_OPERATION);

  return result;
}

extern "C" void bccGetScriptInfoLog(BCCscript *script,
                                    BCCsizei maxLength,
                                    BCCsizei *length,
                                    BCCchar *infoLog) {
  BCC_FUNC_LOGGER();
  LOGE("%s is deprecated. *********************************\n", __func__);
}

extern "C" void bccGetScriptLabel(BCCscript *script,
                                  const BCCchar *name,
                                  BCCvoid **address) {
  BCC_FUNC_LOGGER();
  void *value = script->lookup(name);
  if (value) {
    *address = value;
#if defined(USE_DISASSEMBLER_FILE)
    LOGI("[GetScriptLabel] %s @ 0x%x", name, value);
#endif
  } else {
    script->setError(BCC_INVALID_VALUE);
  }
}

extern "C" void bccGetExportVars(BCCscript *script,
                                 BCCsizei *actualCount,
                                 BCCsizei varListSize,
                                 BCCvoid **varList) {
  BCC_FUNC_LOGGER();

  if (actualCount) {
    *actualCount = static_cast<BCCsizei>(script->getExportVarCount());
  }

  if (varList) {
    script->getExportVarList(static_cast<size_t>(varListSize), varList);

#if defined(USE_DISASSEMBLER_FILE)
    size_t count = script->getExportVarCount();
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

extern "C" void bccGetExportFuncs(BCCscript *script,
                                  BCCsizei *actualCount,
                                  BCCsizei funcListSize,
                                  BCCvoid **funcList) {
  BCC_FUNC_LOGGER();

  if (actualCount) {
    *actualCount = static_cast<BCCsizei>(script->getExportFuncCount());
  }

  if (funcList) {
    script->getExportFuncList(static_cast<size_t>(funcListSize), funcList);

#if defined(USE_DISASSEMBLER_FILE)
    size_t count = script->getExportFuncCount();
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

extern "C" void bccGetPragmas(BCCscript *script,
                              BCCsizei *actualCount,
                              BCCsizei stringListSize,
                              BCCchar **stringList) {
  BCC_FUNC_LOGGER();

  if (actualCount) {
    *actualCount = static_cast<BCCsizei>(script->getPragmaCount() * 2);
  }

  if (stringList) {
    size_t pragmaListSize = static_cast<size_t>(stringListSize) / 2;

    char const **buf = new (nothrow) char const *[pragmaListSize * 2];
    if (!buf) {
      return;
    }

    char const **keyList = buf;
    char const **valueList = buf + pragmaListSize;

    script->getPragmaList(pragmaListSize, keyList, valueList);

    for (size_t i = 0; i < pragmaListSize; ++i) {
      *stringList++ = const_cast<BCCchar *>(keyList[i]);
      *stringList++ = const_cast<BCCchar *>(valueList[i]);
    }

    delete [] buf;

#if defined(USE_DISASSEMBLER_FILE)
    size_t count = script->getPragmaCount();
    LOGD("PragmaCount = %lu\n", count);

    if (count > pragmaListSize) {
      count = pragmaListSize;
    }

    for (size_t i = 0; i < count; ++i) {
      LOGD("Pragma[%lu] = (%s , %s)\n",
           (unsigned long)i, stringList[2 * i], stringList[2 * i + 1]);
    }
#endif
  }
}

extern "C" void bccGetFunctions(BCCscript *script,
                                BCCsizei *actualCount,
                                BCCsizei funcNameListSize,
                                BCCchar **funcNameList) {
  BCC_FUNC_LOGGER();

  if (actualCount) {
    *actualCount = static_cast<BCCsizei>(script->getFuncCount());
  }

  if (funcNameList) {
    script->getFuncNameList(static_cast<size_t>(funcNameListSize),
                            const_cast<char const **>(funcNameList));
  }
}

extern "C" void bccGetFunctionBinary(BCCscript *script,
                                     BCCchar *funcname,
                                     BCCvoid **base,
                                     BCCsizei *length) {
  BCC_FUNC_LOGGER();

  size_t funcLength = 0;
  script->getFuncBinary(funcname, base, &funcLength);

  if (length) {
    *length = static_cast<BCCsizei>(funcLength);
  }
}
