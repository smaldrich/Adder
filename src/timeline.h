#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "snooze.h"
#include "ui.h"

typedef struct {
    geo_Align orbitOrigin;
    HMM_Vec2 orbitAngle;
    float orbitDist;

    mesh_Mesh* mesh;
} tl_Scene;

tl_Scene tl_sceneInit() {
    // FIXME: initialize this to not be inside of geometry
    tl_Scene out = (tl_Scene){
        .orbitDist = 5,
        .orbitOrigin = geo_alignZero(),
        .mesh = NULL,
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
        mesh_Mesh baseGeometry;
    } val;
    tl_OpArg args[TL_OP_ARG_MAX_COUNT];
    tl_Scene scene; // updated per solve - camera data persists tho
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
        .scene = tl_sceneInit(),
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

// also generates geoIds for everything in the mesh - mesh shouldn't be retained after this call
tl_Op* tl_timelinePushBaseGeometry(tl_Timeline* tl, HMM_Vec2 pos, mesh_Mesh mesh) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_BASE_GEOMETRY;
    out->val.baseGeometry = mesh;
    out->scene.mesh = &out->val.baseGeometry;

    int64_t nextId = 0;
    for (int64_t i = 0; i < mesh.corners.count; i++) {
        mesh.corners.elems[i].id = (mesh_GeoID){
            .geoKind = MESH_GK_CORNER,
            .baseNodeId = nextId,
            .opUniqueId = out->uniqueId,
        };
        nextId++;
    }

    for (mesh_Edge* e = mesh.firstEdge; e; e = e->next) {
        e->id = (mesh_GeoID){
            .geoKind = MESH_GK_EDGE,
            .baseNodeId = nextId,
            .opUniqueId = out->uniqueId,
        };
        nextId++;
    }

    for (mesh_Face* f = mesh.firstFace; f; f = f->next) {
        f->id = (mesh_GeoID){
            .geoKind = MESH_GK_FACE,
            .baseNodeId = nextId,
            .opUniqueId = out->uniqueId,
        };
        nextId++;
    }
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
            if (tl_timelineGetOpByUID(t, op->args[i].geoId.opUniqueId)) {
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

void tl_solveForNode(tl_Timeline* t, tl_Op* targetOp, snz_Arena* scratch) {
    if (!targetOp) {
        return;
    }

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

    snz_arenaClear(t->generatedArena);
    poolAllocClear(t->generatedPool);

    for (int i = dependencies.count - 1; i >= 0; i--) {
        tl_Op* op = dependencies.elems[i];
        SNZ_ASSERT(!op->markedForDeletion, "tl op marked for delete made it to the solving stage");

        if (op->kind == TL_OPK_SKETCH) {
            sk_sketchSolve(&op->val.sketch);

            mesh_Mesh* m = SNZ_ARENA_PUSH(t->generatedArena, mesh_Mesh);
            *m = skt_sketchTriangulate(op->uniqueId, &op->val.sketch, t->generatedArena, scratch);
            if (m->renderMesh.vaId) { // FIXME: get a decent check for null rendermeshes
                // FIXME: probably shouldnt store one of these per scene if solves only happen for one op
                ren3d_meshDeinit(&op->scene.mesh->renderMesh);
            }
            m->renderMesh = mesh_BSPTriListToRenderMesh(m->bspTris, scratch);
            mesh_BSPTriListToFaceTris(t->generatedPool, m);
            op->scene.mesh = m;
        } else if (op->kind == TL_OPK_BASE_GEOMETRY) {
            continue;
        } else {
            SNZ_ASSERTF(false, "unreachable. kind: %lld", op->kind);
        }
    } // end loop solving
}
