#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_structs.h"

// Opaque Allocation type operations
extern uint32_t __attribute__((overloadable))
    rsAllocationGetDimX(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.state.dimensionX;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimY(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.state.dimensionY;
}

extern uint32_t __attribute__((overloadable))
        rsAllocationGetDimZ(rs_allocation a) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    return alloc->mHal.state.dimensionZ;
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
    const uint32_t dimY = alloc->mHal.state.dimensionY;
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

