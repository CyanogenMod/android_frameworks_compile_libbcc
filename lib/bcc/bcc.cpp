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
  return script->readBC(bitcode, bitcodeSize, 0, 0, resName, cacheDir);
}

extern "C" void bccLinkBC(BCCscript *script,
                          const BCCchar *bitcode,
                          BCCint size) {
  BCC_FUNC_LOGGER();
  script->linkBC(bitcode, size);
}

extern "C" int bccLoadBinary(BCCscript *script) {
  LOGE("bccLoadBinary is deprecated **************************\n");
  return 1;
}

extern "C" int bccCompileBC(BCCscript *script) {
  BCC_FUNC_LOGGER();
#if defined(__arm__)
  android::StopWatch compileTimer("RenderScript compile time");
#endif

  int result = script->compile();
  if (result)
    script->setError(BCC_INVALID_OPERATION);

  return result;
}

extern "C" void bccGetScriptInfoLog(BCCscript *script,
                                    BCCsizei maxLength,
                                    BCCsizei *length,
                                    BCCchar *infoLog) {
  BCC_FUNC_LOGGER();
  char const *message = script->getCompilerErrorMessage();
  int messageLength = strlen(message) + 1;
  if (length)
    *length = messageLength;

  if (infoLog && maxLength > 0) {
    int trimmedLength = maxLength < messageLength ? maxLength : messageLength;
    memcpy(infoLog, message, trimmedLength);
    infoLog[trimmedLength] = 0;
#if defined(USE_DISASSEMBLER_FILE)
    LOGI("[GetScriptInfoLog] %s", infoLog);
#endif
  }
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
                                 BCCsizei *actualVarCount,
                                 BCCsizei maxVarCount,
                                 BCCvoid **vars) {
  BCC_FUNC_LOGGER();
  script->getExportVars(actualVarCount, maxVarCount, vars);

#if defined(USE_DISASSEMBLER_FILE)
  int i;
  if (actualVarCount) {
    LOGI("[ExportVars] #=%d:", *actualVarCount);
  } else {
    for (i = 0; i < maxVarCount; i++) {
      LOGI("[ExportVars] #%d=0x%x", i, vars[i]);
    }
  }
#endif
}

extern "C" void bccGetExportFuncs(BCCscript *script,
                                  BCCsizei *actualFuncCount,
                                  BCCsizei maxFuncCount,
                                  BCCvoid **funcs) {
  BCC_FUNC_LOGGER();
  script->getExportFuncs(actualFuncCount, maxFuncCount, funcs);

#if defined(USE_DISASSEMBLER_FILE)
  int i;
  if (actualFuncCount) {
    LOGI("[ExportFunc] #=%d:", *actualFuncCount);
  } else {
    for (i = 0; i < maxFuncCount; i++) {
      LOGI("[ExportFunc] #%d=0x%x", i, funcs[i]);
    }
  }
#endif
}

extern "C" void bccGetPragmas(BCCscript *script,
                              BCCsizei *actualStringCount,
                              BCCsizei maxStringCount,
                              BCCchar **strings) {
  BCC_FUNC_LOGGER();
  script->getPragmas(actualStringCount, maxStringCount, strings);

#if defined(USE_DISASSEMBLER_FILE)
  int i;
  LOGI("[Pragma] #=%d:", *actualStringCount);
  for (i = 0; i < *actualStringCount; i++) {
    LOGI("  %s", strings[i]);
  }
#endif
}

extern "C" void bccGetFunctions(BCCscript *script,
                                BCCsizei *actualFunctionCount,
                                BCCsizei maxFunctionCount,
                                BCCchar **functions) {
  BCC_FUNC_LOGGER();
  script->getFunctions(actualFunctionCount,
                                maxFunctionCount,
                                functions);
}

extern "C" void bccGetFunctionBinary(BCCscript *script,
                                     BCCchar *function,
                                     BCCvoid **base,
                                     BCCsizei *length) {
  BCC_FUNC_LOGGER();
  script->getFunctionBinary(function, base, length);
}
