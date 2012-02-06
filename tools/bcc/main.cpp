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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__HOST__)
  #if defined(__cplusplus)
    extern "C" {
  #endif
      extern char *TARGET_TRIPLE_STRING;
  #if defined(__cplusplus)
    };
  #endif
#else
#endif

#include <bcc/bcc.h>

#define DEFAULT_OUTPUT_FILENAME "a.out"

typedef int (*MainPtr)();

// This is a separate function so it can easily be set by breakpoint in gdb.
static int run(MainPtr mainFunc) {
  return mainFunc();
}

static void* lookupSymbol(void* pContext, const char* name) {
  return (void*) dlsym(RTLD_DEFAULT, name);
}

const char* inFile = NULL;
const char* outFile = NULL;
bool runResults = false;

struct option_info {
  const char *option_name;

  // min_option_argc is the minimum number of arguments this option should
  // have. This is for sanity check before invoking processing function.
  unsigned min_option_argc;

  const char *argument_desc;
  const char *help_message;

  // The function to process this option. Return the number of arguments it
  // consumed or < 0 if there's an error during the processing.
  int (*process)(int argc, char **arg);
};

// forward declaration of option processing functions
static int do_set_tripe(int, char **);
static int do_set_input(int, char **);
static int do_set_output(int, char **);
static int do_run(int, char **);
static int do_help(int, char **);

static const struct option_info options[] = {
#if defined(__HOST__)
  { "C", 1, "triple", "setup the triple string.",             do_set_tripe  },
#endif

  { "o", 1, "output", "write the result native to output "
                      "file",                                 do_set_output },

  { "R", 0, NULL,     "run root() method after successfully "
                      "load and compile.",                    do_run        },

  { "h", 0, NULL,     "print this help.",                     do_help       },
};
#define NUM_OPTIONS (sizeof(options) / sizeof(struct option_info))

static int parseOption(int argc, char** argv) {
  if (argc <= 1) {
    do_help(argc, argv);
    return 0; // unreachable
  }

  // argv[i] is the current processing arguments from command line
  int i = 1;
  while (i < argc) {
    const unsigned left_argc = argc - i - 1;

    if (argv[i][0] == '-') {
      // Find the corresponding option_info object
      unsigned opt_idx = 0;
      while (opt_idx < NUM_OPTIONS) {
        if (::strcmp(&argv[i][1], options[opt_idx].option_name) == 0) {
          const struct option_info *cur_option = &options[opt_idx];
          if (left_argc < cur_option->min_option_argc) {
            fprintf(stderr, "%s: '%s' requires at least %u arguments", argv[0],
                    cur_option->option_name, cur_option->min_option_argc);
            return 1;
          }

          int result = cur_option->process(left_argc, &argv[i]);
          if (result >= 0) {
            // consume the used arguments
            i += result;
          } else {
            // error occurs
            return 1;
          }

          break;
        }
        ++opt_idx;
      }
      if (opt_idx >= NUM_OPTIONS) {
        fprintf(stderr, "%s: unrecognized option '%s'", argv[0], argv[i]);
        return 1;
      }
    } else {
      if (inFile == NULL) {
        do_set_input(left_argc, &argv[i]);
      } else {
        fprintf(stderr, "%s: single input file is allowed currently.", argv[0]);
        return 1;
      }
    }
    i++;
  }

  return 0;
}

static BCCScriptRef loadScript() {
  if (!inFile) {
    fprintf(stderr, "input file required\n");
    return NULL;
  }

  BCCScriptRef script = bccCreateScript();

  if (bccReadFile(script, inFile, /* flags */BCC_SKIP_DEP_SHA1) != 0) {
    fprintf(stderr, "bcc: FAILS to read bitcode");
    bccDisposeScript(script);
    return NULL;
  }

  bccRegisterSymbolCallback(script, lookupSymbol, NULL);

  char *output = NULL;
  const char *outDir, *outFilename;

  if (outFile != NULL) {
    // Copy the outFile since we're going to modify it
    size_t outFileLen = strlen(outFile);
    output = new char [outFileLen + 1];
    strncpy(output, outFile, outFileLen);
  } else {
    output = new char [(sizeof(DEFAULT_OUTPUT_FILENAME) - 1) + 1];
    strncpy(output, DEFAULT_OUTPUT_FILENAME, sizeof(DEFAULT_OUTPUT_FILENAME) - 1);
  }

  char *lastSlash = strrchr(output, '/');
  if (lastSlash) {
    outDir = output;
    *lastSlash = '\0';
    // *lastSlash should not be the last character. We checked it in
    // do_set_output().
    outFilename = lastSlash + 1;
  } else {
    // no slash found
    outDir = ".";
    outFilename = output;
  }

  if (bccPrepareExecutable(script, outDir, outFilename, /* flags */0) != 0) {
    fprintf(stderr, "bcc: FAILS to prepare executable.\n");
    delete [] output;
    bccDisposeScript(script);
    return NULL;
  }

  delete [] output;

  return script;
}

static int runMain(BCCScriptRef script) {
  MainPtr mainPointer = (MainPtr)bccGetFuncAddr(script, "main");

  if (!mainPointer) {
    mainPointer = (MainPtr)bccGetFuncAddr(script, "root");
  }
  if (!mainPointer) {
    mainPointer = (MainPtr)bccGetFuncAddr(script, "_Z4rootv");
  }
  if (!mainPointer) {
    fprintf(stderr, "Could not find root or main or mangled root.\n");
    return 1;
  }

  fprintf(stderr, "Executing compiled code:\n");

  int result = run(mainPointer);
  fprintf(stderr, "result: %d\n", result);

  return 0;
}

int main(int argc, char** argv) {
  if(parseOption(argc, argv)) {
    return 1;
  }

  BCCScriptRef script;

  if((script = loadScript()) == NULL) {
    fprintf(stderr, "failed to load source\n");
    return 2;
  }

  if(runResults && runMain(script)) {
    fprintf(stderr, "failed to execute\n");
    return 6;
  }

  bccDisposeScript(script);

  return 0;
}

/*
 * Functions to process the command line option.
 */
#if defined(__HOST__)
static int do_set_tripe(int, char **arg) {
  TARGET_TRIPLE_STRING = arg[0];
  return 1;
}
#endif

static int do_set_input(int, char **arg) {
  // Check the input file path
  struct stat statInFile;
  if (stat(arg[0], &statInFile) < 0) {
    fprintf(stderr, "Unable to stat input file: %s\n", strerror(errno));
    return -1;
  }

  if (!S_ISREG(statInFile.st_mode)) {
    fprintf(stderr, "Input file should be a regular file.\n");
    return -1;
  }

  inFile = arg[0];
  return 0;
}

static int do_set_output(int, char **arg) {
  char *lastSlash = strrchr(arg[1], '/');
  if ((lastSlash != NULL) && *(lastSlash + 1) == '\0') {
    fprintf(stderr, "bcc: output file cannot ends with '/'.");
    return -1;
  }

  outFile = arg[1];
  return 1;
}

static int do_run(int, char **) {
  runResults = true;
  return 0;
}

static int do_help(int, char **) {
  printf("Usage: bcc [OPTION]... [input file]\n\n");
  for (unsigned i = 0; i < NUM_OPTIONS; i++) {
    const struct option_info *opt = &options[i];

    printf("\t-%s", opt->option_name);
    if (opt->argument_desc)
      printf(" %s ", opt->argument_desc);
    printf("\t%s\n", opt->help_message);
  }
  exit(0);
}
