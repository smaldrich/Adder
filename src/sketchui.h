#include "HMM/HandmadeMath.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

static float main_gridLineGap(float area, float visibleCount) {
    float lineGap = area / visibleCount;

    float dec = lineGap;  // get the base ten decimal/exponents
    int exp = 0;
    while (dec < 1) {
        dec *= 10;
        exp--;
    }
    while (dec > 10) {
        dec /= 10;
        exp++;
    }

    int roundingTargets[] = { 5, 2, 1 };
    for (int i = 0; i < 3; i++) {
        if (dec > roundingTargets[i]) {
            dec = (float)roundingTargets[i];
            break;
        }
    }

    return dec * powf(10, (float)exp);
}

static HMM_Vec3 _sku_mulM4V3(HMM_Mat4 m, HMM_Vec3 v) {
    return HMM_Mul(m, HMM_V4(v.X, v.Y, v.Z, 1)).XYZ;
}

#define SKU_LINE_THICKNESS 2.0f
#define SKU_LINE_HOVERED_THICKNESS 5.0f
#define SKU_CONSTRAINT_THICKNESS 4.0f
#define SKU_CONSTRAINT_HOVERED_THICKNESS 7.0f
#define SKU_LABEL_SIZE 0.04f
#define SKU_ANGLE_CONSTRAINT_OFFSET 0.05f
#define SKU_DISTANCE_CONSTRAINT_OFFSET 0.025f

static bool _sku_lineContainsPt(HMM_Vec2 p1, HMM_Vec2 p2, float epsilon, HMM_Vec2 pt) {
    HMM_Vec2 diff = HMM_Sub(p2, p1);
    float length = HMM_Len(diff);
    HMM_Vec2 normal = HMM_Norm(diff);

    float t = HMM_Dot(HMM_Sub(pt, p1), normal);
    if (t < -epsilon || t > length + epsilon) {
        return false;
    }

    HMM_Vec2 projectedPt = HMM_Add(p1, HMM_Mul(normal, t));
    float distFromLine = HMM_Len(HMM_Sub(projectedPt, pt));
    if (distFromLine > epsilon) {
        return false;
    }

    return true;
}

static float _sku_angleOfV2(HMM_Vec2 v) {
    return atan2f(v.Y, v.X);
}

// puts it in the range of -180 to +180, in rads
static float _sku_normalizeAngle(float a) {
    while (a > HMM_AngleDeg(180)) {
        a -= HMM_AngleDeg(360);
    }
    while (a < HMM_AngleDeg(-180)) {
        a += HMM_AngleDeg(360);
    }
    return a;
}

// Returned X comp is line 1's angle, Y comp is line 2's
static HMM_Vec2 _sku_angleOfLinesInAngleConstraint(sk_Constraint* c) {
    SNZ_ASSERTF(c->kind == SK_CK_ANGLE, "constraint wasn't an angle constraint. kind: %d", c->kind);
    HMM_Vec2 diff1 = HMM_Sub(c->line1->p2->pos, c->line1->p1->pos);
    float a1 = atan2f(diff1.Y, diff1.X);
    HMM_Vec2 diff2 = HMM_Sub(c->line2->p2->pos, c->line2->p1->pos);
    float a2 = atan2f(diff2.Y, diff2.X);

    if (c->flipLine1) {
        a1 += HMM_AngleDeg(180);
    }

    if (c->flipLine2) {
        a2 += HMM_AngleDeg(180);
    }

    return HMM_V2(a1, a2);
}

// outparams assumed non-null
static void _sku_constraintScaleFactorAndCenter(sk_Constraint* c, HMM_Mat4 model, HMM_Vec3 cameraPos, HMM_Vec2* outCenter, float* outScaleFactor) {
    HMM_Vec2 visualCenter = HMM_V2(0, 0);
    float scaleFactor = 0;

    if (c->kind == SK_CK_ANGLE) {
        visualCenter = c->flipLine1 ? c->line1->p2->pos : c->line1->p1->pos;
    } else if (c->kind == SK_CK_DISTANCE) {
        visualCenter = HMM_DivV2F(HMM_Add(c->line1->p1->pos, c->line1->p2->pos), 2);
    } else {
        SNZ_ASSERTF(false, "unreachable case. kind: %d", c->kind);
    }
    HMM_Vec3 transformedCenter = _sku_mulM4V3(model, HMM_V3(visualCenter.X, visualCenter.Y, 0));
    scaleFactor = HMM_Len(HMM_Sub(cameraPos, transformedCenter));

    *outCenter = visualCenter;
    *outScaleFactor = scaleFactor;
}

