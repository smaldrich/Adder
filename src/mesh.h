#pragma once

#include <inttypes.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "snooze.h"
#include "ui.h"
#include "geometry.h"

typedef enum {
    MESH_GK_DOES_NOT_EXIST,
    MESH_GK_CORNER,
    MESH_GK_EDGE,
    MESH_GK_FACE,
} mesh_GeoKind;

/*
Geo ids are the backend idea to make the parametric part of this thing work.
there is one embedded in every piece of geometry, describing it in terms of the operations that created it.
Other operations use ids to find the geometry they are supposed to be operating on at solve time

these things are intended to be stored and passed by value, and diffGeoptrs should not be shared.
*/

typedef struct mesh_GeoID mesh_GeoID;
struct mesh_GeoID {
    mesh_GeoKind geoKind;
    int64_t opUniqueId; // uid of the operation that created/last changed this piece of geo
    int64_t baseNodeId; // if from a sketch or base geo node, a uid matching what part

    // used for unions and other operations that need to reference another piece of geometry to refer to a resulting piece of geometry
    mesh_GeoID* diffGeo1;
    mesh_GeoID* diffGeo2;
};

typedef struct {
    mesh_GeoID id;
    geo_TriSlice tris;
} mesh_Face;

SNZ_SLICE(mesh_Face);

ren3d_Mesh mesh_facesToRenderMesh(const mesh_FaceSlice* faces, snz_Arena* scratch) {
    SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
    for (int64_t i = 0; i < faces->count; i++) {
        mesh_Face f = faces->elems[i];
        for (int64_t j = 0; j < f.tris.count; j++) {
            geo_Tri tri = f.tris.elems[j];
            HMM_Vec3 normal = geo_triNormal(tri);
            for (int i = 0; i < 3; i++) {
                *SNZ_ARENA_PUSH(scratch, ren3d_Vert) = (ren3d_Vert){
                    .pos = tri.elems[i],
                    .normal = normal,
                    .color = HMM_V4(1, 1, 1, 1),
                };
            } // vert loop
        } // tris loop
    } // face loop
    ren3d_VertSlice s = SNZ_ARENA_ARR_END(scratch, ren3d_Vert);
    return ren3d_meshInit(s.elems, s.count);
}

mesh_FaceSlice mesh_facesDuplicate(mesh_FaceSlice faces, snz_Arena* arena) {
    SNZ_ARENA_ARR_BEGIN(arena, geo_Tri);
    for (int64_t i = 0; i < faces.count; i++) {
        const mesh_Face* f = &faces.elems[i];
        for (int64_t j = 0; j < f->tris.count; j++) {
            *SNZ_ARENA_PUSH(arena, geo_Tri) = f->tris.elems[j];
            // FIXME: should be a memcpy but eh
        }
    }
    geo_TriSlice tris = SNZ_ARENA_ARR_END(arena, geo_Tri);

    SNZ_ARENA_ARR_BEGIN(arena, mesh_Face);
    int64_t triIdx = 0;
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face ogFace = faces.elems[i];
        *SNZ_ARENA_PUSH(arena, mesh_Face) = (mesh_Face){
            .id = ogFace.id,
            .tris = (geo_TriSlice) {
                .elems = &tris.elems[triIdx],
                .count = ogFace.tris.count,
            }
        };
        triIdx += ogFace.tris.count;
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Face);
}

bool mesh_faceFlat(const mesh_Face* f) {
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

void mesh_facesTranslate(mesh_FaceSlice faces, HMM_Vec3 offset) {
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* f = &faces.elems[i];
        for (int64_t j = 0; j < f->tris.count; j++) {
            geo_Tri* tri = &f->tris.elems[j];
            for (int k = 0; k < 3; k++) {
                tri->elems[k] = HMM_Add(tri->elems[k], offset);
            } // verts
        } // tris
    } // faces
}

void mesh_facesTransform(mesh_FaceSlice faces, HMM_Mat4 transform) {
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* f = &faces.elems[i];
        for (int64_t j = 0; j < f->tris.count; j++) {
            geo_Tri* tri = &f->tris.elems[j];
            for (int k = 0; k < 3; k++) {
                HMM_Vec4 tmp = HMM_V4(0, 0, 0, 0);
                tmp.XYZ = tri->elems[k];
                tri->elems[k] = HMM_Mul(transform, tmp).XYZ;
            } // verts
        } // tris
    } // faces
}

