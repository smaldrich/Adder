#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include <memory.h>

#define UI_ASSERT(expr) assert(expr)

// TODO: useMem
// TODO: new as parent
// TODO: rendering things
// TODO: easing functions
// TODO: input structs
// TODO: test shiz
// TODO: utilities

typedef struct ui_Box ui_Box;
struct ui_Box {
    const char* tag;
    float z; // where z+ is closer to screen, z- is farther // integer gaps are between divs, decimals between children
    HMM_Vec2 start;
    HMM_Vec2 end;
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
} ui_Inst;
ui_Inst globs;

// TODO: formatted version
ui_Box* ui_boxNew(const char* tag) {
    UI_ASSERT(globs.currentParentBox != NULL);
    ui_Box* b = BUMP_PUSH_NEW(&globs.arena, ui_Box);
    b->tag = tag; // TODO: probs copy this into the arena

    b->parent = globs.currentParentBox;
    if (globs.currentParentBox->lastChild) {
        globs.currentParentBox->lastChild->nextSibling = b;
        b->prevSibling = globs.currentParentBox->lastChild;
        globs.currentParentBox->lastChild = b;
        float lastZ = b->parent->lastChild->z;
        UI_ASSERT(fmodf(lastZ, 1) < 0.9999); // TODO: test check for this one
        b->z = lastZ + 0.0001;
    } else {
        globs.currentParentBox->firstChild = b;
        globs.currentParentBox->lastChild = b;
        b->z = b->parent->z + 1;
    }
    return b;
}

void ui_frameStart() {
    if (!globs.arena.start) {
        globs.arena = bump_allocate(1000000, "ui frame arena");
    }
    memset(&globs.treeParent, 0, sizeof(globs.treeParent));
    globs.treeParent.tag = "__this is the global ui tree parent :)__";
    globs.currentParentBox = &globs.treeParent;
}

void ui_boxEnter(ui_Box* parent) {
    globs.currentParentBox = parent;
}
void ui_boxExit() {
    globs.currentParentBox = globs.currentParentBox->parent;
    UI_ASSERT(globs.currentParentBox != NULL);
}
#define ui_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define ui_boxScope(b) ui_defer(ui_boxEnter(b), ui_boxExit())