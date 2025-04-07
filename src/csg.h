#pragma once
#include "snooze.h"
#include "geometry.h"
#include "PoolAlloc.h"
#include "mesh.h"

typedef struct csg_Tri csg_Tri;
struct csg_Tri {
    csg_Tri* next;
    csg_Tri* ancestor;
    mesh_Face* face;
    geo_Tri tri;
    bool anyChildDeleted;
    bool recovered;
};

typedef struct {
    csg_Tri* first;
    csg_Tri* last;
} csg_TriList;

typedef struct csg_Node csg_Node;
struct csg_Node {
    csg_Node* outerTree;
    csg_Node* innerTree;
    csg_Node* nextAvailible;  // used for construction only, probs should remove for perf but who cares
    geo_Tri tri;
};

typedef enum {
    CSG_PR_COPLANAR,
    CSG_PR_WITHIN,
    CSG_PR_OUTSIDE,
    CSG_PR_SPANNING,
} _csg_PlaneRelation;

csg_TriList csg_triListInit() {
    return (csg_TriList) { .first = NULL, .last = NULL };
}

// destructive to node->next
// FIXME: this is extremely unintuitive and has knack for causing circular lists
// huge problem, please find a better solution for the list situation
// same issue with other list functions also
void csg_triListPush(csg_TriList* list, csg_Tri* node) {
    assert(node != NULL);
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
void csg_triListPushNew(snz_Arena* arena, csg_TriList* list, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, mesh_Face* source) {
    csg_Tri* new = SNZ_ARENA_PUSH(arena, csg_Tri);
    *new = (csg_Tri){
        .tri = (geo_Tri){
            .a = a,
            .b = b,
            .c = c,
        },
        .sourceFace = source,
    };
    csg_triListPush(list, new);
}

// flips the normals for every tri within list
void csg_triListInvert(csg_TriList* list) {
    for (csg_Tri* node = list->first; node; node = node->next) {
        HMM_Vec3 temp = node->tri.a;
        node->tri.a = node->tri.c;
        node->tri.c = temp;
    }
}

void csg_triListToSTLFile(const csg_TriList* list, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (const csg_Tri* tri = list->first; tri; tri = tri->next) {
        HMM_Vec3 normal = geo_triNormal(tri->tri);
        fprintf(f, "facet normal %f %f %f\n", normal.X, normal.Y, normal.Z);
        fprintf(f, "outer loop\n");
        fprintf(f, "vertex %f %f %f\n", tri->tri.a.X, tri->tri.a.Y, tri->tri.a.Z);
        fprintf(f, "vertex %f %f %f\n", tri->tri.b.X, tri->tri.b.Y, tri->tri.b.Z);
        fprintf(f, "vertex %f %f %f\n", tri->tri.c.X, tri->tri.c.Y, tri->tri.c.Z);
        fprintf(f, "endloop\n");
        fprintf(f, "endfacet\n");
    }

    fprintf(f, "endsolid object\n");
    fclose(f);
}

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

// parent assumed non-null, new nodes allocated into arena
// takes a list of nodes with normals and origins filled out, organizes it + its children into the tree shape based on splits
// will allocate more nodes over the course of fixing, these are put into arena
static void _csg_nodeFix(snz_Arena* arena, csg_Node* parent, csg_Node* listOfPossibleNodes) {
    HMM_Vec3 splitNormal = geo_triNormal(parent->tri);

    csg_Node* innerList = NULL;
    csg_Node* outerList = NULL;
    {
        csg_Node* next = NULL;
        for (csg_Node* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            _csg_PlaneRelation rel = _csg_triClassify(node->tri, splitNormal, parent->tri.a);
            if (rel == CSG_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == CSG_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                csg_Node* duplicate = SNZ_ARENA_PUSH(arena, csg_Node);
                duplicate->tri = node->tri;

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
        _csg_nodeFix(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree != NULL) {
        _csg_nodeFix(arena, parent->outerTree, outerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the list of tris, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
csg_Node* csg_triListToNodes(const csg_TriList* tris, snz_Arena* arena) {
    csg_Node* tree = NULL;

    for (const csg_Tri* tri = tris->first; tri; tri = tri->next) {
        csg_Node* node = SNZ_ARENA_PUSH(arena, csg_Node);
        node->nextAvailible = tree;
        tree = node;
        node->tri = tri->tri;
    }

    _csg_nodeFix(arena, tree, tree->nextAvailible);
    return tree;
}

bool csg_bspContainsPoint(csg_Node* tree, HMM_Vec3 point) {
    csg_Node* node = tree;
    while (true) {  // FIXME: failsafe here :)
        HMM_Vec3 diff = HMM_SubV3(point, node->tri.a);
        float dot = HMM_DotV3(diff, geo_triNormal(node->tri));
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

// FIXME: point deduplication of some kind?
// destructive to tri->next, and both outlists next
static void _csg_triSplit(csg_Tri* tri, snz_Arena* arena, csg_TriList* outOutsideList, csg_TriList* outInsideList, csg_Node* cutter) {
    HMM_Vec3 cutNormal = geo_triNormal(cutter->tri);

    _csg_PlaneRelation rel = _csg_triClassify(tri->tri, cutNormal, cutter->tri.a);

    if (rel == CSG_PR_COPLANAR) {
        assert(false);  // FIXME: this
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
            HMM_Vec3 pt = tri->tri.elems[i];
            verts[vertCount] = pt;
            assert(vertCount < 5);
            vertCount++;

            HMM_Vec3 nextPt = tri->tri.elems[(i + 1) % 3];
            HMM_Vec3 diff = HMM_SubV3(nextPt, pt);
            HMM_Vec3 direction = HMM_NormV3(diff);
            float t = 0;
            bool intersectExists = geo_rayPlaneIntersection(cutter->tri.a, cutNormal, pt, direction, &t);
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
        HMM_Vec3 rotatedVerts[5] = { 0 };
        for (int i = 0; i < vertCount; i++) {
            rotatedVerts[i] = verts[(i + firstIntersectionIdx) % vertCount];
        }

        if (vertCount == 5) {
            bool t1Outside = HMM_DotV3(HMM_SubV3(rotatedVerts[1], cutter->tri.a), cutNormal) > 0;
            csg_Tri* t1 = SNZ_ARENA_PUSH(arena, csg_Tri);

            *t1 = (csg_Tri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,  // FIXME: sometimes, if a face gets split to become non-contiguous, this is just wrong and we need another face
            };

            csg_Tri* t2 = SNZ_ARENA_PUSH(arena, csg_Tri);
            *t2 = (csg_Tri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[2],
                    .b = rotatedVerts[3],
                    .c = rotatedVerts[4],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            csg_Tri* t3 = SNZ_ARENA_PUSH(arena, csg_Tri);
            *t3 = (csg_Tri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[4],
                    .b = rotatedVerts[0],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            csg_TriList* t1List = t1Outside ? outOutsideList : outInsideList;
            csg_TriList* t2and3List = t1Outside ? outInsideList : outOutsideList;

            csg_triListPush(t1List, t1);
            csg_triListPush(t2and3List, t2);
            csg_triListPush(t2and3List, t3);
        }  // end 5 vert-check
        else if (vertCount == 4) {
            csg_Tri* t1 = SNZ_ARENA_PUSH(arena, csg_Tri);
            *t1 = (csg_Tri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            csg_Tri* t2 = SNZ_ARENA_PUSH(arena, csg_Tri);
            *t2 = (csg_Tri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[2],
                    .b = rotatedVerts[3],
                    .c = rotatedVerts[0],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            // t1B should never be colinear with the cut plane so long as rotation has been done correctly
            bool t1Outside = HMM_DotV3(HMM_SubV3(t1->tri.b, cutter->tri.a), cutNormal) > 0;
            csg_triListPush(t1Outside ? outOutsideList : outInsideList, t1);
            csg_triListPush(t1Outside ? outInsideList : outOutsideList, t2);
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
csg_TriList* csg_triListClip(bool within, csg_TriList* meshTris, csg_Node* tree, snz_Arena* arena) {
    csg_TriList inside = csg_triListInit();
    csg_TriList outside = csg_triListInit();

    csg_Tri* next = NULL;  // memod because placing in the other list would overwrite
    for (csg_Tri* tri = meshTris->first; tri; tri = next) {
        next = tri->next;
        _csg_triSplit(tri, arena, &outside, &inside, tree);
    }

    csg_TriList* listToClear = NULL;
    if (tree->innerTree != NULL) {
        inside = *csg_triListClip(within, &inside, tree->innerTree, arena);
    } else {
        if (within) {
            listToClear = &inside;
        }
    }

    if (tree->outerTree != NULL) {
        outside = *csg_triListClip(within, &outside, tree->outerTree, arena);
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
        for (csg_Tri* node = listToClear->first; node; node = node->next) {
            for (csg_Tri* n = node; n; n = n->ancestor) {
                n->anyChildDeleted = true;
            }
        }
        listToClear->first = NULL;
        listToClear->last = NULL;
    }

    csg_TriList* out = SNZ_ARENA_PUSH(arena, csg_TriList);
    *out = *csg_triListJoin(&inside, &outside);
    return out;
}

// destructive to the original tri list
void csg_triListRecoverNonBroken(csg_TriList** tris, snz_Arena* arena) {
    csg_TriList* recovered = SNZ_ARENA_PUSH(arena, csg_TriList);
    csg_TriList* trisRemaining = SNZ_ARENA_PUSH(arena, csg_TriList);

    csg_Tri* next = NULL;
    for (csg_Tri* node = (*tris)->first; node; node = next) {
        next = node->next;

        csg_Tri* oldest = node->ancestor;
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
                csg_triListPush(recovered, oldest);
                oldest->recovered = true;
            }
        } else {
            csg_triListPush(trisRemaining, node);
        }
    }

    *tris = csg_triListJoin(trisRemaining, recovered);
}

void csg_tests() {
    snz_testPrintSection("csg");

    snz_Arena arena = snz_arenaInit(1000000, "csg test arena");
    PoolAlloc pool = poolAllocInit();

    {
        HMM_Vec3 verts[] = {
            HMM_V3(0, 0, 0),
            HMM_V3(1, 0, 0),
            HMM_V3(1, 1, 0),
            HMM_V3(1, 0, -1),
        };

        csg_TriList list = csg_triListInit();
        csg_triListPushNew(&arena, &list, verts[0], verts[1], verts[2], NULL);
        csg_triListPushNew(&arena, &list, verts[0], verts[2], verts[3], NULL);
        csg_triListPushNew(&arena, &list, verts[0], verts[3], verts[1], NULL);
        csg_triListPushNew(&arena, &list, verts[3], verts[2], verts[1], NULL);

        csg_Node* tree = csg_triListToNodes(&list, &arena);
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
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

        csg_TriList list = csg_triListInit();
        // top faces
        csg_triListPushNew(&arena, &list, verts[1], verts[2], verts[3], NULL);
        csg_triListPushNew(&arena, &list, verts[1], verts[4], verts[2], NULL);
        csg_triListPushNew(&arena, &list, verts[1], verts[3], verts[0], NULL);
        csg_triListPushNew(&arena, &list, verts[1], verts[0], verts[4], NULL);

        // bottom faces
        csg_triListPushNew(&arena, &list, verts[0], verts[3], verts[2], NULL);
        csg_triListPushNew(&arena, &list, verts[0], verts[2], verts[4], NULL);
        csg_triListToSTLFile(&list, "testing/object.stl");

        csg_Node* tree = csg_triListToNodes(&list, &arena);

        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        snz_testPrint(csg_bspContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    snz_arenaClear(&arena);
    poolAllocClear(&pool);

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));

        csg_Node* treeA = csg_triListToNodes(&cubeA.bspTris, &arena);
        csg_Node* treeB = csg_triListToNodes(&cubeB.bspTris, &arena);

        csg_TriList* aClipped = csg_triListClip(true, &cubeA.bspTris, treeB, &arena);
        csg_TriList* bClipped = csg_triListClip(true, &cubeB.bspTris, treeA, &arena);
        csg_TriList* final = csg_triListJoin(aClipped, bClipped);
        csg_triListRecoverNonBroken(&final, &arena);
        csg_triListToSTLFile(final, "testing/union.stl");
    }

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));

        csg_Node* treeA = csg_triListToNodes(&cubeA.bspTris, &arena);
        csg_Node* treeB = csg_triListToNodes(&cubeB.bspTris, &arena);

        csg_TriList* aClipped = csg_triListClip(true, &cubeA.bspTris, treeB, &arena);
        csg_TriList* bClipped = csg_triListClip(false, &cubeB.bspTris, treeA, &arena);
        csg_triListInvert(bClipped);
        csg_TriList* final = csg_triListJoin(aClipped, bClipped);
        csg_triListRecoverNonBroken(&final, &arena);
        csg_triListToSTLFile(final, "testing/intersection.stl");
    }

    snz_arenaDeinit(&arena);
    poolAllocDeinit(&pool);
}