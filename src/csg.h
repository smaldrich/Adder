#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "base/allocators.h"
#include "base/testing.h"

typedef struct {
    union {
        struct {
            int64_t aIdx;
            int64_t bIdx;
            int64_t cIdx;
        };
        int64_t elems[3];
    };
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
    HMM_Vec3 point2;  // FIXME: space inefficent
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
            if (p1Dot >= 0 && p2Dot >= 0 && p3Dot >= 0) {
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
        _csg_BSPTreeFixNode(arena, parent->outerTree, outerList->nextAvailible);
    }
}

HMM_Vec3 csg_meshTriNormal(csg_MeshTri tri, const csg_Mesh* mesh) {
    HMM_Vec3 p1 = mesh->verts[tri.aIdx];
    HMM_Vec3 p2 = mesh->verts[tri.bIdx];
    HMM_Vec3 p3 = mesh->verts[tri.cIdx];
    return HMM_Cross(HMM_SubV3(p2, p1), HMM_SubV3(p3, p1));  // isn't this backwards?
}

// returns the top of a bsp tree for the mesh, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
csg_BSPNode* csg_BSPTreeFromMesh(const csg_Mesh* mesh, BumpAlloc* arena) {
    csg_BSPNode* tree = NULL;

    for (int64_t i = 0; i < mesh->triCount; i++) {
        csg_BSPNode* node = BUMP_PUSH_NEW(arena, csg_BSPNode);
        node->nextAvailible = tree;
        tree = node;

        const csg_MeshTri* tri = &mesh->tris[i];
        node->splitNormal = csg_meshTriNormal(*tri, mesh);
        node->point1 = mesh->verts[tri->aIdx];
        node->point2 = mesh->verts[tri->bIdx];
        node->point3 = mesh->verts[tri->cIdx];
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

typedef struct csg_VertLoopNode csg_VertLoopNode;
typedef struct csg_VertLoopNode {
    csg_VertLoopNode* next;
    HMM_Vec3 vert;
};

typedef struct csg_VertLoop csg_VertLoop;
struct csg_VertLoop {
    csg_VertLoop* nextLoop;
    csg_VertLoopNode* firstNode;
};

/*
DT for a halfedge mesh

array of halfedge*, indicies line up with vert indicies
arena of halfedges, where each has a twin, next, and vert*

// how the fuck is this supposed to work mid-triangulation??

*/

void csg_meshToVertLoops(csg_Mesh* mesh, BumpAlloc* arena, csg_VertLoop* outLoops, int64_t* outLoopCount) {
    for (int64_t i = 0; i < mesh->triCount; i++) {
        csg_VertLoop* loop = BUMP_PUSH_NEW(arena, csg_VertLoop);
        for (int j = 2; j >= 0; j--) {
            csg_VertLoopNode* newNode = BUMP_PUSH_NEW(arena, csg_VertLoopNode);
            newNode->vert = mesh->verts[mesh->tris[i].elems[j]];
            newNode->next = loop;
            loop = newNode;
        }
    }
}

#define CSG_EPSILON 0.0001

bool csg_floatZero(float a) {
    return fabsf(a) < CSG_EPSILON;
}

bool csg_floatEqual(float a, float b) {
    return csg_floatZero(a - b);
}

typedef enum {
    CSG_PPR_WITHIN,
    CSG_PPR_OUTSIDE,
    CSG_PPR_COPLANAR,
} csg_PointPlaneRelation;

// assumes three points in the pts array and the outClasses array sorry not sorry
void _csg_classifyTrianglePoints(HMM_Vec3* pts, csg_PointPlaneRelation* outClasses, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    int firstPointSide = 0;
    bool onePointOnDiffSide = false;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(pts[i], planeStart), planeNormal);
        if (csg_floatZero(dot)) {
            outClasses[i] = CSG_PPR_COPLANAR;
        }
        outClasses[i] = dot > 0 ? CSG_PPR_OUTSIDE : CSG_PPR_WITHIN;
    }
    return true;
}

HMM_Vec3 csg_planeLineIntersection(HMM_Vec3 planeOrigin, HMM_Vec3 planeNormal, HMM_Vec3 lineOrigin, HMM_Vec3 lineDir) {
    float t = HMM_Dot(HMM_SubV3(planeOrigin, lineOrigin), planeNormal);
    t /= HMM_DotV3(lineDir, planeNormal);
    assert(isfinite(t));  // FIXME: is this enough? no. Do I care? also no. // intersection/coplanar checks beforehand should cover any non-intersection || parallel cases
    return HMM_AddV3(HMM_MulV3F(lineDir, t), lineOrigin);
}

