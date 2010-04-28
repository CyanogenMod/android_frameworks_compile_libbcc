#include <ctype.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#if defined(__arm__)
#include <unistd.h>
#endif

#if defined(__arm__)
#define PROVIDE_ARM_DISASSEMBLY
#endif

#ifdef PROVIDE_ARM_DISASSEMBLY
#include "disassem.h"
#endif

#include <bcc/bcc.h>

typedef int (*MainPtr)(int, char**);
// This is a separate function so it can easily be set by breakpoint in gdb.
static int run(MainPtr mainFunc, int argc, char** argv) 
{
  return mainFunc(argc, argv);
}

static BCCvoid* symbolLookup(BCCvoid* pContext, const BCCchar* name) 
{
  return (BCCvoid*) dlsym(RTLD_DEFAULT, name);
}

#ifdef PROVIDE_ARM_DISASSEMBLY

static FILE* disasmOut;

static u_int
disassemble_readword(u_int address)
{
  return(*((u_int *)address));
}

static void
disassemble_printaddr(u_int address)
{
  fprintf(disasmOut, "0x%08x", address);
}

static void
disassemble_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(disasmOut, fmt, ap);
  va_end(ap);
}

static int disassemble(BCCscript* script, FILE* out) {
  disasmOut = out;
  disasm_interface_t  di;
  di.di_readword = disassemble_readword;
  di.di_printaddr = disassemble_printaddr;
  di.di_printf = disassemble_printf;

  BCCvoid* base;
  BCCsizei length;

  BCCsizei numFunctions;
  bccGetFunctions(script, &numFunctions, 0, NULL);
  if (numFunctions) {
    char** labels = new char*[numFunctions];
    bccGetFunctions(script, NULL, numFunctions, labels);

    for(BCCsizei i = 0; i < numFunctions; i++) {
      bccGetFunctionBinary(script, labels[i], &base, &length);

      unsigned long* pBase = (unsigned long*) base;
      unsigned long* pEnd = (unsigned long*) (((unsigned char*) base) + length);

      for(unsigned long* pInstruction = pBase; pInstruction < pEnd; pInstruction++) {
        fprintf(out, "%08x: %08x  ", (int) pInstruction, (int) *pInstruction);
        ::disasm(&di, (uint) pInstruction, 0);
      }
    }
    delete[] labels;
  }   

  return 1;
}
#else
static int disassemble(BCCscript* script, FILE* out) {
  return 1;
}
#endif // PROVIDE_ARM_DISASSEMBLY

static int reflection(BCCscript* script, FILE* out) {
#if 0
  moduleHandle m = bccGetModuleHandle(script);

  fprintf(out, "#types = %d\n", bccGetNumTypes(m));

  for (int i = 0; i < bccGetNumTypes(m); i++) {
    typeHandle h = bccGetTypeHandleOf(m, i);

    const char *str = bccGetTypeName(m, h);
    fprintf(out, "Type #%d:\n -Name: %s\n", i, str);

    // Double-check using bccGetTypeHandle
    if (h != bccGetTypeHandle(m, str)) {
      fprintf(out, "typeHandle <-> nameHandle not 1:1\n");
    }

    fprintf(out, " -Structural: %s\n", bccGetTypeStruct(h));

    fprintf(out, " -FieldNames: %s\n",
            bccGetTypeFieldNames(m, h));

    unsigned j = bccGetNumFields(h);
    fprintf(out, " -#fields: %d\n", j);

    if (j > 1) {
      unsigned k;
      for (k = 0; k < j; k++) {
        typeHandle f = bccGetFieldType(h, k);
        fprintf(out, "  Field #%d:\n   -Structural: %s\n   -Name: %s\n",
                k,
                bccGetTypeStruct(f),
                bccGetFieldName(m, h, k));

        fprintf(out, "   -Size: %d\n   -Offst: %d\n",
                bccGetFieldSizeInBits(m, h, k),
                bccGetFieldOffsetInBits(m, h, k));
      }
    }
  }

  bccDeleteModuleHandle(m);
#endif

  return 1;
}

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

