#pragma once

#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "snooze.h"
#include "ui.h"
#include "geometry.h"

typedef struct mesh_Face mesh_Face;
struct mesh_Face {
    mesh_Face* next;
    geo_TriSlice tris;
    ui_SelectionState sel;
};

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

typedef struct mesh_Edge mesh_Edge;
struct mesh_Edge {
    mesh_Edge* next;
    HMM_Vec3Slice points;
    ui_SelectionState sel;

    mesh_Face* faceA;
    mesh_Face* faceB;
};

typedef struct {
    HMM_Vec3 pos;
    ui_SelectionState sel;
} mesh_Corner;

SNZ_SLICE(mesh_Corner);

typedef struct {
    ren3d_Mesh renderMesh;

    mesh_BSPTriList bspTris;
    mesh_BSPNode* bspTree;

    mesh_Face* firstFace;
    mesh_Edge* firstEdge;
    mesh_CornerSlice corners;
} mesh_Mesh;

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

ren3d_Mesh mesh_BSPTriListToRenderMesh(mesh_BSPTriList list, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
    for (mesh_BSPTri* tri = list.first; tri; tri = tri->next) {
        HMM_Vec3 triNormal = geo_triNormal(tri->tri);
        for (int i = 0; i < 3; i++) {
            *SNZ_ARENA_PUSH(scratch, ren3d_Vert) = (ren3d_Vert){
                .pos = tri->tri.elems[i],
                .normal = triNormal,
                .color = HMM_V4(1, 1, 1, 1),
            };
        }
    }
    ren3d_VertSlice s = SNZ_ARENA_ARR_END(scratch, ren3d_Vert);
    return ren3d_meshInit(s.elems, s.count);
}

// uses a meshes BSPTriList to regenerate the face tri arrays
// assumes valid mesh->bspTris and and old but non-null mesh->faces data.
void mesh_BSPTriListToFaceTris(PoolAlloc* pool, mesh_Mesh* mesh) {
    for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        f->tris = (geo_TriSlice){ 0 };
    }
    for (mesh_BSPTri* tri = mesh->bspTris.first; tri; tri = tri->next) {
        SNZ_ASSERT(tri->sourceFace, "tri with a null source face.");
        geo_Tri* t = poolAllocPushArray(pool, tri->sourceFace->tris.elems, tri->sourceFace->tris.count, geo_Tri);
        *t = tri->tri;
    }
}

// 2 width cube, centered on the origin
// FIXME: ew
// out mesh has valid bsp tris a face list that the tris are linked to (which does not have its copy of the triangle data)
mesh_Mesh mesh_cube(snz_Arena* arena) {
    mesh_Mesh out = (mesh_Mesh){ 0 };
    out.bspTris = mesh_BSPTriListInit();

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
    mesh_Face* face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[0], v[2], v[1], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[0], v[3], v[2], face);

    face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[7], v[6], v[2], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[7], v[2], v[3], face);

    face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[6], v[5], v[1], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[6], v[1], v[2], face);

    face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[5], v[4], v[0], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[5], v[0], v[1], face);

    face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[4], v[7], v[3], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[4], v[3], v[0], face);

    face = SNZ_ARENA_PUSH(arena, mesh_Face);
    face->next = out.firstFace;
    out.firstFace = face;
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[4], v[5], v[6], face);
    mesh_BSPTriListPushNew(arena, &out.bspTris, v[4], v[6], v[7], face);

    return out;
}

static ui_SelectionStatus* _mesh_meshGenSelStatuses(mesh_Mesh* mesh, mesh_Face* hoveredFace, mesh_Edge* hoveredEdge, mesh_Corner* hoveredCorner, snz_Arena* scratch) {
    ui_SelectionStatus* firstStatus = NULL;
    for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = f == hoveredFace,
            .next = firstStatus,
            .state = &f->sel,
        };
        firstStatus = status;
    }

    for (mesh_Edge* e = mesh->firstEdge; e; e = e->next) {
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = e == hoveredEdge,
            .next = firstStatus,
            .state = &e->sel,
        };
        firstStatus = status;
    }

    for (int i = 0; i < mesh->corners.count; i++) {
        mesh_Corner* c = &mesh->corners.elems[i];
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = c == hoveredCorner,
            .next = firstStatus,
            .state = &c->sel,
        };
        firstStatus = status;
    }
    return firstStatus;
}

