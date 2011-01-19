#include "rs_types.rsh"

extern float4 __attribute__((overloadable)) convert_float4(uchar4 c);

#define BCC_PREPARE_BC 1
#include "rs_core.rsh"