static BCCscript* loadScript() {
  FILE* in = stdin;

  if (!inFile) {
    fprintf(stderr, "input file required\n");
    return NULL;
  }

  if (inFile) {
    in = fopen(inFile, "r");
    if (!in) {
      fprintf(stderr, "Could not open input file %s\n", inFile);
      return NULL;
    }
  }

  fseek(in, 0, SEEK_END);
  size_t codeSize = (size_t) ftell(in);
  rewind(in);
  BCCchar* bitcode = new BCCchar[codeSize + 1];
  size_t bytesRead = fread(bitcode, 1, codeSize, in);
  if (bytesRead != codeSize) 
      fprintf(stderr, "Could not read all of file %s\n", inFile);

  BCCscript* script = bccCreateScript();

  bitcode[codeSize] = '\0'; /* must be null-terminated */
  bccScriptBitcode(script, bitcode, codeSize);
  delete [] bitcode;

  return script;
}

static int compile(BCCscript* script) {
  bccRegisterSymbolCallback(script, symbolLookup, NULL);

  bccCompileScript(script);

  int result = bccGetError(script);
  if (result != 0) {
    BCCsizei bufferLength;
    bccGetScriptInfoLog(script, 0, &bufferLength, NULL);
    char* buf = (char*) malloc(bufferLength + 1); 
    if (buf != NULL) {
        bccGetScriptInfoLog(script, bufferLength + 1, NULL, buf);
        fprintf(stderr, "%s", buf);
        free(buf);
    } else {
        fprintf(stderr, "Out of memory.\n");
    }   
    bccDeleteScript(script);
    return 0;
  }   

  {
    BCCsizei numPragmaStrings;
    bccGetPragmas(script, &numPragmaStrings, 0, NULL);
    if (numPragmaStrings) {
      char** strings = new char*[numPragmaStrings];
      bccGetPragmas(script, NULL, numPragmaStrings, strings);
      for(BCCsizei i = 0; i < numPragmaStrings; i += 2) 
        fprintf(stderr, "#pragma %s(%s)\n", strings[i], strings[i+1]);
      delete[] strings;
    }
  }

  return 1;
}

static int runMain(BCCscript* script, int argc, char** argv) {
  MainPtr mainPointer = 0;

  bccGetScriptLabel(script, "main", (BCCvoid**) &mainPointer);

  int result = bccGetError(script);
  if (result != BCC_NO_ERROR) {
    fprintf(stderr, "Could not find main: %d\n", result);
  } else {
    fprintf(stderr, "Executing compiled code:\n");
    int codeArgc = argc - optind;
    char** codeArgv = argv + optind;
    //codeArgv[0] = (char*) (inFile ? inFile : "stdin");
    result = run(mainPointer, codeArgc, codeArgv);
    fprintf(stderr, "result: %d\n", result);
  }   

  return 1;

}

int main(int argc, char** argv) 
{
  int result = 0;
  BCCscript* script;

  if(!parseOption(argc, argv)) {
    result = 1;
    fprintf(stderr, "failed to parse option\n");
    goto exit; 
  }

  if((script = loadScript()) == NULL) {
    result = 2;
    fprintf(stderr, "failed to load source\n");
    goto exit;
  }

  if(printTypeInformation && !reflection(script, stderr)) {
    result = 3;
    fprintf(stderr, "failed to retrieve type information\n");
    goto exit;
  }

  if(!compile(script)) {
    result = 4;
    fprintf(stderr, "failed to compile\n");
    goto exit;
  }

  if(printListing && !disassemble(script, stderr)) {
    result = 5;
    fprintf(stderr, "failed to disassemble\n");
    goto exit;
  }

  if(runResults && !runMain(script, argc, argv)) {
    result = 6;
    fprintf(stderr, "failed to execute\n");
    goto exit;
  }

exit:

  return result;
}