// FIXME: geo_ui.h for consistency
void mesh_meshBuild(mesh_Mesh* mesh, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseDir, const snzu_Interaction* inter, HMM_Vec2 panelSize, snz_Arena* scratch) {
    SNZ_ASSERT(cameraPos.X || !cameraPos.X, "AHH");
    SNZ_ASSERT(mouseDir.X || !mouseDir.X, "AHH");

    mesh_Face* hoveredFace = NULL;
    mesh_Edge* hoveredEdge = NULL;
    mesh_Corner* hoveredCorner = NULL;
    { // finding hovered elts.
        float minDistSquared = INFINITY;
        mesh_Face* minFace = NULL;
        for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
            for (int i = 0; i < f->tris.count; i++) {
                geo_Tri t = f->tris.elems[i];
                HMM_Vec3 pos = HMM_V3(0, 0, 0);
                // FIXME: bounding box opt. to cull tri checks
                if (geo_rayTriIntersection(cameraPos, mouseDir, t, &pos)) {
                    float distSquared = HMM_LenSqr(HMM_Sub(pos, cameraPos));
                    if (distSquared < minDistSquared) {
                        minDistSquared = distSquared;
                        minFace = f;
                        break;
                    }
                }
            }
        } // end face loop

        float clipDist = minDistSquared;
        if (!isinf(minDistSquared)) {
            clipDist = sqrtf(clipDist);
        }

        mesh_Edge* minEdge = NULL;
        for (mesh_Edge* e = mesh->firstEdge; e; e = e->next) {
            for (int i = 0; i < e->points.count - 1; i++) {
                geo_Line l = (geo_Line){
                    .a = e->points.elems[i],
                    .b = e->points.elems[i + 1],
                };
                float distFromRay = 0;
                HMM_Vec3 pos = geo_rayClosestPointOnSegment(cameraPos, HMM_Add(cameraPos, mouseDir), l.a, l.b, &distFromRay);
                float distFromCamera = HMM_Len(HMM_Sub(pos, cameraPos));

                float size = 0.01 * distFromCamera;
                if (distFromRay > size) {
                    continue;
                } else if (distFromCamera > clipDist + size) {
                    continue;
                }
                clipDist = distFromCamera;
                minFace = NULL;
                minEdge = e;
            }
        }

        mesh_Corner* minCorner = NULL;
        for (int i = 0; i < mesh->corners.count; i++) {
            mesh_Corner* c = &mesh->corners.elems[i];
            float dist = geo_rayPointDistance(cameraPos, mouseDir, c->pos);
            float distFromCamera = HMM_Len(HMM_Sub(c->pos, cameraPos));
            float size = 0.002 * distFromCamera;
            if (dist > size) {
                continue;
            } else if (distFromCamera > clipDist + size) {
                continue;
            }
            clipDist = dist;
            minEdge = NULL;
            minFace = NULL;
            minCorner = c;
        }

        hoveredCorner = minCorner;
        hoveredEdge = minEdge;
        hoveredFace = minFace;
    } // end hover checks

    { // sel region logic
        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "region");
        ui_SelectionStatus* firstStatus = _mesh_meshGenSelStatuses(mesh, hoveredFace, hoveredEdge, hoveredCorner, scratch);
        ui_selectionRegionUpdate(region, firstStatus, inter->mouseActions[SNZU_MB_LEFT], inter->mousePosLocal, inter->keyMods & KMOD_SHIFT, true, false);
        ui_selectionRegionAnimate(region, firstStatus);
    }

    ren3d_VertSlice faceMeshVerts = { 0 };
    {
        SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
        for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
            float sumAnim = f->sel.hoverAnim + f->sel.selectionAnim;
            if (!geo_floatZero(sumAnim)) {
                HMM_Vec4 targetColor = ui_colorAccent;
                targetColor.A = 0.8;
                HMM_Vec4 color = HMM_Lerp(ui_colorTransparentPanel, f->sel.selectionAnim, targetColor);
                color.A = HMM_Lerp(0.0f, SNZ_MIN(sumAnim, 1), color.A);
                for (int i = 0; i < f->tris.count; i++) {
                    geo_Tri t = f->tris.elems[i];
                    HMM_Vec3 normal = geo_triNormal(t);
                    for (int j = 0; j < 3; j++) {
                        float scaleFactor = HMM_Len(HMM_Sub(cameraPos, t.elems[j])) * f->sel.hoverAnim * 0.02f;
                        HMM_Vec3 pos = HMM_Add(t.elems[j], HMM_MulV3F(normal, scaleFactor));
                        ren3d_Vert* v = SNZ_ARENA_PUSH(scratch, ren3d_Vert);
                        *v = (ren3d_Vert){
                            .normal = normal,
                            .pos = pos,
                            .color = color,
                        };
                    } // end tri pt loop
                } // end tri loop
            }
        } // end face loop
        faceMeshVerts = SNZ_ARENA_ARR_END(scratch, ren3d_Vert);

        // text to indicate index of each face
        // int i = -1;
        // for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        //     i++;
        //     geo_Align uiAlign = (geo_Align){
        //         .pt = HMM_V3(0, 0, 0),
        //         .normal = HMM_V3(0, 0, 1),
        //         .vertical = HMM_V3(0, 1, 0),
        //     };
        //     geo_Tri t = f->tris.elems[0];
        //     geo_Align faceAlign = (geo_Align){
        //         .pt = t.a,
        //         .normal = geo_triNormal(t),
        //         .vertical = HMM_Sub(t.b, t.a),
        //     };
        //     HMM_Mat4 textVP = geo_alignToM4(uiAlign, faceAlign);
        //     textVP = HMM_Mul(vp, textVP);
        //     const char* str = snz_arenaFormatStr(scratch, "face %d", i);
        //     HMM_Vec4 color = HMM_Lerp(ui_colorText, f->sel.selectionAnim, ui_colorAccent);
        //     snzr_drawTextScaled(
        //         HMM_V2(0, 0),
        //         HMM_V2(-INFINITY, -INFINITY),
        //         HMM_V2(INFINITY, INFINITY),
        //         color,
        //         str,
        //         strlen(str),
        //         ui_labelFont,
        //         textVP,
        //         0.1,
        //         false);
        // }
    }
    { // render
        ren3d_drawMesh(
            &mesh->renderMesh,
            vp, HMM_M4D(1.0f),
            HMM_V4(1, 1, 1, 1), HMM_V3(-1, -1, -1), ui_lightAmbient);

        if (faceMeshVerts.count && faceMeshVerts.elems) {
            HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
            glDisable(GL_DEPTH_TEST);
            ren3d_Mesh renderMesh = ren3d_meshInit(faceMeshVerts.elems, faceMeshVerts.count);
            // FIXME: lighting shouldn't affect this
            ren3d_drawMesh(&renderMesh, vp, model, HMM_V4(1, 1, 1, 1), HMM_V3(-1, -1, -1), 1);
            ren3d_meshDeinit(&renderMesh); // FIXME: do a buffer data instead??
            glEnable(GL_DEPTH_TEST);
        }

        for (mesh_Edge* edge = mesh->firstEdge; edge; edge = edge->next) {
            // FIXME: ew gross
            if (edge->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            for (int i = 0; i < edge->points.count - 1; i++) {
                HMM_Vec3 a = edge->points.elems[i];
                HMM_Vec3 b = edge->points.elems[i + 1];
                HMM_Vec4 pts[2] = {
                    HMM_V4(a.X, a.Y, a.Z, 1),
                    HMM_V4(b.X, b.Y, b.Z, 1),
                };

                for (int i = 0; i < 2; i++) {
                    float scaleFactor = HMM_Len(HMM_Sub(cameraPos, pts[i].XYZ)) * 0.01;
                    HMM_Vec3 offset = HMM_Mul(HMM_Norm(HMM_Sub(cameraPos, pts[i].XYZ)), scaleFactor);
                    pts[i].XYZ = HMM_Add(pts[i].XYZ, offset);
                }

                HMM_Vec4 color = HMM_Lerp(ui_colorText, edge->sel.selectionAnim, ui_colorAccent);
                float thickness = HMM_Lerp(ui_lineThickness, edge->sel.hoverAnim, ui_lineHoveredThickness);
                snzr_drawLine(pts, 2, color, thickness, vp);
            }
            // FIXME: ew gross
            if (edge->sel.selected) {
                glEnable(GL_DEPTH_TEST);
            }
        }

        for (int i = 0; i < mesh->corners.count; i++) {
            mesh_Corner* corner = &mesh->corners.elems[i];
            // FIXME: ew gross
            if (corner->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            float size = HMM_Lerp(ui_cornerHalfSize, corner->sel.hoverAnim + corner->sel.selectionAnim, ui_cornerHoveredHalfSize);
            HMM_Vec4 col = HMM_Lerp(ui_colorText, corner->sel.selectionAnim, ui_colorAccent);
            ren3d_drawBillboard(vp, panelSize, *ui_cornerTexture, col, corner->pos, HMM_V2(size, size));
            if (corner->sel.selected) {
                glEnable(GL_DEPTH_TEST);
            }
        }

        // really dirty wireframe for tris in each face
        // glDisable(GL_DEPTH_TEST);
        // for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        //     for (int64_t i = 0; i < f->tris.count; i++) {
        //         geo_Tri t = f->tris.elems[i];
        //         HMM_Vec4 pts[4] = { 0 };
        //         pts[0].XYZ = t.a;
        //         pts[1].XYZ = t.b;
        //         pts[2].XYZ = t.c;
        //         pts[3].XYZ = t.a;
        //         snzr_drawLine(pts, 4, HMM_V4(0.5, 0.5, 0.5, 0.5), 2, vp);
        //     }
        // }
        // glEnable(GL_DEPTH_TEST);
    } // end render
}

typedef struct {
    geo_Line a;
    geo_Line b;
} _mesh_LinePair;

SNZ_SLICE(_mesh_LinePair);

typedef struct {
    mesh_Face* a;
    mesh_Face* b;
} _mesh_FacePair;

SNZ_SLICE(_mesh_FacePair);

static bool _mesh_linePairAdjacentFast(geo_Line a, geo_Line b) {
    if (geo_v3Equal(a.a, b.a)) {
        return true;
    } else if (geo_v3Equal(a.a, b.b)) {
        return true;
    } else if (geo_v3Equal(a.b, b.a)) {
        return true;
    } else if (geo_v3Equal(a.b, b.b)) {
        return true;
    }
    return false;
}

// clips A to B
static bool _mesh_linePairAdjacent(geo_Line a, geo_Line b, geo_Line* outClipped) {
    HMM_Vec3 aDir = HMM_Norm(HMM_Sub(a.b, a.a));
    HMM_Vec3 bDir = HMM_Norm(HMM_Sub(b.b, b.a));

    if (!geo_v3Equal(bDir, aDir) && !geo_v3Equal(HMM_Sub(HMM_V3(0, 0, 0), bDir), aDir)) {
        return false;
    }

    float dot = HMM_Dot(HMM_Norm(HMM_Sub(b.a, a.a)), aDir);
    if (geo_floatZero(HMM_LenSqr(HMM_Sub(b.a, a.a)))) {
        dot = 1;
    }
    if (!(geo_floatEqual(dot, 1) || geo_floatEqual(dot, -1))) {
        return false;
    }

    float aaProj = 0; // would end up as a.a - a.a, aka 0
    float abProj = HMM_Dot(HMM_Sub(a.b, a.a), aDir);
    float baProj = HMM_Dot(HMM_Sub(b.a, a.a), aDir);
    float bbProj = HMM_Dot(HMM_Sub(b.b, a.a), aDir);

    float aMin = SNZ_MIN(aaProj, abProj);
    float bMax = SNZ_MAX(baProj, bbProj);
    if (bMax < aMin) {
        return false;
    }

    float aMax = SNZ_MAX(aaProj, abProj);
    float bMin = SNZ_MIN(baProj, bbProj);
    if (bMin > aMax) {
        return false;
    }

    float overlapA = SNZ_MAX(aMin, bMin);
    float overlapB = SNZ_MIN(aMax, bMax);;

    // FIXME: I have no idea if this is the behvaior that it should be, but it seems right enough to not want zero len edges??
    if (geo_floatEqual(overlapA, overlapB)) {
        return false;
    }

    *outClipped = (geo_Line){
        .a = HMM_Add(a.a, HMM_Mul(aDir, overlapA)),
        .b = HMM_Add(a.a, HMM_Mul(aDir, overlapB)),
    };
    return true;
}

// only does one direction from the start point, marks any used lines NaN
// doesn't push the start point to the outputted slice, will push end point
static HMM_Vec3Slice _mesh_groupPointsAdjacent(geo_LineSlice lines, HMM_Vec3 start, snz_Arena* arena) {
    HMM_Vec3 pt = start;
    SNZ_ARENA_ARR_BEGIN(arena, HMM_Vec3);
    while (true) {
        bool found = false;
        for (int i = 0; i < lines.count; i++) {
            geo_Line l = lines.elems[i];
            bool aEqual = geo_v3Equal(l.a, pt);
            if (aEqual || geo_v3Equal(l.b, pt)) {
                pt = aEqual ? l.b : l.a;
                lines.elems[i].a = HMM_V3(NAN, NAN, NAN);
                lines.elems[i].b = HMM_V3(NAN, NAN, NAN);
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        *SNZ_ARENA_PUSH(arena, HMM_Vec3) = pt;
        if (geo_v3Equal(pt, start)) {
            break;
        }
    }
    return SNZ_ARENA_ARR_END(arena, HMM_Vec3);
}

static HMM_Vec3Slice _mesh_orderedPointsFromLineSet(geo_LineSlice lines, snz_Arena* scratch, snz_Arena* arena) {
    HMM_Vec3 startPt = HMM_V3(NAN, NAN, NAN);
    for (int i = 0; i < lines.count; i++) {
        if (isnan(lines.elems[i].a.X)) {
            continue;
        }
        startPt = lines.elems[i].a;
        break;
    }
    if (isnan(startPt.X)) {
        return (HMM_Vec3Slice) { 0 };
    }

    FILE* f = fopen("testing/meshDebugLog", "w");
    fprintf(f, "lines:\n");
    for (int i = 0; i < lines.count; i++) {
        geo_Line l = lines.elems[i];
        fprintf(f, "%d: %.2f, %.2f, %.2f\n", i, l.a.X, l.a.Y, l.a.Z);
        fprintf(f, "%d: %.2f, %.2f, %.2f\n", i, l.b.X, l.b.Y, l.b.Z);
    }

    fprintf(f, "initial");
    for (int i = 0; i < lines.count; i++) {
        if (isnan(lines.elems[i].a.X)) {
            fprintf(f, "X");
        } else {
            fprintf(f, "O");
        }
    }
    fprintf(f, "\n");

    HMM_Vec3Slice forward = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    fprintf(f, "post-fwd");
    for (int i = 0; i < lines.count; i++) {
        if (isnan(lines.elems[i].a.X)) {
            fprintf(f, "X");
        } else {
            fprintf(f, "O");
        }
    }
    fprintf(f, "\n");
    HMM_Vec3Slice reverse = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    fprintf(f, "post-rev");
    for (int i = 0; i < lines.count; i++) {
        if (isnan(lines.elems[i].a.X)) {
            fprintf(f, "X");
        } else {
            fprintf(f, "O");
        }
    }
    fprintf(f, "\n");
    fclose(f);
    // HMM_Vec3Slice extra = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    // SNZ_ASSERT(!extra.count, "edge points can't be ordered, there are three segments adjacent.");

    HMM_Vec3Slice out = { 0 };
    // add one for center point, which has not been pushed by either call to group adj
    out.count = forward.count + 1 + reverse.count;
    out.elems = SNZ_ARENA_PUSH_ARR(arena, out.count, HMM_Vec3);

    for (int i = 0; i < reverse.count; i++) {
        out.elems[i] = reverse.elems[(reverse.count - 1) - i];
    }
    out.elems[reverse.count] = startPt;
    memcpy(&out.elems[reverse.count + 1], forward.elems, sizeof(HMM_Vec3) * forward.count);
    return out;
}

// expects valid face tris on the mesh
// no issue if out and scratch are the same arena
// FIXME: time complexity is horrifying
void mesh_meshGenerateEdges(mesh_Mesh* mesh, snz_Arena* out, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, _mesh_FacePair); // FIXME: wasting a lot of space here storing next ptrs (GOD I WISH SUBSTRUCTS EXISTED AHHH)
    for (mesh_Face* faceA = mesh->firstFace; faceA; faceA = faceA->next) {
        for (mesh_Face* faceB = faceA->next; faceB; faceB = faceB->next) {
            *SNZ_ARENA_PUSH(scratch, _mesh_FacePair) = (_mesh_FacePair){
                .a = faceA,
                .b = faceB,
            };
            // FIXME: bounding box checks to cut pair count down
        }
    }
    _mesh_FacePairSlice facePairs = SNZ_ARENA_ARR_END(scratch, _mesh_FacePair);

    for (int pairIdx = 0; pairIdx < facePairs.count; pairIdx++) {
        _mesh_FacePair p = facePairs.elems[pairIdx];
        mesh_Face* faceA = p.a;
        mesh_Face* faceB = p.b;

        SNZ_ARENA_ARR_BEGIN(scratch, _mesh_LinePair);
        for (int aIdx = 0; aIdx < faceA->tris.count; aIdx++) {
            for (int bIdx = 0; bIdx < faceB->tris.count; bIdx++) {
                geo_Tri aTri = faceA->tris.elems[aIdx];
                geo_Tri bTri = faceB->tris.elems[bIdx];

                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        _mesh_LinePair pair = (_mesh_LinePair){
                            .a = (geo_Line) {
                                .a = aTri.elems[i],
                                .b = aTri.elems[(i + 1) % 3],
                            },
                            .b = (geo_Line) {
                                .a = bTri.elems[j],
                                .b = bTri.elems[(j + 1) % 3],
                            },
                        };
                        *SNZ_ARENA_PUSH(scratch, _mesh_LinePair) = pair;
                    } // end 2nd tri edge loop
                } // end 1st tri edge loop
            }
        }
        _mesh_LinePairSlice pairs = SNZ_ARENA_ARR_END(scratch, _mesh_LinePair);

        SNZ_ARENA_ARR_BEGIN(out, geo_Line);
        for (int i = 0; i < pairs.count; i++) {
            _mesh_LinePair pair = pairs.elems[i];
            geo_Line s = { 0 };
            bool adj = _mesh_linePairAdjacent(pair.a, pair.b, &s);
            if (adj) {
                *SNZ_ARENA_PUSH(out, geo_Line) = s;
                SNZ_ASSERTF(!geo_v3Equal(s.a, s.b), "Edge gen with 0 len. %f,%f,%f", s.a.X, s.a.Y, s.a.Z);
            }
        }
        geo_LineSlice clipped = SNZ_ARENA_ARR_END(out, geo_Line);
        if (clipped.count <= 0) {
            continue;
        }

        HMM_Vec3Slice points = _mesh_orderedPointsFromLineSet(clipped, scratch, out);
        while (points.count) { // FIXME: cutoff
            mesh_Edge* e = SNZ_ARENA_PUSH(out, mesh_Edge);
            *e = (mesh_Edge){
                .faceA = faceA,
                .faceB = faceB,
                .points = points,
                .next = mesh->firstEdge,
            };
            mesh->firstEdge = e;
            points = _mesh_orderedPointsFromLineSet(clipped, scratch, out);
        }

        // Individual edges for each segment
        // for (int i = 0; i < clipped.count; i++) {
        //     SNZ_ARENA_ARR_BEGIN(out, HMM_Vec3);
        //     *SNZ_ARENA_PUSH(out, HMM_Vec3) = clipped.elems[i].a;
        //     *SNZ_ARENA_PUSH(out, HMM_Vec3) = clipped.elems[i].b;
        //     HMM_Vec3Slice points = SNZ_ARENA_ARR_END(out, HMM_Vec3);

        //     mesh_Edge* e = SNZ_ARENA_PUSH(out, mesh_Edge);
        //     *e = (mesh_Edge){
        //         .faceA = faceA,
        //         .faceB = faceB,
        //         .points = points,
        //         .next = mesh->firstEdge,
        //     };
        //     mesh->firstEdge = e;
        // }
    } // end face pair loop
}

