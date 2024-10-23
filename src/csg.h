#pragma once

#include <assert.h>
#include <inttypes.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"

typedef struct {
    int64_t aIdx;
    int64_t bIdx;
} csg_MeshEdge;

typedef struct {
    int64_t vertCount;
    HMM_Vec2* verts;

    int64_t indexCount;
    int64_t* indicies;

    PoolAlloc* alloc;
} csg_Mesh;

void csg_meshAppendVert(csg_Mesh* mesh, HMM_Vec2 vert) {
}

void csg_meshAppendEdge(csg_Mesh* mesh, HMM_Vec2 vert) {
}

void csg_union(csg_Mesh* meshA) {
}