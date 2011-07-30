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

#include <ctype.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>

#if defined(__arm__)
#define PROVIDE_ARM_DISASSEMBLY
#endif

#ifdef PROVIDE_ARM_DISASSEMBLY
#include "disassem.h"
#endif

#include <bcc/bcc.h>

#include <vector>


typedef int (*MainPtr)(int, char**);

// This is a separate function so it can easily be set by breakpoint in gdb.
static int run(MainPtr mainFunc, int argc, char** argv) {
  return mainFunc(argc, argv);
}

static void* lookupSymbol(void* pContext, const char* name) {
  return (void*) dlsym(RTLD_DEFAULT, name);
}

#ifdef PROVIDE_ARM_DISASSEMBLY

static FILE* disasmOut;

static u_int disassemble_readword(u_int address) {
  return(*((u_int *)address));
}

static void disassemble_printaddr(u_int address) {
  fprintf(disasmOut, "0x%08x", address);
}

static void disassemble_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(disasmOut, fmt, ap);
  va_end(ap);
}

static int disassemble(BCCScriptRef script, FILE* out) {
  /* Removed by srhines
  disasmOut = out;
  disasm_interface_t  di;
  di.di_readword = disassemble_readword;
  di.di_printaddr = disassemble_printaddr;
  di.di_printf = disassemble_printf;

  size_t numFunctions = bccGetFuncCount(script);
  fprintf(stderr, "Function Count: %lu\n", (unsigned long)numFunctions);
  if (numFunctions) {
    BCCFuncInfo *infos = new BCCFuncInfo[numFunctions];
    bccGetFuncInfoList(script, numFunctions, infos);

    for(size_t i = 0; i < numFunctions; i++) {
      fprintf(stderr, "-----------------------------------------------------\n");
      fprintf(stderr, "%s\n", infos[i].name);
      fprintf(stderr, "-----------------------------------------------------\n");

      unsigned long* pBase = (unsigned long*) infos[i].addr;
      unsigned long* pEnd =
        (unsigned long*) (((unsigned char*) infos[i].addr) + infos[i].size);

      for(unsigned long* pInstruction = pBase; pInstruction < pEnd; pInstruction++) {
        fprintf(out, "%08x: %08x  ", (int) pInstruction, (int) *pInstruction);
        ::disasm(&di, (uint) pInstruction, 0);
      }
    }
    delete [] infos;
  }
  */

  return 1;
}
#else
static int disassemble(BCCScriptRef script, FILE* out) {
  fprintf(stderr, "Disassembler not supported on this build.\n");
  return 1;
}
#endif // PROVIDE_ARM_DISASSEMBLY

const char* inFile = NULL;
bool printTypeInformation = false;
bool printListing = false;
bool runResults = false;

extern int opterr;
extern int optind;

static int parseOption(int argc, char** argv)
{
  int c;
  while ((c = getopt (argc, argv, "RST")) != -1) {
    opterr = 0;

    switch(c) {
      case 'R':
        runResults = true;
        break;

      case 'S':
        printListing = true;
        break;

      case 'T':
        printTypeInformation = true;
        break;

      case '?':
        // ignore any error
        break;

      default:
        // Critical error occurs
        return 0;
        break;
    }
  }

  if(optind >= argc) {
    fprintf(stderr, "input file required\n");
    return 0;
  }

  inFile = argv[optind];
  return 1;
}

static BCCScriptRef loadScript() {
  if (!inFile) {
    fprintf(stderr, "input file required\n");
    return NULL;
  }

  struct stat statInFile;
  if (stat(inFile, &statInFile) < 0) {
    fprintf(stderr, "Unable to stat input file: %s\n", strerror(errno));
    return NULL;
  }

  if (!S_ISREG(statInFile.st_mode)) {
    fprintf(stderr, "Input file should be a regular file.\n");
    return NULL;
  }

  FILE *in = fopen(inFile, "r");
  if (!in) {
    fprintf(stderr, "Could not open input file %s\n", inFile);
    return NULL;
  }

  size_t bitcodeSize = statInFile.st_size;

  std::vector<char> bitcode(bitcodeSize + 1, '\0');
  size_t nread = fread(&*bitcode.begin(), 1, bitcodeSize, in);

  if (nread != bitcodeSize)
      fprintf(stderr, "Could not read all of file %s\n", inFile);

  BCCScriptRef script = bccCreateScript();

  if (bccReadBC(script, "file", &*bitcode.begin(), bitcodeSize, 0) != 0) {
    fprintf(stderr, "bcc: FAILS to read bitcode");
    bccDisposeScript(script);
    return NULL;
  }

  bccRegisterSymbolCallback(script, lookupSymbol, NULL);

  if (bccPrepareExecutable(script, ".", "cache", 0) != 0) {
    fprintf(stderr, "bcc: FAILS to prepare executable.\n");
    bccDisposeScript(script);
    return NULL;
  }

  return script;
}

static void printPragma(BCCScriptRef script) {
/* Removed by srhines
  size_t numPragma = bccGetPragmaCount(script);
  if (numPragma) {
    char const ** keyList = new char const *[numPragma];
    char const ** valueList = new char const *[numPragma];

    bccGetPragmaList(script, numPragma, keyList, valueList);
    for(size_t i = 0; i < numPragma; ++i) {
      fprintf(stderr, "#pragma %s(%s)\n", keyList[i], valueList[i]);
    }

    delete [] keyList;
    delete [] valueList;
  }
*/
}

static int runMain(BCCScriptRef script, int argc, char** argv) {
  MainPtr mainPointer = (MainPtr)bccGetFuncAddr(script, "root");

  if (!mainPointer) {
    fprintf(stderr, "Could not find root.\n");
    return 0;
  }

  fprintf(stderr, "Executing compiled code:\n");

  int argc1 = argc - optind;
  char** argv1 = argv + optind;

  int result = run(mainPointer, argc1, argv1);
  fprintf(stderr, "result: %d\n", result);

  return 1;
}

int main(int argc, char** argv) {
  if(!parseOption(argc, argv)) {
    fprintf(stderr, "failed to parse option\n");
    return 1;
  }

  BCCScriptRef script;

  if((script = loadScript()) == NULL) {
    fprintf(stderr, "failed to load source\n");
    return 2;
  }

#if 0
  if(printTypeInformation && !reflection(script, stderr)) {
    fprintf(stderr, "failed to retrieve type information\n");
    return 3;
  }
#endif

  printPragma(script);

  if(printListing && !disassemble(script, stderr)) {
    fprintf(stderr, "failed to disassemble\n");
    return 5;
  }

  if(runResults && !runMain(script, argc, argv)) {
    fprintf(stderr, "failed to execute\n");
    return 6;
  }

  return 0;
}
