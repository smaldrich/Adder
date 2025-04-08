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