// 2 width cube, centered on the origin, face geoids not filled out
mesh_FaceSlice mesh_cube(snz_Arena* arena) {
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

    SNZ_ARENA_ARR_BEGIN(arena, geo_Tri);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[0], v[2], v[1]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[0], v[3], v[2]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[7], v[6], v[2]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[7], v[2], v[3]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[6], v[5], v[1]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[6], v[1], v[2]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[5], v[4], v[0]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[5], v[0], v[1]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[4], v[7], v[3]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[4], v[3], v[0]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[4], v[5], v[6]);
    *SNZ_ARENA_PUSH(arena, geo_Tri) = geo_triInit(v[4], v[6], v[7]);
    geo_TriSlice tris = SNZ_ARENA_ARR_END(arena, geo_Tri);

    SNZ_ARENA_ARR_BEGIN(arena, mesh_Face);
    for (int i = 0; i < 6; i++) {
        *SNZ_ARENA_PUSH(arena, mesh_Face) = (mesh_Face){
            .tris = (geo_TriSlice) {
                .elems = &tris.elems[i * 2],
                .count = 2,
            },
        };
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Face);
}

typedef struct mesh_Edge mesh_Edge;
struct mesh_Edge {
    mesh_Edge* next;
    mesh_GeoID id;
    HMM_Vec3Slice points;
};

SNZ_SLICE(mesh_Edge);

typedef struct mesh_Corner mesh_Corner;
struct mesh_Corner {
    mesh_Corner* next;
    mesh_GeoID id;
    HMM_Vec3 position;
};

typedef struct {
    mesh_Edge* firstEdge;
    mesh_Corner* firstCorner;
} mesh_TempGeo;

static bool _mesh_geoIdEqual(mesh_GeoID a, mesh_GeoID b) {
    bool equal = true;
    equal &= a.geoKind == b.geoKind;
    equal &= a.opUniqueId == b.opUniqueId;
    equal &= a.baseNodeId == b.baseNodeId;
    equal &= (a.diffGeo1 == NULL) == (b.diffGeo1 == NULL);
    equal &= (a.diffGeo2 == NULL) == (b.diffGeo2 == NULL);
    if (!equal) {
        return false;
    }

    if (a.diffGeo1) {
        equal &= _mesh_geoIdEqual(*a.diffGeo1, *b.diffGeo1);
    }
    if (a.diffGeo2) {
        equal &= _mesh_geoIdEqual(*a.diffGeo2, *b.diffGeo2);
    }
    return equal;
}

typedef struct {
    mesh_GeoKind kind;
    union {
        const mesh_Corner* corner;
        const mesh_Edge* edge;
        const mesh_Face* face;
    };
} mesh_GeoIDResult;

// searches faces and tempGeo for a piece of geometry with a matching geo id to the one given
// return will have a null ptr and kind of MESH_GK_DOES_NOT_EXIST if nothing is found
// FIXME: for a perf boost, use a hashtable for id -> geo in each mesh instead of looping
// FIXME: assert no more than one match
mesh_GeoIDResult mesh_geoIdFind(const mesh_FaceSlice* faces, const mesh_TempGeo* tempGeo, mesh_GeoID target) {
    mesh_GeoIDResult out = (mesh_GeoIDResult){
        .kind = target.geoKind,
    };

    if (out.kind == MESH_GK_FACE) {
        for (int64_t i = 0; i < faces->count; i++) {
            if (_mesh_geoIdEqual(target, faces->elems[i].id)) {
                out.face = &faces->elems[i];
                return out;
            }
        }
    } else if (out.kind == MESH_GK_EDGE) {
        for (mesh_Edge* e = tempGeo->firstEdge; e; e = e->next) {
            if (_mesh_geoIdEqual(target, e->id)) {
                out.edge = e;
                return out;
            }
        }
    } else if (out.kind == MESH_GK_CORNER) {
        for (mesh_Corner* c = tempGeo->firstCorner; c; c = c->next) {
            if (_mesh_geoIdEqual(target, c->id)) {
                out.corner = c;
                return out;
            }
        }
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", target.geoKind);
    }
    out.kind = MESH_GK_DOES_NOT_EXIST;
    return out;
}

