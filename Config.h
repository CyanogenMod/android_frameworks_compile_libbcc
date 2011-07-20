#ifndef BCC_CONFIG_H
#define BCC_CONFIG_H

#include "ConfigFromMk.h"

//---------------------------------------------------------------------------
// Configuration for JIT & MC Assembler
//---------------------------------------------------------------------------
#if !USE_OLD_JIT && !USE_MCJIT
#error "You should choose at least one code generation method."
#endif

//---------------------------------------------------------------------------
// Configuration for Disassembler
//---------------------------------------------------------------------------

#if !USE_OLD_JIT
#undef DEBUG_OLD_JIT_DISASSEMBLER
#define DEBUG_OLD_JIT_DISASSEMBLER 0
#endif

#if !USE_MCJIT
#undef DEBUG_MCJIT_DISASSEMBLER
#define DEBUG_MCJIT_DISASSEMBLER 0
#endif

#if DEBUG_OLD_JIT_DISASSEMBLER || DEBUG_MCJIT_DISASSEMBLER
#define USE_DISASSEMBLER 1
#else
#define USE_DISASSEMBLER 0
#endif

#define DEBUG_OLD_JIT_DISASSEMBLER_FILE "/data/local/tmp/oldjit-dis.s"
#define DEBUG_MCJIT_DISASSEMBLER_FILE "/data/local/tmp/mcjit-dis.s"

//---------------------------------------------------------------------------
// Configuration for ContextManager
//---------------------------------------------------------------------------

// Note: Most of the code should NOT use these constants.  Use the public
// static member of ContextManager instead, which is type-safe.  For example,
// if you need BCC_CONTEXT_FIXED_ADDR_, then you should write:
// ContextManager::ContextFixedAddr

#define BCC_CONTEXT_FIXED_ADDR_ reinterpret_cast<char *>(0x7e000000)

#define BCC_CONTEXT_SLOT_COUNT_ 8

#define BCC_CONTEXT_CODE_SIZE_ (128 * 1024)

#define BCC_CONTEXT_DATA_SIZE_ (128 * 1024)

//---------------------------------------------------------------------------
// Configuration for CodeGen and CompilerRT
//---------------------------------------------------------------------------

#if defined(__arm__)
  #define DEFAULT_ARM_CODEGEN
  #define PROVIDE_ARM_CODEGEN
#elif defined(__i386__)
  #define DEFAULT_X86_CODEGEN
  #define PROVIDE_X86_CODEGEN
#elif defined(__x86_64__)
  #define DEFAULT_X64_CODEGEN
  #define PROVIDE_X64_CODEGEN
#endif

#if defined(FORCE_ARM_CODEGEN)
  #define DEFAULT_ARM_CODEGEN
  #undef DEFAULT_X86_CODEGEN
  #undef DEFAULT_X64_CODEGEN
  #define PROVIDE_ARM_CODEGEN
  #undef PROVIDE_X86_CODEGEN
  #undef PROVIDE_X64_CODEGEN
#elif defined(FORCE_X86_CODEGEN)
  #undef DEFAULT_ARM_CODEGEN
  #define DEFAULT_X86_CODEGEN
  #undef DEFAULT_X64_CODEGEN
  #undef PROVIDE_ARM_CODEGEN
  #define PROVIDE_X86_CODEGEN
  #undef PROVIDE_X64_CODEGEN
#elif defined(FORCE_X64_CODEGEN)
  #undef DEFAULT_ARM_CODEGEN
  #undef DEFAULT_X86_CODEGEN
  #define DEFAULT_X64_CODEGEN
  #undef PROVIDE_ARM_CODEGEN
  #undef PROVIDE_X86_CODEGEN
  #define PROVIDE_X64_CODEGEN
#endif

#if defined(DEFAULT_ARM_CODEGEN)
  #define TARGET_TRIPLE_STRING "armv7-none-linux-gnueabi"
#elif defined(DEFAULT_X86_CODEGEN)
  #define TARGET_TRIPLE_STRING "i686-unknown-linux"
#elif defined(DEFAULT_X64_CODEGEN)
  #define TARGET_TRIPLE_STRING "x86_64-unknown-linux"
#endif

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
  #define ARM_USE_VFP
#endif

//---------------------------------------------------------------------------

#endif // BCC_CONFIG_H
