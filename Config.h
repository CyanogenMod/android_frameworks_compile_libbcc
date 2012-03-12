#ifndef BCC_CONFIG_H
#define BCC_CONFIG_H

#include "ConfigFromMk.h"

//---------------------------------------------------------------------------
// Configuration for JIT & MC Assembler
//---------------------------------------------------------------------------
#if !USE_MCJIT
#error "You should choose at least one code generation method."
#endif

//---------------------------------------------------------------------------
// Configuration for Disassembler
//---------------------------------------------------------------------------

#if !USE_MCJIT
#undef DEBUG_MCJIT_DISASSEMBLER
#define DEBUG_MCJIT_DISASSEMBLER 0
#endif

#if DEBUG_MCJIT_DISASSEMBLER
#define USE_DISASSEMBLER 1
#else
#define USE_DISASSEMBLER 0
#endif

#if defined(__HOST__)
#define DEBUG_MCJIT_DISASSEMBLER_FILE "/tmp/mcjit-dis.s"
#else
#define DEBUG_MCJIT_DISASSEMBLER_FILE "/data/local/tmp/mcjit-dis.s"
#endif // defined(__HOST__)

//---------------------------------------------------------------------------
// Configuration for CodeGen and CompilerRT
//---------------------------------------------------------------------------

#if defined(FORCE_ARM_CODEGEN)
  #define PROVIDE_ARM_CODEGEN
  #define DEFAULT_ARM_CODEGEN

#elif defined(FORCE_MIPS_CODEGEN)
  #define PROVIDE_MIPS_CODEGEN
  #define DEFAULT_MIPS_CODEGEN

#elif defined(FORCE_X86_CODEGEN)
  #define PROVIDE_X86_CODEGEN

  #if defined(__i386__)
    #define DEFAULT_X86_CODEGEN
  #elif defined(__x86_64__)
    #define DEFAULT_X86_64_CODEGEN
  #endif

#else
  #define PROVIDE_ARM_CODEGEN
  #define PROVIDE_MIPS_CODEGEN
  #define PROVIDE_X86_CODEGEN

  #if defined(__arm__)
    #define DEFAULT_ARM_CODEGEN
  #elif defined(__mips__)
    #define DEFAULT_MIPS_CODEGEN
  #elif defined(__i386__)
    #define DEFAULT_X86_CODEGEN
  #elif defined(__x86_64__)
    #define DEFAULT_X86_64_CODEGEN
  #endif
#endif

#if defined(DEFAULT_ARM_CODEGEN)
  #define DEFAULT_TARGET_TRIPLE_STRING "armv7-none-linux-gnueabi"
#elif defined(DEFAULT_MIPS_CODEGEN)
  #define DEFAULT_TARGET_TRIPLE_STRING "mipsel-none-linux-gnueabi"
#elif defined(DEFAULT_X86_CODEGEN)
  #define DEFAULT_TARGET_TRIPLE_STRING "i686-unknown-linux"
#elif defined(DEFAULT_X86_64_CODEGEN)
  #define DEFAULT_TARGET_TRIPLE_STRING "x86_64-unknown-linux"
#endif

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
  #define ARM_USE_VFP
#endif

//---------------------------------------------------------------------------

#endif // BCC_CONFIG_H
