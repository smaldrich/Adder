#pragma once

#include "snooze.h"
#include "geometry.h"
#include "mesh.h"
#include "PoolAlloc.h"

// Nodes here are what hold a BSP Tree structure for csg operations on meshes.
typedef struct csg_Node csg_Node;
struct csg_Node {
    union {
        struct {
            csg_Node* outerTree;
            csg_Node* innerTree;
            HMM_Vec3 origin;
            HMM_Vec3 normal;
        };
        struct { // used during construction of trees as temp vars
            csg_Node* nextUnsorted;
            geo_Tri* sourceTri;
            bool shouldSkip;
        } temp;
    };
};

typedef enum {
    CSG_PR_COPLANAR,
    CSG_PR_WITHIN,
    CSG_PR_OUTSIDE,
    CSG_PR_SPANNING,
} _csg_PlaneRelation;

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

static void _csg_facesToNodesInner(snz_Arena* arena, csg_Node* parent, csg_Node* unsorted) {
    HMM_Vec3 normal = geo_triNormal(*parent->temp.sourceTri);
    HMM_Vec3 origin = parent->temp.sourceTri->a;

    csg_Node* innerList = NULL;
    csg_Node* outerList = NULL;
    {
        csg_Node* next = NULL;
        for (csg_Node* node = unsorted; node; node = next) {
            next = node->temp.nextUnsorted;
            if (node->temp.shouldSkip) {
                continue;
            }

            _csg_PlaneRelation rel = _csg_triClassify(*node->temp.sourceTri, normal, origin);
            if (rel == CSG_PR_OUTSIDE) {
                node->temp.nextUnsorted = outerList;
                outerList = node;
            } else if (rel == CSG_PR_WITHIN) {
                node->temp.nextUnsorted = innerList;
                innerList = node;
            } else if (rel == CSG_PR_COPLANAR) {
                // if coplanar, add this doesn't do anything
                // FIXME: this case is only deduping adjacent coplanar tris, but there are more cases to catch to make trees smaller
                node->temp.shouldSkip = true;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                csg_Node* duplicate = SNZ_ARENA_PUSH(arena, csg_Node);
                duplicate->temp.sourceTri = node->temp.sourceTri;

                node->temp.nextUnsorted = innerList;
                innerList = node;
                duplicate->temp.nextUnsorted = outerList;
                outerList = duplicate;
            }
        }
    }
    memset(parent, 0, sizeof(*parent));
    parent->innerTree = innerList;
    parent->outerTree = outerList;
    parent->origin = origin;
    parent->normal = normal;

    if (parent->innerTree != NULL) {
        _csg_facesToNodesInner(arena, parent->innerTree, innerList->temp.nextUnsorted);
    }
    if (parent->outerTree != NULL) {
        _csg_facesToNodesInner(arena, parent->outerTree, outerList->temp.nextUnsorted);
    }
}

csg_Node* csg_facesToNodes(const mesh_FaceSlice* faces, snz_Arena* arena) {
    csg_Node* firstNode = NULL;
    for (int64_t faceIdx = 0; faceIdx < faces->count; faceIdx++) {
        mesh_Face* f = &faces->elems[faceIdx];
        for (int64_t triIdx = 0; triIdx < f->tris.count; triIdx++) {
            geo_Tri* t = &f->tris.elems[triIdx];
            csg_Node* node = SNZ_ARENA_PUSH(arena, csg_Node);
            *node = (csg_Node){
                .temp = {
                    .nextUnsorted = firstNode,
                    .sourceTri = t,
                },
            };
            firstNode = node;
        }
    }
    _csg_facesToNodesInner(arena, firstNode, firstNode->temp.nextUnsorted);
    return firstNode;
}

