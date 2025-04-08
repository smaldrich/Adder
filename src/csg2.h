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
        };
    };

    // variables used during a clip:
    bool anyChildrenRemoved;
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
    csg_Node* innerList = NULL;
    csg_Node* outerList = NULL;
    {
        csg_Node* next = NULL;
        for (csg_Node* node = unsorted; node; node = next) {
            next = node->nextUnsorted;

            _csg_PlaneRelation rel = _csg_triClassify(*node->sourceTri, parent->normal, parent->origin);
            if (rel == CSG_PR_OUTSIDE) {
                node->nextUnsorted = outerList;
                outerList = node;
            } else if (rel == CSG_PR_WITHIN) {
                node->nextUnsorted = innerList;
                innerList = node;
            } else if (rel == CSG_PR_COPLANAR) {
                // if coplanar, add this doesn't do anything
                // FIXME: this case is only deduping adjacent coplanar tris, but there are more cases to catch to make trees smaller
                _csg_facesToNodesInner(arena, parent->nextUnsorted, unsorted);
            } else {
                // it spans both sides, we need one node for each side // FIXME: does this need to be split too?
                csg_Node* duplicate = SNZ_ARENA_PUSH(arena, csg_Node);
                duplicate->origin = node->origin;
                duplicate->normal = node->normal;

                node->nextUnsorted = innerList;
                innerList = node;
                duplicate->nextUnsorted = outerList;
                outerList = duplicate;
            }
        }
    }
    parent->innerTree = innerList;
    parent->outerTree = outerList;

    if (parent->innerTree != NULL) {
        _csg_nodeFix(arena, parent->innerTree, innerList->nextUnsorted);
    }
    if (parent->outerTree != NULL) {
        _csg_nodeFix(arena, parent->outerTree, outerList->nextUnsorted);
    }
}

csg_Node* csg_facesToNodes(mesh_FaceSlice faces, snz_Arena* arena) {
    csg_Node* firstNode;
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* f = &faces.elems[i];
        for (int64_t j = 0; j < f->tris.count; j++) {
            geo_Tri* t = &f->tris.elems[j];
            csg_Node* node = SNZ_ARENA_PUSH(arena, csg_Node);
            *node = (csg_Node){
                .nextUnsorted = firstNode,
                .sourceTri = t,
            };
            firstNode = node;
        }
    }
    _csg_facesToNodesInner(arena, firstNode, firstNode->nextUnsorted);
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

typedef struct _csg_TempFace _csg_TempFace;
struct _csg_TempFace {
    _csg_TempFace* next;
    mesh_Face face;
};

static void _csg_splitTri(geo_Tri tri, const csg_Node* cutter, geo_Tri** resultTris, ) {

}

// return indicates if any child got clipped
// pushes all sub-tris to arena if any were clipped, if nothing is clipped, defers push to caller
static bool _csg_clipTri(geo_Tri tri, bool clipWithin, const csg_Node* cutter, snz_Arena* arena) {
    geo_Tri splitties[3]; // all possible outputs fit here
    int splitCount = __;
    int countWithin = __;

    // splitties

    bool anyClipped = false;
    bool clipped[3] = { 0 };
    for (int i = 0; i < 3; i++) {
        bool within = i < countWithin;
        csg_Node* nextCutter = within ? cutter->innerTree : cutter->outerTree;
        if (!nextCutter) {
            if (within == clipWithin) {
                anyClipped = true;
                clipped[i] = true;
            }
        } else {
            anyClipped |= _csg_clipTri(splitties[i], clipWithin, nextCutter, arena);
        }
    }

    if (!anyClipped) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (clipped[i]) {
            continue;
        }
        *SNZ_ARENA_PUSH(arena, geo_Tri) = splitties[i];
    }
    return true;
}

mesh_FaceSlice csg_facesClip(mesh_FaceSlice faces, const csg_Node* tree, bool removeWithin, snz_Arena* arena, snz_Arena* scratch) {
    _csg_TempFace* firstInFace = NULL;
    for (int64_t i = 0; i < faces.count; i++) {
        const mesh_Face* ogFace = &faces.elems[i];
        _csg_TempFace* newFace = SNZ_ARENA_PUSH(scratch, _csg_TempFace);
        newFace->face = *ogFace;
        newFace->next = firstInFace;
        firstInFace = newFace;
    }

    _csg_TempFace* firstOutFace = NULL;
    for (_csg_TempFace* f = firstInFace; f;) {
        SNZ_ARENA_ARR_BEGIN(arena, geo_Tri);
        for (int64_t i = 0; i < f->face.tris.count; i++) {
            geo_Tri t = f->face.tris.elems[i];
            bool anyClipped = _csg_clipTri(t, removeWithin, tree, arena);
            if (!anyClipped) {
                *SNZ_ARENA_PUSH(arena, geo_Tri) = t;
            }
        }
        f->face.tris = SNZ_ARENA_ARR_END(arena, geo_Tri);

        _csg_TempFace* next = f->next;
        // don't push to out list if nothing made it past clipping
        if (f->face.tris.count > 0) {
            firstOutFace = f;
            f->next = firstOutFace;
        }
        f = next;
    }

    // recollect from a LL to slice
    SNZ_ARENA_ARR_BEGIN(arena, mesh_Face);
    for (_csg_TempFace* f = firstOutFace; f; f = f->next) {
        *SNZ_ARENA_PUSH(arena, mesh_Face) = f->face;
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Face);
}

/*
push new face arr
per face:
    per tri:
        split into temp space against the whole tree,
        take things that are good
        reformat, push to output face
    collect output face
ret that shit

a tri -> split on the whole tree until dead
anything left ? push to the output within the current face
*/
