#pragma once

#include "HMM/HandmadeMath.h"
#include "shortcuts.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

static float _sku_gridLineGap(float area, float visibleCount) {
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

static float _sku_angleDifferenceForConstraint(sk_Constraint* c, float a1, float a2) {
    SNZ_ASSERT(c->kind == SK_CK_ANGLE, "constraint wasn't an angle constraint.");
    float diff = a2 - a1;
    if (c->value < 0) {
        while (diff > 0) {
            diff -= HMM_AngleDeg(360);
        }
    } else {
        while (diff < 0) {
            diff += HMM_AngleDeg(360);
        }
    }  // force angle range to go the correct way around based on the sign of the constraint value
    // FIXME: this is kinda hacky, i'm sure theres a more direct way of fixing this by modifying _sku_angleOfLinesInAngleConstraint,
    // but this works so were gonna leave it.
    return diff;
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
    bool out = true;
    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * scaleFactor);
        p1 = HMM_Add(p1, offset);
        p2 = HMM_Add(p2, offset);
        out = _sku_lineContainsPt(p1, p2, 0.01 * scaleFactor, mousePos);
    } else if (c->kind == SK_CK_ANGLE) {
        HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
        float angleDiff = _sku_angleDifferenceForConstraint(c, angles.Left, angles.Right);
        HMM_Vec2 diff = HMM_Sub(mousePos, visualCenter);
        if (HMM_Len(diff) > (SKU_ANGLE_CONSTRAINT_OFFSET * 1.4 * scaleFactor)) {
            out = false;
        }
        float mouseAngle = _sku_angleOfV2(diff);
        float midAngle = angles.Left + (angleDiff / 2);
        float halfRange = fabsf(angleDiff / 2);
        if (fabsf(geo_normalizeAngle(midAngle - mouseAngle)) > halfRange) {
            out = false;
        }
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", c);
    }

    if (c->uiInfo.textArea.inter.hovered) {
        out = true;
    }
    return out;
}

// see _sku_buildConstraint, which this is dependent on for state
static void _sku_drawConstraint(sk_Constraint* c, snz_Arena* scratch, HMM_Mat4 sketchMVP, float soundPct) {
    float drawnThickness = HMM_Lerp(SKU_CONSTRAINT_THICKNESS, c->uiInfo.sel.hoverAnim, SKU_CONSTRAINT_HOVERED_THICKNESS);

    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * (1 + soundPct) * c->uiInfo.scaleFactor);
        p1 = HMM_Add(p1, offset);
        p2 = HMM_Add(p2, offset);
        HMM_Vec4 points[2] = { 0 };
        points[0].XY = p1;
        points[1].XY = p2;
        snzr_drawLine(points, 2, c->uiInfo.drawnColor, drawnThickness, sketchMVP);
    } else if (c->kind == SK_CK_ANGLE) {
        sk_Point* joint = NULL;
        // FIXME: this can be incorrect based on flippings, fix to always be accurate
        if (c->line1->p1 == c->line2->p1 || c->line1->p1 == c->line2->p2) {
            joint = c->line1->p1;
        } else if (c->line1->p2 == c->line2->p1 || c->line1->p2 == c->line2->p2) {
            joint = c->line1->p2;
        } else {
            return;
        }

        float offset = SKU_ANGLE_CONSTRAINT_OFFSET * (1 + soundPct) * c->uiInfo.scaleFactor;
        if (geo_floatEqual(fabsf(c->value), HMM_AngleDeg(90))) {
            sk_Point* otherOnLine1 = (c->line1->p1 == joint) ? c->line1->p2 : c->line1->p1;
            sk_Point* otherOnLine2 = (c->line2->p1 == joint) ? c->line2->p2 : c->line2->p1;
            HMM_Vec2 offset1 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine1->pos, joint->pos)), offset);
            HMM_Vec2 offset2 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine2->pos, joint->pos)), offset);
            HMM_Vec4 pts[3] = { 0 };
            pts[0].XY = HMM_Add(joint->pos, offset1);
            pts[1].XY = HMM_Add(joint->pos, HMM_Add(offset1, offset2));
            pts[2].XY = HMM_Add(joint->pos, offset2);
            snzr_drawLine(pts, 3, c->uiInfo.drawnColor, drawnThickness, sketchMVP);
        } else {
            HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
            float startAngle = angles.X;
            float angleRange = _sku_angleDifferenceForConstraint(c, angles.Left, angles.Right);

            int ptCount = (int)(fabsf(angleRange) / HMM_AngleDeg(10)) + 1;
            HMM_Vec4* linePts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec4);
            for (int i = 0; i < ptCount; i++) {
                HMM_Vec2 o = HMM_RotateV2(HMM_V2(offset, 0), startAngle + (i * c->value / (ptCount - 1)));
                linePts[i].XY = HMM_Add(joint->pos, o);
            }
            snzr_drawLine(linePts, ptCount, c->uiInfo.drawnColor, drawnThickness, sketchMVP);
        }
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", c->kind);
    }
}

