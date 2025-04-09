#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "snooze.h"
#include "ui.h"
#include "mesh.h"
#include "csg2.h"

typedef struct {
    ui_SelectionState sel;
    mesh_GeoID id;
    union {
        HMM_Vec3 cornerPos;
        HMM_Vec3Slice edgePoints;
        geo_TriSlice faceTris;
    };
} tl_SceneGeo;

SNZ_SLICE(tl_SceneGeo);

typedef struct {
    geo_Align orbitOrigin;
    HMM_Vec2 orbitAngle;
    float orbitDist;
    ren3d_Mesh renderMesh;

    tl_SceneGeoSlice corners;
    tl_SceneGeoSlice edges;
    tl_SceneGeoSlice faces;
    tl_SceneGeoSlice allGeo; // overlaps with corners, edges, faces
} tl_Scene;

// copies faces and tempgeo elts to a new arena with space for ui data for them
// scene returned is valid for the length of arenas life
// FIXME: array content isn't getting copied rn so the above isn't actually true
tl_Scene tl_sceneInit(const mesh_FaceSlice* faces, mesh_TempGeo* tempGeo, snz_Arena* arena, snz_Arena* scratch) {
    // FIXME: initialize camera to always be outside mesh based on faces
    tl_Scene out = (tl_Scene){
        .orbitDist = 5,
        .orbitOrigin = geo_alignZero(),
        .renderMesh = mesh_facesToRenderMesh(faces, scratch),
    };

    out.faces = (tl_SceneGeoSlice){
        .count = faces->count,
        .elems = SNZ_ARENA_PUSH_ARR(arena, faces->count, tl_SceneGeo),
    };
    for (int64_t i = 0; i < faces->count; i++) {
        out.faces.elems[i] = (tl_SceneGeo){
            .id = faces->elems[i].id,
            .faceTris = faces->elems[i].tris,
        };
        SNZ_ASSERT(faces->elems[i].id.geoKind == MESH_GK_FACE, "Face has a geoid that isn't a face.");
    }

    SNZ_ARENA_ARR_BEGIN(arena, tl_SceneGeo);
    for (mesh_Edge* e = tempGeo->firstEdge; e; e = e->next) {
        *SNZ_ARENA_PUSH(arena, tl_SceneGeo) = (tl_SceneGeo){
            .id = e->id,
            .edgePoints = e->points,
        };
        SNZ_ASSERT(e->id.geoKind == MESH_GK_EDGE, "Edge has a geoid that isn't an edge.");
    }
    out.edges = SNZ_ARENA_ARR_END(arena, tl_SceneGeo);

    SNZ_ARENA_ARR_BEGIN(arena, tl_SceneGeo);
    for (mesh_Corner* c = tempGeo->firstCorner; c; c = c->next) {
        *SNZ_ARENA_PUSH(arena, tl_SceneGeo) = (tl_SceneGeo){
            .id = c->id,
            .cornerPos = c->position,
        };
        SNZ_ASSERT(c->id.geoKind == MESH_GK_CORNER, "Corner has a geoid that isn't a corner.");
    }
    out.corners = SNZ_ARENA_ARR_END(arena, tl_SceneGeo);

    // FIXME: this is a little hacky and a little dangerous, and a little scuffed
    // and very wierd, but it makes a lot of logic much simpler to be able to
    // iterate over all geo in one loop, so here it is
    out.allGeo = (tl_SceneGeoSlice){
        .count = out.faces.count + out.edges.count + out.corners.count,
        .elems = out.faces.elems,
    };

    return out;
}

typedef enum {
    TL_OPK_NONE,
    TL_OPK_SKETCH,
    TL_OPK_BASE_GEOMETRY,
    TL_OPK_EXTRUDE,

    TL_OPK_COUNT,
} tl_OpKind;

const char* tl_opKindNames[] = {
    [TL_OPK_SKETCH] = "sketch",
    [TL_OPK_BASE_GEOMETRY] = "geometry",
    [TL_OPK_EXTRUDE] = "extrude",
};