static bool _sku_constraintHovered(sk_Constraint* c, float scaleFactor, HMM_Vec2 visualCenter, HMM_Vec2 mousePos) {
    // FIXME: include text in collision zone
    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * scaleFactor);
        p1 = HMM_Add(p1, offset);
        p2 = HMM_Add(p2, offset);
        return _sku_lineContainsPt(p1, p2, 0.01 * scaleFactor, mousePos);
    } else if (c->kind == SK_CK_ANGLE) {
        HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
        HMM_Vec2 diff = HMM_Sub(mousePos, visualCenter);
        if (HMM_Len(diff) > (SKU_ANGLE_CONSTRAINT_OFFSET * 1.4 * scaleFactor)) {
            return false;
        }
        float mouseAngle = _sku_angleOfV2(diff);
        float midAngle = (angles.Right + angles.Left) / 2;
        float halfRange = fabsf(_sku_normalizeAngle(angles.Right - angles.Left) / 2);
        if (fabsf(_sku_normalizeAngle(midAngle - mouseAngle)) > halfRange) {
            return false;
        }

        return true;
    }
    SNZ_ASSERTF(false, "unreachable. kind: %d", c);
    return false;
}

static void _sku_drawConstraint(sk_Constraint* c, HMM_Mat4 model, HMM_Vec3 cameraPos, snz_Arena* scratch, HMM_Mat4 mvp, float soundPct) {
    HMM_Vec4 drawnColor = UI_TEXT_COLOR;
    if (c->violated) {
        drawnColor = UI_RED;
    }
    drawnColor = HMM_LerpV4(drawnColor, c->uiInfo.selectionAnim, UI_ACCENT_COLOR);

    HMM_Vec2 visualCenter = HMM_V2(0, 0);
    float scaleFactor = 1;
    _sku_constraintScaleFactorAndCenter(c, model, cameraPos, &visualCenter, &scaleFactor);

    float drawnThickness = HMM_Lerp(SKU_CONSTRAINT_THICKNESS, c->uiInfo.hoverAnim, SKU_CONSTRAINT_HOVERED_THICKNESS);

    HMM_Vec2 textTopLeft = HMM_V2(0, 0);
    const char* text = NULL;

    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * (1 + soundPct) * scaleFactor);
        p1 = HMM_Add(p1, offset);
        p2 = HMM_Add(p2, offset);
        HMM_Vec2 points[] = { p1, p2 };
        snzr_drawLine(points, 2, drawnColor, drawnThickness, mvp);

        textTopLeft = HMM_Add(visualCenter, HMM_Mul(offset, 2.0f));
        text = snz_arenaFormatStr(scratch, "%.2fm", c->value);
    } else if (c->kind == SK_CK_ANGLE) {
        sk_Point* joint = NULL;
        if (c->line1->p1 == c->line2->p1 || c->line1->p1 == c->line2->p2) {
            joint = c->line1->p1;
        } else if (c->line1->p2 == c->line2->p1 || c->line1->p2 == c->line2->p2) {
            joint = c->line1->p2;
        } else {
            return;
        }

        float offset = SKU_ANGLE_CONSTRAINT_OFFSET * (1 + soundPct) * scaleFactor;
        if (csg_floatEqual(c->value, HMM_AngleDeg(90))) {
            sk_Point* otherOnLine1 = (c->line1->p1 == joint) ? c->line1->p2 : c->line1->p1;
            sk_Point* otherOnLine2 = (c->line2->p1 == joint) ? c->line2->p2 : c->line2->p1;
            HMM_Vec2 offset1 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine1->pos, joint->pos)), offset);
            HMM_Vec2 offset2 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine2->pos, joint->pos)), offset);
            HMM_Vec2 pts[] = {
                HMM_Add(joint->pos, offset1),
                HMM_Add(joint->pos, HMM_Add(offset1, offset2)),
                HMM_Add(joint->pos, offset2),
            };
            snzr_drawLine(pts, 3, drawnColor, drawnThickness, mvp);
            return;
        } else {
            HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
            float startAngle = angles.X;
            float angleRange = angles.Y - startAngle;

            int ptCount = (int)(fabsf(angleRange) / HMM_AngleDeg(10)) + 1;
            HMM_Vec2* linePts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);
            for (int i = 0; i < ptCount; i++) {
                HMM_Vec2 o = HMM_RotateV2(HMM_V2(offset, 0), startAngle + (i * c->value / (ptCount - 1)));
                linePts[i] = HMM_Add(joint->pos, o);
            }
            snzr_drawLine(linePts, ptCount, drawnColor, drawnThickness, mvp);

            textTopLeft = HMM_RotateV2(HMM_V2(offset * 1.5, 0), startAngle + angleRange / 2);
            textTopLeft = HMM_Add(textTopLeft, visualCenter);
            text = snz_arenaFormatStr(scratch, "%.1fdeg", HMM_ToDeg(fabsf(c->value)));
            // FIXME: ^^ unicode + the degree symol
        }
    }

    float drawnHeight = SKU_LABEL_SIZE * scaleFactor;
    textTopLeft.Y -= drawnHeight / 2;

    // FIXME: move labels if camera is on the other side
    // FIXME: less self intersection on angled lines
    snzr_drawTextScaled(
        textTopLeft,
        HMM_V2(-100000, -100000), HMM_V2(100000, 100000),
        drawnColor, text, strlen(text), ui_titleFont,
        mvp, drawnHeight, false, true);
}