// does UI state processing, calculates scaleFactor, center, color also, & should be called before drawConstraint
// updates the constraints value (which will be unsolved unless equal to the previous frames value)
static void _sku_buildConstraint(sk_Constraint* c, float sound, HMM_Mat4 model, HMM_Vec3 cameraPos, snz_Arena* scratch) {
    c->uiInfo.drawnColor = ui_colorText;
    if (c->violated) {
        c->uiInfo.drawnColor = ui_colorErr;
    }
    c->uiInfo.drawnColor = HMM_LerpV4(c->uiInfo.drawnColor, c->uiInfo.sel.selectionAnim, ui_colorAccent);

    _sku_constraintScaleFactorAndCenter(c, model, cameraPos, &c->uiInfo.visualCenter, &c->uiInfo.scaleFactor);

    float drawnHeight = SKU_LABEL_SIZE * c->uiInfo.scaleFactor;

    HMM_Vec2 textTopLeft = HMM_V2(0, 0);  // TL of the label in sketch space
    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * (1 + sound) * c->uiInfo.scaleFactor);
        textTopLeft = HMM_Add(c->uiInfo.visualCenter, HMM_Mul(offset, 2.0f));
    } else if (c->kind == SK_CK_ANGLE) {
        HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
        float startAngle = angles.X;
        float angleRange = _sku_angleDifferenceForConstraint(c, angles.Left, angles.Right);
        float offset = SKU_ANGLE_CONSTRAINT_OFFSET * (1 + sound) * c->uiInfo.scaleFactor;
        textTopLeft = HMM_RotateV2(HMM_V2(offset * 1.5, 0), startAngle + angleRange / 2);
        textTopLeft = HMM_Add(textTopLeft, c->uiInfo.visualCenter);
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", c->kind);
    }

    char* boxName = snz_arenaFormatStr(scratch, "%p", c);
    snzu_boxNew(boxName);
    textTopLeft.Y *= -1;  // flip to UI space before drawing
    snzu_boxSetStart(textTopLeft);

    // FIXME: this can be clicked while in line mode and it is not correct
    ui_textArea(&c->uiInfo.textArea, &ui_titleFont, drawnHeight, c->uiInfo.drawnColor, c->uiInfo.shouldStartFocus);
    c->uiInfo.shouldStartFocus = false;

    float val = atof(c->uiInfo.textArea.chars);

    // FIXME: unit spec parsing
    if (c->kind == SK_CK_ANGLE) {
        val = HMM_AngleDeg(val);

        if (val > HMM_AngleDeg(2 * 360)) {
            val = 0;
        } else if (val < HMM_AngleDeg(-2 * 360)) {
            val = 0;
        }

        while (val > HMM_AngleDeg(360)) {
            val -= HMM_AngleDeg(360);
        }
        while (val < HMM_AngleDeg(-360)) {
            val += HMM_AngleDeg(360);
        }
    }

    if (!geo_floatZero(val)) {
        c->value = val;  // FIXME: when this turns into a 90deg angle, the text box immediately disappears
    }

    if (!c->uiInfo.textArea.wasFocused) {  // FIXME: kinda wasteful to have this running constantly
        // FIXME: this has a one frame delay on scene open before things have text. ig its fine but not ideal.
        const char* str = sk_constraintLabelStr(c, scratch);
        ui_textAreaSetStr(&c->uiInfo.textArea, str, strlen(str));
    }

    // FIXME: flip labels if camera is on the other side
    // FIXME: less self intersection on angled lines
}

