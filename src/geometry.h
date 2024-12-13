#pragma once

#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "snooze.h"
#include "ui.h"

typedef struct geo_TriListNode geo_TriListNode;
struct geo_TriListNode {
    geo_TriListNode* next;
    bool anyChildDeleted;
    bool recovered;
    geo_TriListNode* ancestor;
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
    geo_TriListNode* first;
    geo_TriListNode* last;
} geo_TriList;

typedef struct geo_BSPNode geo_BSPNode;
struct geo_BSPNode {
    geo_BSPNode* outerTree;
    geo_BSPNode* innerTree;

    geo_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    HMM_Vec3 point1;  // FIXME: redundant?
    HMM_Vec3 point2;  // FIXME: space inefficent
    HMM_Vec3 point3;
};

typedef enum {
    geo_PR_COPLANAR,
    geo_PR_WITHIN,
    geo_PR_OUTSIDE,
    geo_PR_SPANNING,
} geo_PlaneRelation;

#define geo_EPSILON 0.0001

bool geo_floatZero(float a) {
    return fabsf(a) < geo_EPSILON;
}

bool geo_floatEqual(float a, float b) {
    return geo_floatZero(a - b);
}

bool geo_floatLessEqual(float a, float b) {
    return geo_floatEqual(a, b) || a < b;
}

bool geo_floatGreaterEqual(float a, float b) {
    return geo_floatEqual(a, b) || a > b;
}

bool geo_v2Equal(HMM_Vec2 a, HMM_Vec2 b) {
    return geo_floatEqual(a.X, b.X) && geo_floatEqual(a.Y, b.Y);
}

HMM_Vec3 geo_triNormal(HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(c, a));  // isn't this backwards?
    return HMM_NormV3(n);
}

geo_TriList geo_triListInit() {
    return (geo_TriList){.first = NULL, .last = NULL};
}

// destructive to node->next
// FIXME: this is extremely unintuitive and has knack for causing circular lists
// huge problem, please find a better solution for the list situation
// same issue with other list functions also
void geo_triListPush(geo_TriList* list, geo_TriListNode* node) {
    assert(node != NULL);
    node->next = list->first;
    if (list->first == NULL) {
        list->last = node;
    }
    list->first = node;
}