// expects facetris to be filled out
bool mesh_faceFlat(mesh_Face* f) {
    SNZ_ASSERTF(f->tris.count > 0, "face with %lld tris.", f->tris.count);
    HMM_Vec3 normal = geo_triNormal(f->tris.elems[0]);
    for (int i = 1; i < f->tris.count; i++) {
        geo_Tri t = f->tris.elems[i];
        if (!geo_v3Equal(geo_triNormal(t), normal)) {
            return false;
        }
    }
    return true;
}

SNZ_SLICE_NAMED(mesh_Edge*, mesh_EdgePtrSlice);

// HMM_Vec3Slice geo_meshFaceToVertLoops(mesh_Mesh* m, geo_MeshFace* f, snz_Arena* scratch, snz_Arena* out) {
//     SNZ_ARENA_ARR_BEGIN(scratch, mesh_Edge*);
//     for (mesh_Edge* e = m->firstEdge; e; e = e->next) {
//         if (e->faceA != f && e->faceB != f) {
//             continue;
//         }
//         *SNZ_ARENA_PUSH(scratch, mesh_Edge*) = e;
//     }
//     // collect edges that match this face from the mesh
//     geo_MeshEdgePtrSlice edges = SNZ_ARENA_ARR_END_NAMED(scratch, mesh_Edge*, geo_MeshEdgePtrSlice);