bool csg_nodesContainPoint(csg_Node* tree, HMM_Vec3 point) {
    csg_Node* node = tree;
    while (true) {  // FIXME: failsafe here :)
        HMM_Vec3 diff = HMM_SubV3(point, node->origin);
        float dot = HMM_DotV3(diff, node->normal);
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
    SNZ_ASSERT(false, "unreachable.");
}

// returns number of subtris that tri got split into (also length of resultTris and inOrOut)
// outInOrOut true values mean in
static int _csg_splitTri(const geo_Tri* tri, const csg_Node* cutter, geo_Tri** outResultTris, bool** outInOrOut, snz_Arena* arena) {
    _csg_PlaneRelation rel = _csg_triClassify(*tri, cutter->normal, cutter->origin);
    if (rel == CSG_PR_COPLANAR) {
        assert(false);  // FIXME: this
        // FIXME: how do coplanar things factor into this?
        // FIXME: full coplanar testing
    } else if (rel == CSG_PR_OUTSIDE || rel == CSG_PR_WITHIN) {
        *outResultTris = SNZ_ARENA_PUSH(arena, geo_Tri);
        (*outResultTris)[0] = *tri;
        *outInOrOut = SNZ_ARENA_PUSH(arena, bool);
        (*outInOrOut)[0] = (rel == CSG_PR_WITHIN);
        return 1;
    }
    // tri is spanning, we can do an actual split here

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
        bool intersectExists = geo_rayPlaneIntersection(cutter->origin, cutter->normal, pt, direction, &t);
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
        geo_Tri* tris = SNZ_ARENA_PUSH_ARR(arena, 3, geo_Tri);
        bool* insOrOuts = SNZ_ARENA_PUSH_ARR(arena, 3, bool);

        bool t1Outside = HMM_DotV3(HMM_SubV3(rotatedVerts[1], cutter->origin), cutter->normal) > 0;
        insOrOuts[0] = t1Outside;
        tris[0] = (geo_Tri){
            .a = rotatedVerts[0],
            .b = rotatedVerts[1],
            .c = rotatedVerts[2],
        };

        insOrOuts[1] = !t1Outside;
        tris[1] = (geo_Tri){
            .a = rotatedVerts[2],
            .b = rotatedVerts[3],
            .c = rotatedVerts[4],
        };

        insOrOuts[2] = !t1Outside;
        tris[2] = (geo_Tri){
            .a = rotatedVerts[4],
            .b = rotatedVerts[0],
            .c = rotatedVerts[2],
        };

        (*outResultTris) = tris;
        (*outInOrOut) = insOrOuts;
        return 3;
    }  // end 5 vert-check
    else if (vertCount == 4) {
        geo_Tri* tris = SNZ_ARENA_PUSH_ARR(arena, 2, geo_Tri);
        bool* insOrOuts = SNZ_ARENA_PUSH_ARR(arena, 2, bool);

        // t1B should never be colinear with the cut plane so long as rotation has been done correctly
        bool t1Outside = HMM_DotV3(HMM_SubV3(rotatedVerts[1], cutter->origin), cutter->normal) > 0;
        insOrOuts[0] = t1Outside;
        tris[0] = (geo_Tri){
            .a = rotatedVerts[0],
            .b = rotatedVerts[1],
            .c = rotatedVerts[2],
        };

        insOrOuts[1] = !t1Outside;
        tris[1] = (geo_Tri){
            .a = rotatedVerts[2],
            .b = rotatedVerts[3],
            .c = rotatedVerts[0],
        };

        (*outResultTris) = tris;
        (*outInOrOut) = insOrOuts;
        return 2;
    }
    // anything greater than 5 should be impossible, anything less than 4 should have been put on
    // one side, not marked spanning
    SNZ_ASSERT(false, "unreachable.");
    return 0;
}

// return indicates if any child got clipped
// pushes all sub-tris to arena if any were clipped, if nothing is clipped, defers push to caller
static bool _csg_clipTri(const geo_Tri* tri, bool clipWithin, const csg_Node* cutter, snz_Arena* arena, snz_Arena* scratch) {
    bool* splitTrisInOrOut = NULL;
    geo_Tri* splitTris = NULL;
    int splitCount = _csg_splitTri(tri, cutter, &splitTris, &splitTrisInOrOut, scratch);

    bool anyClipped = false;
    bool clipped[3] = { 0 };
    SNZ_ASSERTF(splitCount <= 3, "Somehow a triangle got split into %d sub-tris.", splitCount);
    for (int i = 0; i < splitCount; i++) {
        bool within = splitTrisInOrOut[i];
        csg_Node* nextCutter = within ? cutter->innerTree : cutter->outerTree;
        if (!nextCutter) {
            if (within == clipWithin) {
                anyClipped = true;
                clipped[i] = true;
            }
        } else {
            anyClipped |= _csg_clipTri(&splitTris[i], clipWithin, nextCutter, arena, scratch);
        }
    }

    if (!anyClipped) {
        return false;
    }

    for (int i = 0; i < splitCount; i++) {
        if (clipped[i]) {
            continue;
        }
        *SNZ_ARENA_PUSH(arena, geo_Tri) = splitTris[i];
    }
    return true;
}

typedef struct _csg_TempFace _csg_TempFace;
struct _csg_TempFace {
    _csg_TempFace* next;
    mesh_Face face;
};

static _csg_TempFace* _csg_tempFacesFindLast(_csg_TempFace* first) {
    for (_csg_TempFace* f = first; f; f = f->next) {
        if (f->next == NULL) {
            return f;
        }
    }
    SNZ_ASSERT(false, "this function probably shouldn't be used for whatever you are doing.");
    return NULL;
}

