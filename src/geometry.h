#pragma once

#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "snooze.h"
#include "ui.h"

typedef struct {
    union {
        struct {
            HMM_Vec3 a;
            HMM_Vec3 b;
            HMM_Vec3 c;
        };
        HMM_Vec3 elems[3];
    };
} geo_Tri;

SNZ_SLICE(geo_Tri);

typedef struct geo_MeshFace geo_MeshFace;
struct geo_MeshFace {
    geo_MeshFace* next;
    geo_TriSlice tris;
};

typedef struct geo_BSPTri geo_BSPTri;
struct geo_BSPTri {
    geo_BSPTri* next;
    geo_BSPTri* ancestor;
    geo_MeshFace* sourceFace;
    geo_Tri tri;
    bool anyChildDeleted;
    bool recovered;
};

typedef struct {
    geo_BSPTri* first;
    geo_BSPTri* last;
} geo_BSPTriList;

typedef struct geo_BSPNode geo_BSPNode;
struct geo_BSPNode {
    geo_BSPNode* outerTree;
    geo_BSPNode* innerTree;

    geo_BSPNode* nextAvailible;  // used for construction only, probs should remove for perf but who cares

    // FIXME: redundant? // Space inefficent??
    geo_Tri tri;
};

typedef struct {
    HMM_Vec3 a;
    HMM_Vec3 b;
} geo_MeshEdgeSegment;

SNZ_SLICE(geo_MeshEdgeSegment);

typedef struct geo_MeshEdge geo_MeshEdge;
struct geo_MeshEdge {
    geo_MeshEdge* next;
    geo_MeshEdgeSegmentSlice segments;
};

typedef struct {
    HMM_Vec3 pos;
} geo_MeshCorner;

SNZ_SLICE(geo_MeshCorner);

typedef struct {
    ren3d_Mesh renderMesh;
    geo_BSPTriList bspTris;
    geo_BSPNode* bspTree;
    geo_MeshFace* firstFace;

    geo_MeshEdge* firstEdge;
    geo_MeshCornerSlice corners;
} geo_Mesh;

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

bool geo_v3Equal(HMM_Vec3 a, HMM_Vec3 b) {
    return geo_floatEqual(a.X, b.X) && geo_floatEqual(a.Y, b.Y) && geo_floatEqual(a.Z, b.Z);
}

typedef struct {
    HMM_Vec3 startPt;
    HMM_Vec3 startNormal;
    HMM_Vec3 startVertical;

    HMM_Vec3 endPt;
    HMM_Vec3 endNormal;
    HMM_Vec3 endVertical;
} geo_Align;

static float _geo_angleBetweenV3(HMM_Vec3 a, HMM_Vec3 b) {
    return acosf(HMM_Dot(a, b) / (HMM_Len(a) * HMM_Len(b)));
}

HMM_Quat geo_alignToQuat(geo_Align a) {
    HMM_Vec3 normalCross = HMM_Cross(a.startNormal, a.endNormal);
    float normalAngle = _geo_angleBetweenV3(a.startNormal, a.endNormal);
    HMM_Quat planeRotate = HMM_QFromAxisAngle_RH(normalCross, normalAngle);
    if (geo_floatEqual(normalAngle, 0)) {
        planeRotate = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), 0);
    }

    HMM_Vec3 postRotateVertical = HMM_MulM4V4(HMM_QToM4(planeRotate), HMM_V4(a.startVertical.X, a.startVertical.Y, a.startVertical.Z, 1)).XYZ;
    // stolen: https://stackoverflow.com/questions/5188561/signed-angle-between-two-3d-vectors-with-same-origin-within-the-same-plane
    // tysm internet
    float y = HMM_Dot(HMM_Cross(postRotateVertical, a.endVertical), a.endNormal);
    float x = HMM_Dot(postRotateVertical, a.endVertical);
    float postRotateAngle = atan2(y, x);
    HMM_Quat postRotate = HMM_QFromAxisAngle_RH(a.endNormal, postRotateAngle);

    return HMM_MulQ(postRotate, planeRotate);
}