static void _sku_drawManifold(sk_Point* p, HMM_Vec3 cameraPos, HMM_Mat4 model, HMM_Mat4 mvp, float sound, snz_Arena* scratch) {
    HMM_Vec4* pts = NULL;
    int ptCount = 0;

    float distToCamera = HMM_Len(HMM_Sub(cameraPos, _sku_mulM4V3(model, HMM_V3(p->pos.X, p->pos.Y, 0))));
    float scaleFactor = distToCamera;  // FIXME: ortho switch

    if (p->manifold.kind == SK_MK_POINT) {
        return;
    } else if (p->manifold.kind == SK_MK_CIRCLE) {
        ptCount = 20;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec4);

        float angleRange = HMM_AngleDeg(90);
        HMM_Vec2 diff = HMM_Sub(p->pos, p->manifold.circle.origin);
        float startAngle = atan2f(diff.Y, diff.X);
        for (int i = 0; i < ptCount; i++) {
            float angle = startAngle + (i - (ptCount / 2)) * (angleRange / ptCount);
            pts[i].XY = HMM_RotateV2(HMM_V2(p->manifold.circle.radius, 0), angle);
            pts[i].XY = HMM_Add(pts[i].XY, p->manifold.circle.origin);
        }
    } else if (p->manifold.kind == SK_MK_LINE) {
        ptCount = 2;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec4);
        pts[0].XY = p->pos;
        pts[1].XY = HMM_Add(p->pos, HMM_Mul(HMM_Norm(p->manifold.line.direction), 0.4f * scaleFactor));
    } else if (p->manifold.kind == SK_MK_ANY) {
        // make it really big so the cross line is entirely faded out
        ptCount = 4;
        pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec4);
        pts[0].XY = HMM_V2(-1 * scaleFactor, 0);
        pts[1].XY = HMM_V2(1 * scaleFactor, 0);
        pts[2].XY = HMM_V2(0, -1 * scaleFactor);
        pts[3].XY = HMM_V2(0, 1 * scaleFactor);

        for (int i = 0; i < ptCount; i++) {
            pts[i].XY = HMM_RotateV2(pts[i].XY, HMM_AngleDeg(10));
            pts[i].XY = HMM_AddV2(pts[i].XY, p->pos);
        }
    } else if (p->manifold.kind == SK_MK_TWO_POINTS) {
        return;
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
        scaleFactor = 0.2;
    }
    snzr_drawLineFaded(
        pts, ptCount,
        ui_colorAccent, 4,
        mvp, HMM_V3(p->pos.X, p->pos.Y, 0),
        scaleFactor * (1 + sound),
        scaleFactor * (1 + sound));
}

static bool _sku_AABBContainsPt(HMM_Vec2 boxStart, HMM_Vec2 boxEnd, HMM_Vec2 pt) {
    if (boxStart.X < pt.X && boxEnd.X > pt.X) {
        if (boxStart.Y < pt.Y && boxEnd.Y > pt.Y) {
            return true;
        }
    }
    return false;
}

