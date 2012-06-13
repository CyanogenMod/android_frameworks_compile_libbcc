/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "rs_types.rsh"

extern short __attribute__((overloadable, always_inline)) rsClamp(short amount, short low, short high);
extern float4 __attribute__((overloadable)) clamp(float4 amount, float4 low, float4 high);
extern uchar4 __attribute__((overloadable)) convert_uchar4(short4);


/*
 * CLAMP
 */

extern float __attribute__((overloadable)) clamp(float amount, float low, float high) {
    return amount < low ? low : (amount > high ? high : amount);
}

extern float2 __attribute__((overloadable)) clamp(float2 amount, float2 low, float2 high) {
    float2 r;
    r.x = amount.x < low.x ? low.x : (amount.x > high.x ? high.x : amount.x);
    r.y = amount.y < low.y ? low.y : (amount.y > high.y ? high.y : amount.y);
    return r;
}

extern float3 __attribute__((overloadable)) clamp(float3 amount, float3 low, float3 high) {
    float3 r;
    r.x = amount.x < low.x ? low.x : (amount.x > high.x ? high.x : amount.x);
    r.y = amount.y < low.y ? low.y : (amount.y > high.y ? high.y : amount.y);
    r.z = amount.z < low.z ? low.z : (amount.z > high.z ? high.z : amount.z);
    return r;
}

extern float4 __attribute__((overloadable)) clamp(float4 amount, float4 low, float4 high) {
    float4 r;
    r.x = amount.x < low.x ? low.x : (amount.x > high.x ? high.x : amount.x);
    r.y = amount.y < low.y ? low.y : (amount.y > high.y ? high.y : amount.y);
    r.z = amount.z < low.z ? low.z : (amount.z > high.z ? high.z : amount.z);
    r.w = amount.w < low.w ? low.w : (amount.w > high.w ? high.w : amount.w);
    return r;
}

extern float2 __attribute__((overloadable)) clamp(float2 amount, float low, float high) {
    float2 r;
    r.x = amount.x < low ? low : (amount.x > high ? high : amount.x);
    r.y = amount.y < low ? low : (amount.y > high ? high : amount.y);
    return r;
}

extern float3 __attribute__((overloadable)) clamp(float3 amount, float low, float high) {
    float3 r;
    r.x = amount.x < low ? low : (amount.x > high ? high : amount.x);
    r.y = amount.y < low ? low : (amount.y > high ? high : amount.y);
    r.z = amount.z < low ? low : (amount.z > high ? high : amount.z);
    return r;
}

extern float4 __attribute__((overloadable)) clamp(float4 amount, float low, float high) {
    float4 r;
    r.x = amount.x < low ? low : (amount.x > high ? high : amount.x);
    r.y = amount.y < low ? low : (amount.y > high ? high : amount.y);
    r.z = amount.z < low ? low : (amount.z > high ? high : amount.z);
    r.w = amount.w < low ? low : (amount.w > high ? high : amount.w);
    return r;
}


/*
 * FMAX
 */

extern float __attribute__((overloadable)) fmax(float v1, float v2) {
    return v1 > v2 ? v1 : v2;
}

extern float2 __attribute__((overloadable)) fmax(float2 v1, float2 v2) {
    float2 r;
    r.x = v1.x > v2.x ? v1.x : v2.x;
    r.y = v1.y > v2.y ? v1.y : v2.y;
    return r;
}

extern float3 __attribute__((overloadable)) fmax(float3 v1, float3 v2) {
    float3 r;
    r.x = v1.x > v2.x ? v1.x : v2.x;
    r.y = v1.y > v2.y ? v1.y : v2.y;
    r.z = v1.z > v2.z ? v1.z : v2.z;
    return r;
}

extern float4 __attribute__((overloadable)) fmax(float4 v1, float4 v2) {
    float4 r;
    r.x = v1.x > v2.x ? v1.x : v2.x;
    r.y = v1.y > v2.y ? v1.y : v2.y;
    r.z = v1.z > v2.z ? v1.z : v2.z;
    r.w = v1.w > v2.w ? v1.w : v2.w;
    return r;
}

extern float2 __attribute__((overloadable)) fmax(float2 v1, float v2) {
    float2 r;
    r.x = v1.x > v2 ? v1.x : v2;
    r.y = v1.y > v2 ? v1.y : v2;
    return r;
}

