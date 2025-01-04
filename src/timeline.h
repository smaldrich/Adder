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

tl_Op tl_opCommentInit(HMM_Vec2 pos, const char* text) {
    tl_Op out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_COMMENT,
        .val.comment.text = text,
    };
    return out;
}

HMM_Vec2 _tl_pixelToWorldSpace(HMM_Vec2 mousePosPx, HMM_Vec2 panelSize, HMM_Mat4 inverseVP) {
    HMM_Vec2 mousePos = HMM_DivV2(mousePosPx, panelSize);  // move to 0-1 coords
    mousePos = HMM_MulV2F(mousePos, 2);
    mousePos = HMM_SubV2(mousePos, HMM_V2(1, 1));  // now -1 to 1 coords
    mousePos.Y *= -1;
    mousePos = HMM_Mul(inverseVP, HMM_V4(mousePos.X, mousePos.Y, 0, 1)).XY;
    return mousePos;
}

// returns mouse position in world space and a vp matrix to use ending the instances frame
// mouse panel should be the input to send to the instance at the end of the frame
void tl_build(tl_Op* operations, snz_Arena* scratch, HMM_Vec2 panelSize, HMM_Vec2 mousePosInPanel, HMM_Vec2* outMousePos, HMM_Mat4* outVP) {
    snzu_boxNew("timeline");
    snzu_boxSetStart(HMM_V2(-INFINITY, -INFINITY));
    snzu_boxSetEnd(HMM_V2(INFINITY, INFINITY));

    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);
    // FIXME: drag stuck on when left bar gets opened and mouseupd

    {  // calculate out values
        HMM_Vec2* const camPos = SNZU_USE_MEM(HMM_Vec2, "campos");
        float* const camHeight = SNZU_USE_MEM(float, "camheight");
        if (snzu_useMemIsPrevNew()) {
            *camHeight = 1000;  // in pixels
        }

        *camHeight += inter->mouseScrollY * (*camHeight) * 0.05;

        {
            HMM_Vec2* const prevMouse = SNZU_USE_MEM(HMM_Vec2, "camlastmouse");  // FIXME: This is ahead of other inputs by a frame? is that problematic?
            HMM_Vec2 diff = HMM_V2(0, 0);
            if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN) {
                *prevMouse = mousePosInPanel;
            } else if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DRAG) {
                diff = HMM_Sub(mousePosInPanel, *prevMouse);
            }
            *prevMouse = mousePosInPanel;

            diff = HMM_MulV2F(diff, *camHeight / panelSize.Y);
            *camPos = HMM_Sub(*camPos, diff);
        }

        float aspect = panelSize.X / panelSize.Y;
        float halfHeight = *camHeight / 2;
        float halfWidth = aspect * halfHeight;
        HMM_Mat4 proj = HMM_Orthographic_RH_NO(-halfWidth, halfWidth, halfHeight, -halfHeight, 0, 1000);
        HMM_Mat4 view = HMM_InvGeneral(HMM_Translate(HMM_V3(camPos->X, camPos->Y, 0)));
        HMM_Mat4 vp = HMM_Mul(proj, view);

        HMM_Mat4 vpInverse = HMM_InvGeneral(vp);
        HMM_Vec2 mousePos = _tl_pixelToWorldSpace(mousePosInPanel, panelSize, vpInverse);

        *outVP = vp;
        *outMousePos = mousePos;
    }

    snzu_boxScope() {
        // Handle selection status and dragging for everything in the region
        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "sel region");
        {
            ui_SelectionStatus* firstStatus = NULL;
            HMM_Vec2 dragMin = HMM_V2(SNZ_MIN(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MIN(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            HMM_Vec2 dragMax = HMM_V2(SNZ_MAX(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MAX(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            for (tl_Op* op = operations; op; op = op->next) {
                bool inDragZone = false;
                if (dragMin.X < op->ui.pos.X && dragMax.X > op->ui.pos.X) {
                    if (dragMin.Y < op->ui.pos.Y && dragMax.Y > op->ui.pos.Y) {
                        inDragZone = true;
                    }
                }

                ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
                *status = (ui_SelectionStatus){
                    .next = firstStatus,
                    .state = &(op->ui.sel),
                    .hovered = op->ui.inter.hovered,
                    .withinDragZone = inDragZone,
                    .mouseAct = op->ui.inter.mouseActions[SNZU_MB_LEFT],
                };
                firstStatus = status;
            }

            ui_selectionRegionUpdate(
                region,
                inter->mouseActions[SNZU_MB_LEFT],
                inter->mousePosGlobal,
                inter->keyMods & KMOD_SHIFT,
                firstStatus,
                true);
            ui_selectionRegionAnimate(firstStatus);
        }

        for (tl_Op* op = operations; op; op = op->next) {
            snzu_boxNew(snz_arenaFormatStr(scratch, "%p", op));
            snzu_boxSetInteractionOutput(&op->ui.inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS);

            const char* labelStr = NULL;
            if (op->kind == TL_OPK_COMMENT) {
                labelStr = op->val.comment.text;
            } else if (op->kind == TL_OPK_SKETCH) {
                labelStr = "sketch";
            }
            HMM_Vec4 textColor = HMM_Lerp(ui_colorText, op->ui.sel.selectionAnim, ui_colorAccent);
            snzu_boxSetDisplayStr(&ui_labelFont, textColor, labelStr);
            float radius = 60 + (10 * op->ui.sel.hoverAnim);
            snzu_boxSetCornerRadius(radius);
            snzu_boxSetStart(HMM_Sub(op->ui.pos, HMM_V2(radius, radius)));
            snzu_boxSetEnd(HMM_Add(op->ui.pos, HMM_V2(radius, radius)));
            snzu_boxSetBorder(ui_borderThickness, textColor);
        }

        if (region->dragging) {
            snzu_boxNew("selBox");
            snzu_boxSetStart(region->dragOrigin);
            snzu_boxSetEnd(inter->mousePosGlobal);
            snzu_boxSetColor(ui_colorTransparentAccent);
        }
    }  // end main parent box scope
}
