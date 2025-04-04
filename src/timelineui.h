#include "snooze.h"
#include "timeline.h"
#include "shortcuts.h"

HMM_Vec2 _tl_pixelToWorldSpace(HMM_Vec2 mousePosPx, HMM_Vec2 panelSize, HMM_Mat4 inverseVP) {
    HMM_Vec2 mousePos = HMM_DivV2(mousePosPx, panelSize);  // move to 0-1 coords
    mousePos = HMM_MulV2F(mousePos, 2);
    mousePos = HMM_SubV2(mousePos, HMM_V2(1, 1));  // now -1 to 1 coords
    mousePos.Y *= -1;
    mousePos = HMM_Mul(inverseVP, HMM_V4(mousePos.X, mousePos.Y, 0, 1)).XY;
    return mousePos;
}

float _tl_radiusOfNode(tl_Op* op, float sound) {
    return 60 + (10 * op->ui.sel.hoverAnim) + (20 * sound); // FIXME: why are these big on load??
}

// returns mouse position in world space and a vp matrix to use ending the instances frame
// mouse panel should be the input to send to the instance at the end of the frame
void tl_build(tl_Timeline* timeline, snz_Arena* scratch, HMM_Vec2 panelSize, HMM_Vec2 mousePosInPanel, float sound, HMM_Vec2* outMousePos, HMM_Mat4* outVP) {
    snzu_boxNew("timeline");
    snzu_boxSetStart(HMM_V2(-INFINITY, -INFINITY));
    snzu_boxSetEnd(HMM_V2(INFINITY, INFINITY));

    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);
    // FIXME: drag stuck on when left bar gets opened and mouseupd


    HMM_Mat4 vp = { 0 };
    {  // calculate out values + camera things
        timeline->camHeight += inter->mouseScrollY * (timeline->camHeight) * 0.05;

        {
            HMM_Vec2* const prevMouse = SNZU_USE_MEM(HMM_Vec2, "camlastmouse");  // FIXME: This is ahead of other inputs by a frame? is that problematic?
            HMM_Vec2 diff = HMM_V2(0, 0);
            if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN) {
                *prevMouse = mousePosInPanel;
            } else if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DRAG) {
                diff = HMM_Sub(mousePosInPanel, *prevMouse);
            }
            *prevMouse = mousePosInPanel;

            diff = HMM_MulV2F(diff, timeline->camHeight / panelSize.Y);
            timeline->camPos = HMM_Sub(timeline->camPos, diff);
        }

        float aspect = panelSize.X / panelSize.Y;
        float halfHeight = timeline->camHeight / 2;
        float halfWidth = aspect * halfHeight;
        HMM_Mat4 proj = HMM_Orthographic_RH_NO(-halfWidth, halfWidth, halfHeight, -halfHeight, 0, 1000);
        HMM_Mat4 view = HMM_InvGeneral(HMM_Translate(HMM_V3(timeline->camPos.X, timeline->camPos.Y, 0)));
        vp = HMM_Mul(proj, view);

        HMM_Mat4 vpInverse = HMM_InvGeneral(vp);
        HMM_Vec2 mousePos = _tl_pixelToWorldSpace(mousePosInPanel, panelSize, vpInverse);

        *outVP = vp;
        *outMousePos = mousePos;
    }

    snzu_boxScope() {
        bool inRotateOrMoveMode =
            sc_getActiveCommand() == scc_timelineMove ||
            sc_getActiveCommand() == scc_timelineRotate;

        // Handle selection status and dragging for everything in the region
        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "sel region");
        {
            ui_SelectionStatus* firstStatus = NULL;
            HMM_Vec2 dragMin = HMM_V2(SNZ_MIN(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MIN(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            HMM_Vec2 dragMax = HMM_V2(SNZ_MAX(inter->mousePosGlobal.X, region->dragOrigin.X), SNZ_MAX(inter->mousePosGlobal.Y, region->dragOrigin.Y));
            for (tl_Op* op = timeline->firstOp; op; op = op->next) {
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

            if (!inRotateOrMoveMode) {
                ui_selectionRegionUpdate(
                    region,
                    firstStatus,
                    inter->mouseActions[SNZU_MB_LEFT],
                    inter->mousePosGlobal,
                    inter->keyMods & KMOD_SHIFT,
                    true, true);
            } else {
                ui_selectionRegionUpdateIgnoreMouse(region, firstStatus);
            }
            ui_selectionRegionAnimate(region, firstStatus);
        }

        if (inRotateOrMoveMode) {
            // this works because we are setting fallthrough on every node when a tool is active
            if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
                sc_cancelActiveCommand();
                tl_timelineDeselectAll(timeline);
            }

            // FIXME: probaby should build diffs into snzu_inter cause this is appearing a lot
            HMM_Vec2* const prevMouse = SNZU_USE_MEM(HMM_Vec2, "prevMouse");
            if (snzu_useMemIsPrevNew()) {
                *prevMouse = inter->mousePosGlobal;
            }

            if (sc_getActiveCommand() == scc_timelineMove) {
                HMM_Vec2 diff = HMM_Sub(inter->mousePosGlobal, *prevMouse);

                for (tl_Op* op = timeline->firstOp; op; op = op->next) {
                    if (op->ui.sel.selected) {
                        op->ui.pos = HMM_Add(op->ui.pos, diff);
                    }
                }
            } else if (sc_getActiveCommand() == scc_timelineRotate) {
                HMM_Vec2* const center = SNZU_USE_MEM(HMM_Vec2, "center"); // FIXME: mark this on screen somehow or else default it to the center
                if (snzu_useMemIsPrevNew()) {
                    *center = inter->mousePosGlobal; // FIXME: this is wrong cause panning will still work, should be saved in pixel space
                }

                HMM_Vec2* const centerOfMass = SNZU_USE_MEM(HMM_Vec2, "centerofmass");
                if (snzu_useMemIsPrevNew()) {
                    int opCount = 0;
                    for (tl_Op* op = timeline->firstOp; op; op = op->next) {
                        if (op->ui.sel.selected) {
                            *centerOfMass = HMM_Add(*centerOfMass, op->ui.pos);
                            opCount++;
                        }
                    }
                    *centerOfMass = HMM_DivV2F(*centerOfMass, opCount);
                }

                float angle = atan2f(inter->mousePosGlobal.Y, inter->mousePosGlobal.X);
                float lastAngle = atan2f(prevMouse->Y, prevMouse->X);
                float angleDiff = geo_normalizeAngle(angle - lastAngle);

                for (tl_Op* op = timeline->firstOp; op; op = op->next) {
                    if (op->ui.sel.selected) {
                        HMM_Vec2 pos = HMM_Sub(op->ui.pos, *centerOfMass);
                        pos = HMM_RotateV2(pos, angleDiff);
                        pos = HMM_Add(pos, *centerOfMass);
                        op->ui.pos = pos;
                    }
                }
            } // end rotate mode check

            *prevMouse = inter->mousePosGlobal;
        } // end rotate/move mode check

        for (tl_Op* op = timeline->firstOp; op; op = op->next) {
            snzu_boxNew(snz_arenaFormatStr(scratch, "%p", op));
            uint64_t flags = SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS;
            if (inRotateOrMoveMode) {
                flags |= SNZU_IF_ALLOW_EVENT_FALLTHROUGH;
            }
            snzu_boxSetInteractionOutput(&op->ui.inter, flags);

            HMM_Vec4 textColor = HMM_Lerp(ui_colorText, op->ui.sel.selectionAnim, ui_colorAccent);
            SNZ_ASSERTF(op->kind > TL_OPK_NONE && op->kind < TL_OPK_COUNT, "op kind out of bounds. was: %d", op->kind);
            snzu_boxSetDisplayStr(&ui_labelFont, textColor, tl_opKindNames[op->kind]);

            float radius = _tl_radiusOfNode(op, sound);
            snzu_boxSetCornerRadius(radius);
            snzu_boxSetStart(HMM_Sub(op->ui.pos, HMM_V2(radius, radius)));
            snzu_boxSetEnd(HMM_Add(op->ui.pos, HMM_V2(radius, radius)));

            snzu_boxSetColor(ui_colorTransparentPanel);
            snzu_boxSetBorder(ui_borderThickness, textColor);

            // dep lines
            for (int i = 0; i < TL_OP_ARG_MAX_COUNT; i++) {
                int expectedKinds = tl_opArgKindsExpected[op->kind][i];
                tl_OpArgKind argKind = op->args[i].kind;
                if (!expectedKinds) {
                    continue;
                } else if (!argKind) {
                    snzu_boxSetBorder(ui_borderThickness, ui_colorErr);
                    continue;
                }

                SNZ_ASSERT(argKind & expectedKinds, "Invalid kind in a tl op.");
                if (!tl_opArgKindExpectsDependency(argKind)) {
                    continue;
                }

                tl_Op* dependency = tl_timelineGetOpByUID(timeline, op->args[i].geoId.opUniqueId);
                HMM_Vec4 pts[2] = { 0 };
                pts[0].XY = op->ui.pos;
                pts[1].XY = dependency->ui.pos;
                { // FIXME: instead of clipping the line like this, use alpha magic to clip it GPU side
                    HMM_Vec4 diff = HMM_Norm(HMM_Sub(pts[1], pts[0]));
                    diff = HMM_MulV4F(diff, radius);
                    pts[0] = HMM_Add(pts[0], diff);

                    diff = HMM_Norm(HMM_Sub(pts[0], pts[1]));
                    diff = HMM_MulV4F(diff, _tl_radiusOfNode(dependency, sound));
                    pts[1] = HMM_Add(pts[1], diff);
                }
                snzr_drawLine(pts, 2, ui_colorText, ui_borderThickness, vp);
            }

            if (op == timeline->activeOp) {
                snzu_boxSetColor(ui_colorTransparentAccent);
            }
        }

        if (region->dragging) {
            snzu_boxNew("selBox");
            snzu_boxSetStart(region->dragOrigin);
            snzu_boxSetEnd(inter->mousePosGlobal);
            snzu_boxSetColor(ui_colorTransparentAccent);
        }
    }  // end main parent box scope
}

void tl_argbarBuild(tl_Op* op) {
    if (!op) {
        return;
    }
    // clears any usemems if the op changes
    snzu_boxNewF("argbar %p", (void*)op);
    snzu_boxFillParent();
    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_labelFont.renderedSize + 2 * ui_padding);
    snzu_boxSetColor(ui_colorTransparentPanel);

    snzu_boxScope() {
        snzu_boxNew("gap");
        snzu_boxSizePctParent(0.3, SNZU_AX_X);

        snzu_boxNew("name");
        snzu_boxSetDisplayStr(&ui_labelFont, ui_colorText, tl_opKindNames[op->kind]);
        snzu_boxSetSizeFitText(ui_padding);
        snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_CENTER);

        for (int i = 0; i < TL_OP_ARG_MAX_COUNT; i++) {
            int expectedKinds = tl_opArgKindsExpected[op->kind][i];
            if (!expectedKinds) {
                break;
            }

            bool done = (op->args[i].kind & expectedKinds);
            snzu_boxNewF("%d", i);
            snzu_boxSetCornerRadius(5);
            snzu_boxSetSizeMarginFromParentAx(ui_padding, SNZU_AX_Y);
            snzu_boxSetSizeFromStartAx(SNZU_AX_X, snzu_boxGetSize().Y);
            snzu_boxSetColor(done ? ui_colorAccent : ui_colorErr);
        }
        snzu_boxOrderSiblingsInRowRecurse(ui_padding, SNZU_AX_X, SNZU_ALIGN_CENTER);

        snzu_boxNew("bottom border");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_Y, -ui_borderThickness);
        snzu_boxSetColor(ui_colorText);
    }
}