typedef enum {
    TL_OPAK_NONE = 0,
    TL_OPAK_NUMBER = (1 << 0),
    TL_OPAK_GEOID_CORNER = (1 << 1),
    TL_OPAK_GEOID_EDGE = (1 << 2),
    TL_OPAK_GEOID_FACE = (1 << 3),
} tl_OpArgKind;

bool tl_opArgKindExpectsDependency(tl_OpArgKind argKind) {
    if (argKind == TL_OPAK_NONE) {
        return false;
    } else if (argKind == TL_OPAK_NUMBER) {
        return false;
    } else if (argKind == TL_OPAK_GEOID_CORNER) {
        return true;
    } else if (argKind == TL_OPAK_GEOID_EDGE) {
        return true;
    } else if (argKind == TL_OPAK_GEOID_FACE) {
        return true;
    }
    SNZ_ASSERTF(false, "unreachable. kind: %d", argKind);
    return false;
}

typedef struct tl_Op tl_Op;
typedef struct {
    tl_OpArgKind kind;
    float number;
    mesh_GeoID geoId;
} tl_OpArg;

#define TL_OP_ARG_MAX_COUNT 5
// lookup from opKind -> opArgKinds
int tl_opArgKindsExpected[][TL_OP_ARG_MAX_COUNT] = {
    [TL_OPK_SKETCH] = { 0 },
    [TL_OPK_BASE_GEOMETRY] = { 0 },
    [TL_OPK_EXTRUDE] = {TL_OPAK_GEOID_FACE, TL_OPAK_NUMBER},
};

// lookup from opKind -> arg names
const char* tl_opArgNames[][TL_OP_ARG_MAX_COUNT] = {
    [TL_OPK_SKETCH] = { 0 },
    [TL_OPK_BASE_GEOMETRY] = { 0 },
    [TL_OPK_EXTRUDE] = { "face", "distance" },
};

struct tl_Op {
    tl_Op* next;
    int64_t uniqueId; // incrementing number to safely identify tl nodes in geo refs (and break them when tl_ops are deleted)
    bool markedForDeletion;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        sk_Sketch sketch;
        mesh_FaceSlice baseGeometry;
    } val;
    tl_OpArg args[TL_OP_ARG_MAX_COUNT];

    struct {
        const mesh_TempGeo* tempGeo;
        const mesh_FaceSlice* faces;
    } solve;
};

SNZ_SLICE_NAMED(tl_Op*, tl_OpPtrSlice);

typedef struct {
    tl_Op* firstOp;
    tl_Op* activeOp;
    int64_t nextUniqueId; // used to safely id nodes even if the address is reused
    HMM_Vec2 camPos;
    float camHeight;
    snz_Arena* operationArena;

    snz_Arena* generatedArena;
    PoolAlloc* generatedPool;

    tl_OpArg takenArgSignal; // set by scs to take geo, unset by handling code in argbar
} tl_Timeline;

tl_Timeline tl_timelineInit(snz_Arena* opArena, snz_Arena* generatedArena, PoolAlloc* generatedPool) {
    tl_Timeline out = {
        .operationArena = opArena,
        .generatedArena = generatedArena,
        .generatedPool = generatedPool,
        .camHeight = 1000,
        .camPos = HMM_V2(0, 0),
        .nextUniqueId = 1,
    };
    return out;
}

// pushes to list for the TL, sets a good uniqueID, and inits scene to a default
static tl_Op* _tl_timelinePushOp(tl_Timeline* tl) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .next = tl->firstOp,
        .uniqueId = tl->nextUniqueId,
    };
    tl->firstOp = out;
    tl->nextUniqueId++;
    return out;
}

// FIXME: make it a hash lookup
tl_Op* tl_timelineGetOpByUID(tl_Timeline* tl, int64_t uid) {
    if (uid == 0) { // temporary, for perf
        return NULL;
    }

    for (tl_Op* op = tl->firstOp; op; op = op->next) {
        if (op->uniqueId == uid) {
            return op;
        }
    }
    return NULL;
}

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_SKETCH;
    out->val.sketch = sketch;
    return out;
}

