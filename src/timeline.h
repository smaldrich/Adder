#include "snooze.h"
#include "geometry.h"
#include "ui.h"
#include "sketches2.h"

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
        ui_SelectableState sel;
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

tl_Op tl_opCommentInit(HMM_Vec2 pos, const char* text) {
    tl_Op out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_COMMENT,
        .val.comment.text = text,
    };
    return out;
}

void tl_build(tl_Op* operations, snz_Arena* scratch) {
    snzu_boxNew("timeline");
    snzu_boxFillParent();
    snzu_boxClipChildren(true);
    snzu_boxScope() {
        snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);

        // Handle selection status and dragging for everything in the region
        ui_SelectableRegion* const region = SNZU_USE_MEM(ui_SelectableRegion, "sel region");
        {
            if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
                region->dragOrigin = inter->mousePosGlobal;
                region->dragging = true;
                // FIXME: BUG: when you drag and then open the left bar and mouseup, dragging gets stuck on
            }

            ui_SelectableStatus* firstStatus = NULL;
            HMM_Vec2 dragMin = HMM_V2(SNZ_MIN(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MIN(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            HMM_Vec2 dragMax = HMM_V2(SNZ_MAX(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MAX(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            for (tl_Op* op = operations; op; op = op->next) {
                bool inDragZone = false;
                if (dragMin.X < op->ui.pos.X && dragMax.X > op->ui.pos.X) {
                    if (dragMin.Y < op->ui.pos.Y && dragMax.Y > op->ui.pos.Y) {
                        inDragZone = true;
                    }
                }

                ui_SelectableStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectableStatus);
                *status = (ui_SelectableStatus){
                    .next = firstStatus,
                    .state = &(op->ui.sel),
                    .hovered = op->ui.inter.hovered,
                    .withinDragZone = inDragZone,
                };
                firstStatus = status;
            }
            ui_selectableRegionUpdate(region, firstStatus, inter->mouseActions[SNZU_MB_LEFT], inter->keyMods & KMOD_SHIFT);
        }

        for (tl_Op* op = operations; op; op = op->next) {
            snzu_boxNew(snz_arenaFormatStr(scratch, "%p", op));
            snzu_boxSetInteractionOutput(&op->ui.inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_ALLOW_EVENT_FALLTHROUGH); // FIXME: this is a bad way of making the sel region work

            const char* labelStr = NULL;
            if (op->kind == TL_OPK_COMMENT) {
                labelStr = "comment";
            } else if (op->kind == TL_OPK_SKETCH) {
                labelStr = "sketch";
            }
            HMM_Vec4 textColor = HMM_Lerp(ui_colorText, op->ui.sel.selectionAnim, ui_colorAccent);
            snzu_boxSetDisplayStr(&ui_labelFont, textColor, labelStr);
            float radius = 60 + (10 * op->ui.sel.hoverAnim);
            snzu_boxSetCornerRadius(radius);
            snzu_boxSetStart(HMM_Sub(op->ui.pos, HMM_V2(radius, radius)));
            snzu_boxSetEnd(HMM_Add(op->ui.pos, HMM_V2(radius, radius)));
            snzu_boxSetColor(ui_colorBackground);
            snzu_boxSetBorder(ui_borderThickness, textColor);
        }

        if (region->dragging) {
            snzu_boxNew("selBox");
            snzu_boxSetStart(region->dragOrigin);
            snzu_boxSetEnd(inter->mousePosGlobal);
            snzu_boxSetColor(ui_colorTransparentAccent);
        }
    } // end main parent box scope
}
