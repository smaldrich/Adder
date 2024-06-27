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
        _ui_globs.currentParentBox->lastChild->nextSibling = b;
        b->prevSibling = _ui_globs.currentParentBox->lastChild;
        _ui_globs.currentParentBox->lastChild = b;
        float lastZ = b->parent->lastChild->z;
        UI_ASSERT(fmodf(lastZ, 1) < 0.9999); // TODO: test check for this one
        b->z = lastZ + 0.0001;
    } else {
        _ui_globs.currentParentBox->firstChild = b;
        _ui_globs.currentParentBox->lastChild = b;
        b->z = b->parent->z + 1;
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