// generates geoIds for faces - which shouldn't be retained after this call
tl_Op* tl_timelinePushBaseGeometry(tl_Timeline* tl, HMM_Vec2 pos, mesh_FaceSlice faces) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_BASE_GEOMETRY;

    int64_t nextId = 1;
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* f = &faces.elems[i];
        f->id = (mesh_GeoID){
            .geoKind = MESH_GK_FACE,
            .baseNodeId = nextId,
            .opUniqueId = out->uniqueId,
        };
        nextId++;
    }
    out->val.baseGeometry = faces;
    return out;
}

tl_Op* tl_timelinePushExtrude(tl_Timeline* tl, HMM_Vec2 pos) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_EXTRUDE;
    return out;
}

void tl_timelineDeselectAll(tl_Timeline* tl) {
    for (tl_Op* op = tl->firstOp; op; op = op->next) {
        op->ui.sel.selected = false;
    }
}

static bool tl_timelineAnySelected(tl_Timeline* tl) {
    for (tl_Op* o = tl->firstOp; o; o = o->next) {
        if (o->ui.sel.selected) {
            return true;
        }
    }
    return false;
}

void tl_timelineCullOpsMarkedForDelete(tl_Timeline* t) {
    // cull dependencies to deleted ops
    for (tl_Op* op = t->firstOp; op; op = op->next) {
        if (t->activeOp == op && op->markedForDeletion) {
            t->activeOp = NULL;
        }

        for (int i = 0; i < TL_OP_ARG_MAX_COUNT; i++) {
            if (op->args[i].kind == TL_OPAK_NONE) {
                continue;
            }
            // if the op has no source (i.e. is a number, source id will be zero, which will never match)
            tl_Op* other = tl_timelineGetOpByUID(t, op->args[i].geoId.opUniqueId);
            if (other && other->markedForDeletion) {
                op->args[i] = (tl_OpArg){ 0 };
            }
        }
    }

    // cull elts marked for delete
    tl_Op* next = NULL;
    tl_Op** lastNextPtr = &t->firstOp;
    for (tl_Op* op = t->firstOp; op; op = next) {
        next = op->next;
        if (op->markedForDeletion) {
            *lastNextPtr = op->next;
            memset(op, 0, sizeof(*op));
            continue;
        }
        lastNextPtr = &op->next;
    }
    // FIXME: free list
}