// FIXME: factor out inter, only pass mouse pos
static void _sku_draw(sk_Sketch* sketch, snzu_Interaction* inter, HMM_Mat4 model, HMM_Mat4 sketchMVP, HMM_Mat4 uiMVP, HMM_Vec3 cameraPos, snz_Arena* scratch, float sound, HMM_Vec2 resolution) {
    {  // grid around the cursor
        HMM_Vec3 mousePos = HMM_V3(inter->mousePosGlobal.X, -inter->mousePosGlobal.Y, 0);
        float scaleFactor = HMM_LenV3(HMM_Sub(_sku_mulM4V3(model, mousePos), cameraPos));

        // FIXME: jarring switches in gaplen
        // FIXME: unpleasant clipping into coplanar geometry
        int lineCount = 13;  // FIXME: batch all of these verts into one line
        float lineGap = _sku_gridLineGap(scaleFactor * 2, lineCount);
        HMM_Vec2 origin = inter->mousePosGlobal;  // FIXME: snap to the last origins position instead of 0,0
        if (sketch->originPt != NULL) {
            HMM_Vec2 skOrigin = sketch->originPt->pos;
            skOrigin.Y *= -1;
            origin = HMM_Sub(inter->mousePosGlobal, skOrigin);
        }
        HMM_Vec3 fadeOrigin = HMM_V3(inter->mousePosGlobal.X, inter->mousePosGlobal.Y, 0);
        for (int ax = 0; ax < 2; ax++) {
            float axOffset = fmod(origin.Elements[ax], lineGap);
            for (int i = 0; i < lineCount; i++) {
                float x = (i - (lineCount / 2)) * lineGap;
                x -= axOffset;
                HMM_Vec4 pts[2] = { 0 };
                pts[0].XY = inter->mousePosGlobal;
                pts[1].XY = inter->mousePosGlobal;

                pts[0].XY.Elements[ax] += x;
                pts[0].XY.Elements[!ax] += 1.5 * scaleFactor;
                pts[1].XY.Elements[ax] += x;
                pts[1].XY.Elements[!ax] += -1.5 * scaleFactor;

                // FIXME: have this invert color when behind smth in the scene
                snzr_drawLineFaded(pts, 2, ui_colorAlmostBackground, 1, uiMVP, fadeOrigin, 0, 0.5 * scaleFactor);
            }
        }
    }  // end grid

    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        _sku_drawManifold(p, cameraPos, model, sketchMVP, sound, scratch);
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        _sku_drawConstraint(c, scratch, sketchMVP, sound);
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        HMM_Vec4 points[2] = { 0 };
        points[0].XY = l->p1->pos;
        points[1].XY = l->p2->pos;
        float thickness = HMM_Lerp(ui_lineThickness, l->sel.hoverAnim, ui_lineHoveredThickness);
        HMM_Vec4 color = HMM_LerpV4(ui_colorText, l->sel.selectionAnim, ui_colorAccent);
        snzr_drawLine(points, 2, color, thickness, sketchMVP);
    }

    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        HMM_Vec4 color = HMM_LerpV4(ui_colorText, p->sel.selectionAnim, ui_colorAccent);
        float sizeAnim = p->sel.hoverAnim + p->sel.selectionAnim;
        float size = HMM_Lerp(ui_cornerHalfSize, sizeAnim, ui_cornerHoveredHalfSize);
        ren3d_drawBillboard(
            sketchMVP,
            resolution,
            *ui_cornerTexture,
            color,
            HMM_V3(p->pos.X, p->pos.Y, 0),
            HMM_V2(size, size));

        if (p == sketch->originPt) {
            // FIXME: bad when we move back to origin angle
            bool flip = sketch->originPt == sketch->originLine->p2;
            float ang = sk_angleOfLine(sketch->originLine->p1->pos, sketch->originLine->p2->pos, flip);
            HMM_Mat4 vp = HMM_Rotate_RH(ang, HMM_V3(0, 0, 1));
            vp = HMM_Mul(HMM_Translate(HMM_V3(p->pos.X, p->pos.Y, 0)), vp);
            vp = HMM_Mul(sketchMVP, vp);

            float rad = 0.04 * p->scaleFactor * (1 + (sizeAnim * 0.25));
            HMM_Vec4 pts[3] = { 0 };
            pts[0].XY = HMM_RotateV2(HMM_V2(rad, 0), HMM_AngleDeg(60));
            pts[1].XY = HMM_RotateV2(HMM_V2(rad, 0), HMM_AngleDeg(180));
            pts[2].XY = HMM_RotateV2(HMM_V2(rad, 0), HMM_AngleDeg(-60));

            snzr_drawLine(
                pts,
                sizeof(pts) / sizeof(*pts),
                color,
                SKU_CONSTRAINT_THICKNESS,
                vp);
        }  // end origin pt check
    }  // end pt loop
}  // end draw sketch

