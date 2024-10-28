#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "base/allocators.h"
#include "base/testing.h"

typedef struct {
    int64_t aIdx;
    int64_t bIdx;
    int64_t cIdx;
} csg_MeshTri;

typedef struct {
    int64_t vertCount;
    HMM_Vec3* verts;

    int64_t triCount;
    csg_MeshTri* tris;

    PoolAlloc* alloc;
} csg_Mesh;

csg_Mesh csg_meshInit(PoolAlloc* alloc) {
    csg_Mesh out;
    memset(&out, 0, sizeof(out));

    out.alloc = alloc;
    out.verts = poolAllocAlloc(alloc, 0);
    out.tris = poolAllocAlloc(alloc, 0);
    return out;
}

void csg_meshPushVert(csg_Mesh* mesh, HMM_Vec3 vert) {
    *poolAllocPushArray(mesh->alloc, mesh->verts, mesh->vertCount, HMM_Vec3) = vert;
}

void csg_meshPushTri(csg_Mesh* mesh, csg_MeshTri tri) {
    *poolAllocPushArray(mesh->alloc, mesh->tris, mesh->triCount, csg_MeshTri) = tri;
}

typedef enum {
    CSG_BSPNK_INVALID,
    CSG_BSPNK_SPLIT,
    CSG_BSPNK_LEAF_WITHIN_MESH,
    CSG_BSPNK_LEAF_OUTSIDE_MESH
} csg_BSPNodeKind;

typedef struct csg_BSPNode csg_BSPNode;
struct csg_BSPNode {
    csg_BSPNode* outerTree;
    csg_BSPNode* innerTree;
    HMM_Vec3 splitNormal;  // FIXME: redundant?
    csg_BSPNodeKind kind;

    csg_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    HMM_Vec3 point1;  // FIXME: redundant?
    HMM_Vec3 point2; // FIXME: space inefficent
    HMM_Vec3 point3;
};

// parent assumed non-null, new nodes allocated into arena
// takes a list of nodes with normals and origins filled out, organizes it + its children into the tree shape based on splits
// will allocate more nodes over the course of fixing, these are put into arena
void _csg_BSPTreeFixNode(BumpAlloc* arena, csg_BSPNode* parent, csg_BSPNode* listOfPossibleNodes) {
    csg_BSPNode* innerList = NULL;
    csg_BSPNode* outerList = NULL;
    {
        csg_BSPNode* next = NULL;
        for (csg_BSPNode* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            float p1Dot = HMM_DotV3(HMM_SubV3(node->point1, parent->point1), parent->splitNormal);
            float p2Dot = HMM_DotV3(HMM_SubV3(node->point2, parent->point1), parent->splitNormal);
            float p3Dot = HMM_DotV3(HMM_SubV3(node->point3, parent->point1), parent->splitNormal);
            if (p1Dot > 0 && p2Dot > 0 && p3Dot > 0) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (p1Dot <= 0 && p2Dot <= 0 && p3Dot <= 0) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side
                csg_BSPNode* duplicate = BUMP_PUSH_NEW(arena, csg_BSPNode);
                duplicate->splitNormal = node->splitNormal;
                duplicate->point1 = node->point1;
                duplicate->point2 = node->point2;

                node->nextAvailible = innerList;
                innerList = node;
                duplicate->nextAvailible = outerList;
                outerList = duplicate;
            }
        }
    }

    parent->innerTree = innerList;
    parent->outerTree = outerList;
    parent->kind = CSG_BSPNK_SPLIT;

    if (parent->innerTree == NULL) {
        parent->innerTree = BUMP_PUSH_NEW(arena, csg_BSPNode);
        parent->innerTree->kind = CSG_BSPNK_LEAF_WITHIN_MESH;
    } else {
        _csg_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree == NULL) {
        parent->outerTree = BUMP_PUSH_NEW(arena, csg_BSPNode);
        parent->outerTree->kind = CSG_BSPNK_LEAF_OUTSIDE_MESH;
    } else {
        _csg_BSPTreeFixNode(arena, parent->outerTree, innerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the mesh, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
csg_BSPNode* csg_BSPTreeFromMesh(const csg_Mesh* mesh, BumpAlloc* arena) {
    csg_BSPNode* tree = NULL;

    for (int64_t i = 0; i < mesh->triCount; i++) {
        const csg_MeshTri* tri = &mesh->tris[i];
        HMM_Vec3 p1 = mesh->verts[tri->aIdx];
        HMM_Vec3 p2 = mesh->verts[tri->bIdx];
        HMM_Vec3 p3 = mesh->verts[tri->cIdx];

        csg_BSPNode* node = BUMP_PUSH_NEW(arena, csg_BSPNode);
        node->nextAvailible = tree;
        tree = node;

        node->splitNormal = HMM_Cross(HMM_SubV3(p2, p1), HMM_SubV3(p3, p1)); // itsn't  this cross prod backwards?
        node->point1 = p1;
        node->point2 = p2;
        node->point3 = p3;
    }

    _csg_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool csg_BSPContainsPoint(csg_BSPNode* tree, HMM_Vec3 point) {
    csg_BSPNode* node = tree;
    while (node->kind != CSG_BSPNK_LEAF_OUTSIDE_MESH && node->kind != CSG_BSPNK_LEAF_WITHIN_MESH) {
        HMM_Vec3 diff = HMM_SubV3(point, node->point1);
        float dot = HMM_DotV3(diff, node->splitNormal);
        if (dot <= 0) {
            node = node->innerTree;
        } else {
            node = node->outerTree;
        }
        assert(node != NULL);
    }
    return (node->kind == CSG_BSPNK_LEAF_WITHIN_MESH) ? true : false;
}

// // returns a set of points on meshA that are not in meshB
// void csg_difference(csg_Mesh* meshA, csg_Mesh* meshB) {
//     for (int64_t i = 0; i < meshA->vertCount; i++) {
//     }
// }

/*
mesh ops:
push new vert
push new tri
split tri
edges/tris per vert
glue meshes
*/

// void csg_union(csg_Mesh* meshA, csg_Mesh* meshB) {
//     // 1: find a out B
//     // 2: find B out A
//     // 3: gloopers && profit
// }

void csg_tests() {
    test_printSectionHeader("csg");

    BumpAlloc arena = bump_allocate(1000000, "csg test arena");
    PoolAlloc pool = poolAllocInit();
    PoolAlloc* p = &pool;

    {
        csg_Mesh mesh = csg_meshInit(p);

        csg_meshPushVert(&mesh, HMM_V3(0, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(1, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(1, 1, 0));
        csg_meshPushVert(&mesh, HMM_V3(1, 0, -1));

        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 1, .cIdx = 2 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 2, .cIdx = 3 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 3, .cIdx = 1 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 3, .bIdx = 2, .cIdx = 1 });

        csg_BSPNode* tree = csg_BSPTreeFromMesh(&mesh, &arena);
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Triangle contains pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Triangle doesn't contain pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Triangle contains edge pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Triangle contains edge pt 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Triangle doesn't contain point 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Triangle doesn't contain point 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Triangle doesn't contain invalid floats");
    }

    bump_free(&arena);
    poolAllocDeinit(p);
}