#include "runtime/lib/absvdi2.c"
#include "runtime/lib/absvsi2.c"
#include "runtime/lib/addvdi3.c"
#include "runtime/lib/addvsi3.c"
#include "runtime/lib/ashldi3.c"
#ifndef ANDROID
#   include "runtime/lib/ashrdi3.c"
#endif
#include "runtime/lib/clzdi2.c"
#include "runtime/lib/clzsi2.c"
#include "runtime/lib/cmpdi2.c"
#include "runtime/lib/ctzdi2.c"
#include "runtime/lib/ctzsi2.c"
#ifndef ANDROID // no complex.h
#   include "runtime/lib/divdc3.c"
#endif
#include "runtime/lib/divdi3.c"
#ifndef ANDROID // no complex.h
#   include "runtime/lib/divsc3.c"
#endif
#include "runtime/lib/ffsdi2.c"
#include "runtime/lib/fixdfdi.c"
#include "runtime/lib/fixsfdi.c"
#include "runtime/lib/fixunsdfdi.c"
#include "runtime/lib/fixunsdfsi.c"
#include "runtime/lib/fixunssfdi.c"
#include "runtime/lib/fixunssfsi.c"
#include "runtime/lib/floatdidf.c"
#include "runtime/lib/floatdisf.c"
#include "runtime/lib/floatundidf.c"
#include "runtime/lib/floatundisf.c"
#include "runtime/lib/lshrdi3.c"
#include "runtime/lib/moddi3.c"
#ifndef ANDROID // no complex.h
#   include "runtime/lib/muldc3.c"
#endif
#include "runtime/lib/muldi3.c"
#ifndef ANDROID // no complex.h
#   include "runtime/lib/mulsc3.c"
#endif
#include "runtime/lib/mulvdi3.c"
#include "runtime/lib/mulvsi3.c"
#include "runtime/lib/negdi2.c"
#include "runtime/lib/negvdi2.c"
#include "runtime/lib/negvsi2.c"
#include "runtime/lib/paritydi2.c"
#include "runtime/lib/paritysi2.c"
#include "runtime/lib/popcountdi2.c"
#include "runtime/lib/popcountsi2.c"
#include "runtime/lib/powidf2.c"
#include "runtime/lib/powisf2.c"
#include "runtime/lib/subvdi3.c"
#include "runtime/lib/subvsi3.c"
#include "runtime/lib/ucmpdi2.c"
#include "runtime/lib/udivdi3.c"
#include "runtime/lib/udivmoddi4.c"
#include "runtime/lib/umoddi3.c"
#include "runtime/lib/eprintf.c"

#include <string.h>
#include <stdlib.h>

typedef struct {
  const char* mName; 
  void* mPtr;
} RuntimeFunction;

#define USE_VFP_RUNTIME 1

#if defined(__arm__)
  #define DEF_GENERIC_RUNTIME(func)   \
    extern void* func;
  #define DEF_VFP_RUNTIME(func) \
    extern void* func ## vfp;
  #define DEF_LLVM_RUNTIME(func)
#include "bcc_runtime.def"
#endif

static const RuntimeFunction Runtimes[] = {
#if defined(__arm__)
  #define DEF_GENERIC_RUNTIME(func)   \
    { #func, (void*) &func },
  // TODO: enable only when target support VFP
  #define DEF_VFP_RUNTIME(func) \
    { #func, (void*) &func ## vfp },
#else
  // host compiler library must contain generic runtime
  #define DEF_GENERIC_RUNTIME(func)
  #define DEF_VFP_RUNTIME(func)
#endif
#define DEF_LLVM_RUNTIME(func)   \
  { #func, (void*) &func },
#include "bcc_runtime.def"
};

static int CompareRuntimeFunction(const void* a, const void* b) {
  return strcmp(((const RuntimeFunction*) a)->mName, 
               ((const RuntimeFunction*) b)->mName);
}

void* FindRuntimeFunction(const char* Name) {
  /* binary search */
  const RuntimeFunction Key = { Name, NULL };
  const RuntimeFunction* R = 
      bsearch(&Key, 
              Runtimes, 
              sizeof(Runtimes) / sizeof(RuntimeFunction), 
              sizeof(RuntimeFunction),
              CompareRuntimeFunction);
  if(R)
    return R->mPtr;
  else
    return NULL;
}