HMM_Mat4 geo_alignToM4(geo_Align a) {
    HMM_Quat q = geo_alignToQuat(a);
    HMM_Mat4 translate = HMM_Translate(HMM_Sub(a.endPt, a.startPt));
    return HMM_Mul(translate, HMM_QToM4(q));
}

// puts it in the range of -180 to +180, in rads
float geo_normalizeAngle(float a) {
    while (a > HMM_AngleDeg(180)) {
        a -= HMM_AngleDeg(360);
    }
    while (a < HMM_AngleDeg(-180)) {
        a += HMM_AngleDeg(360);
    }
    return a;
}

HMM_Vec3 geo_triNormal(geo_Tri t) {
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(t.b, t.a), HMM_SubV3(t.c, t.a));  // isn't this backwards?
    return HMM_NormV3(n);
}

geo_BSPTriList geo_BSPTriListInit() {
    return (geo_BSPTriList) { .first = NULL, .last = NULL };
}

// destructive to node->next
// FIXME: this is extremely unintuitive and has knack for causing circular lists
// huge problem, please find a better solution for the list situation
// same issue with other list functions also
void geo_BSPTriListPush(geo_BSPTriList* list, geo_BSPTri* node) {
    assert(node != NULL);
    node->next = list->first;
    if (list->first == NULL) {
        list->last = node;
    }
    list->first = node;
}

