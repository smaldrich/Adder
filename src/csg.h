#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "base/allocators.h"
#include "base/testing.h"

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

typedef struct {
    csg_TriListNode* first;
    csg_TriListNode* last;
} csg_TriList;

typedef struct csg_BSPNode csg_BSPNode;
struct csg_BSPNode {
    csg_BSPNode* outerTree;
    csg_BSPNode* innerTree;

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

HMM_Vec3 csg_triNormal(HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(c, a));  // isn't this backwards?
    return HMM_NormV3(n);
}

csg_TriList csg_triListInit() {
    return (csg_TriList) { .first = NULL, .last = NULL };
}

// destructive to node->next
void csg_triListPush(csg_TriList* list, csg_TriListNode* node) {
    node->next = list->first;
    if (list->first == NULL) {
        list->last = node;
    }
    list->first = node;
}

// destructive to node->next in listA, both ptrs can be null
csg_TriList* csg_triListJoin(csg_TriList* listA, csg_TriList* listB) {
    if (listA->last != NULL) {
        listA->last->next = listB->first;
        if (listB->last != NULL) {
            listA->last = listB->last;
        }
        return listA;
    } else {
        return listB;
    }
}

// destructive to node->next in list
void csg_triListPushNew(BumpAlloc* arena, csg_TriList* list, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    csg_TriListNode* new = BUMP_PUSH_NEW(arena, csg_TriListNode);
    *new = (csg_TriListNode){
        .a = a,
        .b = b,
        .c = c,
    };
    csg_triListPush(list, new);
}

void csg_triListTransform(csg_TriList* list, HMM_Mat4 transform) {
    for (csg_TriListNode* node = list->first; node; node = node->next) {
        for (int i = 0; i < 3; i++) {
            HMM_Vec4 v4 = (HMM_Vec4){ .XYZ = node->elems[i], .W = 1 };
            node->elems[i] = HMM_MulM4V4(transform, v4).XYZ;
        }
    }
}

void csg_triListToSTLFile(const csg_TriList* list, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (const csg_TriListNode* tri = list->first; tri; tri = tri->next) {
        HMM_Vec3 normal = csg_triNormal(tri->a, tri->b, tri->c);
        fprintf(f, "facet normal %f %f %f\n", normal.X, normal.Y, normal.Z);
        fprintf(f, "outer loop\n");
        fprintf(f, "vertex %f %f %f\n", tri->a.X, tri->a.Y, tri->a.Z);
        fprintf(f, "vertex %f %f %f\n", tri->b.X, tri->b.Y, tri->b.Z);
        fprintf(f, "vertex %f %f %f\n", tri->c.X, tri->c.Y, tri->c.Z);
        fprintf(f, "endloop\n");
        fprintf(f, "endfacet\n");
    }

    fprintf(f, "endsolid object\n");
    fclose(f);
}