mesh_GeoID* mesh_geoIdDuplicate(const mesh_GeoID* src, snz_Arena* arena) {
    mesh_GeoID* new = SNZ_ARENA_PUSH(arena, mesh_GeoID);
    *new = *src;
    if (new->diffGeo1) {
        new->diffGeo1 = mesh_geoIdDuplicate(new->diffGeo1, arena);
    }

    if (new->diffGeo2) {
        new->diffGeo2 = mesh_geoIdDuplicate(new->diffGeo2, arena);
    }
    return new;
}

typedef struct {
    geo_Line a;
    geo_Line b;
} _mesh_LinePair;

SNZ_SLICE(_mesh_LinePair);

static bool _mesh_linePairAdjacentFast(geo_Line a, geo_Line b) {
    if (geo_v3Equal(a.a, b.a)) {
        return true;
    } else if (geo_v3Equal(a.a, b.b)) {
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

    HMM_Vec3Slice forward = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    HMM_Vec3Slice reverse = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    HMM_Vec3Slice extra = _mesh_groupPointsAdjacent(lines, startPt, scratch);
    SNZ_ASSERT(!extra.count, "edge points can't be ordered, there are three segments adjacent.");

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
// opUid to make a correct geoId on the outputted edge
// FIXME: time complexity is horrifying
mesh_Edge mesh_facesToEdge(const mesh_Face* faceA, const mesh_Face* faceB, int64_t opUid, snz_Arena* arena, snz_Arena* scratch) {
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

    SNZ_ARENA_ARR_BEGIN(arena, geo_Line);
    for (int i = 0; i < pairs.count; i++) {
        _mesh_LinePair pair = pairs.elems[i];
        geo_Line s = { 0 };
        bool adj = _mesh_linePairAdjacent(pair.a, pair.b, &s);
        if (adj) {
            *SNZ_ARENA_PUSH(arena, geo_Line) = s;
            SNZ_ASSERTF(!geo_v3Equal(s.a, s.b), "Edge gen with 0 len. %f,%f,%f", s.a.X, s.a.Y, s.a.Z);
        }
    }
    geo_LineSlice clipped = SNZ_ARENA_ARR_END(arena, geo_Line);
    if (clipped.count <= 0) {
        return (mesh_Edge) { 0 };
    }

    mesh_Edge out = (mesh_Edge){
        .id = (mesh_GeoID) {
            .geoKind = MESH_GK_EDGE,
            .opUniqueId = opUid,
            .diffGeo1 = mesh_geoIdDuplicate(&faceA->id, arena),
            .diffGeo2 = mesh_geoIdDuplicate(&faceB->id, arena),
        },
        .points = _mesh_orderedPointsFromLineSet(clipped, scratch, arena),
    };
    return out;
}

mesh_EdgeSlice mesh_faceToAllAdjacentEdges(const mesh_TempGeo* tempGeo, const mesh_Face* face, snz_Arena* arena) {
    SNZ_ARENA_ARR_BEGIN(arena, mesh_Edge);
    for (const mesh_Edge* e = tempGeo->firstEdge; e; e = e->next) {
        const mesh_GeoID* idA = e->id.diffGeo1;
        const mesh_GeoID* idB = e->id.diffGeo2;
        if (_mesh_geoIdEqual(*idA, face->id) || _mesh_geoIdEqual(*idB, face->id)) {
            mesh_Edge* new = SNZ_ARENA_PUSH(arena, mesh_Edge);
            new->next = NULL;
        }
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Edge);
}

// return indicates success
bool mesh_edgesToCorner(HMM_Vec3Slice a, HMM_Vec3Slice b, HMM_Vec3* outPt) {
    SNZ_ASSERTF(a.count > 0, "edge with only %lld points.", a.count);
    SNZ_ASSERTF(b.count > 0, "edge with only %lld points.", b.count);

    HMM_Vec3 aStart = a.elems[0];
    HMM_Vec3 aEnd = a.elems[a.count - 1];
    HMM_Vec3 bStart = b.elems[0];
    HMM_Vec3 bEnd = b.elems[b.count - 1];

    if (geo_v3Equal(aStart, bStart)) {
        *outPt = aStart;
        return true;
    } else if (geo_v3Equal(aStart, bEnd)) {
        *outPt = aStart;
        return true;
    } else if (geo_v3Equal(aEnd, bEnd)) {
        *outPt = aEnd;
        return true;
    }
    return false;
}

typedef struct _mesh_TriSliceLLNode _mesh_TriSliceLLNode;
struct _mesh_TriSliceLLNode {
    _mesh_TriSliceLLNode* next;
    mesh_Face face;
};

// FIXME: OCTREES YEAHH!!!
// FIXME: fillet seam detection??
static mesh_FaceSlice _mesh_groupTrisToFaces(geo_TriSlice tris, PoolAlloc* pool, snz_Arena* arena, snz_Arena* scratch) {
    _mesh_TriSliceLLNode* firstTriSlice = NULL;

    geo_LineSlice segments = (geo_LineSlice){
        .count = 0,
        .elems = poolAllocAlloc(pool, 0),
    };
    HMM_Vec3* normals = poolAllocAlloc(pool, 0);
    int64_t normalCount = 0;

    bool* triTakenFlags = SNZ_ARENA_PUSH_ARR(scratch, tris.count, bool);

    // FIXME: the two while trues in here should really have cutoffs
    while (true) {
        { // find and seed a new face
            geo_Tri tri = { 0 };
            bool anyFound = false;
            for (int64_t i = 0; i < tris.count; i++) {
                if (triTakenFlags[i]) {
                    continue;
                }
                tri = tris.elems[i];
                anyFound = true;
                break;
            }
            if (!anyFound) {
                break; // No tris left, everything has a face and we can end.
            }

            segments.count = 0;
            normalCount = 0;
            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(pool, segments.elems, segments.count, geo_Line) = (geo_Line){
                    .a = tri.elems[i],
                    .b = tri.elems[(i + 1) % 3],
                };
                *poolAllocPushArray(pool, normals, normalCount, HMM_Vec3) = geo_triNormal(tri);
            }

            { // debug logging status because this algo takes forever
                int64_t sectioned = 0;
                for (int64_t i = 0; i < tris.count; i++) {
                    sectioned += triTakenFlags[i];
                }
                SNZ_LOGF("pushed a new face! %lld/%lld", sectioned, tris.count);
            }

            _mesh_TriSliceLLNode* node = SNZ_ARENA_PUSH(scratch, _mesh_TriSliceLLNode);
            *node = (_mesh_TriSliceLLNode){
                .face = { { 0 } }, // no idea why, but apparently gcc is only happy if there are two curlies here
                .next = firstTriSlice,
            };
            firstTriSlice = node;
        } // end seeding section

        bool anyPushedThisLoop = true;
        int64_t triCount = 0;
        int64_t triIdx = tris.count; // iterator var
        SNZ_ARENA_ARR_BEGIN(arena, geo_Tri); // tris for the face
        while (true) {
            if (triIdx == tris.count) {
                if (!anyPushedThisLoop) {
                    break;
                }
                triIdx = 0;
                anyPushedThisLoop = false;
            } else {
                triIdx++;
            }
            if (triTakenFlags[triIdx]) {
                continue;
            }

            geo_Tri tri = tris.elems[triIdx];
            HMM_Vec3 triNormal = geo_triNormal(tri);
            geo_Line triSegments[3] = { 0 };
            for (int i = 0; i < 3; i++) {
                triSegments[i] = (geo_Line){
                    .a = tri.elems[i],
                    .b = tri.elems[(i + 1) % 3],
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

            triTakenFlags[triIdx] = true;
            anyPushedThisLoop = true;
            triCount++;
            *SNZ_ARENA_PUSH(arena, geo_Tri) = tri;

            // FIXME: we can put one of these in place of the edge that we started with and not push it to the end
            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(pool, segments.elems, segments.count, geo_Line) = triSegments[i];
                *poolAllocPushArray(pool, normals, normalCount, HMM_Vec3) = triNormal;
            }

            if (triCount > 1000) {
                SNZ_LOG("quitting!");
                break;
            }
        } // end hunting for tris that could fit the face
        firstTriSlice->face = (mesh_Face){
            .tris = SNZ_ARENA_ARR_END(arena, geo_Tri),
        };
    } // end while(any tris left)

    // FIXME: this 'arenas being continguous only and no general allocs'
    // creates this kind of thing where it would be easier if there was just one more
    // arena, but there isn't so we make due by doing things retroactive style.
    // kinda awful if ima be honest, almost like the industry was right to use
    // general purpose allocations and the arena impl should just be a collection
    // of general allocs (like a PoolAlloc in this codebase)
    // But then again, the point of the adjacent only pools was to remove memory
    // fragmentation, and this does do that
    SNZ_ARENA_ARR_BEGIN(arena, mesh_Face);
    for (_mesh_TriSliceLLNode* node = firstTriSlice; node; node = node->next) {
        *SNZ_ARENA_PUSH(arena, mesh_Face) = node->face;
    }
    return SNZ_ARENA_ARR_END(arena, mesh_Face);
}

// FIXME: error handling without the asserts
mesh_FaceSlice mesh_stlFileToFaces(const char* path, snz_Arena* arena, snz_Arena* scratch, PoolAlloc* pool) {
    SNZ_LOGF("Loading mesh from %s.", path);

    SNZ_ARENA_ARR_BEGIN(arena, geo_Tri);
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

            geo_Tri* t = SNZ_ARENA_PUSH(arena, geo_Tri);
            for (int i = 0; i < 3; i++) {
                char vertex[7] = { 0 };
                SNZ_ASSERT(fscanf(f, "%6s%f%f%f", vertex, &t->elems[i].X, &t->elems[i].Y, &t->elems[i].Z) == 4, "fscanf failed.");
                SNZ_ASSERTF(strcmp(vertex, "vertex") == 0, "expected 'vertex', found '%s'", vertex);
            }

            char endloop[8] = { 0 };
            SNZ_ASSERT(fscanf(f, "%7s", endloop) == 1, "fscanf failed.");
            SNZ_ASSERTF(strcmp(endloop, "endloop") == 0, "expected 'endloop', found '%s'", endloop);

            char endfacet[9] = { 0 };
            SNZ_ASSERT(fscanf(f, "%8s", endfacet) == 1, "fscanf failed.");
            SNZ_ASSERTF(strcmp(endfacet, "endfacet") == 0, "expected 'endfacet', found '%s'", endfacet);
        }

        fclose(f);
    }
    geo_TriSlice tris = SNZ_ARENA_ARR_END(arena, geo_Tri);

    { // move mesh to center on the origin regardless of offsets
        HMM_Vec3 center = HMM_V3(0, 0, 0);
        int ptCount = 0;
        for (int64_t i = 0; i < tris.count; i++) {
            geo_Tri* tri = &tris.elems[i];
            center = HMM_Add(center, tri->a);
            center = HMM_Add(center, tri->b);
            center = HMM_Add(center, tri->c);
            ptCount += 3;
        }
        center = HMM_DivV3F(center, -ptCount);

        for (int64_t i = 0; i < tris.count; i++) {
            geo_Tri* tri = &tris.elems[i];
            for (int j = 0; j < 3; j++) {
                tri->elems[j] = HMM_Add(tri->elems[j], center);
            }
        }
    }
    return _mesh_groupTrisToFaces(tris, pool, arena, scratch);
}

void mesh_facesToSTLFile(mesh_FaceSlice faces, const char* path) {
    FILE* f = fopen(path, "w");
    fprintf(f, "solid object\n");

    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* face = &faces.elems[i];
        for (int64_t j = 0; j < face->tris.count; j++) {
            geo_Tri tri = face->tris.elems[j];
            HMM_Vec3 normal = geo_triNormal(tri);
            fprintf(f, "facet normal %f %f %f\n", normal.X, normal.Y, normal.Z);
            fprintf(f, "outer loop\n");
            fprintf(f, "vertex %f %f %f\n", tri.a.X, tri.a.Y, tri.a.Z);
            fprintf(f, "vertex %f %f %f\n", tri.b.X, tri.b.Y, tri.b.Z);
            fprintf(f, "vertex %f %f %f\n", tri.c.X, tri.c.Y, tri.c.Z);
            fprintf(f, "endloop\n");
            fprintf(f, "endfacet\n");
        }
    }

    fprintf(f, "endsolid object\n");
    fclose(f);
}