static _csg_TempFace* _csg_facesToTempFaces(const mesh_FaceSlice* faces, snz_Arena* arena) {
    _csg_TempFace* firstInFace = NULL;
    for (int64_t i = 0; i < faces->count; i++) {
        const mesh_Face* ogFace = &faces->elems[i];
        _csg_TempFace* newFace = SNZ_ARENA_PUSH(arena, _csg_TempFace);
        newFace->face = *ogFace;
        newFace->next = firstInFace;
        firstInFace = newFace;
    }
    return firstInFace;
}

static mesh_FaceSlice _csg_tempFacesToFaces(_csg_TempFace* firstFace, snz_Arena* arena) {
    SNZ_ARENA_ARR_BEGIN(arena, mesh_Face);
    for (_csg_TempFace* f = firstFace; f; f = f->next) {
        *SNZ_ARENA_PUSH(arena, mesh_Face) = f->face;
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Face);
}

// destructive to OG face list - reuses nodes in output
// FIXME: handle the case where a face gets split and we need new faceIds
// FIXME: put new faceIDs on to everything that changes
static _csg_TempFace* _csg_tempFacesClip(_csg_TempFace* faces, const csg_Node* tree, bool removeWithin, snz_Arena* arena, snz_Arena* scratch) {
    _csg_TempFace* firstOutFace = NULL;
    int faceIdx = 0;
    for (_csg_TempFace* f = faces; f; faceIdx++) {
        SNZ_ARENA_ARR_BEGIN(arena, geo_Tri);
        for (int64_t i = 0; i < f->face.tris.count; i++) {
            void* scratchStart = scratch->end;

            geo_Tri t = f->face.tris.elems[i];
            bool anyClipped = _csg_clipTri(&t, removeWithin, tree, arena, scratch);
            if (!anyClipped) {
                *SNZ_ARENA_PUSH(arena, geo_Tri) = t;
            }

            scratch->end = scratchStart;
        }
        f->face.tris = SNZ_ARENA_ARR_END(arena, geo_Tri);

        _csg_TempFace* next = f->next;
        // don't push to out list if nothing made it past clipping
        if (f->face.tris.count > 0) {
            f->next = firstOutFace;
            firstOutFace = f;
        }
        f = next;
    }
    return firstOutFace;
}

static void _csg_tempFacesInvert(_csg_TempFace* first) {
    for (_csg_TempFace* f = first; f; f = f->next) {
        for (int64_t j = 0; j < f->face.tris.count; j++) {
            geo_Tri* tri = &f->face.tris.elems[j];
            HMM_Vec3 temp = tri->c;
            tri->c = tri->b;
            tri->b = temp;
        }
    }
}

mesh_FaceSlice csg_facesUnion(const mesh_FaceSlice* a, const mesh_FaceSlice* b, snz_Arena* arena, snz_Arena* scratch) {
    _csg_TempFace* aFaces = _csg_facesToTempFaces(a, scratch);
    _csg_TempFace* bFaces = _csg_facesToTempFaces(b, scratch);
    csg_Node* aNodes = csg_facesToNodes(a, scratch);
    csg_Node* bNodes = csg_facesToNodes(b, scratch);

    aFaces = _csg_tempFacesClip(aFaces, bNodes, true, arena, scratch);
    bFaces = _csg_tempFacesClip(bFaces, aNodes, true, arena, scratch);

    _csg_TempFace* last = _csg_tempFacesFindLast(aFaces);
    last->next = bFaces;
    return _csg_tempFacesToFaces(aFaces, arena);
}

mesh_FaceSlice csg_facesDifference(const mesh_FaceSlice* a, const mesh_FaceSlice* b, snz_Arena* arena, snz_Arena* scratch) {
    _csg_TempFace* aFaces = _csg_facesToTempFaces(a, scratch);
    _csg_TempFace* bFaces = _csg_facesToTempFaces(b, scratch);
    csg_Node* aNodes = csg_facesToNodes(a, scratch);
    csg_Node* bNodes = csg_facesToNodes(b, scratch);

    aFaces = _csg_tempFacesClip(aFaces, bNodes, true, arena, scratch);
    bFaces = _csg_tempFacesClip(bFaces, aNodes, false, arena, scratch);
    _csg_tempFacesInvert(bFaces);

    _csg_TempFace* last = _csg_tempFacesFindLast(aFaces);
    last->next = bFaces;
    return _csg_tempFacesToFaces(aFaces, arena);
}

