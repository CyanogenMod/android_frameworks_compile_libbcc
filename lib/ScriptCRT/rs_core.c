#include "rs_core.rsh"
#include "rs_graphics.rsh"
/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class Allocation owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsAllocation.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsAllocationGetDimX(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * allocations.
 *
 *****************************************************************************/
typedef enum {
    RS_ALLOCATION_MIPMAP_NONE = 0,
    RS_ALLOCATION_MIPMAP_FULL = 1,
    RS_ALLOCATION_MIPMAP_ON_SYNC_TO_TEXTURE = 2
} rs_allocation_mipmap_control;

typedef struct Allocation {
    char __pad[28];
    struct {
        void * drv;
        struct {
            const void *type;
            uint32_t usageFlags;
            rs_allocation_mipmap_control mipmapControl;
            uint32_t dimensionX;
            uint32_t dimensionY;
            uint32_t dimensionZ;
            uint32_t elementSizeBytes;
            bool hasMipmaps;
            bool hasFaces;
            bool hasReferences;
            void * usrPtr;
            int32_t surfaceTextureID;
            void * wndSurface;
        } state;

        struct DrvState {
            void * mallocPtr;
        } drvState;
    } mHal;
} Allocation_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class ProgramStore owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsProgramStore.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsgProgramStoreGetDepthFunc(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * program store.
 *
 *****************************************************************************/
typedef struct ProgramStore {
    char __pad[36];
    struct {
        struct {
            bool ditherEnable;
            bool colorRWriteEnable;
            bool colorGWriteEnable;
            bool colorBWriteEnable;
            bool colorAWriteEnable;
            rs_blend_src_func blendSrc;
            rs_blend_dst_func blendDst;
            bool depthWriteEnable;
            rs_depth_func depthFunc;
        } state;
    } mHal;
} ProgramStore_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class ProgramRaster owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsProgramRaster.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsgProgramRasterGetCullMode(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * program raster.
 *
 *****************************************************************************/
typedef struct ProgramRaster {
    char __pad[36];
    struct {
        struct {
            bool pointSprite;
            rs_cull_mode cull;
        } state;
    } mHal;
} ProgramRaster_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class Sampler owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsSampler.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsgProgramRasterGetMagFilter(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * samplers.
 *
 *****************************************************************************/
typedef struct Sampler {
    char __pad[32];
    struct {
        struct {
            rs_sampler_value magFilter;
            rs_sampler_value minFilter;
            rs_sampler_value wrapS;
            rs_sampler_value wrapT;
            rs_sampler_value wrapR;
            float aniso;
        } state;
    } mHal;
} Sampler_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class Element owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsElement.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsElementGetSubElementCount(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * elements.
 *
 *****************************************************************************/
typedef struct Element {
    char __pad[28];
    struct {
        void *drv;
        struct {
            rs_data_type dataType;
            rs_data_kind dataKind;
            uint32_t vectorSize;
            uint32_t elementSizeBytes;

            // Subelements
            const void **fields;
            uint32_t *fieldArraySizes;
            const char **fieldNames;
            uint32_t *fieldNameLengths;
            uint32_t *fieldOffsetBytes;
            uint32_t fieldsCount;
        } state;
    } mHal;
} Element_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class Type owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsType.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsAllocationGetElement(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * types.
 *
 *****************************************************************************/
typedef struct Type {
    char __pad[28];
    struct {
        void *drv;
        struct {
            const void * element;
            uint32_t dimX;
            uint32_t dimY;
            uint32_t dimZ;
            bool dimLOD;
            bool faces;
        } state;
    } mHal;
} Type_t;

/*****************************************************************************
 * CAUTION
 *
 * The following structure layout provides a more efficient way to access
 * internal members of the C++ class Mesh owned by librs. Unfortunately,
 * since this class has virtual members, we can't simply use offsetof() or any
 * other compiler trickery to dynamically get the appropriate values at
 * build-time. This layout may need to be updated whenever
 * frameworks/base/libs/rs/rsMesh.h is modified.
 *
 * Having the layout information available in this file allows us to
 * accelerate functionality like rsMeshGetVertexAllocationCount(). Without this
 * information, we would not be able to inline the bitcode, thus resulting in
 * potential runtime performance penalties for tight loops operating on
 * meshes.
 *
 *****************************************************************************/
typedef struct Mesh {
    char __pad[28];
    struct {
        void *drv;
        struct {
            void **vertexBuffers;
            uint32_t vertexBuffersCount;

            // indexBuffers[i] could be NULL, in which case only primitives[i] is used
            void **indexBuffers;
            uint32_t indexBuffersCount;
            rs_primitive *primitives;
            uint32_t primitivesCount;
        } state;
    } mHal;
} Mesh_t;


/* Declaration of 4 basic functions in libRS */
extern void __attribute__((overloadable))
    rsDebug(const char *, float, float);
extern void __attribute__((overloadable))
    rsDebug(const char *, float, float, float);
extern void __attribute__((overloadable))
    rsDebug(const char *, float, float, float, float);
extern float4 __attribute__((overloadable)) convert_float4(uchar4 c);

/* Implementation of Core Runtime */

extern void __attribute__((overloadable)) rsDebug(const char *s, float2 v) {
    rsDebug(s, v.x, v.y);
}

extern void __attribute__((overloadable)) rsDebug(const char *s, float3 v) {
    rsDebug(s, v.x, v.y, v.z);
}

extern void __attribute__((overloadable)) rsDebug(const char *s, float4 v) {
    rsDebug(s, v.x, v.y, v.z, v.w);
}

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

extern float4 rsUnpackColor8888(uchar4 c)
{
    float4 ret = (float4)0.003921569f;
    ret *= convert_float4(c);
    return ret;
}

/////////////////////////////////////////////////////
// Matrix ops
/////////////////////////////////////////////////////

extern void __attribute__((overloadable))
rsMatrixSet(rs_matrix4x4 *m, uint32_t row, uint32_t col, float v) {
    m->m[row * 4 + col] = v;
}

extern float __attribute__((overloadable))
rsMatrixGet(const rs_matrix4x4 *m, uint32_t row, uint32_t col) {
    return m->m[row * 4 + col];
}

extern void __attribute__((overloadable))
rsMatrixSet(rs_matrix3x3 *m, uint32_t row, uint32_t col, float v) {
    m->m[row * 3 + col] = v;
}

extern float __attribute__((overloadable))
rsMatrixGet(const rs_matrix3x3 *m, uint32_t row, uint32_t col) {
    return m->m[row * 3 + col];
}

extern void __attribute__((overloadable))
rsMatrixSet(rs_matrix2x2 *m, uint32_t row, uint32_t col, float v) {
    m->m[row * 2 + col] = v;
}

extern float __attribute__((overloadable))
rsMatrixGet(const rs_matrix2x2 *m, uint32_t row, uint32_t col) {
    return m->m[row * 2 + col];
}


extern float4 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix4x4 *m, float4 in) {
    float4 ret;
    ret.x = (m->m[0] * in.x) + (m->m[4] * in.y) + (m->m[8] * in.z) + (m->m[12] * in.w);
    ret.y = (m->m[1] * in.x) + (m->m[5] * in.y) + (m->m[9] * in.z) + (m->m[13] * in.w);
    ret.z = (m->m[2] * in.x) + (m->m[6] * in.y) + (m->m[10] * in.z) + (m->m[14] * in.w);
    ret.w = (m->m[3] * in.x) + (m->m[7] * in.y) + (m->m[11] * in.z) + (m->m[15] * in.w);
    return ret;
}
extern float4 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix4x4 *m, float4 in) {
    return rsMatrixMultiply((const rs_matrix4x4 *)m, in);
}

extern float4 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix4x4 *m, float3 in) {
    float4 ret;
    ret.x = (m->m[0] * in.x) + (m->m[4] * in.y) + (m->m[8] * in.z) + m->m[12];
    ret.y = (m->m[1] * in.x) + (m->m[5] * in.y) + (m->m[9] * in.z) + m->m[13];
    ret.z = (m->m[2] * in.x) + (m->m[6] * in.y) + (m->m[10] * in.z) + m->m[14];
    ret.w = (m->m[3] * in.x) + (m->m[7] * in.y) + (m->m[11] * in.z) + m->m[15];
    return ret;
}
extern float4 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix4x4 *m, float3 in) {
    return rsMatrixMultiply((const rs_matrix4x4 *)m, in);
}

extern float4 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix4x4 *m, float2 in) {
    float4 ret;
    ret.x = (m->m[0] * in.x) + (m->m[4] * in.y) + m->m[12];
    ret.y = (m->m[1] * in.x) + (m->m[5] * in.y) + m->m[13];
    ret.z = (m->m[2] * in.x) + (m->m[6] * in.y) + m->m[14];
    ret.w = (m->m[3] * in.x) + (m->m[7] * in.y) + m->m[15];
    return ret;
}
extern float4 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix4x4 *m, float2 in) {
    return rsMatrixMultiply((const rs_matrix4x4 *)m, in);
}