//     SNZ_ARENA_ARR_BEGIN(out, HMM_Vec3);
//     int i = -1;
//     while (true) {
//         i++;
//         if (i > edges.count) {
//             i = 0;
//         }
//         mesh_Edge* edge = edges.elems[i];
//         SNZ_ASSERTF(edge->points.count > 0, "edge with %lld points.", edge->points.count);

//         HMM_Vec3 start = edge->points.elems[0];
//         HMM_Vec3 end = edge->points.elems[edge->points.count - 1];
//     }

//     return SNZ_ARENA_ARR_END(out, HMM_Vec3);
// }


static bool _mesh_LinesAdjacent(geo_Line a, geo_Line b, HMM_Vec3* outPt) {
    if (geo_v3Equal(a.a, b.a)) {
        *outPt = a.a;
        return true;
    } else if (geo_v3Equal(a.b, b.b)) {
        *outPt = a.b;
        return true;
    } else if (geo_v3Equal(a.a, b.b)) {
        *outPt = a.a;
        return true;
    } else if (geo_v3Equal(a.b, b.a)) {
        *outPt = a.b;
        return true;
    }
    return false;
}

void mesh_meshGenerateCorners(mesh_Mesh* mesh, snz_Arena* out, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, _mesh_LinePair);
    // FIXME: i am so sorry (add a bounding box optimization)
    for (mesh_Edge* edge = mesh->firstEdge; edge; edge = edge->next) {
        for (mesh_Edge* other = edge->next; other; other = other->next) {
            for (int i = 0; i < edge->points.count - 1; i++) {
                geo_Line lineA = (geo_Line){
                    .a = edge->points.elems[i],
                    .b = edge->points.elems[i + 1],
                };
                for (int j = 0; j < other->points.count - 1; j++) {
                    *SNZ_ARENA_PUSH(scratch, _mesh_LinePair) = (_mesh_LinePair){
                        .a = lineA,
                        .b = (geo_Line) {
                            .a = other->points.elems[j],
                            .b = other->points.elems[j + 1],
                        },
                    };
                }
            }
        }
    }
    _mesh_LinePairSlice pairs = SNZ_ARENA_ARR_END(scratch, _mesh_LinePair);

    SNZ_ARENA_ARR_BEGIN(out, mesh_Corner);
    for (int i = 0; i < pairs.count; i++) {
        _mesh_LinePair p = pairs.elems[i];
        HMM_Vec3 pt = { 0 };
        if (_mesh_LinesAdjacent(p.a, p.b, &pt)) {
            *SNZ_ARENA_PUSH(out, mesh_Corner) = (mesh_Corner){
                .pos = pt,
            };
        }
    }
    mesh->corners = SNZ_ARENA_ARR_END(out, mesh_Corner);
}

