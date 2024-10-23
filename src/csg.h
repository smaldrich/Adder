#include <assert.h>
#include <inttypes.h>

#include "HMM/HandmadeMath.h"

typedef struct {
    int64_t aIdx;
    int64_t bIdx;
} csg_MeshEdge;

typedef struct {
    int64_t vertCount;
    HMM_Vec2* verts;

    int64_t indexCount;
    int64_t* indicies;
} csg_Mesh;

void csg_meshAppendVert(csg_Mesh* mesh, HMM_Vec2 vert) {
    if (mesh->vertCount + 1 >= mesh->vertCapacity) {
        mesh->verts = realloc(mesh->verts, mesh->inde);
        assert(mesh->verts != NULL);
    }
}

void csg_meshAppendEdge(csg_Mesh* mesh, HMM_Vec2 vert) {
}

void csg_union(csg_Mesh* meshA) {
}