extern float3 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix3x3 *m, float3 in) {
    float3 ret;
    ret.x = (m->m[0] * in.x) + (m->m[3] * in.y) + (m->m[6] * in.z);
    ret.y = (m->m[1] * in.x) + (m->m[4] * in.y) + (m->m[7] * in.z);
    ret.z = (m->m[2] * in.x) + (m->m[5] * in.y) + (m->m[8] * in.z);
    return ret;
}
extern float3 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix3x3 *m, float3 in) {
    return rsMatrixMultiply((const rs_matrix3x3 *)m, in);
}


extern float3 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix3x3 *m, float2 in) {
    float3 ret;
    ret.x = (m->m[0] * in.x) + (m->m[3] * in.y);
    ret.y = (m->m[1] * in.x) + (m->m[4] * in.y);
    ret.z = (m->m[2] * in.x) + (m->m[5] * in.y);
    return ret;
}
extern float3 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix3x3 *m, float2 in) {
    return rsMatrixMultiply((const rs_matrix3x3 *)m, in);
}

extern float2 __attribute__((overloadable))
rsMatrixMultiply(const rs_matrix2x2 *m, float2 in) {
    float2 ret;
    ret.x = (m->m[0] * in.x) + (m->m[2] * in.y);
    ret.y = (m->m[1] * in.x) + (m->m[3] * in.y);
    return ret;
}
extern float2 __attribute__((overloadable))
rsMatrixMultiply(rs_matrix2x2 *m, float2 in) {
    return rsMatrixMultiply((const rs_matrix2x2 *)m, in);
}