// FIXME: OCTREES YEAHH!!!
// FIXME: fillet seam detection??
static mesh_Face* _mesh_groupBSPTriListToFaces(mesh_BSPTriList tris, PoolAlloc* pool, snz_Arena* arena) {
    mesh_Face* faces = NULL;

    geo_LineSlice segments = (geo_LineSlice){
        .count = 0,
        .elems = poolAllocAlloc(pool, 0),
    };
    HMM_Vec3* normals = poolAllocAlloc(pool, 0);
    int64_t normalCount = 0;

    // FIXME: the two while trues in here should really have cutoffs
    while (true) {
        { // find and seed a new face
            mesh_BSPTri* t = { 0 };
            for (mesh_BSPTri* tri = tris.first; tri; tri = tri->next) {
                if (tri->sourceFace) {
                    continue;
                }
                t = tri;
                break;
            }
            if (!t) {
                break; // No tris left, everything has a face and we can end.
            }

            segments.count = 0;
            normalCount = 0;

            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(pool, segments.elems, segments.count, geo_Line) = (geo_Line){
                    .a = t->tri.elems[i],
                    .b = t->tri.elems[(i + 1) % 3],
                };
                *poolAllocPushArray(pool, normals, normalCount, HMM_Vec3) = geo_triNormal(t->tri);
            }


            int64_t total = 0;
            int64_t sectioned = 0;
            for (mesh_BSPTri* tri = tris.first; tri; tri = tri->next) {
                if (tri->sourceFace) {
                    sectioned++;
                }
                total++;
            }
            SNZ_LOGF("pushed a new face! %lld/%lld", sectioned, total);

            mesh_Face* f = SNZ_ARENA_PUSH(arena, mesh_Face);
            *f = (mesh_Face){
                .next = faces,
            };
            faces = f;
        }

        // FIXME: perf might be significantly better if we can iterate over an array of just the tri pts instead of a full LLNode + whatever else
        // but then we need to store the source face (WHY CANT I HAVE SUBSTRUCTS PLEASE PLEASE PLEASE)
        mesh_BSPTri* t = NULL;
        bool anyPushedThisLoop = true;
        int64_t triCount = 0;
        while (true) {
            if (!t || t->next == NULL) {
                if (!anyPushedThisLoop) {
                    break;
                }
                t = tris.first;
                anyPushedThisLoop = false;
            } else {
                t = t->next;
            }

            if (t->sourceFace) {
                continue;
            }

            HMM_Vec3 triNormal = geo_triNormal(t->tri);
            geo_Line triSegments[3] = { 0 };
            for (int i = 0; i < 3; i++) {
                triSegments[i] = (geo_Line){
                    .a = t->tri.elems[i],
                    .b = t->tri.elems[(i + 1) % 3],
                };
            }

            bool adj = false;
            for (int64_t i = 0; i < segments.count; i++) {
                float angle = _geo_angleBetweenV3(triNormal, normals[i]);
                if (angle > HMM_AngleDeg(30)) {
                    continue;
                }

                for (int j = 0; j < 3; j++) {
                    if (_mesh_linePairAdjacentFast(triSegments[j], segments.elems[i])) {
                        adj = true; // pop this tri
                        i = segments.count; // break the segment loop
                        break;
                    }
                }
            } // end segment loop

            if (!adj) {
                continue;
            }

            t->sourceFace = faces; // set source face to most recent one
            anyPushedThisLoop = true;
            triCount++;

            // FIXME: we can put one of these in place of the edge that we started with and not push it to the end
            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(pool, segments.elems, segments.count, geo_Line) = triSegments[i];
                *poolAllocPushArray(pool, normals, normalCount, HMM_Vec3) = geo_triNormal(t->tri);
            }

            if (triCount > 1000) {
                SNZ_LOG("quitting!");
                break;
            }
        } // end hunting for tris that could fit the face
    } // end while(any tris left)
    return faces;
}

