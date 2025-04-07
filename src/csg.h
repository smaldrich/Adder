#pragma once
#include "snooze.h"
#include "geometry.h"
#include "PoolAlloc.h"
#include "mesh.h"

typedef struct mesh_BSPTri mesh_BSPTri;
struct mesh_BSPTri {
    mesh_BSPTri* next;
    mesh_BSPTri* ancestor;
    mesh_Face* sourceFace;
    geo_Tri tri;
    bool anyChildDeleted;
    bool recovered;
};

typedef struct {
    mesh_BSPTri* first;
    mesh_BSPTri* last;
} mesh_BSPTriList;

typedef struct mesh_BSPNode mesh_BSPNode;
struct mesh_BSPNode {
    mesh_BSPNode* outerTree;
    mesh_BSPNode* innerTree;

    mesh_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    // FIXME: redundant? // Space inefficent??
    geo_Tri tri;
};

typedef enum {
    MESH_PR_COPLANAR,
    MESH_PR_WITHIN,
    MESH_PR_OUTSIDE,
    MESH_PR_SPANNING,
} mesh_PlaneRelation;

mesh_BSPTriList mesh_BSPTriListInit() {
    return (mesh_BSPTriList) { .first = NULL, .last = NULL };
}

// destructive to node->next
// FIXME: this is extremely unintuitive and has knack for causing circular lists
// huge problem, please find a better solution for the list situation
// same issue with other list functions also
void mesh_BSPTriListPush(mesh_BSPTriList* list, mesh_BSPTri* node) {
    assert(node != NULL);
    node->next = list->first;
    if (list->first == NULL) {
        list->last = node;
    }
    list->first = node;
}

