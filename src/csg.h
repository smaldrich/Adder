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
} csg_MeshEdge;

typedef struct {
    int64_t vertCount;
    HMM_Vec2* verts;

    int64_t edgeCount;
    csg_MeshEdge* edges;

    PoolAlloc* alloc;
} csg_Mesh;

csg_Mesh csg_meshInit(PoolAlloc* alloc) {
    csg_Mesh out;
    memset(&out, 0, sizeof(out));

    out.alloc = alloc;
    out.verts = poolAllocAlloc(alloc, 0);
    out.edges = poolAllocAlloc(alloc, 0);
    return out;
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
    HMM_Vec2 splitNormal;  // FIXME: redundant?
    csg_BSPNodeKind kind;

    csg_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    HMM_Vec2 point1;  // FIXME: redundant?
    HMM_Vec2 point2;
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

            float p1Dot = HMM_DotV2(HMM_SubV2(node->point1, parent->point1), parent->splitNormal);
            float p2Dot = HMM_DotV2(HMM_SubV2(node->point2, parent->point1), parent->splitNormal);
            if (p1Dot > 0 && p2Dot > 0) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (p1Dot <= 0 && p2Dot <= 0) {
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

    for (int64_t i = 0; i < mesh->edgeCount; i++) {
        const csg_MeshEdge* edge = &mesh->edges[i];
        HMM_Vec2 p1 = mesh->verts[edge->aIdx];
        HMM_Vec2 p2 = mesh->verts[edge->bIdx];

        csg_BSPNode* node = BUMP_PUSH_NEW(arena, csg_BSPNode);
        node->nextAvailible = tree;
        tree = node;

        HMM_Vec2 diff = HMM_SubV2(p2, p1);
        node->splitNormal = HMM_V2(-diff.Y, diff.X);
        node->point1 = p1;
        node->point2 = p2;
    }

    _csg_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool csg_BSPContainsPoint(csg_BSPNode* tree, HMM_Vec2 point) {
    csg_BSPNode* node = tree;
    while (node->kind != CSG_BSPNK_LEAF_OUTSIDE_MESH && node->kind != CSG_BSPNK_LEAF_WITHIN_MESH) {
        HMM_Vec2 diff = HMM_SubV2(point, node->point1);
        float dot = HMM_DotV2(diff, node->splitNormal);
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
// }

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

        *poolAllocPushArray(p, mesh.verts, mesh.vertCount, HMM_Vec2) = HMM_V2(0, 0);
        *poolAllocPushArray(p, mesh.verts, mesh.vertCount, HMM_Vec2) = HMM_V2(1, 0);
        *poolAllocPushArray(p, mesh.verts, mesh.vertCount, HMM_Vec2) = HMM_V2(1, 1);

        *poolAllocPushArray(p, mesh.edges, mesh.edgeCount, csg_MeshEdge) = (csg_MeshEdge){
            .aIdx = 0,
            .bIdx = 2,
        };

        *poolAllocPushArray(p, mesh.edges, mesh.edgeCount, csg_MeshEdge) = (csg_MeshEdge){
            .aIdx = 2,
            .bIdx = 1,
        };

        *poolAllocPushArray(p, mesh.edges, mesh.edgeCount, csg_MeshEdge) = (csg_MeshEdge){
            .aIdx = 1,
            .bIdx = 0,
        };

        csg_BSPNode* tree = csg_BSPTreeFromMesh(&mesh, &arena);
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(0.5, 0.5)) == true, "Triangle contains pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(0.5, 1.0)) == false, "Triangle doesn't contain pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(0, 0)) == true, "Triangle contains edge pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(1, 0)) == true, "Triangle contains edge pt 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(-1, 0)) == false, "Triangle doesn't contain point 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V2(INFINITY, NAN)) == false, "Triangle doesn't contain invalid floats");
    }

    bump_free(&arena);
    poolAllocDeinit(p);
}