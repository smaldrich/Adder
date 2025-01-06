#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    TL_OPK_SKETCH,
    TL_OPK_COMMENT,
    TL_OPK_GEO_IMPORT,
} tl_OpKind;

typedef struct {
    sk_Sketch sketch;
    geo_Align align;
} tl_OpSketch;

typedef struct {
    const char* text;
} tl_OpComment;

typedef struct {
    const char* path;
    geo_Mesh mesh;
} tl_OpGeoImport;

typedef struct tl_Op tl_Op;
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
        tl_OpComment comment; // FIXME: factor out to it's own construct
        tl_OpSketch sketch;
        tl_OpGeoImport geoImport;
    } val;
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

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch, geo_Align align) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->arena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_SKETCH,
        .val.sketch.sketch = sketch,
        .val.sketch.align = align,
        .next = tl->firstOp,
    };
    tl->firstOp = out;
    return out;
}

tl_Op* tl_timelinePushComment(tl_Timeline* tl, HMM_Vec2 pos, const char* text) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->arena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_COMMENT,
        .val.comment.text = text,
        .next = tl->firstOp,
    };
    tl->firstOp = out;
    return out;
}

// FIXME: make take a path (?) (might need to be some file embed ref instead but eh)
tl_Op* tl_timelinePushGeoImport(tl_Timeline* tl, HMM_Vec2 pos, geo_Mesh mesh) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->arena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_GEO_IMPORT,
        .next = tl->firstOp,
        .val.geoImport.mesh = mesh,
    };

    // FIXME: make the geo now :) thanks
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
