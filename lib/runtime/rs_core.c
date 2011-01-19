#include "../../../../base/libs/rs/scriptc/rs_types.rsh"

#define bool  int
#define true  1
#define false 0
#define M_PI        3.14159265358979323846264338327950288f   /* pi */

extern float4 __attribute__((overloadable)) convert_float4(uchar4 c);

#define BCC_PREPARE_BC 1
#include "../../../../base/libs/rs/scriptc/rs_core.rsh"