// destructive to node->next in listA, both ptrs can be null
geo_TriList* geo_triListJoin(geo_TriList* listA, geo_TriList* listB) {
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
void geo_triListPushNew(snz_Arena* arena, geo_TriList* list, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    geo_TriListNode* new = SNZ_ARENA_PUSH(arena, geo_TriListNode);
    *new = (geo_TriListNode){
        .a = a,
        .b = b,
        .c = c,
    };
    geo_triListPush(list, new);
}

void geo_triListTransform(geo_TriList* list, HMM_Mat4 transform) {
    for (geo_TriListNode* node = list->first; node; node = node->next) {
        for (int i = 0; i < 3; i++) {
            HMM_Vec4 v4 = (HMM_Vec4){.XYZ = node->elems[i], .W = 1};
            node->elems[i] = HMM_MulM4V4(transform, v4).XYZ;
        }
    }
}

// flips the normals for every tri within list
void geo_triListInvert(geo_TriList* list) {
    for (geo_TriListNode* node = list->first; node; node = node->next) {
        HMM_Vec3 temp = node->a;
        node->a = node->c;
        node->c = temp;
    }
}

void geo_triListToSTLFile(const geo_TriList* list, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (const geo_TriListNode* tri = list->first; tri; tri = tri->next) {
        HMM_Vec3 normal = geo_triNormal(tri->a, tri->b, tri->c);
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
geo_PlaneRelation _geo_triClassify(HMM_Vec3* pts, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    geo_PlaneRelation finalRel = 0;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(pts[i], planeStart), planeNormal);
        if (geo_floatZero(dot)) {
            finalRel |= geo_PR_COPLANAR;
        } else {
            finalRel |= dot > 0 ? geo_PR_OUTSIDE : geo_PR_WITHIN;
        }
    }
    return finalRel;
}

// parent assumed non-null, new nodes allocated into arena
// takes a list of nodes with normals and origins filled out, organizes it + its children into the tree shape based on splits
// will allocate more nodes over the course of fixing, these are put into arena
void _geo_BSPTreeFixNode(snz_Arena* arena, geo_BSPNode* parent, geo_BSPNode* listOfPossibleNodes) {
    HMM_Vec3 splitNormal = geo_triNormal(parent->point1, parent->point2, parent->point3);

    geo_BSPNode* innerList = NULL;
    geo_BSPNode* outerList = NULL;
    {
        geo_BSPNode* next = NULL;
        for (geo_BSPNode* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            HMM_Vec3 pts[3] = {
                node->point1,
                node->point2,
                node->point3,
            };
            geo_PlaneRelation rel = _geo_triClassify(pts, splitNormal, parent->point1);
            if (rel == geo_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == geo_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                geo_BSPNode* duplicate = SNZ_ARENA_PUSH(arena, geo_BSPNode);
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
        _geo_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree != NULL) {
        _geo_BSPTreeFixNode(arena, parent->outerTree, outerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the list of tris, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
geo_BSPNode* geo_triListToBSP(const geo_TriList* tris, snz_Arena* arena) {
    geo_BSPNode* tree = NULL;

    for (const geo_TriListNode* tri = tris->first; tri; tri = tri->next) {
        geo_BSPNode* node = SNZ_ARENA_PUSH(arena, geo_BSPNode);
        node->nextAvailible = tree;
        tree = node;

        node->point1 = tri->a;
        node->point2 = tri->b;
        node->point3 = tri->c;
    }

    _geo_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool geo_BSPContainsPoint(geo_BSPNode* tree, HMM_Vec3 point) {
    geo_BSPNode* node = tree;
    while (true) {  // FIXME: failsafe here :)
        HMM_Vec3 diff = HMM_SubV3(point, node->point1);
        float dot = HMM_DotV3(diff, geo_triNormal(node->point1, node->point2, node->point3));
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
// t may be negative
// outT assumed non-null, written for output
bool geo_planeLineIntersection(HMM_Vec3 planeOrigin, HMM_Vec3 planeNormal, HMM_Vec3 lineOrigin, HMM_Vec3 lineDir, float* outT) {
    float t = HMM_Dot(HMM_SubV3(planeOrigin, lineOrigin), planeNormal);
    t /= HMM_DotV3(lineDir, planeNormal);
    *outT = t;
    if (!isfinite(t)) {
        t = 0;
        return false;
    }
    return true;
}

// FIXME: point deduplication of some kind?
// destructive to tri->next, and both outlists next
void _geo_triSplit(geo_TriListNode* tri, snz_Arena* arena, geo_TriList* outOutsideList, geo_TriList* outInsideList, geo_BSPNode* cutter) {
    HMM_Vec3 cutNormal = geo_triNormal(cutter->point1, cutter->point2, cutter->point3);

    geo_PlaneRelation rel = _geo_triClassify(tri->elems, cutNormal, cutter->point1);

    if (rel == geo_PR_COPLANAR) {
        assert(false);  // FIXME: this
        // FIXME: how do coplanar things factor into this?
        // FIXME: full coplanar testing
    } else if (rel == geo_PR_OUTSIDE) {
        geo_triListPush(outOutsideList, tri);
    } else if (rel == geo_PR_WITHIN) {
        geo_triListPush(outInsideList, tri);
    } else {
        HMM_Vec3 verts[5] = {0};
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
            bool intersectExists = geo_planeLineIntersection(cutter->point1, cutNormal, pt, direction, &t);
            if (!intersectExists) {
                continue;
            } else if (geo_floatEqual(t * t, HMM_LenSqr(diff))) {
                continue;
            } else if (t < 0 || geo_floatZero(t)) {
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
        HMM_Vec3 rotatedVerts[5] = {0};
        for (int i = 0; i < vertCount; i++) {
            rotatedVerts[i] = verts[(i + firstIntersectionIdx) % vertCount];
        }

        if (vertCount == 5) {
            bool t1Outside = HMM_DotV3(HMM_SubV3(rotatedVerts[1], cutter->point1), cutNormal) > 0;
            geo_TriListNode* t1 = SNZ_ARENA_PUSH(arena, geo_TriListNode);
            *t1 = (geo_TriListNode){
                .a = rotatedVerts[0],
                .b = rotatedVerts[1],
                .c = rotatedVerts[2],
                .ancestor = tri,
            };

            geo_TriListNode* t2 = SNZ_ARENA_PUSH(arena, geo_TriListNode);
            *t2 = (geo_TriListNode){
                .a = rotatedVerts[2],
                .b = rotatedVerts[3],
                .c = rotatedVerts[4],
                .ancestor = tri,
            };

            geo_TriListNode* t3 = SNZ_ARENA_PUSH(arena, geo_TriListNode);
            *t3 = (geo_TriListNode){
                .a = rotatedVerts[4],
                .b = rotatedVerts[0],
                .c = rotatedVerts[2],
                .ancestor = tri,
            };

            geo_TriList* t1List = t1Outside ? outOutsideList : outInsideList;
            geo_TriList* t2and3List = t1Outside ? outInsideList : outOutsideList;

            geo_triListPush(t1List, t1);
            geo_triListPush(t2and3List, t2);
            geo_triListPush(t2and3List, t3);
        }  // end 5 vert-check
        else if (vertCount == 4) {
            geo_TriListNode* t1 = SNZ_ARENA_PUSH(arena, geo_TriListNode);
            *t1 = (geo_TriListNode){
                .a = rotatedVerts[0],
                .b = rotatedVerts[1],
                .c = rotatedVerts[2],
                .ancestor = tri,
            };

            geo_TriListNode* t2 = SNZ_ARENA_PUSH(arena, geo_TriListNode);
            *t2 = (geo_TriListNode){
                .a = rotatedVerts[2],
                .b = rotatedVerts[3],
                .c = rotatedVerts[0],
                .ancestor = tri,
            };

            // t1B should never be colinear with the cut plane so long as rotation has been done correctly
            bool t1Outside = HMM_DotV3(HMM_SubV3(t1->b, cutter->point1), cutNormal) > 0;
            geo_triListPush(t1Outside ? outOutsideList : outInsideList, t1);
            geo_triListPush(t1Outside ? outInsideList : outOutsideList, t2);
        } else {
            // anything greater than 5 should be impossible, anything less than 4 should have been put on
            // one side, not marked spanning
            assert(false);
        }
    }  // end spanning check
}

// clips all tris in meshTris from tree, returns a LL of the new tris, destructive to the original
// if within is set, tris within are clipped, otherwise tris outside
// everything allocated to arena
geo_TriList* geo_bspClipTris(bool within, geo_TriList* meshTris, geo_BSPNode* tree, snz_Arena* arena) {
    geo_TriList inside = geo_triListInit();
    geo_TriList outside = geo_triListInit();

    geo_TriListNode* next = NULL;  // memod because placing in the other list would overwrite
    for (geo_TriListNode* tri = meshTris->first; tri; tri = next) {
        next = tri->next;
        _geo_triSplit(tri, arena, &outside, &inside, tree);
    }

    geo_TriList* listToClear = NULL;
    if (tree->innerTree != NULL) {
        inside = *geo_bspClipTris(within, &inside, tree->innerTree, arena);
    } else {
        if (within) {
            listToClear = &inside;
        }
    }

    if (tree->outerTree != NULL) {
        outside = *geo_bspClipTris(within, &outside, tree->outerTree, arena);
    } else {
        if (!within) {
            listToClear = &outside;
        }
    }

    if (listToClear != NULL) {
        // when we clear some portion of geometry, poison all ancestors so the geometry doesn't come back
        // later, because these splits are actually important to the end geometry.
        // other splits are just there to make inner splits possible, but can be reverted to save tris after
        // the entire operation.
        for (geo_TriListNode* node = listToClear->first; node; node = node->next) {
            for (geo_TriListNode* n = node; n; n = n->ancestor) {
                n->anyChildDeleted = true;
            }
        }
        listToClear->first = NULL;
        listToClear->last = NULL;
    }

    geo_TriList* out = SNZ_ARENA_PUSH(arena, geo_TriList);
    *out = *geo_triListJoin(&inside, &outside);
    return out;
}

// destructive to the original tri list
void geo_triListRecoverNonBroken(geo_TriList** tris, snz_Arena* arena) {
    geo_TriList* recovered = SNZ_ARENA_PUSH(arena, geo_TriList);
    geo_TriList* trisRemaining = SNZ_ARENA_PUSH(arena, geo_TriList);

    geo_TriListNode* next = NULL;
    for (geo_TriListNode* node = (*tris)->first; node; node = next) {
        next = node->next;

        geo_TriListNode* oldest = node->ancestor;
        while (true) {
            if (oldest == NULL) {
                break;
            } else if (oldest->anyChildDeleted) {
                oldest = NULL;
                break;
            } else if (oldest->ancestor != NULL) {
                if (!oldest->ancestor->anyChildDeleted) {
                    oldest = oldest->ancestor;
                    continue;
                }
            }
            break;
        }
        if (oldest) {
            if (!oldest->recovered) {
                geo_triListPush(recovered, oldest);
                oldest->recovered = true;
            }
        } else {
            geo_triListPush(trisRemaining, node);
        }
    }

    *tris = geo_triListJoin(trisRemaining, recovered);
}

// FIXME: this is horrible
// 2 width cube, centered on the origin
geo_TriList geo_cube(snz_Arena* arena) {
    geo_TriList list = geo_triListInit();

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
    geo_triListPushNew(arena, &list, v[0], v[2], v[1]);
    geo_triListPushNew(arena, &list, v[0], v[3], v[2]);
    geo_triListPushNew(arena, &list, v[7], v[6], v[2]);
    geo_triListPushNew(arena, &list, v[7], v[2], v[3]);
    geo_triListPushNew(arena, &list, v[6], v[5], v[1]);
    geo_triListPushNew(arena, &list, v[6], v[1], v[2]);
    geo_triListPushNew(arena, &list, v[5], v[4], v[0]);
    geo_triListPushNew(arena, &list, v[5], v[0], v[1]);
    geo_triListPushNew(arena, &list, v[4], v[7], v[3]);
    geo_triListPushNew(arena, &list, v[4], v[3], v[0]);
    geo_triListPushNew(arena, &list, v[4], v[5], v[6]);
    geo_triListPushNew(arena, &list, v[4], v[6], v[7]);
    return list;
}

void geo_tests() {
    snz_testPrintSection("geo");

    snz_Arena arena = snz_arenaInit(1000000, "geo test arena");
    PoolAlloc pool = poolAllocInit();

    {
        HMM_Vec3 verts[] = {
            HMM_V3(0, 0, 0),
            HMM_V3(1, 0, 0),
            HMM_V3(1, 1, 0),
            HMM_V3(1, 0, -1),
        };

        geo_TriList list = geo_triListInit();
        geo_triListPushNew(&arena, &list, verts[0], verts[1], verts[2]);
        geo_triListPushNew(&arena, &list, verts[0], verts[2], verts[3]);
        geo_triListPushNew(&arena, &list, verts[0], verts[3], verts[1]);
        geo_triListPushNew(&arena, &list, verts[3], verts[2], verts[1]);

        geo_BSPNode* tree = geo_triListToBSP(&list, &arena);
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
    }

    snz_arenaClear(&arena);
    poolAllocClear(&pool);

    {
        HMM_Vec3 verts[] = {
            HMM_V3(-0.5, 0, 0),
            HMM_V3(0, 1, 0),
            HMM_V3(0.5, 0, 0),
            HMM_V3(0, -1, -1),
            HMM_V3(0, -1, 1),
        };

        geo_TriList list = geo_triListInit();
        // top faces
        geo_triListPushNew(&arena, &list, verts[1], verts[2], verts[3]);
        geo_triListPushNew(&arena, &list, verts[1], verts[4], verts[2]);
        geo_triListPushNew(&arena, &list, verts[1], verts[3], verts[0]);
        geo_triListPushNew(&arena, &list, verts[1], verts[0], verts[4]);

        // bottom faces
        geo_triListPushNew(&arena, &list, verts[0], verts[3], verts[2]);
        geo_triListPushNew(&arena, &list, verts[0], verts[2], verts[4]);
        geo_triListToSTLFile(&list, "testing/object.stl");

        geo_BSPNode* tree = geo_triListToBSP(&list, &arena);

        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        snz_testPrint(geo_BSPContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    snz_arenaClear(&arena);
    poolAllocClear(&pool);

    {
        geo_TriList cubeA = geo_cube(&arena);
        geo_TriList cubeB = geo_cube(&arena);
        geo_triListTransform(&cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        geo_triListTransform(&cubeB, HMM_Translate(HMM_V3(1, 1, 1)));

        geo_BSPNode* treeA = geo_triListToBSP(&cubeA, &arena);
        geo_BSPNode* treeB = geo_triListToBSP(&cubeB, &arena);

        geo_TriList* aClipped = geo_bspClipTris(true, &cubeA, treeB, &arena);
        geo_TriList* bClipped = geo_bspClipTris(true, &cubeB, treeA, &arena);
        geo_TriList* final = geo_triListJoin(aClipped, bClipped);
        geo_triListRecoverNonBroken(&final, &arena);
        geo_triListToSTLFile(final, "testing/union.stl");
    }

    {
        geo_TriList cubeA = geo_cube(&arena);
        geo_TriList cubeB = geo_cube(&arena);
        geo_triListTransform(&cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        geo_triListTransform(&cubeB, HMM_Translate(HMM_V3(1, 1, 1)));

        geo_BSPNode* treeA = geo_triListToBSP(&cubeA, &arena);
        geo_BSPNode* treeB = geo_triListToBSP(&cubeB, &arena);

        geo_TriList* aClipped = geo_bspClipTris(true, &cubeA, treeB, &arena);
        geo_TriList* bClipped = geo_bspClipTris(false, &cubeB, treeA, &arena);
        geo_triListInvert(bClipped);
        geo_TriList* final = geo_triListJoin(aClipped, bClipped);
        geo_triListRecoverNonBroken(&final, &arena);
        geo_triListToSTLFile(final, "testing/intersection.stl");
    }

    snz_arenaDeinit(&arena);
    poolAllocDeinit(&pool);
}

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
bool geo_intersectRayAndTri(HMM_Vec3 rayOrigin, HMM_Vec3 rayDir,
                            HMM_Vec3 pts[3], HMM_Vec3* outPos) {
    *outPos = HMM_V3(0, 0, 0);

    HMM_Vec3 edge1 = HMM_Sub(pts[1], pts[0]);
    HMM_Vec3 edge2 = HMM_Sub(pts[2], pts[0]);
    HMM_Vec3 ray_cross_e2 = HMM_Cross(rayDir, edge2);
    float det = HMM_Dot(edge1, ray_cross_e2);

    if (geo_floatZero(det)) {
        return false;
    }

    float inv_det = 1.0 / det;
    HMM_Vec3 s = HMM_Sub(rayOrigin, pts[0]);
    float u = inv_det * HMM_Dot(s, ray_cross_e2);

    if ((u < 0 && fabsf(u) > geo_EPSILON) || (u > 1 && fabsf(u - 1) > geo_EPSILON)) {
        return false;
    }

    HMM_Vec3 s_cross_e1 = HMM_Cross(s, edge1);
    float v = inv_det * HMM_Dot(rayDir, s_cross_e1);

    if ((v < 0 && fabsf(v) > geo_EPSILON) || (u + v > 1 && fabsf(u + v - 1) > geo_EPSILON)) {
        return false;
    }

    // At this stage we can compute t to find out where the intersection point is on the line.
    float t = inv_det * HMM_Dot(edge2, s_cross_e1);

    // ray intersection
    if (t > geo_EPSILON) {
        *outPos = HMM_Mul(rayDir, t);
        *outPos = HMM_Add(rayOrigin, *outPos);
        return true;
    } else {
        // This means that there is a line intersection but not a ray intersection.
        return false;
    }
}

typedef struct geo_MeshFace geo_MeshFace;
struct geo_MeshFace {
    geo_TriListNode* tri;  // FIXME: this should be >1 but thats a project.
    geo_MeshFace* next;
};

typedef struct {
    ren3d_Mesh renderMesh;
    geo_TriList triList;
    geo_BSPNode* bspTree;
    geo_MeshFace* firstFace;
} geo_Mesh;

ren3d_Mesh _geo_hoverMesh;

void geo_buildHoverAndSelectionMesh(geo_Mesh* mesh, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseRayDir) {
    geo_MeshFace* closestHovered = NULL;
    {  // find and draw the hovered face
        float closestHoveredDist = INFINITY;

        for (geo_MeshFace* face = mesh->firstFace; face; face = face->next) {
            HMM_Vec3 intersection = HMM_V3(0, 0, 0);
            // NOTE: any model transform will have to change this to adapt
            bool hovered = geo_intersectRayAndTri(cameraPos, mouseRayDir, face->tri->elems, &intersection);
            if (hovered) {
                float dist = HMM_LenSqr(HMM_Sub(intersection, cameraPos));
                if (dist < closestHoveredDist) {
                    closestHoveredDist = dist;
                    closestHovered = face;
                }
            }  // end hover check
        }  // end face loop

        if (!closestHovered) {
            return;
        }

        float* const selectionAnim = SNZU_USE_MEM(float, "geo selection anim");
        geo_MeshFace** const prevFace = SNZU_USE_MEM(geo_MeshFace*, "geo prevFace");
        if (snzu_useMemIsPrevNew() || (*prevFace) != closestHovered) {
            *selectionAnim = 0;
        }
        *prevFace = closestHovered;
        snzu_easeExp(selectionAnim, true, 15);

        ren3d_meshDeinit(&_geo_hoverMesh);
        geo_TriListNode* t = closestHovered->tri;
        HMM_Vec3 normal = geo_triNormal(t->a, t->b, t->c);
        float scaleFactor = HMM_SqrtF(closestHoveredDist);
        HMM_Vec3 offset = HMM_Mul(normal, scaleFactor * 0.03f * *selectionAnim);
        ren3d_Vert verts[] = {
            (ren3d_Vert){
                .pos = HMM_Add(t->a, offset),
                .normal = normal,
            },
            (ren3d_Vert){
                .pos = HMM_Add(t->b, offset),
                .normal = normal,
            },
            (ren3d_Vert){
                .pos = HMM_Add(t->c, offset),
                .normal = normal,
            },
        };
        _geo_hoverMesh = ren3d_meshInit(verts, 3);
        HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
        HMM_Vec4 color = ui_colorText;  // FIXME: not text colored
        color.A = 0.1;                  // FIXME: ui val for this alpha
        ren3d_drawMesh(&_geo_hoverMesh, vp, model, color, HMM_V3(-1, -1, -1), 1);
    }  // end hovered drawing
}

/*
so we are making a way of identifying geometry for shit

doing it LL of opp style is miserable in so many ways
    each operation needs a method of location a piece of geometry
    i.e. it's own struct/enum tag/values/handling code etc.
    each op also needs a way of storing what made it

a backup would be lovely -> 'closest fuckin position + normal?'


so sketches are 'primatives' (stored ptr style to identify components, edge+dir to ident faces)
the planned operations we have are:

union
difference
intersection
extrusion
revolve
pattern
shell
loft
fillet
chamfer


and the outputted geometry can be distinguished via:
and that gives you the exact fucking thing you need to find a source sketch by golly ive done it again

union:
    f1+f2->line
    or f->f
    or l->l
    or l+f->p
    or p->p
diff: same as union
intersection: same as union

extrude:
    f->f1
    f->f2
    l->f
    p->l
    p->p1
    p->p2
    l->l1
    l->l2

revolve:
    l->f
    l->l
    f->f1
    f->f2
    p->p1
    p->p2
    p->l

pattern:
    l|f|p -> (l|f|p)i,j,k,etc.

shell:
    l->f
    f->f
    f->f1
    f->f2
    p->p1
    p->p2
    l->l1
    l->l2

loft:
    f->f1
    f->f2
    l->f
    l->l1
    l->l2
    p->l
    p->p1
    p->p2

fillet:
    l->f
    l->l1
    l->l2
    p->f
    p->l
chamfer: same as fillet

and then the face for some geometry just stores the ID for which of these it is and your're golden
*/