extern float3 __attribute__((overloadable)) fmax(float3 v1, float v2) {
    float3 r;
    r.x = v1.x > v2 ? v1.x : v2;
    r.y = v1.y > v2 ? v1.y : v2;
    r.z = v1.z > v2 ? v1.z : v2;
    return r;
}

extern float4 __attribute__((overloadable)) fmax(float4 v1, float v2) {
    float4 r;
    r.x = v1.x > v2 ? v1.x : v2;
    r.y = v1.y > v2 ? v1.y : v2;
    r.z = v1.z > v2 ? v1.z : v2;
    r.w = v1.w > v2 ? v1.w : v2;
    return r;
}

extern float __attribute__((overloadable)) fmin(float v1, float v2) {
    return v1 < v2 ? v1 : v2;
}


/*
 * FMIN
 */
extern float2 __attribute__((overloadable)) fmin(float2 v1, float2 v2) {
    float2 r;
    r.x = v1.x < v2.x ? v1.x : v2.x;
    r.y = v1.y < v2.y ? v1.y : v2.y;
    return r;
}

extern float3 __attribute__((overloadable)) fmin(float3 v1, float3 v2) {
    float3 r;
    r.x = v1.x < v2.x ? v1.x : v2.x;
    r.y = v1.y < v2.y ? v1.y : v2.y;
    r.z = v1.z < v2.z ? v1.z : v2.z;
    return r;
}

extern float4 __attribute__((overloadable)) fmin(float4 v1, float4 v2) {
    float4 r;
    r.x = v1.x < v2.x ? v1.x : v2.x;
    r.y = v1.y < v2.y ? v1.y : v2.y;
    r.z = v1.z < v2.z ? v1.z : v2.z;
    r.w = v1.w < v2.w ? v1.w : v2.w;
    return r;
}

extern float2 __attribute__((overloadable)) fmin(float2 v1, float v2) {
    float2 r;
    r.x = v1.x < v2 ? v1.x : v2;
    r.y = v1.y < v2 ? v1.y : v2;
    return r;
}

extern float3 __attribute__((overloadable)) fmin(float3 v1, float v2) {
    float3 r;
    r.x = v1.x < v2 ? v1.x : v2;
    r.y = v1.y < v2 ? v1.y : v2;
    r.z = v1.z < v2 ? v1.z : v2;
    return r;
}

extern float4 __attribute__((overloadable)) fmin(float4 v1, float v2) {
    float4 r;
    r.x = v1.x < v2 ? v1.x : v2;
    r.y = v1.y < v2 ? v1.y : v2;
    r.z = v1.z < v2 ? v1.z : v2;
    r.w = v1.w < v2 ? v1.w : v2;
    return r;
}

/*
 * YUV
 */
extern uchar4 __attribute__((overloadable)) rsYuvToRGBA_uchar4(uchar y, uchar u, uchar v) {
    short Y = ((short)y) - 16;
    short U = ((short)u) - 128;
    short V = ((short)v) - 128;

    short4 p;
    p.r = (Y * 298 + V * 409 + 128) >> 8;
    p.g = (Y * 298 - U * 100 - V * 208 + 128) >> 8;
    p.b = (Y * 298 + U * 516 + 128) >> 8;
    p.a = 255;
    p.r = rsClamp(p.r, (short)0, (short)255);
    p.g = rsClamp(p.g, (short)0, (short)255);
    p.b = rsClamp(p.b, (short)0, (short)255);

    return convert_uchar4(p);
}

static float4 yuv_U_values = {0.f, -0.392f * 0.003921569f, +2.02 * 0.003921569f, 0.f};
static float4 yuv_V_values = {1.603f * 0.003921569f, -0.815f * 0.003921569f, 0.f, 0.f};

extern float4 __attribute__((overloadable)) rsYuvToRGBA_float4(uchar y, uchar u, uchar v) {
    float4 color = (float)y * 0.003921569f;
    float4 fU = ((float)u) - 128.f;
    float4 fV = ((float)v) - 128.f;

    color += fU * yuv_U_values;
    color += fV * yuv_V_values;
    color = clamp(color, 0.f, 1.f);
    return color;
}