// FIXME: separate instance means that focus and clipping doesn't work right :(
// factored out because the logic to get the mouse pos is a little verbose for main_
void sku_endFrameForUIInstance(snzu_Input input, geo_Align align, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseDir) {
    float t = 0;
    bool hit = geo_rayPlaneIntersection(align.pt, align.normal, cameraPos, mouseDir, &t);
    HMM_Vec3 point = HMM_Add(cameraPos, HMM_Mul(mouseDir, t));
    if (!hit || geo_floatLessEqual(t, 0)) {
        point = HMM_V3(100000, 100000, 100000);
    }
    point = HMM_Sub(point, align.pt);
    HMM_Vec3 xAxis = HMM_Cross(align.vertical, align.normal);
    float x = HMM_Dot(point, xAxis);
    float y = HMM_Dot(point, align.vertical);
    input.mousePos = HMM_V2(x, -y);  // flip from sketch space to UI space here

    HMM_Mat4 sketchMVP = HMM_Mul(vp, geo_alignToM4(geo_alignZero(), align));
    HMM_Mat4 uiMVP = HMM_Mul(sketchMVP, HMM_Scale(HMM_V3(1, -1, 1)));

    glDisable(GL_DEPTH_TEST);
    snzu_frameDrawAndGenInteractions(input, uiMVP);
    glEnable(GL_DEPTH_TEST);
}