/////////////////////////////////////////////////////
// int ops
/////////////////////////////////////////////////////

/*extern uint __attribute__((overloadable, always_inline)) rsClamp(uint amount, uint low, uint high) {
    return amount < low ? low : (amount > high ? high : amount);
}*/
extern int __attribute__((overloadable, always_inline)) rsClamp(int amount, int low, int high) {
    return amount < low ? low : (amount > high ? high : amount);
}
extern ushort __attribute__((overloadable, always_inline)) rsClamp(ushort amount, ushort low, ushort high) {
    return amount < low ? low : (amount > high ? high : amount);
}
extern short __attribute__((overloadable, always_inline)) rsClamp(short amount, short low, short high) {
    return amount < low ? low : (amount > high ? high : amount);
}
extern uchar __attribute__((overloadable, always_inline)) rsClamp(uchar amount, uchar low, uchar high) {
    return amount < low ? low : (amount > high ? high : amount);
}
extern char __attribute__((overloadable, always_inline)) rsClamp(char amount, char low, char high) {
    return amount < low ? low : (amount > high ? high : amount);
}

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
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    return &p[eSize * x];
}

extern const void * __attribute__((overloadable))
        rsGetElementAt(rs_allocation a, uint32_t x, uint32_t y) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    const uint32_t dimX = alloc->mHal.state.dimensionX;
    return &p[eSize * (x + y * dimX)];
}

