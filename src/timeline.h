#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    TL_OPK_SKETCH,
    TL_OPK_COMMENT,
} tl_OpKind;

typedef struct {
    sk_Sketch sketch;
} tl_OpSketch;

typedef struct {
    const char* text;
} tl_OpComment;

typedef struct tl_Op tl_Op;
struct tl_Op {
    tl_Op* next;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        tl_OpComment comment;
        tl_OpSketch sketch;
    } val;
};

// does not push to a list
tl_Op tl_opSketchInit(HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_SKETCH,
        .val.sketch.sketch = sketch,
    };
    return out;
}

void tl_deselectAll(tl_Op* firstOp) {
    for (tl_Op* op = firstOp; op; op = op->next) {
        op->ui.sel.selected = false;
    }
}

tl_Op tl_opCommentInit(HMM_Vec2 pos, const char* text) {
    tl_Op out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_COMMENT,
        .val.comment.text = text,
    };
    return out;
}