// destructive to node->next in listA, both ptrs can be null
geo_BSPTriList* geo_BSPTriListJoin(geo_BSPTriList* listA, geo_BSPTriList* listB) {
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
void geo_BSPTriListPushNew(snz_Arena* arena, geo_BSPTriList* list, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, geo_MeshFace* source) {
    geo_BSPTri* new = SNZ_ARENA_PUSH(arena, geo_BSPTri);
    *new = (geo_BSPTri){
        .tri = (geo_Tri){
            .a = a,
            .b = b,
            .c = c,
        },
        .sourceFace = source,
    };
    geo_BSPTriListPush(list, new);
}

void geo_BSPTriListTransform(geo_BSPTriList* list, HMM_Mat4 transform) {
    for (geo_BSPTri* node = list->first; node; node = node->next) {
        for (int i = 0; i < 3; i++) {
            HMM_Vec4 v4 = (HMM_Vec4){ .XYZ = node->tri.elems[i], .W = 1 };
            node->tri.elems[i] = HMM_MulM4V4(transform, v4).XYZ;
        }
    }
}

// flips the normals for every tri within list
void geo_BSPTriListInvert(geo_BSPTriList* list) {
    for (geo_BSPTri* node = list->first; node; node = node->next) {
        HMM_Vec3 temp = node->tri.a;
        node->tri.a = node->tri.c;
        node->tri.c = temp;
    }
}

void geo_BSPTriListToSTLFile(const geo_BSPTriList* list, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (const geo_BSPTri* tri = list->first; tri; tri = tri->next) {
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

// FIXME: this
// void geo_stlFileToBSPTriList(const char* path) {
//     FILE* f = fopen(path, "r");
//     char* solid = NULL;
//     char* object = NULL;
//     fscanf(f, "%s%s", solid, object);

//     while (true) {
//         char* facet = NULL;
//         char* normalStr = NULL;
//         HMM_Vec3 normal = HMM_V3(0, 0, 0);
//         fscanf(f, "%s%s%f%f%f", &facet, &normalStr, &normal.X, &normal.Y, &normal.Z);

//         char* outer = NULL;
//         char* loop = NULL;
//         fscanf(f, "%s%s", &outer, &loop);

//         for (int i = 0; i < 3; i++) {
//             HMM_Vec3 data = HMM_V3(0, 0, 0);
//             char* vertex = NULL;
//             fscanf(f, "%s%f%f%f", &vertex, &data.X, &data.Y, &data.Z);
//         }

//         char* endloop = NULL;
//         fscanf(f, "%s", endloop); // endloop

//         char* endfacet = NULL;
//         fscanf(f, "%s", endfacet); // endfacet
//     }

//     char* endsolid = NULL;
//     char* objectEnd = NULL;
//     fscanf(f, "%s%s", &endsolid, &objectEnd);

//     fclose(f);
// }

geo_PlaneRelation _geo_triClassify(geo_Tri tri, HMM_Vec3 planeNormal, HMM_Vec3 planeStart) {
    geo_PlaneRelation finalRel = 0;
    for (int i = 0; i < 3; i++) {
        float dot = HMM_Dot(HMM_SubV3(tri.elems[i], planeStart), planeNormal);
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
    HMM_Vec3 splitNormal = geo_triNormal(parent->tri);

    geo_BSPNode* innerList = NULL;
    geo_BSPNode* outerList = NULL;
    {
        geo_BSPNode* next = NULL;
        for (geo_BSPNode* node = listOfPossibleNodes; node; node = next) {
            // memo this beforehand ev loop because appending to the inner/outer list overwrites the nextptr
            next = node->nextAvailible;

            geo_PlaneRelation rel = _geo_triClassify(node->tri, splitNormal, parent->tri.a);
            if (rel == geo_PR_OUTSIDE) {
                node->nextAvailible = outerList;
                outerList = node;
            } else if (rel == geo_PR_WITHIN) {
                node->nextAvailible = innerList;
                innerList = node;
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                geo_BSPNode* duplicate = SNZ_ARENA_PUSH(arena, geo_BSPNode);
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
        _geo_BSPTreeFixNode(arena, parent->innerTree, innerList->nextAvailible);
    }

    if (parent->outerTree != NULL) {
        _geo_BSPTreeFixNode(arena, parent->outerTree, outerList->nextAvailible);
    }
}

// returns the top of a bsp tree for the list of tris, allocated entirely inside arena
// Normals calculated with out being CW, where all normals should be pointing out
geo_BSPNode* geo_BSPTriListToBSP(const geo_BSPTriList* tris, snz_Arena* arena) {
    geo_BSPNode* tree = NULL;

    for (const geo_BSPTri* tri = tris->first; tri; tri = tri->next) {
        geo_BSPNode* node = SNZ_ARENA_PUSH(arena, geo_BSPNode);
        node->nextAvailible = tree;
        tree = node;
        node->tri = tri->tri;
    }

    _geo_BSPTreeFixNode(arena, tree, tree->nextAvailible);
    return tree;
}

bool geo_BSPContainsPoint(geo_BSPNode* tree, HMM_Vec3 point) {
    geo_BSPNode* node = tree;
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
static void _geo_BSPTriSplit(geo_BSPTri* tri, snz_Arena* arena, geo_BSPTriList* outOutsideList, geo_BSPTriList* outInsideList, geo_BSPNode* cutter) {
    HMM_Vec3 cutNormal = geo_triNormal(cutter->tri);

    geo_PlaneRelation rel = _geo_triClassify(tri->tri, cutNormal, cutter->tri.a);

    if (rel == geo_PR_COPLANAR) {
        assert(false);  // FIXME: this
        // FIXME: how do coplanar things factor into this?
        // FIXME: full coplanar testing
    } else if (rel == geo_PR_OUTSIDE) {
        geo_BSPTriListPush(outOutsideList, tri);
    } else if (rel == geo_PR_WITHIN) {
        geo_BSPTriListPush(outInsideList, tri);
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
            bool intersectExists = geo_planeLineIntersection(cutter->tri.a, cutNormal, pt, direction, &t);
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
            geo_BSPTri* t1 = SNZ_ARENA_PUSH(arena, geo_BSPTri);

            *t1 = (geo_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,  // FIXME: sometimes, if a face gets split to become non-contiguous, this is just wrong and we need another face
            };

            geo_BSPTri* t2 = SNZ_ARENA_PUSH(arena, geo_BSPTri);
            *t2 = (geo_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[2],
                    .b = rotatedVerts[3],
                    .c = rotatedVerts[4],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            geo_BSPTri* t3 = SNZ_ARENA_PUSH(arena, geo_BSPTri);
            *t3 = (geo_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[4],
                    .b = rotatedVerts[0],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            geo_BSPTriList* t1List = t1Outside ? outOutsideList : outInsideList;
            geo_BSPTriList* t2and3List = t1Outside ? outInsideList : outOutsideList;

            geo_BSPTriListPush(t1List, t1);
            geo_BSPTriListPush(t2and3List, t2);
            geo_BSPTriListPush(t2and3List, t3);
        }  // end 5 vert-check
        else if (vertCount == 4) {
            geo_BSPTri* t1 = SNZ_ARENA_PUSH(arena, geo_BSPTri);
            *t1 = (geo_BSPTri){
                .tri = (geo_Tri){
                    .a = rotatedVerts[0],
                    .b = rotatedVerts[1],
                    .c = rotatedVerts[2],
                },
                .ancestor = tri,
                .sourceFace = tri->sourceFace,
            };

            geo_BSPTri* t2 = SNZ_ARENA_PUSH(arena, geo_BSPTri);
            *t2 = (geo_BSPTri){
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
            geo_BSPTriListPush(t1Outside ? outOutsideList : outInsideList, t1);
            geo_BSPTriListPush(t1Outside ? outInsideList : outOutsideList, t2);
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
geo_BSPTriList* geo_BSPTriListClip(bool within, geo_BSPTriList* meshTris, geo_BSPNode* tree, snz_Arena* arena) {
    geo_BSPTriList inside = geo_BSPTriListInit();
    geo_BSPTriList outside = geo_BSPTriListInit();

    geo_BSPTri* next = NULL;  // memod because placing in the other list would overwrite
    for (geo_BSPTri* tri = meshTris->first; tri; tri = next) {
        next = tri->next;
        _geo_BSPTriSplit(tri, arena, &outside, &inside, tree);
    }

    geo_BSPTriList* listToClear = NULL;
    if (tree->innerTree != NULL) {
        inside = *geo_BSPTriListClip(within, &inside, tree->innerTree, arena);
    } else {
        if (within) {
            listToClear = &inside;
        }
    }

    if (tree->outerTree != NULL) {
        outside = *geo_BSPTriListClip(within, &outside, tree->outerTree, arena);
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
        for (geo_BSPTri* node = listToClear->first; node; node = node->next) {
            for (geo_BSPTri* n = node; n; n = n->ancestor) {
                n->anyChildDeleted = true;
            }
        }
        listToClear->first = NULL;
        listToClear->last = NULL;
    }

    geo_BSPTriList* out = SNZ_ARENA_PUSH(arena, geo_BSPTriList);
    *out = *geo_BSPTriListJoin(&inside, &outside);
    return out;
}

// destructive to the original tri list
void geo_BSPTriListRecoverNonBroken(geo_BSPTriList** tris, snz_Arena* arena) {
    geo_BSPTriList* recovered = SNZ_ARENA_PUSH(arena, geo_BSPTriList);
    geo_BSPTriList* trisRemaining = SNZ_ARENA_PUSH(arena, geo_BSPTriList);

    geo_BSPTri* next = NULL;
    for (geo_BSPTri* node = (*tris)->first; node; node = next) {
        next = node->next;

        geo_BSPTri* oldest = node->ancestor;
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
                geo_BSPTriListPush(recovered, oldest);
                oldest->recovered = true;
            }
        } else {
            geo_BSPTriListPush(trisRemaining, node);
        }
    }

    *tris = geo_BSPTriListJoin(trisRemaining, recovered);
}

ren3d_Mesh geo_BSPTriListToRenderMesh(geo_BSPTriList list, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
    for (geo_BSPTri* tri = list.first; tri; tri = tri->next) {
        HMM_Vec3 triNormal = geo_triNormal(tri->tri);
        for (int i = 0; i < 3; i++) {
            *SNZ_ARENA_PUSH(scratch, ren3d_Vert) = (ren3d_Vert){
                .pos = tri->tri.elems[i],
                .normal = triNormal,
            };
        }
    }
    ren3d_VertSlice s = SNZ_ARENA_ARR_END(scratch, ren3d_Vert);
    return ren3d_meshInit(s.elems, s.count);
}

// uses a meshes BSPTriList to regenerate the face tri arrays
// assumes valid mesh->bspTris and and old but non-null mesh->faces data.
void geo_BSPTriListToFaceTris(PoolAlloc* pool, geo_Mesh* mesh) {
    for (geo_MeshFace* f = mesh->firstFace; f; f = f->next) {
        f->tris = (geo_TriSlice){ 0 };
    }
    for (geo_BSPTri* tri = mesh->bspTris.first; tri; tri = tri->next) {
        geo_Tri* t = poolAllocPushArray(pool, tri->sourceFace->tris.elems, tri->sourceFace->tris.count, geo_Tri);
        *t = tri->tri;
    }
}

// 2 width cube, centered on the origin
// FIXME: ew
// out mesh has valid bsp tris a face list that the tris are linked to (which does not have its copy of the triangle data)
geo_Mesh geo_cube(snz_Arena* arena) {
    geo_Mesh out = (geo_Mesh){ 0 };
    out.bspTris = geo_BSPTriListInit();

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
    geo_MeshFace* face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[0], v[2], v[1], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[0], v[3], v[2], face);

    face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[7], v[6], v[2], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[7], v[2], v[3], face);

    face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[6], v[5], v[1], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[6], v[1], v[2], face);

    face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[5], v[4], v[0], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[5], v[0], v[1], face);

    face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[4], v[7], v[3], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[4], v[3], v[0], face);

    face = SNZ_ARENA_PUSH(arena, geo_MeshFace);
    face->next = out.firstFace;
    out.firstFace = face;
    geo_BSPTriListPushNew(arena, &out.bspTris, v[4], v[5], v[6], face);
    geo_BSPTriListPushNew(arena, &out.bspTris, v[4], v[6], v[7], face);

    return out;
}

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
bool geo_intersectRayAndTri(HMM_Vec3 rayOrigin, HMM_Vec3 rayDir, geo_Tri tri, HMM_Vec3* outPos) {
    *outPos = HMM_V3(0, 0, 0);

    HMM_Vec3 edge1 = HMM_Sub(tri.b, tri.a);
    HMM_Vec3 edge2 = HMM_Sub(tri.c, tri.a);
    HMM_Vec3 ray_cross_e2 = HMM_Cross(rayDir, edge2);
    float det = HMM_Dot(edge1, ray_cross_e2);

    if (geo_floatZero(det)) {
        return false;
    }

    float inv_det = 1.0 / det;
    HMM_Vec3 s = HMM_Sub(rayOrigin, tri.a);
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

ren3d_Mesh _geo_hoverMesh;

void geo_buildHoverAndSelectionMesh(geo_Mesh* mesh, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseRayDir, snz_Arena* scratch) {
    geo_MeshFace* closestHovered = NULL;
    {  // find and draw the hovered face
        float closestHoveredDist = INFINITY;

        for (geo_MeshFace* face = mesh->firstFace; face; face = face->next) {
            HMM_Vec3 intersection = HMM_V3(0, 0, 0);
            // NOTE: any model transform will have to change this to adapt
            for (int64_t i = 0; i < face->tris.count; i++) {
                bool hovered = geo_intersectRayAndTri(cameraPos, mouseRayDir, face->tris.elems[i], &intersection);
                if (hovered) {
                    float dist = HMM_LenSqr(HMM_Sub(intersection, cameraPos));
                    if (dist < closestHoveredDist) {
                        closestHoveredDist = dist;
                        closestHovered = face;
                    }
                }  // end hover check
            }  // end face tri loop
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

        int64_t vertCount = closestHovered->tris.count * 3;
        ren3d_Vert* verts = SNZ_ARENA_PUSH_ARR(scratch, vertCount, ren3d_Vert);
        for (int64_t i = 0; i < closestHovered->tris.count; i++) {
            geo_Tri t = closestHovered->tris.elems[i];
            HMM_Vec3 normal = geo_triNormal(t);
            float scaleFactor = HMM_SqrtF(closestHoveredDist);
            HMM_Vec3 offset = HMM_Mul(normal, scaleFactor * -0.01f * *selectionAnim);
            for (int j = 0; j < 3; j++) {
                verts[i * 3 + j] = (ren3d_Vert){
                    .pos = HMM_Add(t.elems[j], offset),
                    .normal = normal,
                };
            };
        }

        _geo_hoverMesh = ren3d_meshInit(verts, vertCount);
        HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
        HMM_Vec4 color = ui_colorText;  // FIXME: not text colored
        color.A = 0.1;                  // FIXME: ui val for this alpha
        glDisable(GL_DEPTH_TEST);
        ren3d_drawMesh(&_geo_hoverMesh, vp, model, color, HMM_V3(-1, -1, -1), 1);
        glEnable(GL_DEPTH_TEST);
    }  // end hovered drawing
}

void geo_meshDrawEdges(const geo_Mesh* mesh, HMM_Mat4 vp) {
    for (geo_MeshEdge* edge = mesh->firstEdge; edge; edge = edge->next) {
        for (int i = 0; i < edge->segments.count; i++) {
            geo_MeshEdgeSegment seg = edge->segments.elems[i];
            HMM_Vec4 pts[2] = {
                HMM_V4(seg.a.X, seg.a.Y, seg.a.Z, 1),
                HMM_V4(seg.b.X, seg.b.Y, seg.b.Z, 1),
            };
            snzr_drawLine(pts, 2, ui_colorText, 2, vp);
        }
    }
}

void geo_meshDrawCorners(const geo_Mesh* mesh, HMM_Mat4 vp) {
    for (int i = 0; i < mesh->corners.count; i++) {
        ren3d_drawBillboard(
            vp,
            *ui_sketchPointTexture,
            mesh->corners.elems[i].pos,
            HMM_V2(25, 25));
    }
}

typedef struct {
    geo_MeshEdgeSegment a;
    geo_MeshEdgeSegment b;
} _geo_MeshEdgeSegmentPair;

SNZ_SLICE(_geo_MeshEdgeSegmentPair);

typedef struct {
    geo_MeshFace a;
    geo_MeshFace b;
} _geo_MeshFacePair;

SNZ_SLICE(_geo_MeshFacePair);

// clips A to B
static bool _geo_segmentPairAdjacent(geo_MeshEdgeSegment a, geo_MeshEdgeSegment b, geo_MeshEdgeSegment* outClipped) {
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

    float overlapA = 0;
    float overlapB = 0;
    if (aMin < bMin) {
        overlapA = bMin;
        overlapB = aMax;
    } else {
        overlapA = aMin;
        overlapB = bMax;
    }

    *outClipped = (geo_MeshEdgeSegment){
        .a = HMM_Add(a.a, HMM_Mul(aDir, overlapA)),
        .b = HMM_Add(a.a, HMM_Mul(aDir, overlapB)),
    };
    return true;
}

// expects valid face tris on the mesh
// no issue if out and scratch are the same arena
void geo_meshGenerateEdges(geo_Mesh* mesh, snz_Arena* out, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, _geo_MeshFacePair); // FIXME: wasting a lot of space here storing next ptrs (GOD I WISH SUBSTRUCTS EXISTED AHHH)
    for (geo_MeshFace* faceA = mesh->firstFace; faceA; faceA = faceA->next) {
        for (geo_MeshFace* faceB = faceA->next; faceB; faceB = faceB->next) {
            *SNZ_ARENA_PUSH(scratch, _geo_MeshFacePair) = (_geo_MeshFacePair){
                .a = *faceA,
                .b = *faceB,
            };
            // FIXME: bounding box checks to cut pair count down
        }
    }
    _geo_MeshFacePairSlice facePairs = SNZ_ARENA_ARR_END(scratch, _geo_MeshFacePair);

    for (int pairIdx = 0; pairIdx < facePairs.count; pairIdx++) {
        _geo_MeshFacePair p = facePairs.elems[pairIdx];
        geo_MeshFace faceA = p.a;
        geo_MeshFace faceB = p.b;

        SNZ_ARENA_ARR_BEGIN(scratch, _geo_MeshEdgeSegmentPair);
        for (int aIdx = 0; aIdx < faceA.tris.count; aIdx++) {
            for (int bIdx = 0; bIdx < faceB.tris.count; bIdx++) {
                geo_Tri aTri = faceA.tris.elems[aIdx];
                geo_Tri bTri = faceB.tris.elems[bIdx];

                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        _geo_MeshEdgeSegmentPair pair = (_geo_MeshEdgeSegmentPair){
                            .a = (geo_MeshEdgeSegment) {
                                .a = aTri.elems[i],
                                .b = aTri.elems[(i + 1) % 3],
                            },
                            .b = (geo_MeshEdgeSegment) {
                                .a = bTri.elems[j],
                                .b = bTri.elems[(j + 1) % 3],
                            },
                        };
                        *SNZ_ARENA_PUSH(scratch, _geo_MeshEdgeSegmentPair) = pair;

                        // const char* path = snz_arenaFormatStr(out, "testing/facePair%d_%d.stl", pairIdx, scratch->arrModeElemCount - 1);
                        // geo_BSPTriList l = { 0 };
                        // geo_BSPTriListPushNew(out, &l, pair.a.a, pair.a.b, HMM_V3(0, 0, 0), NULL);
                        // geo_BSPTriListPushNew(out, &l, pair.b.a, pair.b.b, HMM_V3(0, 0, 0), NULL);

                        // geo_BSPTriListPushNew(out, &l, aTri.a, aTri.b, aTri.c, NULL);
                        // geo_BSPTriListPushNew(out, &l, bTri.a, bTri.b, bTri.c, NULL);
                        // geo_BSPTriListToSTLFile(&l, path);
                    } // end 2nd tri edge loop
                } // end 1st tri edge loop
            }
        }
        _geo_MeshEdgeSegmentPairSlice pairs = SNZ_ARENA_ARR_END(scratch, _geo_MeshEdgeSegmentPair);

        SNZ_ARENA_ARR_BEGIN(out, geo_MeshEdgeSegment);
        for (int i = 0; i < pairs.count; i++) {
            _geo_MeshEdgeSegmentPair pair = pairs.elems[i];
            geo_MeshEdgeSegment s = { 0 };
            bool adj = _geo_segmentPairAdjacent(pair.a, pair.b, &s);
            if (adj) {
                *SNZ_ARENA_PUSH(out, geo_MeshEdgeSegment) = s;
                // printf("pair %d popped.\n", i);
            }
        }
        geo_MeshEdgeSegmentSlice clipped = SNZ_ARENA_ARR_END(out, geo_MeshEdgeSegment);

        if (clipped.count > 0) {
            geo_MeshEdge* edge = SNZ_ARENA_PUSH(out, geo_MeshEdge);
            *edge = (geo_MeshEdge){
                .segments = clipped,
                .next = mesh->firstEdge,
            };
            mesh->firstEdge = edge;
        }
    } // end face pair loop
}

static bool _geo_meshEdgeSegmentsAdjacent(geo_MeshEdgeSegment a, geo_MeshEdgeSegment b, HMM_Vec3* outPt) {
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

void geo_generateCorners(geo_Mesh* mesh, snz_Arena* out, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, _geo_MeshEdgeSegmentPair);
    // FIXME: i am so sorry (add a bounding box optimization)
    for (geo_MeshEdge* edge = mesh->firstEdge; edge; edge = edge->next) {
        for (geo_MeshEdge* other = edge->next; other; other = other->next) {
            for (int i = 0; i < edge->segments.count; i++) {
                for (int j = 0; j < other->segments.count; j++) {
                    *SNZ_ARENA_PUSH(scratch, _geo_MeshEdgeSegmentPair) = (_geo_MeshEdgeSegmentPair){
                        .a = edge->segments.elems[i],
                        .b = other->segments.elems[i],
                    };
                }
            }
        }
    }
    _geo_MeshEdgeSegmentPairSlice pairs = SNZ_ARENA_ARR_END(scratch, _geo_MeshEdgeSegmentPair);

    SNZ_ARENA_ARR_BEGIN(out, geo_MeshCorner);
    for (int i = 0; i < pairs.count; i++) {
        _geo_MeshEdgeSegmentPair p = pairs.elems[i];
        HMM_Vec3 pt = { 0 };
        if (_geo_meshEdgeSegmentsAdjacent(p.a, p.b, &pt)) {
            *SNZ_ARENA_PUSH(out, geo_MeshCorner) = (geo_MeshCorner){
                .pos = pt,
            };
        }
    }
    mesh->corners = SNZ_ARENA_ARR_END(out, geo_MeshCorner);
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

        geo_BSPTriList list = geo_BSPTriListInit();
        geo_BSPTriListPushNew(&arena, &list, verts[0], verts[1], verts[2], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[0], verts[2], verts[3], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[0], verts[3], verts[1], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[3], verts[2], verts[1], NULL);

        geo_BSPNode* tree = geo_BSPTriListToBSP(&list, &arena);
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

        geo_BSPTriList list = geo_BSPTriListInit();
        // top faces
        geo_BSPTriListPushNew(&arena, &list, verts[1], verts[2], verts[3], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[1], verts[4], verts[2], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[1], verts[3], verts[0], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[1], verts[0], verts[4], NULL);

        // bottom faces
        geo_BSPTriListPushNew(&arena, &list, verts[0], verts[3], verts[2], NULL);
        geo_BSPTriListPushNew(&arena, &list, verts[0], verts[2], verts[4], NULL);
        geo_BSPTriListToSTLFile(&list, "testing/object.stl");

        geo_BSPNode* tree = geo_BSPTriListToBSP(&list, &arena);

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
        geo_Mesh cubeA = geo_cube(&arena);
        geo_Mesh cubeB = geo_cube(&arena);
        geo_BSPTriListTransform(&cubeB.bspTris, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        geo_BSPTriListTransform(&cubeB.bspTris, HMM_Translate(HMM_V3(1, 1, 1)));

        geo_BSPNode* treeA = geo_BSPTriListToBSP(&cubeA.bspTris, &arena);
        geo_BSPNode* treeB = geo_BSPTriListToBSP(&cubeB.bspTris, &arena);

        geo_BSPTriList* aClipped = geo_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        geo_BSPTriList* bClipped = geo_BSPTriListClip(true, &cubeB.bspTris, treeA, &arena);
        geo_BSPTriList* final = geo_BSPTriListJoin(aClipped, bClipped);
        geo_BSPTriListRecoverNonBroken(&final, &arena);
        geo_BSPTriListToSTLFile(final, "testing/union.stl");
    }

    {
        geo_Mesh cubeA = geo_cube(&arena);
        geo_Mesh cubeB = geo_cube(&arena);
        geo_BSPTriListTransform(&cubeB.bspTris, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        geo_BSPTriListTransform(&cubeB.bspTris, HMM_Translate(HMM_V3(1, 1, 1)));

        geo_BSPNode* treeA = geo_BSPTriListToBSP(&cubeA.bspTris, &arena);
        geo_BSPNode* treeB = geo_BSPTriListToBSP(&cubeB.bspTris, &arena);

        geo_BSPTriList* aClipped = geo_BSPTriListClip(true, &cubeA.bspTris, treeB, &arena);
        geo_BSPTriList* bClipped = geo_BSPTriListClip(false, &cubeB.bspTris, treeA, &arena);
        geo_BSPTriListInvert(bClipped);
        geo_BSPTriList* final = geo_BSPTriListJoin(aClipped, bClipped);
        geo_BSPTriListRecoverNonBroken(&final, &arena);
        geo_BSPTriListToSTLFile(final, "testing/intersection.stl");
    }

    snz_arenaDeinit(&arena);
    poolAllocDeinit(&pool);
}
