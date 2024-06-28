#pragma once
#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include <memory.h>

#define UI_ASSERT(expr) assert(expr)

// TODO: new as parent
// TODO: easing functions
// TODO: test shiz
// TODO: utilities
// TODO: rounding, borders
// TODO: textures
// TODO: singleheader ify
// TODO: docs pass
// TODO: focus system
// TODO: child clipping

typedef enum {
    UI_AX_X,
    UI_AX_Y,
    UI_AX_COUNT
} ui_Axis;

typedef enum {
    UI_IF_NONE = 0,
    UI_IF_HOVER = (1 << 0),
} ui_InteractionFlags;

// TODO: more input functionality
typedef struct ui_Interaction ui_Interaction;
struct ui_Interaction {
    uint64_t interactionFlags; // values of ui_InteractionFlags ored together indicate what happened this frame
    bool hovered;
    HMM_Vec2 mousePosLocal; // mouse pos relative to the start of this box
    HMM_Vec2 mousePosGlobal;
};

typedef struct _ui_Box _ui_Box;
struct _ui_Box {
    const char* pathTag;
    float z; // where z+ is closer to screen, z- is farther // integer gaps are between divs, decimals between children
    HMM_Vec2 start;
    HMM_Vec2 end;
    HMM_Vec4 color;
    ui_Interaction* interactionTarget;
    ui_InteractionFlags interactionMask;
    _ui_Box* firstChild;
    _ui_Box* lastChild;
    _ui_Box* nextSibling;
    _ui_Box* prevSibling;
    _ui_Box* parent; // expected to always be nonnull, except for the trees first parent (which build code should not operate on)
};

typedef struct {
    uint64_t lastFrameTouched;
    uint64_t allocSize;
    void* alloc;
    const char* pathStr; // string of this tag + parents tag + parents parents tag all together
    bool inUse;
} _ui_useMemAllocNode;
#define _UI_USEMEM_MAX_ALLOCS 8000

// typedef enum {
//     UI_MB_LEFT,
//     UI_MB_RIGHT,
//     UI_MB_MIDDLE,
//     UI_MB_COUNT
// } ui_MouseButton;

// typedef enum {
//     UI_ACT_NONE,
//     UI_ACT_DOWN,
//     UI_ACT_UP,
// } ui_Action;

typedef struct {
    HMM_Vec2 mousePos;
} ui_Input;

typedef struct {
    _ui_Box treeParent;
    _ui_Box* currentParentBox;
    BumpAlloc frameArena;

    _ui_useMemAllocNode useMemAllocs[_UI_USEMEM_MAX_ALLOCS];
    bool useMemIsLastAllocTouchedNew;
    uint64_t currentFrameIdx;

    ui_Input currentInputs;
} _ui_Globs;
static _ui_Globs _ui_globs;

const char* _ui_concatStrToFrameArena(const char* a, const char* b) {
    uint64_t aLen = strlen(a);
    uint64_t bLen = strlen(b);
    char* out = BUMP_PUSH_ARR(&_ui_globs.frameArena, aLen + bLen + 1, char);
    memcpy(out, a, aLen);
    memcpy(out + aLen, b, bLen);
    return out;
}

// returns an initially zeroed piece of memory that will persist between frames
// memory is automatically freed when it is not used (with the same size) for a frame
// tag must be unique with all siblings inside the current parent box
// TODO: invalid access unit tests
// TODO: unit tests when input is done
void* ui_useMem(uint64_t size, const char* tag) {
    _ui_Box* pathTarget = _ui_globs.currentParentBox;
    if (pathTarget->lastChild) {
        pathTarget = pathTarget->lastChild;
    }
    const char* pathStr = _ui_concatStrToFrameArena(tag, pathTarget->pathTag);

    // first check if there is a node out there that correllates with this ones tag
    _ui_useMemAllocNode* firstFree = NULL;
    for (uint64_t i = 0; i < _UI_USEMEM_MAX_ALLOCS; i++) {
        _ui_useMemAllocNode* node = &_ui_globs.useMemAllocs[i];
        if (!node->inUse) {
            if (!firstFree) {
                firstFree = node;
            }
            continue;
        } else if (strcmp(node->pathStr, pathStr) == 0) {
            UI_ASSERT(node->lastFrameTouched == _ui_globs.currentFrameIdx - 1); // if still in use, it must be exacly 1 frame old
            node->lastFrameTouched = _ui_globs.currentFrameIdx;
            _ui_globs.useMemIsLastAllocTouchedNew = false;
            return node->alloc;
        }
    }
    UI_ASSERT(firstFree != NULL);
    // no node out there matches, we need to make a new one
    memset(firstFree, 0, sizeof(*firstFree));
    firstFree->inUse = true;
    firstFree->pathStr = pathStr;
    firstFree->allocSize = size;
    firstFree->lastFrameTouched = _ui_globs.currentFrameIdx;
    firstFree->alloc = calloc(1, size);
    UI_ASSERT(firstFree->alloc);
    _ui_globs.useMemIsLastAllocTouchedNew = true;
    return firstFree->alloc;
}