// FIXME: error handling without the asserts
bool mesh_stlFileToMesh(const char* path, snz_Arena* arena, snz_Arena* scratch, PoolAlloc* pool, mesh_Mesh* outMesh) {
    SNZ_LOGF("Loading mesh from %s.", path);

    mesh_BSPTriList tris = { 0 };
    { // parse from file
        FILE* f = fopen(path, "r");
        SNZ_ASSERTF(f, "opening file '%s' failed.", path);
        char solid[6] = { 0 };
        char object[100] = { 0 };
        SNZ_ASSERT(fscanf(f, "%5s%99s", solid, object) == 2, "fscanf failed.");
        SNZ_ASSERTF(strcmp(solid, "solid") == 0, "expected 'solid', found '%s'", solid);

        while (true) {
            char facetOrEndsolid[9] = { 0 };
            SNZ_ASSERT(fscanf(f, "%8s", facetOrEndsolid) == 1, "fscanf failed.");
            if (strcmp(facetOrEndsolid, "endsolid") == 0) {
                break;
            } else if (strcmp(facetOrEndsolid, "facet") == 0) {
                // this the nominal case.
            } else {
                SNZ_ASSERTF(false, "expected 'facet' or 'endsolid', found '%s'", facetOrEndsolid);
            }

            char normalStr[7] = { 0 };
            HMM_Vec3 normal = HMM_V3(0, 0, 0);
            SNZ_ASSERT(fscanf(f, "%6s%f%f%f", normalStr, &normal.X, &normal.Y, &normal.Z) == 4, "fscanf failed.");
            SNZ_ASSERTF(strcmp(normalStr, "normal") == 0, "expected 'normal', found '%s'", normalStr);

            char outer[6] = { 0 };
            char loop[5] = { 0 };
            SNZ_ASSERT(fscanf(f, "%5s%4s", outer, loop) == 2, "fscanf failed.");
            SNZ_ASSERTF(strcmp(outer, "outer") == 0, "expected 'outer', found '%s'", outer);

            HMM_Vec3 verts[3] = { 0 };
            for (int i = 0; i < 3; i++) {
                char vertex[7] = { 0 };
                SNZ_ASSERT(fscanf(f, "%6s%f%f%f", vertex, &verts[i].X, &verts[i].Y, &verts[i].Z) == 4, "fscanf failed.");
                SNZ_ASSERTF(strcmp(vertex, "vertex") == 0, "expected 'vertex', found '%s'", vertex);
            }
            mesh_BSPTriListPushNew(arena, &tris, verts[0], verts[1], verts[2], NULL);

            char endloop[8] = { 0 };
            SNZ_ASSERT(fscanf(f, "%7s", endloop) == 1, "fscanf failed.");
            SNZ_ASSERTF(strcmp(endloop, "endloop") == 0, "expected 'endloop', found '%s'", endloop);

            char endfacet[9] = { 0 };
            SNZ_ASSERT(fscanf(f, "%8s", endfacet) == 1, "fscanf failed.");
            SNZ_ASSERTF(strcmp(endfacet, "endfacet") == 0, "expected 'endfacet', found '%s'", endfacet);
        }

        fclose(f);
    }

    HMM_Vec3 center = HMM_V3(0, 0, 0);
    int ptCount = 0;
    for (mesh_BSPTri* t = tris.first; t; t = t->next) {
        center = HMM_Add(center, t->tri.a);
        center = HMM_Add(center, t->tri.b);
        center = HMM_Add(center, t->tri.c);
        ptCount += 3;
    }
    center = HMM_DivV3F(center, -ptCount);
    mesh_BSPTriListTransform(&tris, HMM_Translate(center));

    *outMesh = (mesh_Mesh){
        .bspTris = tris,
        .firstFace = _mesh_groupBSPTriListToFaces(tris, pool, arena),
        .renderMesh = mesh_BSPTriListToRenderMesh(tris, scratch),
        // .bspTree = mesh_BSPTriListToBSP(&tris, arena),
    };
    mesh_BSPTriListToFaceTris(pool, outMesh);
    mesh_meshGenerateEdges(outMesh, arena, scratch);
    mesh_meshGenerateCorners(outMesh, arena, scratch);
    return true;
}

