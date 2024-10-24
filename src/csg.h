#pragma once

#include <assert.h>
#include <inttypes.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "base/allocators.h"

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

typedef enum {
    CSG_BSPNK_INVALID,
    CSG_BSPNK_SPLIT,
    CSG_BSPNK_LEAF_WITHIN_MESH,
    CSG_BSPNK_LEAF_OUTSIDE_MESH
} csg_BSPNodeKind;

typedef enum {
    CSG_CK_OUTSIDE,
    CSG_CK_WITHIN,
    CSG_CK_ON_THE_EDGE,
} csg_ContainKind;

typedef struct csg_BSPNode csg_BSPNode;
struct csg_BSPNode {
    csg_BSPNode* outerTree;
    csg_BSPNode* innerTree;
    HMM_Vec2 splitOrigin;
    HMM_Vec2 splitNormal;
    csg_BSPNodeKind kind;

    csg_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares
};

// parent assumed non-null, new nodes allocated into arena
// takes a list of nodes with normals and origins filled out, organizes it + its children into the tree shape based on splits
void _csg_BSPTreeFixNode(BumpAlloc* arena, csg_BSPNode* parent, csg_BSPNode* listOfPossibleNodes) {
    csg_BSPNode* innerList = NULL;
    csg_BSPNode* outerList = NULL;
    {
        csg_BSPNode* next = NULL;
        for (csg_BSPNode* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            if (node in) {
                node->nextAvailible = innerList;
                innerList = node;
            } else if (node out) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (split) {
            }
        }
    }

    parent->innerTree = innerList;
    parent->outerTree = outerList;
    parent->kind = CSG_BSPNK_SPLIT;

    if (parent->innerTree == NULL) {
        parent->innerTree = BUMP_PUSH_NEW(arena, csg_BSPNode);
        parent->innerTree->kind = CSG_CK_WITHIN;
    } else {
        _csg_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree == NULL) {
        parent->outerTree = BUMP_PUSH_NEW(arena, csg_BSPNode);
        parent->outerTree->kind = CSG_CK_OUTSIDE;
    } else {
        _csg_BSPTreeFixNode(arena, parent->outerTree, innerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the mesh, allocated entirely inside arena
// Normals calculated with out being CW
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
        node->splitOrigin = p1;
    }

    _csg_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

void csg_isPointWithinMesh(csg_Mesh* mesh, HMM_Vec2 point) {
    for (int i = 0; i < mesh->edgeCount; i++) {
        csg_MeshEdge e = mesh->edges[i];
        HMM_Vec2 p1 = mesh->verts[e.aIdx];
        HMM_Vec2 p2 = mesh->verts[e.bIdx];
        HMM_Vec2 edgeDiff = HMM_SubV2(p2, p1);
        HMM_Vec2 normal = HMM_Vec2(0, 0);
    }
}

void csg_difference(csg_Mesh* meshA, csg_Mesh* meshB) {
}

void csg_union(csg_Mesh* meshA, csg_Mesh* meshB) {
    // 1: find a out B
    // 2: find B out A
}