void csg_splitTriangle(csg_Mesh* mesh, int64_t triIdx, csg_MeshTri cutter, const csg_Mesh* cutterMesh) {
    HMM_Vec3 aNormal = csg_meshTriNormal(mesh->tris[triIdx], mesh);
    HMM_Vec3 bNormal = csg_meshTriNormal(cutter, cutterMesh);

    HMM_Vec3 aPts[3] = {
        mesh->verts[mesh->tris[triIdx].aIdx],
        mesh->verts[mesh->tris[triIdx].bIdx],
        mesh->verts[mesh->tris[triIdx].cIdx],
    };
    HMM_Vec3 bPts[3] = {
        cutterMesh->verts[cutter.aIdx],
        cutterMesh->verts[cutter.bIdx],
        cutterMesh->verts[cutter.cIdx],
    };

    csg_PointPlaneRelation aPointClasses[3];
    _csg_classifyTrianglePoints(aPts, aPointClasses, bNormal, bPts[0]);

    if (aPointClasses[0] == CSG_PPR_OUTSIDE &&
        aPointClasses[1] == CSG_PPR_OUTSIDE &&
        aPointClasses[2] == CSG_PPR_OUTSIDE) {
        return;
    } else if (aPointClasses[0] == CSG_PPR_WITHIN &&
               aPointClasses[1] == CSG_PPR_WITHIN &&
               aPointClasses[2] == CSG_PPR_WITHIN) {
        return;
    } else {
        HMM_Vec3 intersection1 = csg_planeLineIntersection(bPts[0], bNormal, );
        HMM_Vec3 intersection2 = csg_planeLineIntersection(bPts[0], bNormal, );
    }
}

void csg_difference(csg_Mesh* a, csg_Mesh* b, BumpAlloc* arena) {
    /*
    collect all points of A outside B
    get every single edge loop colliding w/ b, fix to not
    get every single edgeloop of B colliding w/ A,
    */

    /*
    find all edgeloops of A that intersect B
    iteratively split tris in A against those from B
    find all edgeloops of B that intersect A
    iteratively split tris in B against those from A

    fix normals
    glue
    done

    ideally done??
    */

    {
        // FIXME: might be benficial to make a half-edge first, then do this op over each vertex instead of each tri
        csg_BSPNode* bspTree = csg_BSPTreeFromMesh(a, arena);
        for (int64_t i = 0; i < a->triCount; i++) {
            csg_MeshTri* tri = &a->tris[i];
            for (int64_t vertIdx = 0; vertIdx < 3; vertIdx++) {
                HMM_Vec3 vert = a->verts[tri->elems[vertIdx]];
                if (!csg_BSPContainsPoint(bspTree, vert)) {
                    continue;
                }

                // split this one
            }  // end vertex loop
        }  // end triangle loop for A
    }
}  // end csg_difference

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

        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 0, .bIdx = 1, .cIdx = 2});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 0, .bIdx = 2, .cIdx = 3});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 0, .bIdx = 3, .cIdx = 1});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 3, .bIdx = 2, .cIdx = 1});

        csg_BSPNode* tree = csg_BSPTreeFromMesh(&mesh, &arena);
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
    }

    bump_clear(&arena);

    {
        csg_Mesh mesh = csg_meshInit(p);
        csg_meshPushVert(&mesh, HMM_V3(-0.5, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(0, 1, 0));
        csg_meshPushVert(&mesh, HMM_V3(0.5, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(0, -1, -1));
        csg_meshPushVert(&mesh, HMM_V3(0, -1, 1));

        // top faces
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 1, .bIdx = 2, .cIdx = 3});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 1, .bIdx = 4, .cIdx = 2});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 1, .bIdx = 3, .cIdx = 0});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 1, .bIdx = 0, .cIdx = 4});

        // bottom faces
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 0, .bIdx = 3, .cIdx = 2});
        csg_meshPushTri(&mesh, (csg_MeshTri){.aIdx = 0, .bIdx = 2, .cIdx = 4});

        csg_BSPNode* tree = csg_BSPTreeFromMesh(&mesh, &arena);

        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    bump_free(&arena);
    poolAllocDeinit(p);
}
