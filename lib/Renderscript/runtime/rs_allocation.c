#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_structs.h"

// Opaque Allocation type operations
extern uint32_t __attribute__((overloadable))
    rsAllocationGetDimX(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.drvState.lod[0].dimX;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimY(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.drvState.lod[0].dimY;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimZ(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.drvState.lod[0].dimZ;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimLOD(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.state.hasMipmaps;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimFaces(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.state.hasFaces;
}

extern const void * __attribute__((overloadable))
        rsGetElementAt(rs_allocation a, uint32_t x) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    return &p[eSize * x];
}

extern const void * __attribute__((overloadable))
        rsGetElementAt(rs_allocation a, uint32_t x, uint32_t y) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    const uint32_t stride = alloc->mHal.drvState.lod[0].stride;
    return &p[(eSize * x) + (y * stride)];
}

extern const void * __attribute__((overloadable))
        rsGetElementAt(rs_allocation a, uint32_t x, uint32_t y, uint32_t z) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    const uint32_t stride = alloc->mHal.drvState.lod[0].stride;
    const uint32_t dimY = alloc->mHal.drvState.lod[0].dimY;
    return &p[(eSize * x) + (y * stride) + (z * stride * dimY)];
}

extern rs_element __attribute__((overloadable))
        rsAllocationGetElement(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    if (alloc == NULL) {
        rs_element nullElem = {0};
        return nullElem;
    }
    Type_t *type = (Type_t *)alloc->mHal.state.type;
    rs_element returnElem = {type->mHal.state.element};
    return returnElem;
}

// TODO: this needs to be optimized, obviously
static void memcpy(void* dst, void* src, size_t size) {
    char* dst_c = (char*) dst, *src_c = (char*) src;
    for (; size > 0; size--) {
        *dst_c++ = *src_c++;
    }
}

extern void __attribute__((overloadable))
        rsSetElementAt(rs_allocation a, void* ptr, uint32_t x) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    memcpy((void*)&p[eSize * x], ptr, eSize);
}

extern void __attribute__((overloadable))
        rsSetElementAt(rs_allocation a, void* ptr, uint32_t x, uint32_t y) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    const uint32_t stride = alloc->mHal.drvState.lod[0].stride;
    memcpy((void*)&p[(eSize * x) + (y * stride)], ptr, eSize);
}

#define SET_ELEMENT_AT(T)                                               \
    extern void __attribute__((overloadable))                           \
    __rsSetElementAt_##T(rs_allocation a, T val, uint32_t x) {          \
        Allocation_t *alloc = (Allocation_t *)a.p;                      \
        const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr; \
        const uint32_t eSize = sizeof(T);                               \
        *((T*)&p[(eSize * x)]) = val;                                   \
    }                                                                   \
    extern void __attribute__((overloadable))                           \
    __rsSetElementAt_##T(rs_allocation a, T val, uint32_t x, uint32_t y) { \
        Allocation_t *alloc = (Allocation_t *)a.p;                      \
        const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.lod[0].mallocPtr; \
        const uint32_t eSize = sizeof(T);                               \
        const uint32_t stride = alloc->mHal.drvState.lod[0].stride;     \
        *((T*)&p[(eSize * x) + (y * stride)]) = val;                    \
    }

SET_ELEMENT_AT(char)
SET_ELEMENT_AT(char2)
SET_ELEMENT_AT(char3)
SET_ELEMENT_AT(char4)
SET_ELEMENT_AT(uchar)
SET_ELEMENT_AT(uchar2)
SET_ELEMENT_AT(uchar3)
SET_ELEMENT_AT(uchar4)
SET_ELEMENT_AT(short)
SET_ELEMENT_AT(short2)
SET_ELEMENT_AT(short3)
SET_ELEMENT_AT(short4)
SET_ELEMENT_AT(ushort)
SET_ELEMENT_AT(ushort2)
SET_ELEMENT_AT(ushort3)
SET_ELEMENT_AT(ushort4)
SET_ELEMENT_AT(int)
SET_ELEMENT_AT(int2)
SET_ELEMENT_AT(int3)
SET_ELEMENT_AT(int4)
SET_ELEMENT_AT(uint)
SET_ELEMENT_AT(uint2)
SET_ELEMENT_AT(uint3)
SET_ELEMENT_AT(uint4)
SET_ELEMENT_AT(long)
SET_ELEMENT_AT(long2)
SET_ELEMENT_AT(long3)
SET_ELEMENT_AT(long4)
SET_ELEMENT_AT(ulong)
SET_ELEMENT_AT(ulong2)
SET_ELEMENT_AT(ulong3)
SET_ELEMENT_AT(ulong4)
SET_ELEMENT_AT(float)
SET_ELEMENT_AT(float2)
SET_ELEMENT_AT(float3)
SET_ELEMENT_AT(float4)
SET_ELEMENT_AT(double)
SET_ELEMENT_AT(double2)
SET_ELEMENT_AT(double3)
SET_ELEMENT_AT(double4)

#undef SET_ELEMENT_AT