void mesh_tests() {
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
        mesh_Mesh cubeA = mesh_cube(&arena);
        mesh_Mesh cubeB = mesh_cube(&arena);
        mesh_BSPTriListTransform(&cubeB.bspTris, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_BSPTriListTransform(&cubeB.bspTris, HMM_Translate(HMM_V3(1, 1, 1)));

        mesh_BSPNode* treeA = mesh_BSPTriListToBSP(&cubeA.bspTris, &arena);
        mesh_BSPNode* treeB = mesh_BSPTriListToBSP(&cubeB.bspTris, &arena);

        mesh_BSPTriList* aClipped = mesh_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        mesh_BSPTriList* bClipped = mesh_BSPTriListClip(true, &cubeB.bspTris, treeA, &arena);
        mesh_BSPTriList* final = mesh_BSPTriListJoin(aClipped, bClipped);
        mesh_BSPTriListRecoverNonBroken(&final, &arena);
        mesh_BSPTriListToSTLFile(final, "testing/union.stl");
    }

    {
        mesh_Mesh cubeA = mesh_cube(&arena);
        mesh_Mesh cubeB = mesh_cube(&arena);
        mesh_BSPTriListTransform(&cubeB.bspTris, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        mesh_BSPTriListTransform(&cubeB.bspTris, HMM_Translate(HMM_V3(1, 1, 1)));

        mesh_BSPNode* treeA = mesh_BSPTriListToBSP(&cubeA.bspTris, &arena);
        mesh_BSPNode* treeB = mesh_BSPTriListToBSP(&cubeB.bspTris, &arena);

        mesh_BSPTriList* aClipped = mesh_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        mesh_BSPTriList* bClipped = mesh_BSPTriListClip(false, &cubeB.bspTris, treeA, &arena);
        mesh_BSPTriListInvert(bClipped);
        mesh_BSPTriList* final = mesh_BSPTriListJoin(aClipped, bClipped);
        mesh_BSPTriListRecoverNonBroken(&final, &arena);
        mesh_BSPTriListToSTLFile(final, "testing/intersection.stl");
    }

    {
        mesh_Mesh m = mesh_cube(&arena);
        mesh_BSPTriListToSTLFile(&m.bspTris, "testing/cube.stl");
    }

    snz_arenaDeinit(&arena);
    poolAllocDeinit(&pool);
}