// FIXME: sketch element selection persists too much
void sku_drawAndBuildSketch(
    sk_Sketch* sketch, geo_Align align,
    HMM_Mat4 vp, HMM_Vec3 cameraPos,
    float sound, HMM_Vec2 resolution,
    snz_Arena* scratch) {
    // We are inverting the y axis because the UI library is built internally in a lot of places to
    // work with down positive coords. It works out if we invert the data it gets fed along with the
    // projection matrix. Very gross but there isn't an obvious better way. Adding a vertical toggle makes
    // many things, notably including build code, much more unwieldy
    HMM_Mat4 model = geo_alignToM4(geo_alignZero(), align);
    HMM_Mat4 sketchMVP = HMM_Mul(vp, model);
    HMM_Mat4 uiMVP = HMM_Mul(sketchMVP, HMM_Scale(HMM_V3(1, -1, 1)));

    snzu_boxNew("sketch ui parent");
    snzu_boxSetStart(HMM_V2(-INFINITY, -INFINITY));
    snzu_boxSetEnd(HMM_V2(INFINITY, INFINITY));
    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);
    // fallthru required because otherwise this captures the mouse on click // i hate it but it it works
    snzu_boxEnter();
    glDisable(GL_DEPTH_TEST);

    bool inLineMode = sc_getActiveCommand() == scc_sketchLineMode;
    bool inMoveMode = sc_getActiveCommand() == scc_sketchMove;
    bool inRotateMode = sc_getActiveCommand() == scc_sketchRotate;
    bool inNonLineTool = inMoveMode || inRotateMode;
    sk_Point* lineSrcPoint = NULL;  // set below. FIXME: gross

    {  // selection // UI state updates
        HMM_Vec2 mouse = inter->mousePosGlobal;
        mouse.Y *= -1;  // interaction mouse pos in UI space, this is in sketch space
        // FIXME: what happens on the border of mouse not being projectable??

        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "region");
        snzu_Action regionAct = inter->mouseActions[SNZU_MB_LEFT];

        ui_SelectionStatus* firstStatus = NULL;
        {  // loop over all elems and make the list of selection statuses
            bool anyPointHovered = false;

            // FIXME: make contains check precise, not per vert
            HMM_Vec2 start = HMM_V2(0, 0);
            HMM_Vec2 end = HMM_V2(0, 0);
            start.X = SNZ_MIN(mouse.X, region->dragOrigin.X);
            start.Y = SNZ_MIN(mouse.Y, region->dragOrigin.Y);
            end.X = SNZ_MAX(mouse.X, region->dragOrigin.X);
            end.Y = SNZ_MAX(mouse.Y, region->dragOrigin.Y);
            for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                p->inDragZone = false;
                if (_sku_AABBContainsPt(start, end, p->pos)) {
                    p->inDragZone = true;
                }

                HMM_Vec3 transformed = HMM_MulM4V4(model, HMM_V4(p->pos.X, p->pos.Y, 0, 1)).XYZ;
                float scaleFactor = HMM_Len(HMM_Sub(cameraPos, transformed));
                p->scaleFactor = scaleFactor;
                bool hovered = HMM_Len(HMM_Sub(mouse, p->pos)) < (0.02 * scaleFactor);
                anyPointHovered |= hovered;
                if (inNonLineTool) {
                    hovered = false;
                }

                ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
                *status = (ui_SelectionStatus){
                    .state = &p->sel,
                    .hovered = hovered,
                    .withinDragZone = p->inDragZone,
                    .next = firstStatus,
                };
                firstStatus = status;
            }  // end point loop

            for (sk_Line* l = sketch->firstLine; l; l = l->next) {
                bool withinDragZone = l->p1->inDragZone && l->p2->inDragZone;

                HMM_Vec2 midpt = HMM_DivV2F(HMM_Add(l->p1->pos, l->p2->pos), 2.0f);
                HMM_Vec3 transformedCenter = HMM_MulM4V4(model, HMM_V4(midpt.X, midpt.Y, 0, 1)).XYZ;
                float distToCamera = HMM_Len(HMM_Sub(transformedCenter, cameraPos));
                bool hovered = _sku_lineContainsPt(l->p1->pos, l->p2->pos, 0.01 * distToCamera, mouse);
                if (anyPointHovered || inLineMode || inNonLineTool) {
                    hovered = false;
                }

                ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
                *status = (ui_SelectionStatus){
                    .state = &l->sel,
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

                // FIXME: multiple elems are animated as if clickable when only one is, pick a side please
                bool hovered = _sku_constraintHovered(c, scaleFactor, visualCenter, mouse);
                // NOTE: cancelling hover here means that even though these statuses are in the set to update
                // when in line mode, they will never be able to capture the mouse or be selected.
                if (anyPointHovered || inLineMode || inNonLineTool) {
                    hovered = false;
                }

                if (hovered) {
                    if (inter->doubleClicked) {
                        c->uiInfo.shouldStartFocus = true;  // this is reset in build, so that signals from shortcuts make it
                    }
                }

                bool withinDragZone = false;
                if (c->kind == SK_CK_ANGLE) {
                    sk_Point* base1 = c->flipLine1 ? c->line1->p2 : c->line1->p1;
                    sk_Point* base2 = c->flipLine2 ? c->line2->p2 : c->line2->p1;
                    withinDragZone = base1 == base2 && base1->inDragZone;
                } else if (c->kind == SK_CK_DISTANCE) {
                    withinDragZone = c->line1->p1->inDragZone && c->line1->p2->inDragZone;
                }

                ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
                *status = (ui_SelectionStatus){
                    .state = &c->uiInfo.sel,
                    .hovered = hovered,
                    .withinDragZone = withinDragZone,
                    .next = firstStatus,
                };
                firstStatus = status;
            }
        }

        { // update statuses for everything in the sketch
            if (inNonLineTool) {
                ui_selectionRegionUpdateIgnoreMouse(region, firstStatus);
            } else {
                ui_selectionRegionUpdate(region, firstStatus, regionAct, mouse, inter->keyMods & KMOD_SHIFT, !inLineMode, false);
            }
            ui_selectionRegionAnimate(region, firstStatus);
        }

        { // handle logic for each tool mode
            if (inLineMode) {
                { // make sure no invalid selections can linger into line mode
                    // FIXME: this number of loops happening ev frame gonna be a perf issue?
                    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
                        l->sel.selected = false;
                    }

                    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
                        c->uiInfo.sel.selected = false;
                    }
                }

                sk_Point** const lastPt = SNZU_USE_MEM(sk_Point*, "lastPt");

                int selectedCount = 0;
                sk_Point* newSel = NULL;
                for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                    if (p->sel.selected) {
                        selectedCount++;
                        if (p != *lastPt) {
                            newSel = p;
                        }
                    }
                }
                sk_sketchDeselectAll(sketch);

                if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
                    if (!*lastPt && !newSel) {
                        HMM_Vec2 mousePos = inter->mousePosGlobal;
                        mousePos.Y *= -1;
                        sk_Point* p = sk_sketchAddPoint(sketch, mousePos);
                        *lastPt = p;
                    } else if (!newSel && *lastPt) {
                        HMM_Vec2 mousePos = inter->mousePosGlobal;
                        mousePos.Y *= -1;
                        sk_Point* p = sk_sketchAddPoint(sketch, mousePos);
                        sk_sketchAddLine(sketch, p, *lastPt);
                        *lastPt = p;
                    } else if (newSel && *lastPt) {
                        sk_sketchAddLine(sketch, *lastPt, newSel);
                        *lastPt = newSel;
                    } else {
                        *lastPt = newSel;
                    }
                }

                if (*lastPt) {
                    (*lastPt)->sel.selected = true;
                }

                lineSrcPoint = *lastPt;
            }  // end line mode logic
            else if (inMoveMode) {
                HMM_Vec2* const prevMouse = SNZU_USE_MEM(HMM_Vec2, "prev mouse");
                if (snzu_useMemIsPrevNew()) {
                    *prevMouse = mouse;
                }
                HMM_Vec2 diff = HMM_Sub(mouse, *prevMouse);
                *prevMouse = mouse;

                // FIXME: selecting a line should select the base pts also?? but then deletion is weird???
                for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                    if (p->sel.selected) {
                        p->pos = HMM_Add(p->pos, diff);
                    }
                }
                if (regionAct == SNZU_ACT_DOWN) {
                    sc_cancelActiveCommand();
                    sk_sketchDeselectAll(sketch);
                }
                // FIXME: doing this by diff instead of abs feels sluggish
            } else if (inRotateMode) {
                HMM_Vec2* const mouseSrc = SNZU_USE_MEM(HMM_Vec2, "mouse src"); // FIXME: mark this on screen somehow or else default it to the center
                if (snzu_useMemIsPrevNew()) {
                    *mouseSrc = mouse;
                }
                float* const prevAngle = SNZU_USE_MEM(float, "prevAngle");
                HMM_Vec2 diff = HMM_Sub(mouse, *mouseSrc);
                float angle = atan2f(diff.Y, diff.X);
                if (snzu_useMemIsPrevNew()) {
                    *prevAngle = angle;
                }

                float angleDiff = geo_normalizeAngle(angle - *prevAngle);
                *prevAngle = angle;

                HMM_Vec2 center = HMM_V2(0, 0);
                int count = 0;
                for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                    if (p->sel.selected) {
                        center = HMM_Add(center, p->pos);
                        count++;
                    }
                }
                center = HMM_DivV2F(center, (float)count);

                for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
                    if (p->sel.selected) {
                        HMM_Vec2 nPos = HMM_RotateV2(HMM_Sub(p->pos, center), angleDiff);
                        p->pos = HMM_Add(nPos, center);
                    }
                }

                if (sketch->originLine->p1->sel.selected && sketch->originLine->p2->sel.selected) {
                    sketch->originAngle += angleDiff;
                }

                if (regionAct == SNZU_ACT_DOWN) {
                    sc_cancelActiveCommand();
                    sk_sketchDeselectAll(sketch);
                }
            }  // end rotate mode logic
        }

        if (region->dragging) {
            snzr_drawRect(region->dragOrigin, mouse,
                          HMM_V2(-100000, -100000), HMM_V2(100000, 100000),
                          ui_colorTransparentAccent,
                          0, 0, HMM_V4(0, 0, 0, 0),
                          sketchMVP, _snzr_globs.solidTex);  // FIXME: internals should not be hacked at like this
        }
    }  // end selection management scope

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        _sku_buildConstraint(c, sound, model, cameraPos, scratch);
    }

    if (inLineMode && lineSrcPoint) {
        HMM_Vec2 mousePos = inter->mousePosGlobal;
        mousePos.Y *= -1;
        HMM_Vec4 pts[2] = { 0 };
        pts[0].XY = lineSrcPoint->pos;
        pts[1].XY = mousePos;
        snzr_drawLine(pts, 2, ui_colorTransparentAccent, ui_lineHoveredThickness, sketchMVP);
    }

    _sku_draw(
        sketch,
        inter,
        model, sketchMVP, uiMVP,
        cameraPos,
        scratch,
        sound,
        resolution);

    snzu_boxExit();  // exit main parent

    glEnable(GL_DEPTH_TEST);
}