// returns whether the last returned call to ui_useMem this frame was newly allocated or persisted
bool ui_useMemIsPrevNew() {
    return _ui_globs.useMemIsLastAllocTouchedNew;
}

// sets the in use flag on each useMem node that has not been touched on the current frame
// TODO: cleanup fns
void _ui_useMemClearOld() {
    for (uint64_t i = 0; i < _UI_USEMEM_MAX_ALLOCS; i++) {
        _ui_useMemAllocNode* node = &_ui_globs.useMemAllocs[i];
        if (!node->inUse) {
            continue;
        }
        UI_ASSERT(node->lastFrameTouched <= _ui_globs.currentFrameIdx); // just in case lol
        if (node->lastFrameTouched < _ui_globs.currentFrameIdx) {
            node->inUse = false;
        }
    }
}

#define UI_USE_MEM(T, tag) ((T*)ui_useMem(sizeof(T), (tag)))
#define UI_USE_ARRAY(T, count, tag) ((T*)ui_useMem(sizeof(T) * (count), (tag)))

// TODO: formatted version
_ui_Box* ui_boxNew(const char* tag) {
    UI_ASSERT(_ui_globs.currentParentBox != NULL);
    _ui_Box* b = BUMP_PUSH_NEW(&_ui_globs.frameArena, _ui_Box);
    b->pathTag = _ui_concatStrToFrameArena(tag, _ui_globs.currentParentBox->pathTag);
    // TODO: make sure this doesn't collide w/ any previous tags

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
    if (!_ui_globs.frameArena.start) {
        _ui_globs.frameArena = bump_allocate(1000000, "ui frame arena");
    } else {
        bump_clear(&_ui_globs.frameArena);
    }
    _ui_useMemClearOld();
    _ui_globs.useMemIsLastAllocTouchedNew = false;
    _ui_globs.currentFrameIdx++;
    memset(&_ui_globs.treeParent, 0, sizeof(_ui_globs.treeParent));
    _ui_globs.treeParent.pathTag = "_";
    _ui_globs.currentParentBox = &_ui_globs.treeParent;
}

void _ui_genInteractionsForBoxAndChildren(_ui_Box* box, uint64_t* remainingInteractionFlags) {
    for (_ui_Box* child = box->lastChild; child; child = child->prevSibling) {
        _ui_genInteractionsForBoxAndChildren(child, remainingInteractionFlags);
    }

    if (box->interactionTarget) {
        memset(box->interactionTarget, 0, sizeof(*box->interactionTarget));
        box->interactionTarget->mousePosGlobal = _ui_globs.currentInputs.mousePos;
        box->interactionTarget->mousePosLocal = HMM_Sub(_ui_globs.currentInputs.mousePos, box->start);
    }
    if (box->interactionMask & UI_IF_HOVER) {
        if ((*remainingInteractionFlags) & UI_IF_HOVER) {
            HMM_Vec2 mousePos = _ui_globs.currentInputs.mousePos;
            if (box->start.X < mousePos.X && box->end.X > mousePos.X) {
                if (box->start.Y < mousePos.Y && box->end.Y > mousePos.Y) {
                    (*remainingInteractionFlags) ^= UI_IF_HOVER;
                    if (box->interactionTarget != NULL) {
                        box->interactionTarget->hovered = true;
                        box->interactionTarget->interactionFlags |= UI_IF_HOVER;
                    }
                }
            } // end collision check
        }
    } // end hover check
}