extern const void * __attribute__((overloadable))
        rsGetElementAt(rs_allocation a, uint32_t x, uint32_t y, uint32_t z) {
    Allocation_t *alloc = (Allocation_t *)a.p;
    const uint8_t *p = (const uint8_t *)alloc->mHal.drvState.mallocPtr;
    const uint32_t eSize = alloc->mHal.state.elementSizeBytes;
    const uint32_t dimX = alloc->mHal.state.dimensionX;
    const uint32_t dimY = alloc->mHal.state.dimensionY;
    return &p[eSize * (x + y * dimX + z * dimX * dimY)];
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

/**
* Program Store
*/
extern rs_depth_func __attribute__((overloadable))
        rsgProgramStoreGetDepthFunc(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return RS_DEPTH_FUNC_INVALID;
    }
    return prog->mHal.state.depthFunc;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetDepthMask(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.depthWriteEnable;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetColorMaskR(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.colorRWriteEnable;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetColorMaskG(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.colorGWriteEnable;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetColorMaskB(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.colorBWriteEnable;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetColorMaskA(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.colorAWriteEnable;
}

extern rs_blend_src_func __attribute__((overloadable))
        rsgProgramStoreGetBlendSrcFunc(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return RS_BLEND_SRC_INVALID;
    }
    return prog->mHal.state.blendSrc;
}

extern rs_blend_dst_func __attribute__((overloadable))
        rsgProgramStoreGetBlendDstFunc(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return RS_BLEND_DST_INVALID;
    }
    return prog->mHal.state.blendDst;
}

extern bool __attribute__((overloadable))
        rsgProgramStoreGetDitherEnabled(rs_program_store ps) {
    ProgramStore_t *prog = (ProgramStore_t *)ps.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.ditherEnable;
}

/**
* Program Raster
*/
extern bool __attribute__((overloadable))
        rsgProgramRasterGetPointSpriteEnabled(rs_program_raster pr) {
    ProgramRaster_t *prog = (ProgramRaster_t *)pr.p;
    if (prog == NULL) {
        return false;
    }
    return prog->mHal.state.pointSprite;
}

extern rs_cull_mode __attribute__((overloadable))
        rsgProgramRasterGetCullMode(rs_program_raster pr) {
    ProgramRaster_t *prog = (ProgramRaster_t *)pr.p;
    if (prog == NULL) {
        return RS_CULL_INVALID;
    }
    return prog->mHal.state.cull;
}

/**
* Sampler
*/
extern rs_sampler_value __attribute__((overloadable))
        rsgSamplerGetMinification(rs_sampler s) {
    Sampler_t *prog = (Sampler_t *)s.p;
    if (prog == NULL) {
        return RS_SAMPLER_INVALID;
    }
    return prog->mHal.state.minFilter;
}

extern rs_sampler_value __attribute__((overloadable))
        rsgSamplerGetMagnification(rs_sampler s) {
    Sampler_t *prog = (Sampler_t *)s.p;
    if (prog == NULL) {
        return RS_SAMPLER_INVALID;
    }
    return prog->mHal.state.magFilter;
}

extern rs_sampler_value __attribute__((overloadable))
        rsgSamplerGetWrapS(rs_sampler s) {
    Sampler_t *prog = (Sampler_t *)s.p;
    if (prog == NULL) {
        return RS_SAMPLER_INVALID;
    }
    return prog->mHal.state.wrapS;
}

extern rs_sampler_value __attribute__((overloadable))
        rsgSamplerGetWrapT(rs_sampler s) {
    Sampler_t *prog = (Sampler_t *)s.p;
    if (prog == NULL) {
        return RS_SAMPLER_INVALID;
    }
    return prog->mHal.state.wrapT;
}

extern float __attribute__((overloadable))
        rsgSamplerGetAnisotropy(rs_sampler s) {
    Sampler_t *prog = (Sampler_t *)s.p;
    if (prog == NULL) {
        return 0.0f;
    }
    return prog->mHal.state.aniso;
}

/**
* Mesh
*/
extern uint32_t __attribute__((overloadable))
        rsMeshGetVertexAllocationCount(rs_mesh m) {
    Mesh_t *mesh = (Mesh_t *)m.p;
    if (mesh == NULL) {
        return 0;
    }
    return mesh->mHal.state.vertexBuffersCount;
}

extern uint32_t __attribute__((overloadable))
        rsMeshGetPrimitiveCount(rs_mesh m) {
    Mesh_t *mesh = (Mesh_t *)m.p;
    if (mesh == NULL) {
        return 0;
    }
    return mesh->mHal.state.primitivesCount;
}

extern rs_allocation __attribute__((overloadable))
        rsMeshGetVertexAllocation(rs_mesh m, uint32_t index) {
    Mesh_t *mesh = (Mesh_t *)m.p;
    if (mesh == NULL || index >= mesh->mHal.state.vertexBuffersCount) {
        rs_allocation nullAlloc = {0};
        return nullAlloc;
    }
    rs_allocation returnAlloc = {mesh->mHal.state.vertexBuffers[index]};
    return returnAlloc;
}

extern rs_allocation __attribute__((overloadable))
        rsMeshGetIndexAllocation(rs_mesh m, uint32_t index) {
    Mesh_t *mesh = (Mesh_t *)m.p;
    if (mesh == NULL || index >= mesh->mHal.state.primitivesCount) {
        rs_allocation nullAlloc = {0};
        return nullAlloc;
    }
    rs_allocation returnAlloc = {mesh->mHal.state.indexBuffers[index]};
    return returnAlloc;
}

extern rs_primitive __attribute__((overloadable))
        rsMeshGetPrimitive(rs_mesh m, uint32_t index) {
    Mesh_t *mesh = (Mesh_t *)m.p;
    if (mesh == NULL || index >= mesh->mHal.state.primitivesCount) {
        return RS_PRIMITIVE_INVALID;
    }
    return mesh->mHal.state.primitives[index];
}

/**
* Element
*/
extern uint32_t __attribute__((overloadable))
        rsElementGetSubElementCount(rs_element e) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL) {
        return 0;
    }
    return element->mHal.state.fieldsCount;
}

extern rs_element __attribute__((overloadable))
        rsElementGetSubElement(rs_element e, uint32_t index) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL || index >= element->mHal.state.fieldsCount) {
        rs_element nullElem = {0};
        return nullElem;
    }
    rs_element returnElem = {element->mHal.state.fields[index]};
    return returnElem;
}

