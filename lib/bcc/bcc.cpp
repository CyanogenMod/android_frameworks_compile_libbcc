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


namespace llvm {
  class Module;
}


extern "C" BCCscript *bccCreateScript() {
  return new BCCscript();
}

extern "C" BCCenum bccGetError(BCCscript *script) {
  return script->getError();
}

extern "C" void bccDeleteScript(BCCscript *script) {
  delete script;
}

extern "C" void bccRegisterSymbolCallback(BCCscript *script,
                                          BCCSymbolLookupFn pFn,
                                          BCCvoid *pContext) {
  script->registerSymbolCallback(pFn, pContext);
}

extern "C" int bccReadModule(BCCscript *script, BCCvoid *module) {
  return script->compiler.readModule(reinterpret_cast<llvm::Module*>(module));
}

extern "C" int bccReadBC(BCCscript *script,
                         const BCCchar *bitcode,
                         BCCint size,
                         const BCCchar *resName) {
  return script->compiler.readBC(bitcode, size, resName);
}

extern "C" void bccLinkBC(BCCscript *script,
                          const BCCchar *bitcode,
                          BCCint size) {
  script->compiler.linkBC(bitcode, size);
}

extern "C" int bccLoadBinary(BCCscript *script) {
  int result = script->compiler.loadCacheFile();

#if defined(USE_DISASSEMBLER_FILE)
  LOGI("[LoadBinary] result=%d", result);
#endif
  if (result) {
    script->setError(BCC_INVALID_OPERATION);
  }

  return result;
}

extern "C" int bccCompileBC(BCCscript *script) {
#if defined(__arm__)
  android::StopWatch compileTimer("RenderScript compile time");
#endif

  int result = script->compiler.compile();
  if (result)
    script->setError(BCC_INVALID_OPERATION);

  return result;
}

extern "C" void bccGetScriptInfoLog(BCCscript *script,
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
#if defined(USE_DISASSEMBLER_FILE)
    LOGI("[GetScriptInfoLog] %s", infoLog);
#endif
  }
}

extern "C" void bccGetScriptLabel(BCCscript *script,
                                  const BCCchar *name,
                                  BCCvoid **address) {
  void *value = script->compiler.lookup(name);
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
  script->compiler.getExportVars(actualVarCount, maxVarCount, vars);

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
  script->compiler.getExportFuncs(actualFuncCount, maxFuncCount, funcs);

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
  script->compiler.getPragmas(actualStringCount, maxStringCount, strings);

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
  script->compiler.getFunctions(actualFunctionCount,
                                maxFunctionCount,
                                functions);
}

extern "C" void bccGetFunctionBinary(BCCscript *script,
                                     BCCchar *function,
                                     BCCvoid **base,
                                     BCCsizei *length) {
  script->compiler.getFunctionBinary(function, base, length);
}