// destructive to node->next in listA, both ptrs can be null
mesh_BSPTriList* mesh_BSPTriListJoin(mesh_BSPTriList* listA, mesh_BSPTriList* listB) {
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
void mesh_BSPTriListPushNew(snz_Arena* arena, mesh_BSPTriList* list, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, mesh_Face* source) {
    mesh_BSPTri* new = SNZ_ARENA_PUSH(arena, mesh_BSPTri);
    *new = (mesh_BSPTri){
        .tri = (geo_Tri){
            .a = a,
            .b = b,
            .c = c,
        },
        .sourceFace = source,
    };
    mesh_BSPTriListPush(list, new);
}

void mesh_BSPTriListTransform(mesh_BSPTriList* list, HMM_Mat4 transform) {
    for (mesh_BSPTri* node = list->first; node; node = node->next) {
        for (int i = 0; i < 3; i++) {
            HMM_Vec4 v4 = (HMM_Vec4){ .XYZ = node->tri.elems[i], .W = 1 };
            node->tri.elems[i] = HMM_MulM4V4(transform, v4).XYZ;
        }
    }
}

// flips the normals for every tri within list
void mesh_BSPTriListInvert(mesh_BSPTriList* list) {
    for (mesh_BSPTri* node = list->first; node; node = node->next) {
        HMM_Vec3 temp = node->tri.a;
        node->tri.a = node->tri.c;
        node->tri.c = temp;
    }
}

void mesh_BSPTriListToSTLFile(const mesh_BSPTriList* list, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (const mesh_BSPTri* tri = list->first; tri; tri = tri->next) {
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

static mesh_PlaneRelation _mesh_triClassify(geo_Tri tri, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    mesh_PlaneRelation finalRel = 0;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(tri.elems[i], planeStart), planeNormal);
        if (geo_floatZero(dot)) {
            finalRel |= MESH_PR_COPLANAR;
        } else {
            finalRel |= dot > 0 ? MESH_PR_OUTSIDE : MESH_PR_WITHIN;
        }
    }
    return finalRel;
}

// parent assumed non-null, new nodes allocated into arena
// takes a list of nodes with normals and origins filled out, organizes it + its children into the tree shape based on splits
// will allocate more nodes over the course of fixing, these are put into arena
static void _mesh_BSPTreeFixNode(snz_Arena* arena, mesh_BSPNode* parent, mesh_BSPNode* listOfPossibleNodes) {
    HMM_Vec3 splitNormal = geo_triNormal(parent->tri);

    mesh_BSPNode* innerList = NULL;
    mesh_BSPNode* outerList = NULL;
    {
        mesh_BSPNode* next = NULL;
        for (mesh_BSPNode* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            mesh_PlaneRelation rel = _mesh_triClassify(node->tri, splitNormal, parent->tri.a);
            if (rel == MESH_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == MESH_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                mesh_BSPNode* duplicate = SNZ_ARENA_PUSH(arena, mesh_BSPNode);
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
        _mesh_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree != NULL) {
        _mesh_BSPTreeFixNode(arena, parent->outerTree, outerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the list of tris, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
mesh_BSPNode* mesh_BSPTriListToBSP(const mesh_BSPTriList* tris, snz_Arena* arena) {
    mesh_BSPNode* tree = NULL;

    for (const mesh_BSPTri* tri = tris->first; tri; tri = tri->next) {
        mesh_BSPNode* node = SNZ_ARENA_PUSH(arena, mesh_BSPNode);
        node->nextAvailible = tree;
        tree = node;
        node->tri = tri->tri;
    }

    _mesh_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool mesh_BSPContainsPoint(mesh_BSPNode* tree, HMM_Vec3 point) {
    mesh_BSPNode* node = tree;
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
static void _mesh_BSPTriSplit(mesh_BSPTri* tri, snz_Arena* arena, mesh_BSPTriList* outOutsideList, mesh_BSPTriList* outInsideList, mesh_BSPNode* cutter) {
    HMM_Vec3 cutNormal = geo_triNormal(cutter->tri);

    mesh_PlaneRelation rel = _mesh_triClassify(tri->tri, cutNormal, cutter->tri.a);

    if (rel == MESH_PR_COPLANAR) {
        assert(false);  // FIXME: this
        // FIXME: how do coplanar things factor into this?
        // FIXME: full coplanar testing
    } else if (rel == MESH_PR_OUTSIDE) {
        mesh_BSPTriListPush(outOutsideList, tri);
    } else if (rel == MESH_PR_WITHIN) {
        mesh_BSPTriListPush(outInsideList, tri);
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
            mesh_BSPTri* t1 = SNZ_ARENA_PUSH(arena, mesh_BSPTri);

            *t1 = (mesh_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,  // FIXME: sometimes, if a face gets split to become non-contiguous, this is just wrong and we need another face
            };

            mesh_BSPTri* t2 = SNZ_ARENA_PUSH(arena, mesh_BSPTri);
            *t2 = (mesh_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[2],
                    .b = rotatedVerts[3],
                    .c = rotatedVerts[4],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            mesh_BSPTri* t3 = SNZ_ARENA_PUSH(arena, mesh_BSPTri);
            *t3 = (mesh_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[4],
                    .b = rotatedVerts[0],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            mesh_BSPTriList* t1List = t1Outside ? outOutsideList : outInsideList;
            mesh_BSPTriList* t2and3List = t1Outside ? outInsideList : outOutsideList;

            mesh_BSPTriListPush(t1List, t1);
            mesh_BSPTriListPush(t2and3List, t2);
            mesh_BSPTriListPush(t2and3List, t3);
        }  // end 5 vert-check
        else if (vertCount == 4) {
            mesh_BSPTri* t1 = SNZ_ARENA_PUSH(arena, mesh_BSPTri);
            *t1 = (mesh_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            mesh_BSPTri* t2 = SNZ_ARENA_PUSH(arena, mesh_BSPTri);
            *t2 = (mesh_BSPTri){
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
            mesh_BSPTriListPush(t1Outside ? outOutsideList : outInsideList, t1);
            mesh_BSPTriListPush(t1Outside ? outInsideList : outOutsideList, t2);
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
mesh_BSPTriList* mesh_BSPTriListClip(bool within, mesh_BSPTriList* meshTris, mesh_BSPNode* tree, snz_Arena* arena) {
    mesh_BSPTriList inside = mesh_BSPTriListInit();
    mesh_BSPTriList outside = mesh_BSPTriListInit();

    mesh_BSPTri* next = NULL;  // memod because placing in the other list would overwrite
    for (mesh_BSPTri* tri = meshTris->first; tri; tri = next) {
        next = tri->next;
        _mesh_BSPTriSplit(tri, arena, &outside, &inside, tree);
    }

    mesh_BSPTriList* listToClear = NULL;
    if (tree->innerTree != NULL) {
        inside = *mesh_BSPTriListClip(within, &inside, tree->innerTree, arena);
    } else {
        if (within) {
            listToClear = &inside;
        }
    }

    if (tree->outerTree != NULL) {
        outside = *mesh_BSPTriListClip(within, &outside, tree->outerTree, arena);
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
        for (mesh_BSPTri* node = listToClear->first; node; node = node->next) {
            for (mesh_BSPTri* n = node; n; n = n->ancestor) {
                n->anyChildDeleted = true;
            }
        }
        listToClear->first = NULL;
        listToClear->last = NULL;
    }

    mesh_BSPTriList* out = SNZ_ARENA_PUSH(arena, mesh_BSPTriList);
    *out = *mesh_BSPTriListJoin(&inside, &outside);
    return out;
}

// destructive to the original tri list
void mesh_BSPTriListRecoverNonBroken(mesh_BSPTriList** tris, snz_Arena* arena) {
    mesh_BSPTriList* recovered = SNZ_ARENA_PUSH(arena, mesh_BSPTriList);
    mesh_BSPTriList* trisRemaining = SNZ_ARENA_PUSH(arena, mesh_BSPTriList);

    mesh_BSPTri* next = NULL;
    for (mesh_BSPTri* node = (*tris)->first; node; node = next) {
        next = node->next;

        mesh_BSPTri* oldest = node->ancestor;
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
                mesh_BSPTriListPush(recovered, oldest);
                oldest->recovered = true;
            }
        } else {
            mesh_BSPTriListPush(trisRemaining, node);
        }
    }

    *tris = mesh_BSPTriListJoin(trisRemaining, recovered);
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

        mesh_BSPTriList list = mesh_BSPTriListInit();
        mesh_BSPTriListPushNew(&arena, &list, verts[0], verts[1], verts[2], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[0], verts[2], verts[3], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[0], verts[3], verts[1], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[3], verts[2], verts[1], NULL);

        mesh_BSPNode* tree = mesh_BSPTriListToBSP(&list, &arena);
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
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

        mesh_BSPTriList list = mesh_BSPTriListInit();
        // top faces
        mesh_BSPTriListPushNew(&arena, &list, verts[1], verts[2], verts[3], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[1], verts[4], verts[2], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[1], verts[3], verts[0], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[1], verts[0], verts[4], NULL);

        // bottom faces
        mesh_BSPTriListPushNew(&arena, &list, verts[0], verts[3], verts[2], NULL);
        mesh_BSPTriListPushNew(&arena, &list, verts[0], verts[2], verts[4], NULL);
        mesh_BSPTriListToSTLFile(&list, "testing/object.stl");

        mesh_BSPNode* tree = mesh_BSPTriListToBSP(&list, &arena);

        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        snz_testPrint(mesh_BSPContainsPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    snz_arenaClear(&arena);
    poolAllocClear(&pool);

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));

        mesh_BSPNode* treeA = mesh_BSPTriListToBSP(&cubeA.bspTris, &arena);
        mesh_BSPNode* treeB = mesh_BSPTriListToBSP(&cubeB.bspTris, &arena);

        mesh_BSPTriList* aClipped = mesh_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        mesh_BSPTriList* bClipped = mesh_BSPTriListClip(true, &cubeB.bspTris, treeA, &arena);
        mesh_BSPTriList* final = mesh_BSPTriListJoin(aClipped, bClipped);
        mesh_BSPTriListRecoverNonBroken(&final, &arena);
        mesh_BSPTriListToSTLFile(final, "testing/union.stl");
    }

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));

        mesh_BSPNode* treeA = mesh_BSPTriListToBSP(&cubeA.bspTris, &arena);
        mesh_BSPNode* treeB = mesh_BSPTriListToBSP(&cubeB.bspTris, &arena);

        mesh_BSPTriList* aClipped = mesh_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        mesh_BSPTriList* bClipped = mesh_BSPTriListClip(false, &cubeB.bspTris, treeA, &arena);
        mesh_BSPTriListInvert(bClipped);
        mesh_BSPTriList* final = mesh_BSPTriListJoin(aClipped, bClipped);
        mesh_BSPTriListRecoverNonBroken(&final, &arena);
        mesh_BSPTriListToSTLFile(final, "testing/intersection.stl");
    }

    snz_arenaDeinit(&arena);
    poolAllocDeinit(&pool);
}