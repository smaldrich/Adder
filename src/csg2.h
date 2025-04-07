#pragma once

#include "snooze.h"
#include "geometry.h"
#include "mesh.h"
#include "PoolAlloc.h"

typedef struct csg_Node csg_Node;
struct csg_Node {
    csg_Node* outerTree;
    csg_Node* innerTree;
    HMM_Vec3 origin;
    HMM_Vec3 normal;
};

typedef enum {
    CSG_PR_COPLANAR,
    CSG_PR_WITHIN,
    CSG_PR_OUTSIDE,
    CSG_PR_SPANNING,
} _csg_PlaneRelation;

static _csg_PlaneRelation _csg_triClassify(geo_Tri tri, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    _csg_PlaneRelation finalRel = 0;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(tri.elems[i], planeStart), planeNormal);
        if (geo_floatZero(dot)) {
            finalRel |= CSG_PR_COPLANAR;
        } else {
            finalRel |= dot > 0 ? CSG_PR_OUTSIDE : CSG_PR_WITHIN;
        }
    }
    return finalRel;
}

static void _csg_facesToNodesInner(mesh_FaceSlice faces, int64_t* faceIdx, int64_t* triIdx, csg_Node* parent) {
    geo_Tri tri = faces.elems[*faceIdx].tris.elems[*triIdx];
    (*triIdx)++;

    _csg_PlaneRelation rel = _csg_triClassify(tri, parent->origin, parent->normal);
}

csg_Node* csg_facesToNodes(mesh_FaceSlice faces, snz_Arena* arena) {

    _csg_nodeFix(arena, tree, tree->nextAvailible);
    return tree;
}
