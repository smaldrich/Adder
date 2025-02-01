#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    TL_OPK_SKETCH,
    TL_OPK_BASE_GEOMETRY,
    TL_OPK_SKETCH_GEOMETRY,
} tl_OpKind;

typedef struct tl_Op tl_Op;

typedef struct {
    sk_Sketch sketch;
} tl_OpSketch;

typedef struct {
    geo_Mesh mesh;
} tl_OpBaseGeometry;

typedef struct {
    tl_Op* sketch;
} tl_OpSketchGeometry;

typedef struct {
    geo_Align orbitOrigin;
    HMM_Vec2 orbitAngle;
    float orbitDist;

    geo_Mesh* mesh;
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
    bool solved;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        tl_OpSketch sketch;
        tl_OpBaseGeometry baseGeometry;
        tl_OpSketchGeometry sketchGeometry;
    } val;

    tl_Scene scene;
};

typedef struct {
    tl_Op* firstOp;
    tl_Op* activeOp;
    HMM_Vec2 camPos;
    float camHeight;
    snz_Arena* arena;
} tl_Timeline;

tl_Timeline tl_timelineInit(snz_Arena* arena) {
    tl_Timeline out = {
        .arena = arena,
        .camHeight = 1000,
        .camPos = HMM_V2(0, 0),
    };
    return out;
}

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->arena, tl_Op);
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

tl_Op* tl_timelinePushGeometry(tl_Timeline* tl, HMM_Vec2 pos, geo_Mesh mesh) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->arena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_BASE_GEOMETRY,
        .next = tl->firstOp,
        .val.sketchGeometry.mesh = mesh,
        .scene = tl_sceneInit(),
    };
    out->scene.mesh = &out->val.sketchGeometry.mesh;

    tl->firstOp = out;
    return out;
}

void tl_timelineDeselectAll(tl_Timeline* tl) {
    for (tl_Op* op = tl->firstOp; op; op = op->next) {
        op->ui.sel.selected = false;
    }
}

// FIXME: pool so that these get reused at some point instead of leaked
void tl_timelineClearOpsMarkedForDelete(tl_Timeline* timeline) {
    tl_Op* prev = NULL;
    for (tl_Op* op = timeline->firstOp; op; op = op->next) {
        if (op->markedForDeletion) {
            if (prev) {
                prev->next = op->next;
            } else {
                timeline->firstOp = op->next;
            }

            if (op == timeline->activeOp) {
                timeline->activeOp = NULL;
            }
        } else {
            prev = op;
        }
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

void tl_solve(tl_Timeline* t, snz_Arena* arena, snz_Arena* scratch, PoolAlloc* pool) {
    for (tl_Op* op = t->firstOp; op; op = op->next) {
        op->solved = false;
        if (op->kind == TL_OPK_BASE_GEOMETRY) {
            op->solved = true;
        }
    }

    for (tl_Op* op = t->firstOp; op; op = op->next) {
        if (op->kind == TL_OPK_SKETCH) {
            // FIXME: bool status
            sk_sketchSolve(&op->val.sketch.sketch);
        } else if (op->kind == TL_OPK_SKETCH_GEOMETRY) {
            tl_Op* other = op->val.sketchGeometry.sketch;
            SNZ_ASSERTF(other->kind == TL_OPK_SKETCH, "dependent op wasn't expected kind. Was: %d, expected %d.", other->kind, TL_OPK_SKETCH);
            SNZ_ASSERT(op->val.sketchGeometry.sketch, "dependent was null.");
            other->scene.mesh = skt_sketchTriangulate(&other->val.sketch.sketch, arena, scratch, pool);
        }
        op->solved = true;
    }

    for (tl_Op* op = t->firstOp; op; op = op->next) {
        SNZ_ASSERT(op->solved, "unsolved tl op.");
    }
}