static void _sku_drawManifold(sk_Point* p, HMM_Vec3 cameraPos, HMM_Mat4 model, HMM_Mat4 mvp, snz_Arena* scratch) {
    HMM_Vec2* pts = NULL;
    int ptCount = 0;

    float distToCamera = HMM_Len(HMM_Sub(cameraPos, _sku_mulM4V3(model, HMM_V3(p->pos.X, p->pos.Y, 0))));
    float scaleFactor = distToCamera;  // FIXME: ortho switch

    if (p->manifold.kind == SK_MK_POINT) {
        return;
    } else if (p->manifold.kind == SK_MK_CIRCLE) {
        ptCount = 11;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);

        float angleRange = HMM_AngleDeg(40);
        HMM_Vec2 diff = HMM_Sub(p->pos, p->manifold.circle.origin);
        float startAngle = atan2f(diff.Y, diff.X);
        for (int i = 0; i < ptCount; i++) {
            float angle = startAngle + (i - (ptCount / 2)) * (angleRange / ptCount);
            pts[i] = HMM_RotateV2(HMM_V2(p->manifold.circle.radius, 0), angle);
            pts[i] = HMM_Add(pts[i], p->manifold.circle.origin);
        }
    } else if (p->manifold.kind == SK_MK_LINE) {
        ptCount = 2;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);
        pts[0] = p->pos;
        pts[1] = HMM_Add(p->pos, HMM_Mul(HMM_Norm(p->manifold.line.direction), 0.4f * scaleFactor));
    } else if (p->manifold.kind == SK_MK_ANY) {
        // make it really big so the cross line is entirely faded out
        ptCount = 4;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);
        pts[0] = HMM_V2(-1 * scaleFactor, 0);
        pts[1] = HMM_V2(1 * scaleFactor, 0);
        pts[2] = HMM_V2(0, -1 * scaleFactor);
        pts[3] = HMM_V2(0, 1 * scaleFactor);

        for (int i = 0; i < ptCount; i++) {
            pts[i] = HMM_RotateV2(pts[i], HMM_AngleDeg(10));
            pts[i] = HMM_AddV2(pts[i], p->pos);
        }
    } else {
        SNZ_ASSERTF(false, "unreachable. kind was: %d", p->manifold.kind);
        return;
    }

    if (p->manifold.kind != SK_MK_CIRCLE) {
        scaleFactor *= 0.07;
    } else {
        // When it's a circle, there's not really a great way to scale up the manifold visually
        // and keep it's radius meaningful. So i'm leaving it like this for now.
        // FIXME: could scale up the visible radius to a point tho, which would probs work
        scaleFactor = 0.07;
    }
    snzr_drawLineFaded(
        pts, ptCount,
        UI_ACCENT_COLOR, 4,
        mvp, HMM_V3(p->pos.X, p->pos.Y, 0), scaleFactor, scaleFactor);
}

static bool _sku_AABBContainsPt(HMM_Vec2 boxStart, HMM_Vec2 boxEnd, HMM_Vec2 pt) {
    if (boxStart.X < pt.X && boxEnd.X > pt.X) {
        if (boxStart.Y < pt.Y && boxEnd.Y > pt.Y) {
            return true;
        }
    }
    return false;
}

