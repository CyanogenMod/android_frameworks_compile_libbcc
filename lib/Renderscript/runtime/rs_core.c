#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_structs.h"

/* Function declarations from libRS */
extern float4 __attribute__((overloadable)) convert_float4(uchar4 c);

/* Implementation of Core Runtime */

/*
extern uchar4 __attribute__((overloadable)) rsPackColorTo8888(float r, float g, float b)
{
    uchar4 c;
    c.x = (uchar)(r * 255.f + 0.5f);
    c.y = (uchar)(g * 255.f + 0.5f);
    c.z = (uchar)(b * 255.f + 0.5f);
    c.w = 255;
    return c;
}

extern uchar4 __attribute__((overloadable)) rsPackColorTo8888(float r, float g, float b, float a)
{
    uchar4 c;
    c.x = (uchar)(r * 255.f + 0.5f);
    c.y = (uchar)(g * 255.f + 0.5f);
    c.z = (uchar)(b * 255.f + 0.5f);
    c.w = (uchar)(a * 255.f + 0.5f);
    return c;
}

extern uchar4 __attribute__((overloadable)) rsPackColorTo8888(float3 color)
{
    color *= 255.f;
    color += 0.5f;
    uchar4 c = {color.x, color.y, color.z, 255};
    return c;
}

extern uchar4 __attribute__((overloadable)) rsPackColorTo8888(float4 color)
{
    color *= 255.f;
    color += 0.5f;
    uchar4 c = {color.x, color.y, color.z, color.w};
    return c;
}
*/

extern float4 rsUnpackColor8888(uchar4 c)
{
    return convert_float4(c) * 0.003921569f;
}


