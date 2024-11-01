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
    csg_BSPNodeKind kind;

    csg_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    HMM_Vec3 point1;  // FIXME: redundant?
    HMM_Vec3 point2;  // FIXME: space inefficent
    HMM_Vec3 point3;
};

typedef enum {
    CSG_PR_COPLANAR,
    CSG_PR_WITHIN,
    CSG_PR_OUTSIDE,
    CSG_PR_SPANNING,
} csg_PlaneRelation;

#define CSG_EPSILON 0.0001

bool csg_floatZero(float a) {
    return fabsf(a) < CSG_EPSILON;
}

bool csg_floatEqual(float a, float b) {
    return csg_floatZero(a - b);
}

// assumes three points in the pts array sorry not sorry
csg_PlaneRelation _csg_classifyTrianglePoints(HMM_Vec3* pts, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    csg_PlaneRelation finalRel = 0;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(pts[i], planeStart), planeNormal);
        if (csg_floatZero(dot)) {
            finalRel |= CSG_PR_COPLANAR;
        } else {
            finalRel |= dot > 0 ? CSG_PR_OUTSIDE : CSG_PR_WITHIN;
        }
    }
    return finalRel;
}

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

            HMM_Vec3 pts[3] = {
                node->point1,
                node->point2,
                node->point3,
            };

            csg_PlaneRelation rel = _csg_classifyTrianglePoints(pts, parent->splitNormal, parent->point1);
            if (rel == CSG_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == CSG_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
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

HMM_Vec3 csg_TriNormal(HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(c, a));  // isn't this backwards?
    return HMM_NormV3(n);
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
        float dot = HMM_DotV3(diff, csg_TriNormal(node->point1, node->point2, node->point3));
        if (dot <= 0) {
            node = node->innerTree;
        } else {
            node = node->outerTree;
        }
        assert(node != NULL);
    }
    return (node->kind == CSG_BSPNK_LEAF_WITHIN_MESH) ? true : false;
}

// returns a T value along the line such that ((t*lineDir) + lineOrigin) = the point of intersection
// done this way so that bounds checking can be done after the return
// false retur nvalue indicates no intersection between the plane and line
// outT assumed non-null, written for output
bool csg_planeLineIntersection(HMM_Vec3 planeOrigin, HMM_Vec3 planeNormal, HMM_Vec3 lineOrigin, HMM_Vec3 lineDir, float* outT) {
    float t = HMM_Dot(HMM_SubV3(planeOrigin, lineOrigin), planeNormal);
    t /= HMM_DotV3(lineDir, planeNormal);
    *outT = t;
    return isfinite(t);
}

typedef struct csg_TriListNode csg_TriListNode;
struct csg_TriListNode {
    csg_TriListNode* next;
    union {
        struct {
            HMM_Vec3 a;
            HMM_Vec3 b;
            HMM_Vec3 c;
        };
        HMM_Vec3 elems[3];
    };
};