typedef struct _sku_SketchEltUIStatus _sku_SketchEltUIStatus;
struct _sku_SketchEltUIStatus {
    bool hovered;
    bool withinDragZone;
    sk_ElementUIInfo* eltUIInfo;
    _sku_SketchEltUIStatus* next;
};

// FIXME: sketch element selection persists too much
void sku_drawSketch(sk_Sketch* sketch, HMM_Mat4 vp, HMM_Mat4 model, snz_Arena* scratch, HMM_Vec3 cameraPos, HMM_Vec2 mousePosInPlane, snzu_Action mouseAct, bool shiftPressed, float soundPct) {
    HMM_Mat4 mvp = HMM_Mul(vp, model);

    glDisable(GL_DEPTH_TEST);

    {  // grid around the cursor
        HMM_Vec3 mousePos = HMM_V3(mousePosInPlane.X, mousePosInPlane.Y, 0);
        float scaleFactor = HMM_LenV3(HMM_Sub(_sku_mulM4V3(model, mousePos), cameraPos));

        // FIXME: jarring switches in gaplen
        // FIXME: unpleasant clipping into coplanar geometry
        int lineCount = 13;  // FIXME: batch all of these verts into one line
        float lineGap = main_gridLineGap(scaleFactor * 2, lineCount);
        for (int ax = 0; ax < 2; ax++) {
            float axOffset = fmod(mousePosInPlane.Elements[ax], lineGap);
            for (int i = 0; i < lineCount; i++) {
                float x = (i - (lineCount / 2)) * lineGap;
                x -= axOffset;
                HMM_Vec2 pts[] = { mousePosInPlane, mousePosInPlane };
                pts[0].Elements[ax] += x;
                pts[0].Elements[!ax] += 1.5 * scaleFactor;
                pts[1].Elements[ax] += x;
                pts[1].Elements[!ax] += -1.5 * scaleFactor;

                HMM_Vec3 fadeOrigin = { 0 };
                fadeOrigin.XY = mousePosInPlane;
                // FIXME: have this invert color when behind smth in the scene
                snzr_drawLineFaded(pts, 2, UI_ALMOST_BACKGROUND_COLOR, 1, mvp, fadeOrigin, 0, 0.5 * scaleFactor);
            }
        }
    }  // end grid

    {  // selection // UI state updates
        // doing this instead of snzu_Interaction.dragBeginning bc. mouse pos is projected
        HMM_Vec2* const dragOrigin = SNZU_USE_MEM(HMM_Vec2, "dragOrigin");
        bool* const dragging = SNZU_USE_MEM(bool, "dragging");
        bool dragEnded = *dragging && mouseAct == SNZU_ACT_UP;

        if (mouseAct == SNZU_ACT_DOWN) {
            *dragOrigin = mousePosInPlane;
            *dragging = true;
            // FIXME: what happens on the border of mouse not being projectable??
        } else if (mouseAct == SNZU_ACT_NONE || mouseAct == SNZU_ACT_UP) {
            *dragging = false;
        }

        {  // loop over all points, check whether they are within the contained zone and update their selection status
            // FIXME: kinda wasteful right now to have points contain full UIInfo structs, but once pt selection and drawing gets added maybe not
            // FIXME: this will also have to change to another variable that isn't selected once that happens, cause selected is persisted
            HMM_Vec2 start = HMM_V2(0, 0);
            HMM_Vec2 end = HMM_V2(0, 0);
            start.X = SNZ_MIN(mousePosInPlane.X, dragOrigin->X);
            start.Y = SNZ_MIN(mousePosInPlane.Y, dragOrigin->Y);
            end.X = SNZ_MAX(mousePosInPlane.X, dragOrigin->X);
            end.Y = SNZ_MAX(mousePosInPlane.Y, dragOrigin->Y);
            for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                p->uiInfo.selected = false;
                if (_sku_AABBContainsPt(start, end, p->pos)) {
                    p->uiInfo.selected = true;
                }
            }  // end point loop
        }

        // batch lines and constraints into one list with all the info we are operating on
        _sku_SketchEltUIStatus* firstStatus = NULL;
        {
            for (sk_Line* l = sketch->firstLine; l; l = l->next) {
                bool withinDragZone = l->p1->uiInfo.selected && l->p2->uiInfo.selected;

                HMM_Vec2 midpt = HMM_DivV2F(HMM_Add(l->p1->pos, l->p2->pos), 2.0f);
                HMM_Vec3 transformedCenter = HMM_MulM4V4(model, HMM_V4(midpt.X, midpt.Y, 0, 1)).XYZ;
                float distToCamera = HMM_Len(HMM_Sub(transformedCenter, cameraPos));
                bool hovered = _sku_lineContainsPt(l->p1->pos, l->p2->pos, 0.01 * distToCamera, mousePosInPlane);
                if (mouseAct == SNZU_ACT_DOWN && hovered) {
                    *dragging = false;  // cancel a drag before it is processed if it lands on this
                }

                _sku_SketchEltUIStatus* status = SNZ_ARENA_PUSH(scratch, _sku_SketchEltUIStatus);
                *status = (_sku_SketchEltUIStatus){
                    .eltUIInfo = &l->uiInfo,
                    .hovered = hovered,
                    .withinDragZone = withinDragZone,
                    .next = firstStatus,
                };
                firstStatus = status;
            }

            for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
                HMM_Vec2 visualCenter;
                float scaleFactor;
                _sku_constraintScaleFactorAndCenter(c, model, cameraPos, &visualCenter, &scaleFactor);
                bool hovered = _sku_constraintHovered(c, scaleFactor, visualCenter, mousePosInPlane);
                if (mouseAct == SNZU_ACT_DOWN && hovered) {
                    *dragging = false;  // cancel a drag before it is processed if it lands on this
                }

                bool withinDragZone = false;
                if (c->kind == SK_CK_ANGLE) {
                    sk_Point* base1 = c->flipLine1 ? c->line1->p2 : c->line1->p1;
                    sk_Point* base2 = c->flipLine2 ? c->line2->p2 : c->line2->p1;
                    withinDragZone = base1 == base2 && base1->uiInfo.selected;
                } else if (c->kind == SK_CK_DISTANCE) {
                    withinDragZone = c->line1->p1->uiInfo.selected && c->line1->p2->uiInfo.selected;
                }

                _sku_SketchEltUIStatus* status = SNZ_ARENA_PUSH(scratch, _sku_SketchEltUIStatus);
                *status = (_sku_SketchEltUIStatus){
                    .eltUIInfo = &c->uiInfo,
                    .hovered = hovered,
                    .withinDragZone = withinDragZone,
                    .next = firstStatus,
                };
                firstStatus = status;
            }
        }

        for (_sku_SketchEltUIStatus* status = firstStatus; status; status = status->next) {
            if (mouseAct == SNZU_ACT_DOWN) {
                if (!shiftPressed && !status->hovered && !status->withinDragZone) {
                    status->eltUIInfo->selected = false;
                } else if (status->hovered) {
                    status->eltUIInfo->selected = !status->eltUIInfo->selected;
                }
            }

            if (dragEnded && status->withinDragZone) {
                status->eltUIInfo->selected = true;
            }

            float hoverTarget = status->hovered && !*dragging;
            snzu_easeExpUnbounded(&status->eltUIInfo->hoverAnim, hoverTarget, 15);

            float selectionTarget = (status->withinDragZone && *dragging) || status->eltUIInfo->selected;
            snzu_easeExpUnbounded(&status->eltUIInfo->selectionAnim, selectionTarget, 15);
        }

        if (*dragging) {
            HMM_Vec4 color = UI_ACCENT_COLOR;
            color.A = 0.2;
            snzr_drawRect(*dragOrigin, mousePosInPlane,
                          HMM_V2(-100000, -100000), HMM_V2(100000, 100000),
                          color,
                          0, 0, HMM_V4(0, 0, 0, 0),
                          mvp, _snzr_globs.solidTex);  // FIXME: internals should not be hacked at like this
        }
    }  // end selection management scope

    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        _sku_drawManifold(p, cameraPos, model, mvp, scratch);
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        _sku_drawConstraint(c, model, cameraPos, scratch, mvp, soundPct);
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        HMM_Vec2 points[] = {
            l->p1->pos,
            l->p2->pos,
        };
        float thickness = HMM_Lerp(2.0f, l->uiInfo.hoverAnim, 5.0f);
        HMM_Vec4 color = HMM_LerpV4(UI_TEXT_COLOR, l->uiInfo.selectionAnim, UI_ACCENT_COLOR);
        snzr_drawLine(points, 2, color, thickness, mvp);
    }

    glEnable(GL_DEPTH_TEST);
}