extern uint32_t __attribute__((overloadable))
        rsElementGetSubElementNameLength(rs_element e, uint32_t index) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL || index >= element->mHal.state.fieldsCount) {
        return 0;
    }
    return element->mHal.state.fieldNameLengths[index];
}

extern uint32_t __attribute__((overloadable))
        rsElementGetSubElementName(rs_element e, uint32_t index, char *name, uint32_t nameLength) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL || index >= element->mHal.state.fieldsCount ||
        nameLength == 0 || name == 0) {
        return 0;
    }

    uint32_t numToCopy = element->mHal.state.fieldNameLengths[index];
    if (nameLength < numToCopy) {
        numToCopy = nameLength;
    }
    // Place the null terminator manually, in case of partial string
    numToCopy --;
    name[numToCopy] = '\0';
    const char *nameSource = element->mHal.state.fieldNames[index];
    for (uint32_t i = 0; i < numToCopy; i ++) {
        name[i] = nameSource[i];
    }
    return numToCopy;
}

extern uint32_t __attribute__((overloadable))
        rsElementGetSubElementArraySize(rs_element e, uint32_t index) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL || index >= element->mHal.state.fieldsCount) {
        return 0;
    }
    return element->mHal.state.fieldArraySizes[index];
}

extern uint32_t __attribute__((overloadable))
        rsElementGetSubElementOffsetBytes(rs_element e, uint32_t index) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL || index >= element->mHal.state.fieldsCount) {
        return 0;
    }
    return element->mHal.state.fieldOffsetBytes[index];
}

extern uint32_t __attribute__((overloadable))
        rsElementGetSizeBytes(rs_element e) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL) {
        return 0;
    }
    return element->mHal.state.elementSizeBytes;
}

extern rs_data_type __attribute__((overloadable))
        rsElementGetDataType(rs_element e) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL) {
        return RS_TYPE_INVALID;
    }
    return element->mHal.state.dataType;
}

extern rs_data_kind __attribute__((overloadable))
        rsElementGetDataKind(rs_element e) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL) {
        return RS_KIND_INVALID;
    }
    return element->mHal.state.dataKind;
}

extern uint32_t __attribute__((overloadable))
        rsElementGetVectorSize(rs_element e) {
    Element_t *element = (Element_t *)e.p;
    if (element == NULL) {
        return 0;
    }
    return element->mHal.state.vectorSize;
}
