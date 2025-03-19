#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    TL_OPK_NONE,
    TL_OPK_SKETCH,
    TL_OPK_BASE_GEOMETRY,
    TL_OPK_SKETCH_GEOMETRY,
} tl_OpKind;

typedef struct tl_Op tl_Op;

typedef struct {
    sk_Sketch sketch;
} tl_OpSketch;

typedef struct {
    mesh_Mesh mesh;
} tl_OpBaseGeometry;

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

struct tl_Op {
    tl_Op* next;
    bool markedForDeletion;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        tl_OpSketch sketch;
        tl_OpBaseGeometry baseGeometry;
    } val;
    tl_Op* dependencies[1];
    bool expectedDependencies[1]; // FIXME: make this a lookup so deserialization doesn't break it

    tl_Scene scene;
};

SNZ_SLICE_NAMED(tl_Op*, tl_OpPtrSlice);

typedef struct {
    tl_Op* firstOp;
    tl_Op* activeOp;
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
    };
    return out;
}

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_SKETCH,
        .val.sketch.sketch = sketch,
        .next = tl->firstOp,
        .scene = tl_sceneInit(),
    };
    tl->firstOp = out;
    return out;
}

tl_Op* tl_timelinePushBaseGeometry(tl_Timeline* tl, HMM_Vec2 pos, mesh_Mesh mesh) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_BASE_GEOMETRY,
        .next = tl->firstOp,
        .val.baseGeometry.mesh = mesh,
        .scene = tl_sceneInit(),
    };
    out->scene.mesh = &out->val.baseGeometry.mesh;

    tl->firstOp = out;
    return out;
}

tl_Op* tl_timelinePushSketchGeometry(tl_Timeline* tl, HMM_Vec2 pos, tl_Op* sketch) {
    SNZ_ASSERTF(sketch->kind == TL_OPK_SKETCH, "tried to add sketch geo for op with kind %d.", sketch->kind);

    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .kind = TL_OPK_SKETCH_GEOMETRY,
        .dependencies[0] = sketch,
        .expectedDependencies[0] = true,
        .next = tl->firstOp,
        .ui.pos = pos,
        .scene = tl_sceneInit(),
    };
    out->scene.mesh = &out->val.baseGeometry.mesh;

    tl->firstOp = out;
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
        if (!op->dependencies[0]) {
            continue;
        } else if (op->dependencies[0]->markedForDeletion) {
            op->dependencies[0] = NULL;
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

        for (int i = 0; i < added.count; i++) {
            tl_Op* new = added.elems[i];
            if (!new->dependencies[0]) {
                continue;
            }
            bool inAdded = false;
            for (int j = 0; j < added.count; j++) {
                if (added.elems[j] == new) {
                    inAdded = true;
                }
            }
            if (inAdded) {
                continue;
            }

            *SNZ_ARENA_PUSH(scratch, tl_Op*) = new;
            anyAdded = true;
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
            sk_sketchSolve(&op->val.sketch.sketch);
        } else if (op->kind == TL_OPK_SKETCH_GEOMETRY) {
            tl_Op* other = op->dependencies[0];
            SNZ_ASSERTF(other->kind == TL_OPK_SKETCH, "dependent op wasn't expected kind. Was: %d, expected %d.", other->kind, TL_OPK_SKETCH);

            mesh_Mesh* m = SNZ_ARENA_PUSH(t->generatedArena, mesh_Mesh);
            *m = skt_sketchTriangulate(&other->val.sketch.sketch, t->generatedArena, scratch);
            if (m->renderMesh.vaId) { // FIXME: get a decent check for null rendermeshes
                // FIXME: probably shouldnt store one of these per scene if solves only happen for one op
                ren3d_meshDeinit(&op->scene.mesh->renderMesh);
            }
            m->renderMesh = mesh_BSPTriListToRenderMesh(m->bspTris, scratch);
            mesh_BSPTriListToFaceTris(t->generatedPool, m);
            other->scene.mesh = m;
            op->scene.mesh = m;
        }
    } // end loop solving
}