// FIXME: bubbles & remove target op plz
tl_Scene tl_solveForNode(tl_Timeline* t, tl_Op* targetOp, snz_Arena* scratch) {
    SNZ_ASSERT(targetOp, "Solve for node requires a node.");

    SNZ_ARENA_ARR_BEGIN(scratch, tl_Op*);
    *SNZ_ARENA_PUSH(scratch, tl_Op*) = targetOp;
    while (true) { // FIXME: cutoff
        bool anyAdded = false;
        tl_OpPtrSlice added = (tl_OpPtrSlice){
            .count = scratch->arrModeElemCount,
            .elems = (tl_Op**)(scratch->end) - scratch->arrModeElemCount,
        };

        for (int opIdx = 0; opIdx < added.count; opIdx++) {
            tl_Op* op = added.elems[opIdx];
            for (int argIdx = 0; argIdx < TL_OP_ARG_MAX_COUNT; argIdx++) {
                tl_Op* new = tl_timelineGetOpByUID(t, op->args[argIdx].geoId.opUniqueId);
                if (!new) {
                    tl_OpArgKind kind = tl_opArgKindsExpected[op->kind][argIdx];
                    if (tl_opArgKindExpectsDependency(kind)) {
                        SNZ_ASSERT(false, "tried to solve with a missing dep.");
                    }
                    continue;
                }
                bool inAdded = false;
                for (int k = 0; k < added.count; k++) {
                    if (added.elems[k] == new) {
                        inAdded = true;
                    }
                }
                if (inAdded) {
                    continue;
                }
                *SNZ_ARENA_PUSH(scratch, tl_Op*) = new;
                anyAdded = true;
            }
        }

        if (!anyAdded) {
            break;
        }
    }
    tl_OpPtrSlice dependencies = SNZ_ARENA_ARR_END_NAMED(scratch, tl_Op*, tl_OpPtrSlice);

    { // reset per solve state
        snz_arenaClear(t->generatedArena);
        poolAllocClear(t->generatedPool);

        for (int64_t i = 0; i < dependencies.count; i++) {
            tl_Op* op = dependencies.elems[i];
            memset(&op->solve, 0, sizeof(op->solve));
        }
    }

    for (int64_t i = dependencies.count - 1; i >= 0; i--) {
        tl_Op* op = dependencies.elems[i];
        SNZ_ASSERT(!op->markedForDeletion, "tl op marked for delete made it to the solving stage");

        if (op->kind == TL_OPK_SKETCH) {
            sk_Sketch* sketch = &op->val.sketch;
            sk_sketchSolve(sketch);
            mesh_FaceSlice* faces = SNZ_ARENA_PUSH(t->generatedArena, mesh_FaceSlice);
            mesh_TempGeo* tempGeo = SNZ_ARENA_PUSH(t->generatedArena, mesh_TempGeo);
            skt_sketchTriangulate(sketch, faces, tempGeo, op->uniqueId, t->generatedArena, scratch);
            op->solve.faces = faces;
            op->solve.tempGeo = tempGeo;
        } else if (op->kind == TL_OPK_BASE_GEOMETRY) {
            op->solve.faces = &op->val.baseGeometry;
            op->solve.tempGeo = ahh;
        } else if (op->kind == TL_OPK_EXTRUDE) {
            tl_Op* targetDep = NULL;
            const mesh_Face* ogFace = NULL;
            float targetSize = 0;
            { // unpack and validate args/dependent geo/etc.
                SNZ_ASSERTF(op->args[0].kind == TL_OPAK_GEOID_FACE, "Extrude requires first arg to be a face. Actual kind: %d", op->args[0].kind);
                mesh_GeoID targetFaceId = op->args[0].geoId;
                targetDep = tl_timelineGetOpByUID(t, op->args[0].geoId.opUniqueId);

                SNZ_ASSERTF(op->args[1].kind == TL_OPAK_NUMBER, "Extrude requires second arg to be a number. Actual kind: %d", op->args[1].kind);
                targetSize = op->args[1].number;

                mesh_GeoIDResult geo = mesh_geoIdFind(targetDep->solve.faces, targetDep->solve.tempGeo, targetFaceId);
                SNZ_ASSERT(geo.kind == MESH_GK_FACE, "Extrude geoid find failed.");
                SNZ_ASSERT(mesh_faceFlat(geo.face), "Face to extrude wasn't flat.");
                ogFace = geo.face;
            }

            int64_t newFaceCount = 2 + edgeCount;
            mesh_FaceSlice newFaces = (mesh_FaceSlice){
                .count = newFaceCount,
                .elems = SNZ_ARENA_PUSH_ARR(t->generatedArena, newFaceCount, mesh_Face),
            };

            HMM_Vec3 translation = HMM_Mul(geo_triNormal(ogFace->tris.elems[0]), targetSize);

            mesh_FaceSlice* faces = SNZ_ARENA_PUSH(t->generatedArena, mesh_FaceSlice);
            *faces = csg_facesUnion(targetDep->solve.faces, &newFaces, t->generatedArena, scratch);
            op->solve.faces = faces;
            op->solve.tempGeo = ahh;
        } else {
            SNZ_ASSERTF(false, "unreachable. kind: %lld", op->kind);
        }
    } // end loop solving

    tl_Scene out = tl_sceneInit(targetOp->solve.faces, targetOp->solve.tempGeo, t->generatedArena, scratch);
    return out;
}
