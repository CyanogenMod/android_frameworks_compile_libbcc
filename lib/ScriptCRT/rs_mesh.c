#include "rs_core.rsh"
#include "rs_graphics.rsh"
#include "rs_structs.h"

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
