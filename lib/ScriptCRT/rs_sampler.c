#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_core.h"

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