void ui_frameGenInteractions(ui_Input input) {
    _ui_globs.currentInputs = input;
    uint64_t interactionFlags = ~UI_IF_NONE;
    _ui_genInteractionsForBoxAndChildren(&_ui_globs.treeParent, &interactionFlags);
}

void ui_boxEnter(_ui_Box* parent) {
    _ui_globs.currentParentBox = parent;
}
void ui_boxExit() {
    _ui_globs.currentParentBox = _ui_globs.currentParentBox->parent;
    UI_ASSERT(_ui_globs.currentParentBox != NULL);
}
#define ui_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define ui_boxScope(b) ui_defer(ui_boxEnter(b), ui_boxExit())

// outInter will be updated with interaction data at the end of the frame, when genInputsForFrame is called.
// reccommended that the mem is useMemd
// outInter may be null, for cases when just blocking interactions is what you are after
// interactionMask should be a set of ui_InteractionFlags ored together, only interactions in that set will be reported
void ui_boxSetInteractionOutput(_ui_Box* box, ui_Interaction* outInter, uint64_t interactionMask) {
    box->interactionTarget = outInter;
    box->interactionMask = interactionMask;
}

void ui_boxSetColor(_ui_Box* box, HMM_Vec4 color) {
    box->color = color;
}

void ui_boxSetStart(_ui_Box* box, HMM_Vec2 newStart) {
    box->start = newStart;
}

void ui_boxSetEnd(_ui_Box* box, HMM_Vec2 newEnd) {
    box->end = newEnd;
}

void ui_boxSetSizeFromStart(_ui_Box* box, HMM_Vec2 newSize) {
    box->end = HMM_Add(box->start, newSize);
}

void ui_boxSetSizeFromEnd(_ui_Box* box, HMM_Vec2 newSize) {
    box->start = HMM_Sub(box->end, newSize);
}

void ui_boxSetSizeFromStartAx(_ui_Box* box, ui_Axis ax, float newSize) {
    box->end.Elements[ax] = box->start.Elements[ax] + newSize;
}

void ui_boxSetSizeFromEndAx(_ui_Box* box, ui_Axis ax, float newSize) {
    box->start.Elements[ax] = box->end.Elements[ax] - newSize;
}

HMM_Vec2 ui_boxGetSize(_ui_Box* box) {
    return HMM_Sub(box->end, box->start);
}

void ui_boxMarginFromParent(_ui_Box* box, float m) {
    box->start = HMM_Add(box->parent->start, HMM_V2(m, m));
    box->end = HMM_Sub(box->parent->end, HMM_V2(m, m));
}

void ui_boxFillParent(_ui_Box* box) {
    box->start = box->parent->start;
    box->end = box->parent->end;
}

void ui_boxSizePctParent(_ui_Box* box, float pct, ui_Axis ax) {
    assert(ax >= 0 && ax < UI_AX_COUNT);
    HMM_Vec2 newSize = ui_boxGetSize(box->parent);
    newSize.Elements[ax] *= pct;
    ui_boxSetSizeFromStart(box, newSize);
}

// TODO: unit test this
// maintains size but moves the box to be centered with other along ax
void ui_boxCenter(_ui_Box* box, _ui_Box* other, ui_Axis ax) {
    float boxCenter = (box->start.Elements[ax] + box->end.Elements[ax]) / 2.0f;
    float otherCenter = (other->start.Elements[ax] + other->end.Elements[ax]) / 2.0f;
    float diff = otherCenter - boxCenter;
    box->start.Elements[ax] += diff;
    box->end.Elements[ax] += diff;
}

// TODO: unit test this
// aligns box to be immediately next to and outside of other along ax
// dir indicates which side on ax should be aligned with, 1 is the lower/right side, -1 is the left/upper side
void ui_boxAlignOuter(_ui_Box* box, _ui_Box* other, ui_Axis ax, int dir) {
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
float ui_boxSizeRemainingFromStart(_ui_Box* parent, ui_Axis ax) {
    float max = 0;
    float parentStart = parent->start.Elements[ax];
    for (_ui_Box* child = parent->firstChild; child; child = child->nextSibling) {
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

float ui_easeExp(float in, float target, float pctPerSec) {
    return in + ((target - in) * pctPerSec);
    // return ((target - in) * pctPerSec * dt) + in;
}