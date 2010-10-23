/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_BCC_BCC_H
#define ANDROID_BCC_BCC_H

#include <stdint.h>
#include <sys/types.h>

typedef char                        BCCchar;
typedef int32_t                     BCCint;
typedef uint32_t                    BCCuint;
typedef ssize_t                     BCCsizei;
typedef unsigned int                BCCenum;
typedef void                        BCCvoid;
typedef struct BCCscript            BCCscript;
typedef struct BCCtype              BCCtype;

#define BCC_NO_ERROR                0x0000
#define BCC_INVALID_ENUM            0x0500
#define BCC_INVALID_OPERATION       0x0502
#define BCC_INVALID_VALUE           0x0501
#define BCC_OUT_OF_MEMORY           0x0505

#define BCC_COMPILE_STATUS          0x8B81
#define BCC_INFO_LOG_LENGTH         0x8B84


// ----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

BCCscript *bccCreateScript();

void bccDeleteScript(BCCscript *script);

typedef BCCvoid *(*BCCSymbolLookupFn)(BCCvoid *pContext, const BCCchar *name);

void bccRegisterSymbolCallback(BCCscript *script,
                               BCCSymbolLookupFn pFn,
                               BCCvoid *pContext);

BCCenum bccGetError( BCCscript *script );

void bccScriptBitcode(BCCscript *script,
                      const BCCchar *bitcode,
                      BCCint size);

// Interface for llvm::Module input. @module should be a valid llvm::Module
// instance.
void bccScriptModule(BCCscript *script,
                     BCCvoid *module);

void bccLinkBitcode(BCCscript *script,
                    const BCCchar *bitcode,
                    BCCint size);

void bccCompileScript(BCCscript *script);

void bccGetScriptInfoLog(BCCscript *script,
                         BCCsizei maxLength,
                         BCCsizei *length,
                         BCCchar *infoLog);

void bccGetScriptLabel(BCCscript *script,
                       const BCCchar *name,
                       BCCvoid **address);

void bccGetExportVars(BCCscript *script,
                      BCCsizei *actualVarCount,
                      BCCsizei maxVarCount,
                      BCCvoid **vars);

void bccGetExportFuncs(BCCscript *script,
                       BCCsizei *actualFuncCount,
                       BCCsizei maxFuncCount,
                       BCCvoid **funcs);

void bccGetPragmas(BCCscript *script,
                   BCCsizei *actualStringCount,
                   BCCsizei maxStringCount,
                   BCCchar **strings);

// Below two functions are for debugging
void bccGetFunctions(BCCscript *script,
                     BCCsizei *actualFunctionCount,
                     BCCsizei maxFunctionCount,
                     BCCchar **functions);

void bccGetFunctionBinary(BCCscript *script,
                          BCCchar *function,
                          BCCvoid **base,
                          BCCsizei *length);

#ifdef __cplusplus
};
#endif

// ----------------------------------------------------------------------------

#endif