// assumes three points in the pts array sorry not sorry
csg_PlaneRelation _csg_triClassify(HMM_Vec3* pts, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
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
    HMM_Vec3 splitNormal = csg_triNormal(parent->point1, parent->point2, parent->point3);

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
            csg_PlaneRelation rel = _csg_triClassify(pts, splitNormal, parent->point1);
            if (rel == CSG_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == CSG_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                csg_BSPNode* duplicate = BUMP_PUSH_NEW(arena, csg_BSPNode);
                duplicate->point1 = node->point1;
                duplicate->point2 = node->point2;
                duplicate->point3 = node->point3;

                node->nextAvailible = innerList;
                innerList = node;
                duplicate->nextAvailible = outerList;
                outerList = duplicate;
            }
        }
    }

    parent->innerTree = innerList;
    parent->outerTree = outerList;

    if (parent->innerTree != NULL) {
        _csg_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree != NULL) {
        _csg_BSPTreeFixNode(arena, parent->outerTree, outerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the list of tris, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
csg_BSPNode* csg_triListToBSP(const csg_TriList* tris, BumpAlloc* arena) {
    csg_BSPNode* tree = NULL;

    for (const csg_TriListNode* tri = tris->first; tri; tri = tri->next) {
        csg_BSPNode* node = BUMP_PUSH_NEW(arena, csg_BSPNode);
        node->nextAvailible = tree;
        tree = node;

        node->point1 = tri->a;
        node->point2 = tri->b;
        node->point3 = tri->c;
    }

    _csg_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool csg_BSPContainsPoint(csg_BSPNode* tree, HMM_Vec3 point) {
    csg_BSPNode* node = tree;
    while (true) { // FIXME: failsafe here :)
        HMM_Vec3 diff = HMM_SubV3(point, node->point1);
        float dot = HMM_DotV3(diff, csg_triNormal(node->point1, node->point2, node->point3));
        if (dot <= 0) {
            node = node->innerTree;
            if (node == NULL) {
                return true;
            }
        } else {
            node = node->outerTree;
            if (node == NULL) {
                return false;
            }
        }
    }
    assert(false);
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

// FIXME: point deduplication of some kind?
// destructive to tri->next, and both outlists next
void _csg_triSplit(csg_TriListNode* tri, BumpAlloc* arena, csg_TriList* outOutsideList, csg_TriList* outInsideList, csg_BSPNode* cutter) {
    HMM_Vec3 cutNormal = csg_triNormal(cutter->point1, cutter->point2, cutter->point3);

    csg_PlaneRelation rel = _csg_triClassify(tri->elems, cutNormal, cutter->point1);

    if (rel == CSG_PR_COPLANAR) {
        assert(false); // FIXME: this
        // FIXME: how do coplanar things factor into this?
        // FIXME: full coplanar testing
    } else if (rel == CSG_PR_OUTSIDE) {
        csg_triListPush(outOutsideList, tri);
    } else if (rel == CSG_PR_WITHIN) {
        csg_triListPush(outInsideList, tri);
    } else {
        HMM_Vec3 verts[5] = { 0 };
        int vertCount = 0;
        int firstIntersectionIdx = -1;

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
            } else if (csg_floatEqual(t * t, HMM_LenSqr(diff))) {
                continue;
            } else if (t < 0 || csg_floatZero(t)) {
                continue;
            } else if ((t * t) > HMM_LenSqr(diff)) {
                continue;
            }

            assert(vertCount < 5);
            HMM_Vec3 intersection = HMM_AddV3(HMM_MulV3F(direction, t), pt);
            if (firstIntersectionIdx == -1 ||
                (firstIntersectionIdx == 1 && vertCount == 4)) {
                // perhaps the worst hack I have performed to date.
                // In order to triangulate a vert loop properly, we are picking an 'origin'
                // for the verts, so that when they get triangulated below, it can happen using a consistant
                // method. The point is to rotate one vert to be idx 0, then triangulate normally from there.
                // because we are splitting one triangle, and it isn't split down the middle, it has to fit one
                // of three cases below. Where nums are vert nums (in the tri) (pre rotate) and pipes are the cuts.
                //     2         2         1
                //    |         | |         |
                //   0 | 3     0   4     0 | 3
                // based on the triangulation below, in every case except one the origin will be the smallest
                // cut to work properly. That one case is tri no. 1 in the diagram, for which we need to
                // select the second cut as the origin. sorry not sorry. couldn't think of a better way to do
                // this while maintaining triangulation throughout the clip. VertCount is being checked for 4
                // because it is incremented after this. sorry again.
                firstIntersectionIdx = vertCount;
            }
            verts[vertCount] = intersection;
            vertCount++;
        }
        // just add a switch and a second prebaked triangulation routine

        // rotate points so that the verts can be triangulated consistantly
        // problem if you don't do this is that the triangulation doesn't end
        // up going across the cut line or will create zero-width tris
        HMM_Vec3 rotatedVerts[5] = { 0 };
        for (int i = 0; i < vertCount; i++) {
            rotatedVerts[i] = verts[(i + firstIntersectionIdx) % vertCount];
        }

        if (vertCount == 5) {
            bool t1Outside = HMM_DotV3(HMM_SubV3(rotatedVerts[1], cutter->point1), cutNormal) > 0;
            csg_TriListNode* t1 = BUMP_PUSH_NEW(arena, csg_TriListNode);
            *t1 = (csg_TriListNode){
                .a = rotatedVerts[0],
                .b = rotatedVerts[1],
                .c = rotatedVerts[2],
            };

            csg_TriListNode* t2 = BUMP_PUSH_NEW(arena, csg_TriListNode);
            *t2 = (csg_TriListNode){
                .a = rotatedVerts[2],
                .b = rotatedVerts[3],
                .c = rotatedVerts[4],
            };

            csg_TriListNode* t3 = BUMP_PUSH_NEW(arena, csg_TriListNode);
            *t3 = (csg_TriListNode){
                .a = rotatedVerts[4],
                .b = rotatedVerts[0],
                .c = rotatedVerts[2],
            };

            csg_TriList* t1List = t1Outside ? outOutsideList : outInsideList;
            csg_TriList* t2and3List = t1Outside ? outInsideList : outOutsideList;

            csg_triListPush(t1List, t1);
            csg_triListPush(t2and3List, t2);
            csg_triListPush(t2and3List, t3);
        } // end 5 vert-check
        else if (vertCount == 4) {
            csg_TriListNode* t1 = BUMP_PUSH_NEW(arena, csg_TriListNode);
            *t1 = (csg_TriListNode){
                .a = rotatedVerts[0],
                .b = rotatedVerts[1],
                .c = rotatedVerts[2],
            };

            csg_TriListNode* t2 = BUMP_PUSH_NEW(arena, csg_TriListNode);
            *t2 = (csg_TriListNode){
                .a = rotatedVerts[2],
                .b = rotatedVerts[3],
                .c = rotatedVerts[0],
            };

            // t1B should never be colinear with the cut plane so long as rotation has been done correctly
            bool t1Outside = HMM_DotV3(HMM_SubV3(t1->b, cutter->point1), cutNormal) > 0;
            csg_triListPush(t1Outside ? outOutsideList : outInsideList, t1);
            csg_triListPush(t1Outside ? outInsideList : outOutsideList, t2);
        } else {
            // anything greater than 5 should be impossible, anything less than 4 should have been put on
            // one side, not marked spanning
            assert(false);
        }
    }  // end spanning check
}

// clips all tris in meshTris that are inside of tree, returns a LL of the new tris, destructive to the original
// everything allocated to arena
// static int iter = 0;
csg_TriList* csg_bspClipTrisWithin(csg_TriList* meshTris, csg_BSPNode* tree, BumpAlloc* arena) {
    csg_TriList inside = csg_triListInit();
    csg_TriList outside = csg_triListInit();

    csg_TriListNode* next = NULL;  // memod because placing in the other list would overwrite
    for (csg_TriListNode* tri = meshTris->first; tri; tri = next) {
        next = tri->next;
        _csg_triSplit(tri, arena, &outside, &inside, tree);
    }

    // int iterForThis = iter;
    // {
    //     iter++;
    //     csg_TriList cutter = csg_triListInit();
    //     csg_triListPushNew(arena, &cutter, tree->point1, tree->point2, tree->point3);
    //     csg_triListToSTLFile(&cutter, bump_formatStr(arena, "testing/%dCutter.stl", iterForThis));

    //     if (inside.first != NULL) {
    //         csg_triListToSTLFile(&inside, bump_formatStr(arena, "testing/%dIn.stl", iterForThis));
    //     }
    //     if (outside.first != NULL) {
    //         csg_triListToSTLFile(&outside, bump_formatStr(arena, "testing/%dOut.stl", iterForThis));
    //     }
    // }


    if (tree->innerTree != NULL) {
        inside = *csg_bspClipTrisWithin(&inside, tree->innerTree, arena);
    } else {
        inside.first = NULL;
        inside.last = NULL;
    }

    if (tree->outerTree != NULL) {
        outside = *csg_bspClipTrisWithin(&outside, tree->outerTree, arena);
    }

    csg_TriList* out = BUMP_PUSH_NEW(arena, csg_TriList);
    *out = *csg_triListJoin(&inside, &outside);
    // csg_triListToSTLFile(out, bump_formatStr(arena, "testing/%dReturned.stl", iterForThis));
    return out;
}

// FIXME: this is horrible
// 2 width cube, centered on the origin
csg_TriList csg_cube(BumpAlloc* arena) {
    csg_TriList list = csg_triListInit();

    HMM_Vec3 v[] = {
        HMM_V3(-1, -1, 1),
        HMM_V3(1, -1, 1),
        HMM_V3(1, -1, -1),
        HMM_V3(-1, -1, -1),
        HMM_V3(-1, 1, 1),
        HMM_V3(1, 1, 1),
        HMM_V3(1, 1, -1),
        HMM_V3(-1, 1, -1),
    };
    csg_triListPushNew(arena, &list, v[0], v[2], v[1]);
    csg_triListPushNew(arena, &list, v[0], v[3], v[2]);
    csg_triListPushNew(arena, &list, v[7], v[6], v[2]);
    csg_triListPushNew(arena, &list, v[7], v[2], v[3]);
    csg_triListPushNew(arena, &list, v[6], v[5], v[1]);
    csg_triListPushNew(arena, &list, v[6], v[1], v[2]);
    csg_triListPushNew(arena, &list, v[5], v[4], v[0]);
    csg_triListPushNew(arena, &list, v[5], v[0], v[1]);
    csg_triListPushNew(arena, &list, v[4], v[7], v[3]);
    csg_triListPushNew(arena, &list, v[4], v[3], v[0]);
    csg_triListPushNew(arena, &list, v[4], v[5], v[6]);
    csg_triListPushNew(arena, &list, v[4], v[6], v[7]);
    return list;
}

void csg_tests() {
    test_printSectionHeader("csg");

    BumpAlloc arena = bump_allocate(1000000, "csg test arena");
    PoolAlloc pool = poolAllocInit();

    {
        HMM_Vec3 verts[] = {
            HMM_V3(0, 0, 0),
            HMM_V3(1, 0, 0),
            HMM_V3(1, 1, 0),
            HMM_V3(1, 0, -1),
        };

        csg_TriList list = csg_triListInit();
        csg_triListPushNew(&arena, &list, verts[0], verts[1], verts[2]);
        csg_triListPushNew(&arena, &list, verts[0], verts[2], verts[3]);
        csg_triListPushNew(&arena, &list, verts[0], verts[3], verts[1]);
        csg_triListPushNew(&arena, &list, verts[3], verts[2], verts[1]);

        csg_BSPNode* tree = csg_triListToBSP(&list, &arena);
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
    }

    bump_clear(&arena);
    poolAllocClear(&pool);

    {
        HMM_Vec3 verts[] = {
            HMM_V3(-0.5, 0, 0),
            HMM_V3(0, 1, 0),
            HMM_V3(0.5, 0, 0),
            HMM_V3(0, -1, -1),
            HMM_V3(0, -1, 1),
        };

        csg_TriList list = csg_triListInit();
        // top faces
        csg_triListPushNew(&arena, &list, verts[1], verts[2], verts[3]);
        csg_triListPushNew(&arena, &list, verts[1], verts[4], verts[2]);
        csg_triListPushNew(&arena, &list, verts[1], verts[3], verts[0]);
        csg_triListPushNew(&arena, &list, verts[1], verts[0], verts[4]);

        // bottom faces
        csg_triListPushNew(&arena, &list, verts[0], verts[3], verts[2]);
        csg_triListPushNew(&arena, &list, verts[0], verts[2], verts[4]);
        csg_triListToSTLFile(&list, "testing/object.stl");

        csg_BSPNode* tree = csg_triListToBSP(&list, &arena);

        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        test_printResult(csg_BSPContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    bump_clear(&arena);
    poolAllocClear(&pool);

    {
        csg_TriList cubeA = csg_cube(&arena);
        csg_TriList cubeB = csg_cube(&arena);
        csg_triListTransform(&cubeB, HMM_Rotate_RH(30, HMM_V3(1, 1, 1)));
        csg_triListTransform(&cubeB, HMM_Translate(HMM_V3(1, 1, 1)));

        csg_BSPNode* treeA = csg_triListToBSP(&cubeA, &arena);
        csg_BSPNode* treeB = csg_triListToBSP(&cubeB, &arena);

        csg_TriList* aClipped = csg_bspClipTrisWithin(&cubeA, treeB, &arena);
        csg_TriList* bClipped = csg_bspClipTrisWithin(&cubeB, treeA, &arena);
        csg_triListToSTLFile(csg_triListJoin(aClipped, bClipped), "testing/cube.stl");
    }

    bump_free(&arena);
    poolAllocDeinit(&pool);
}
