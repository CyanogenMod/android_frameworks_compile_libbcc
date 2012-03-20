#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_core.h"

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