// FIXME: how do coplanar things factor into this?
// FIXME: full coplanar testing
// FIXME: point deduplication of some kind
void csg_splitTriangle(csg_TriListNode* tri, BumpAlloc* arena, csg_TriListNode** outOutsideList, csg_TriListNode** outInsideList, csg_BSPNode* cutter) {
    HMM_Vec3 cutNormal = csg_TriNormal(cutter->point1, cutter->point2, cutter->point3);

    csg_PlaneRelation rel = _csg_classifyTrianglePoints(tri->elems, cutNormal, cutter->point1);

    if (rel == CSG_PR_COPLANAR) {
        assert(false);
    } else if (rel == CSG_PR_OUTSIDE) {
        tri->next = *outOutsideList;
        (*outOutsideList) = tri;
    } else if (rel == CSG_PR_WITHIN) {
        tri->next = *outInsideList;
        (*outInsideList) = tri;
    } else {
        HMM_Vec3 verts[5] = { 0 };
        int vertCount = 0;
        int firstIntersectionIdx = 0;

        // collect all of the verts that need to exist by intersecting each line in the tri
        for (int i = 0; i < 3; i++) {
            HMM_Vec3 pt = tri->elems[i];
            verts[vertCount] = pt;
            assert(vertCount < 5);
            vertCount++;

            HMM_Vec3 nextPt = tri->elems[(i + 1) % 3];
            HMM_Vec3 diff = HMM_SubV3(nextPt, pt);
            HMM_Vec3 direction = HMM_NormV3(diff);
            float t = 0;
            bool intersectExists = csg_planeLineIntersection(cutter->point1, cutNormal, pt, direction, &t);
            if (!intersectExists) {
                continue;
            } else if (t < 0) {
                continue;
            } else if ((t * t) > HMM_LenSqr(diff)) {
                continue;
            }

            assert(vertCount < 5);
            HMM_Vec3 intersection = HMM_AddV3(HMM_MulV3F(direction, t), pt);
            firstIntersectionIdx = vertCount;
            verts[vertCount] = intersection;
            vertCount++;
        }
        assert(vertCount == 5); // FIXME: what happens when one point is coplanar and it gets marked as spanning? i.e. should be only 4 points here and this'll fire
        // just add a switch and a second prebaked triangulation routine

        // rotate so that the verts can be triangulated consistantly
        HMM_Vec3 rotatedVerts[5] = { 0 };
        for (int i = 0; i < 5; i++) {
            rotatedVerts[i] = verts[(i + firstIntersectionIdx) % 5];
        }

        csg_TriListNode* t1 = BUMP_PUSH_NEW(arena, csg_TriListNode);
        *t1 = (csg_TriListNode){
            .a = rotatedVerts[0],
            .b = rotatedVerts[1],
            .c = rotatedVerts[2]
        };

        csg_TriListNode* t2 = BUMP_PUSH_NEW(arena, csg_TriListNode);
        *t2 = (csg_TriListNode){
            .a = rotatedVerts[2],
            .b = rotatedVerts[3],
            .c = rotatedVerts[4]
        };

        csg_TriListNode* t3 = BUMP_PUSH_NEW(arena, csg_TriListNode);
        *t3 = (csg_TriListNode){
            .a = rotatedVerts[4],
            .b = rotatedVerts[0],
            .c = rotatedVerts[2]
        };
    }  // end spanning check
}

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
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
    }

    bump_clear(&arena);
    poolAllocClear(p);

    {
        csg_Mesh mesh = csg_meshInit(p);
        csg_meshPushVert(&mesh, HMM_V3(-0.5, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(0, 1, 0));
        csg_meshPushVert(&mesh, HMM_V3(0.5, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(0, -1, -1));
        csg_meshPushVert(&mesh, HMM_V3(0, -1, 1));

        // top faces
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 1, .bIdx = 2, .cIdx = 3 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 1, .bIdx = 4, .cIdx = 2 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 1, .bIdx = 3, .cIdx = 0 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 1, .bIdx = 0, .cIdx = 4 });

        // bottom faces
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 3, .cIdx = 2 });
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 2, .cIdx = 4 });

        csg_BSPNode* tree = csg_BSPTreeFromMesh(&mesh, &arena);

        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    bump_clear(&arena);
    poolAllocClear(p);

    {
        csg_Mesh mesh = csg_meshInit(p);
        csg_meshPushVert(&mesh, HMM_V3(1, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(-1, 0, 0));
        csg_meshPushVert(&mesh, HMM_V3(0, 1, 0));
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 0, .bIdx = 1, .cIdx = 2 });

        csg_meshPushVert(&mesh, HMM_V3(1, 0.5, -1));
        csg_meshPushVert(&mesh, HMM_V3(0, 0.5, 1));
        csg_meshPushVert(&mesh, HMM_V3(-1, 0.5, -1));
        csg_meshPushTri(&mesh, (csg_MeshTri) { .aIdx = 3, .bIdx = 4, .cIdx = 5 });

        csg_splitTriangle(&mesh, &(mesh.tris[0]), &(mesh.tris[1]), &mesh);
        csg_splitTriangle(&mesh, &(mesh.tris[1]), &(mesh.tris[0]), &mesh);
    }

    bump_free(&arena);
    poolAllocDeinit(p);
}
