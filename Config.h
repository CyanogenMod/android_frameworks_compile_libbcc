#ifndef BCC_CONFIG_H
#define BCC_CONFIG_H

//---------------------------------------------------------------------------

#define USE_CACHE 1

#define USE_DISASSEMBLER 1

#define USE_DISASSEMBLER_FILE 0

#define USE_LIBBCC_SHA1SUM 1

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
