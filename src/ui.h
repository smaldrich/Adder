#pragma once
#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include <memory.h>

#define UI_ASSERT(expr) assert(expr)

// TODO: useMem
// TODO: new as parent
// TODO: easing functions
// TODO: input structs
// TODO: test shiz
// TODO: utilities
// TODO: rounding, borders
// TODO: textures

typedef enum {
    UI_AX_X,
    UI_AX_Y,
    UI_AX_COUNT
} ui_Axis;

typedef struct ui_Box ui_Box;
struct ui_Box {
    const char* tag;
    float z; // where z+ is closer to screen, z- is farther // integer gaps are between divs, decimals between children
    HMM_Vec2 start;
    HMM_Vec2 end;
    HMM_Vec4 color;
    ui_Box* firstChild;
    ui_Box* lastChild;
    ui_Box* nextSibling;
    ui_Box* prevSibling;
    ui_Box* parent; // expected to always be nonnull, except for the trees first parent (which build code should not operate on)
};

typedef struct {
    ui_Box treeParent;
    ui_Box* currentParentBox;
    BumpAlloc arena;
} _ui_Globs;
static _ui_Globs _ui_globs;

// TODO: formatted version
ui_Box* ui_boxNew(const char* tag) {
    UI_ASSERT(_ui_globs.currentParentBox != NULL);
    ui_Box* b = BUMP_PUSH_NEW(&_ui_globs.arena, ui_Box);
    b->tag = tag; // TODO: probs copy this into the arena

    b->parent = _ui_globs.currentParentBox;
    if (_ui_globs.currentParentBox->lastChild) {
        float lastZ = b->parent->lastChild->z;
        UI_ASSERT(fmodf(lastZ, 1) < 0.9999); // TODO: test check for this one
        b->z = lastZ + 0.0001;
        _ui_globs.currentParentBox->lastChild->nextSibling = b;
        b->prevSibling = _ui_globs.currentParentBox->lastChild;
        _ui_globs.currentParentBox->lastChild = b;
    } else {
        b->z = b->parent->z + 1;
        _ui_globs.currentParentBox->firstChild = b;
        _ui_globs.currentParentBox->lastChild = b;
    }
    return b;
}

void ui_frameStart() {
    if (!_ui_globs.arena.start) {
        _ui_globs.arena = bump_allocate(1000000, "ui frame arena");
    } else {
        bump_clear(&_ui_globs.arena);
    }
    memset(&_ui_globs.treeParent, 0, sizeof(_ui_globs.treeParent));
    _ui_globs.treeParent.tag = "__this is the global ui tree parent :)__";
    _ui_globs.currentParentBox = &_ui_globs.treeParent;
}

void ui_boxEnter(ui_Box* parent) {
    _ui_globs.currentParentBox = parent;
}
void ui_boxExit() {
    _ui_globs.currentParentBox = _ui_globs.currentParentBox->parent;
    UI_ASSERT(_ui_globs.currentParentBox != NULL);
}
#define ui_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define ui_boxScope(b) ui_defer(ui_boxEnter(b), ui_boxExit())

void ui_boxSetStart(ui_Box* box, HMM_Vec2 newStart) {
    box->start = newStart;
}

void ui_boxSetEnd(ui_Box* box, HMM_Vec2 newEnd) {
    box->end = newEnd;
}

void ui_boxSetSizeFromStart(ui_Box* box, HMM_Vec2 newSize) {
    box->end = HMM_Add(box->start, newSize);
}

void ui_boxSetSizeFromEnd(ui_Box* box, HMM_Vec2 newSize) {
    box->start = HMM_Sub(box->end, newSize);
}

void ui_boxSetSizeFromStartAx(ui_Box* box, ui_Axis ax, float newSize) {
    box->end.Elements[ax] = box->start.Elements[ax] + newSize;
}

void ui_boxSetSizeFromEndAx(ui_Box* box, ui_Axis ax, float newSize) {
    box->start.Elements[ax] = box->end.Elements[ax] - newSize;
}

HMM_Vec2 ui_boxGetSize(ui_Box* box) {
    return HMM_Sub(box->end, box->start);
}

void ui_boxMarginFromParent(ui_Box* box, float m) {
    box->start = HMM_Add(box->parent->start, HMM_V2(m, m));
    box->end = HMM_Sub(box->parent->end, HMM_V2(m, m));
}

void ui_boxFillParent(ui_Box* box) {
    box->start = box->parent->start;
    box->end = box->parent->end;
}

void ui_boxSizePctParent(ui_Box* box, float pct, ui_Axis ax) {
    assert(ax >= 0 && ax < UI_AX_COUNT);
    HMM_Vec2 newSize = ui_boxGetSize(box->parent);
    newSize.Elements[ax] *= pct;
    ui_boxSetSizeFromStart(box, newSize);
}

// TODO: unit test this
// maintains size but moves the box to be centered with other along ax
void ui_boxCenter(ui_Box* box, ui_Box* other, ui_Axis ax) {
    float boxCenter = (box->start.Elements[ax] + box->end.Elements[ax]) / 2.0f;
    float otherCenter = (other->start.Elements[ax] + other->end.Elements[ax]) / 2.0f;
    float diff = otherCenter - boxCenter;
    box->start.Elements[ax] += diff;
    box->end.Elements[ax] += diff;
}

// TODO: center along axis

// TODO: unit test this
// aligns box to be immediately next to and outside of other along ax
// dir indicates which side on ax should be aligned with, 1 is the lower/right side, -1 is the left/upper side
void ui_boxAlignOuter(ui_Box* box, ui_Box* other, ui_Axis ax, int dir) {
    HMM_Vec2 boxSize = ui_boxGetSize(box);

    float sidePos = 0;
    if (dir == -1) {
        sidePos = other->start.Elements[ax];
        box->end.Elements[ax] = sidePos;
        ui_boxSetSizeFromEnd(box, boxSize);
    } else if (dir == 1) {
        sidePos = other->end.Elements[ax];
        box->start.Elements[ax] = sidePos;
        ui_boxSetSizeFromStart(box, boxSize);
    } else {
        printf("[ui_boxAlignOuter]: dir should only be -1 or 1");
        assert(false);
    }
}

// TODO: unit test this
float ui_boxSizeRemainingFromStart(ui_Box* parent, ui_Axis ax) {
    float max = 0;
    float parentStart = parent->start.Elements[ax];
    for (ui_Box* child = parent->firstChild; child; child = child->nextSibling) {
        float dist = child->start.Elements[ax] - parentStart;
        if (dist > max) {
            max = dist;
        }
        dist = child->end.Elements[ax] - parentStart;
        if (dist > max) {
            max = dist;
        }
    }
    return ui_boxGetSize(parent).Elements[ax] - max;
}