mesh_FaceSlice csg_facesIntersection(const mesh_FaceSlice* a, const mesh_FaceSlice* b, snz_Arena* arena, snz_Arena* scratch) {
    _csg_TempFace* aFaces = _csg_facesToTempFaces(a, scratch);
    _csg_TempFace* bFaces = _csg_facesToTempFaces(b, scratch);
    csg_Node* aNodes = csg_facesToNodes(a, scratch);
    csg_Node* bNodes = csg_facesToNodes(b, scratch);

    aFaces = _csg_tempFacesClip(aFaces, bNodes, false, arena, scratch);
    _csg_tempFacesInvert(aFaces);
    bFaces = _csg_tempFacesClip(bFaces, aNodes, false, arena, scratch);
    _csg_tempFacesInvert(bFaces);

    _csg_TempFace* last = _csg_tempFacesFindLast(aFaces);
    last->next = bFaces;
    return _csg_tempFacesToFaces(aFaces, arena);
}

void csg_tests() {
    snz_testPrintSection("csg");

    snz_Arena arena = snz_arenaInit(1000000, "csg test arena");
    snz_Arena scratch = snz_arenaInit(1000000, "csg test scratch arena");
    PoolAlloc pool = poolAllocInit();

    {
        HMM_Vec3 verts[] = {
            HMM_V3(0, 0, 0),
            HMM_V3(1, 0, 0),
            HMM_V3(1, 1, 0),
            HMM_V3(1, 0, -1),
        };

        mesh_FaceSlice faces = (mesh_FaceSlice){
            .count = 1,
            .elems = SNZ_ARENA_PUSH(&arena, mesh_Face),
        };
        SNZ_ARENA_ARR_BEGIN(&arena, geo_Tri);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[0], verts[1], verts[2]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[0], verts[2], verts[3]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[0], verts[3], verts[1]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[3], verts[2], verts[1]);
        faces.elems[0].tris = SNZ_ARENA_ARR_END(&arena, geo_Tri);

        csg_Node* tree = csg_facesToNodes(&faces, &arena);
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0.5, 0.5, 0.0)) == true, "Tetra contains pt");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0.5, 1.0, 0.5)) == false, "Tetra doesn't contain pt");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, 0, 0)) == true, "Tetra contains edge pt");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(1, 0, -1)) == true, "Tetra contains edge pt 2");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(-1, 0, -1)) == false, "Tetra doesn't contain point 2");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(3, 3, 3)) == false, "Tetra doesn't contain point 3");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(INFINITY, NAN, NAN)) == false, "Tetra doesn't contain invalid floats");
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

        mesh_FaceSlice faces = (mesh_FaceSlice){
            .count = 1,
            .elems = SNZ_ARENA_PUSH(&arena, mesh_Face),
        };
        SNZ_ARENA_ARR_BEGIN(&arena, geo_Tri);

        // top faces
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[1], verts[2], verts[3]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[1], verts[4], verts[2]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[1], verts[3], verts[0]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[1], verts[0], verts[4]);

        // bottom faces
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[0], verts[3], verts[2]);
        *SNZ_ARENA_PUSH(&arena, geo_Tri) = geo_triInit(verts[0], verts[2], verts[4]);
        faces.elems[0].tris = SNZ_ARENA_ARR_END(&arena, geo_Tri);

        mesh_facesToSTLFile(faces, "testing/object.stl");

        csg_Node* tree = csg_facesToNodes(&faces, &arena);

        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, 0, 0)) == true, "horn contain test 1");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, 10, 0)) == false, "horn contain test 2");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, 0, -0.1)) == true, "horn contain test 3");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(-0.5, 0, 0)) == true, "horn contain test 4");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, -1, -1)) == true, "horn contain test 5");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(-1, -1, -1)) == false, "horn contain test 6");
        snz_testPrint(csg_nodesContainPoint(tree, HMM_V3(0, -0.5, 0)) == false, "horn contain test 7");
    }

    snz_arenaClear(&arena);
    snz_arenaClear(&scratch);
    poolAllocClear(&pool);

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));
        mesh_FaceSlice faces = csg_facesUnion(&cubeA, &cubeB, &arena, &scratch);
        mesh_facesToSTLFile(faces, "testing/union.stl");
    }

    {
        mesh_FaceSlice cubeA = mesh_cube(&arena);
        mesh_FaceSlice cubeB = mesh_cube(&arena);
        mesh_facesTransform(cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_facesTranslate(cubeB, HMM_V3(1, 1, 1));
        mesh_FaceSlice faces = csg_facesDifference(&cubeA, &cubeB, &arena, &scratch);
        mesh_facesToSTLFile(faces, "testing/difference.stl");
    }

    snz_arenaDeinit(&arena);
    snz_arenaDeinit(&scratch);
    poolAllocDeinit(&pool);
}
