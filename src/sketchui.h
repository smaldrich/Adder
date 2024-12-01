#include "HMM/HandmadeMath.h"
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

typedef struct {
    HMM_Vec3 startPt;
    HMM_Vec3 startNormal;
    HMM_Vec3 startVertical;

    HMM_Vec3 endPt;
    HMM_Vec3 endNormal;
    HMM_Vec3 endVertical;
} sku_Align;

static float _sku_angleBetweenV3(HMM_Vec3 a, HMM_Vec3 b) {
    return acosf(HMM_Dot(a, b) / (HMM_Len(a) * HMM_Len(b)));
}

HMM_Quat sku_alignToQuat(sku_Align a) {
    HMM_Vec3 normalCross = HMM_Cross(a.startNormal, a.endNormal);
    float normalAngle = _sku_angleBetweenV3(a.startNormal, a.endNormal);
    HMM_Quat planeRotate = HMM_QFromAxisAngle_RH(normalCross, normalAngle);
    if (csg_floatEqual(normalAngle, 0)) {
        planeRotate = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), 0);
    }

    HMM_Vec3 postRotateVertical = HMM_MulM4V4(HMM_QToM4(planeRotate), HMM_V4(a.startVertical.X, a.startVertical.Y, a.startVertical.Z, 1)).XYZ;
    // stolen: https://stackoverflow.com/questions/5188561/signed-angle-between-two-3d-vectors-with-same-origin-within-the-same-plane
    // tysm internet
    float y = HMM_Dot(HMM_Cross(postRotateVertical, a.endVertical), a.endNormal);
    float x = HMM_Dot(postRotateVertical, a.endVertical);
    float postRotateAngle = atan2(y, x);
    HMM_Quat postRotate = HMM_QFromAxisAngle_RH(a.endNormal, postRotateAngle);

    return HMM_MulQ(postRotate, planeRotate);
}

HMM_Mat4 sku_alignToM4(sku_Align a) {
    HMM_Quat q = sku_alignToQuat(a);
    HMM_Mat4 translate = HMM_Translate(HMM_Sub(a.endPt, a.startPt));
    return HMM_Mul(translate, HMM_QToM4(q));
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
    } // force angle range to go the correct way around based on the sign of the constraint value
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
        if (fabsf(_sku_normalizeAngle(midAngle - mouseAngle)) > halfRange) {
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

// modifies constraint value based on user input
static void _sku_drawAndBuildConstraint(sk_Constraint* c, HMM_Mat4 model, HMM_Vec3 cameraPos, snz_Arena* scratch, HMM_Mat4 sketchMVP, float soundPct) {
    HMM_Vec4 drawnColor = ui_colorText;
    if (c->violated) {
        drawnColor = ui_colorErr;
    }
    drawnColor = HMM_LerpV4(drawnColor, c->uiInfo.selectionAnim, ui_colorAccent);

    HMM_Vec2 visualCenter = HMM_V2(0, 0);
    float scaleFactor = 1;
    _sku_constraintScaleFactorAndCenter(c, model, cameraPos, &visualCenter, &scaleFactor);

    float drawnThickness = HMM_Lerp(SKU_CONSTRAINT_THICKNESS, c->uiInfo.hoverAnim, SKU_CONSTRAINT_HOVERED_THICKNESS);

    HMM_Vec2 textTopLeft = HMM_V2(0, 0); // TL of the label in sketch space

    if (c->kind == SK_CK_DISTANCE) {
        HMM_Vec2 p1 = c->line1->p1->pos;
        HMM_Vec2 p2 = c->line1->p2->pos;
        HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
        HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), SKU_DISTANCE_CONSTRAINT_OFFSET * (1 + soundPct) * scaleFactor);
        p1 = HMM_Add(p1, offset);
        p2 = HMM_Add(p2, offset);
        HMM_Vec2 points[] = { p1, p2 };
        snzr_drawLine(points, 2, drawnColor, drawnThickness, sketchMVP);

        textTopLeft = HMM_Add(visualCenter, HMM_Mul(offset, 2.0f));
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
        if (csg_floatEqual(fabsf(c->value), HMM_AngleDeg(90))) {
            sk_Point* otherOnLine1 = (c->line1->p1 == joint) ? c->line1->p2 : c->line1->p1;
            sk_Point* otherOnLine2 = (c->line2->p1 == joint) ? c->line2->p2 : c->line2->p1;
            HMM_Vec2 offset1 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine1->pos, joint->pos)), offset);
            HMM_Vec2 offset2 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine2->pos, joint->pos)), offset);
            HMM_Vec2 pts[] = {
                HMM_Add(joint->pos, offset1),
                HMM_Add(joint->pos, HMM_Add(offset1, offset2)),
                HMM_Add(joint->pos, offset2),
            };
            snzr_drawLine(pts, 3, drawnColor, drawnThickness, sketchMVP);
            return;
        } else {
            HMM_Vec2 angles = _sku_angleOfLinesInAngleConstraint(c);
            float startAngle = angles.X;
            float angleRange = _sku_angleDifferenceForConstraint(c, angles.Left, angles.Right);

            int ptCount = (int)(fabsf(angleRange) / HMM_AngleDeg(10)) + 1;
            HMM_Vec2* linePts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);
            for (int i = 0; i < ptCount; i++) {
                HMM_Vec2 o = HMM_RotateV2(HMM_V2(offset, 0), startAngle + (i * c->value / (ptCount - 1)));
                linePts[i] = HMM_Add(joint->pos, o);
            }
            snzr_drawLine(linePts, ptCount, drawnColor, drawnThickness, sketchMVP);

            textTopLeft = HMM_RotateV2(HMM_V2(offset * 1.5, 0), startAngle + angleRange / 2);
            textTopLeft = HMM_Add(textTopLeft, visualCenter);
        }
    }

    float drawnHeight = SKU_LABEL_SIZE * scaleFactor;

    char* boxName = snz_arenaFormatStr(scratch, "%p", c);
    snzu_boxNew(boxName);
    textTopLeft.Y *= -1; // flip to UI space before drawing
    snzu_boxSetStart(textTopLeft);

    // FIXME: double click on the rest of the constraint should also enter edit mode
    ui_textArea(&c->uiInfo.textArea, &ui_titleFont, drawnHeight, drawnColor);

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

    if (!csg_floatZero(val)) {
        c->value = val; // FIXME: when this turns into a 90deg angle, the text box immediately disappears
    }

    if (!c->uiInfo.textArea.wasFocused) { // FIXME: kinda wasteful to have this running constantly
        const char* suffix = NULL;
        if (c->kind == SK_CK_ANGLE) {
            suffix = "deg"; // FIXME: unicode + the degree symol
        } else if (c->kind == SK_CK_DISTANCE) {
            suffix = "m";
        }
        float renderedVal = c->value;
        if (c->kind == SK_CK_ANGLE) {
            renderedVal = HMM_ToDeg(renderedVal);
        }
        // FIXME: cull trailing zeros
        const char* str = snz_arenaFormatStr(scratch, "%.2f%s", renderedVal, suffix);
        strcpy(c->uiInfo.textArea.chars, str);
        c->uiInfo.textArea.charCount = strlen(str);
        c->uiInfo.textArea.cursorPos = SNZ_MIN(c->uiInfo.textArea.cursorPos, c->uiInfo.textArea.charCount);
        _ui_textAreaAssertValid(&c->uiInfo.textArea);
        // FIXME: setter for textArea string;
        // FIXME: this has a one frame delay on scene open before things have text. ig its fine but not ideal.
    }

    // FIXME: flip labels if camera is on the other side
    // FIXME: less self intersection on angled lines
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
        scaleFactor = 0.07;
    }
    snzr_drawLineFaded(
        pts, ptCount,
        ui_colorAccent, 4,
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

// Expects a UI instance that isn't the main one to be selected, for use exclusively here
// this fn will do frameStart/end calls for the instance
// FIXME: that is disgusting and way to subtle, the instance variable should only be here, but then there are problems w/ not freeing usemems, etc.
// so well see. Also not portable at all, but this doesn't seem like the kind of thing that is happening more than once.
// Inputs should be inputs that are being fed to the box this instance is being drawn on. mouse coords are ignored because
// everythin in the sketch has been projected.

// FIXME: passing inputs like this is ignoring clipping interactions that are outside the viewport
void sku_drawSketch(
    sk_Sketch* sketch, sku_Align align,
    HMM_Mat4 vp, HMM_Vec3 cameraPos,
    HMM_Vec3 mouseRayNormal, snzu_Input inputs,
    float sound, float dt,
    snz_Arena* scratch) {

    // We are inverting the y axis because the UI library is built internally in a lot of places to
    // work with down positive coords. It works out if we invert the data it gets fed along with the
    // projection matrix. Very gross but there isn't an obvious better way. Adding a vertical toggle makes
    // many things, notably including build code, much more unwieldy
    HMM_Mat4 model = sku_alignToM4(align);
    HMM_Mat4 sketchMVP = HMM_Mul(vp, model);
    HMM_Mat4 uiMVP = HMM_Mul(sketchMVP, HMM_Scale(HMM_V3(1, -1, 1)));

    {
        float t = 0;
        bool hit = csg_planeLineIntersection(align.endPt, align.endNormal, cameraPos, mouseRayNormal, &t);
        HMM_Vec3 point = HMM_Add(cameraPos, HMM_Mul(mouseRayNormal, t));
        if (!hit) {
            point = HMM_V3(INFINITY, INFINITY, INFINITY);
        }
        point = HMM_Sub(point, align.endPt);
        HMM_Vec3 xAxis = HMM_Cross(align.endVertical, align.endNormal);
        float x = HMM_Dot(point, xAxis);
        float y = HMM_Dot(point, align.endVertical);
        inputs.mousePos = HMM_V2(x, -y); // flip from sketch space to UI space here
    }

    snzu_frameStart(scratch, HMM_V2(0, 0), dt);
    snzu_boxNew("sketch ui parent");
    snzu_boxSetStart(HMM_V2(-INFINITY, -INFINITY));
    snzu_boxSetEnd(HMM_V2(INFINITY, INFINITY));
    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL | SNZU_IF_ALLOW_EVENT_FALLTHROUGH);
    // fallthru required because otherwise this captures the mouse on click // i hate it but it it works
    snzu_boxEnter();

    glDisable(GL_DEPTH_TEST);

    {  // grid around the cursor
        HMM_Vec3 mousePos = HMM_V3(inter->mousePosGlobal.X, inter->mousePosGlobal.Y, 0);
        float scaleFactor = HMM_LenV3(HMM_Sub(_sku_mulM4V3(model, mousePos), cameraPos));

        // FIXME: jarring switches in gaplen
        // FIXME: unpleasant clipping into coplanar geometry
        int lineCount = 13;  // FIXME: batch all of these verts into one line
        float lineGap = _sku_gridLineGap(scaleFactor * 2, lineCount);
        HMM_Vec2 origin = inter->mousePosGlobal; // FIXME: snap to the last origins position instead of 0,0
        if (sketch->originPt != NULL) {
            origin = HMM_Sub(inter->mousePosGlobal, sketch->originPt->pos);
        }
        for (int ax = 0; ax < 2; ax++) {
            float axOffset = fmod(origin.Elements[ax], lineGap);
            for (int i = 0; i < lineCount; i++) {
                float x = (i - (lineCount / 2)) * lineGap;
                x -= axOffset;
                HMM_Vec2 pts[] = { inter->mousePosGlobal, inter->mousePosGlobal };
                pts[0].Elements[ax] += x;
                pts[0].Elements[!ax] += 1.5 * scaleFactor;
                pts[1].Elements[ax] += x;
                pts[1].Elements[!ax] += -1.5 * scaleFactor;

                HMM_Vec3 fadeOrigin = { 0 };
                fadeOrigin.XY = inter->mousePosGlobal;
                // FIXME: have this invert color when behind smth in the scene
                snzr_drawLineFaded(pts, 2, ui_colorAlmostBackground, 1, uiMVP, fadeOrigin, 0, 0.5 * scaleFactor);
            }
        }
    }  // end grid

    {  // selection // UI state updates
        HMM_Vec2 mouse = inter->mousePosGlobal;
        mouse.Y *= -1; // interaction mouse pos in UI space, this is in sketch space

        bool shiftPressed = SNZU_USE_MEM(bool, "shiftPressed");
        {
            if (!snzu_isNothingFocused()) {
                shiftPressed = false;
            } else {
                shiftPressed = inter->keyMods & KMOD_SHIFT;
            }
        }

        // doing this instead of snzu_Interaction.dragBeginning bc. mouse pos is projected
        // in sketch coords (up is Y+)
        HMM_Vec2* const dragOrigin = SNZU_USE_MEM(HMM_Vec2, "dragOrigin");
        bool* const dragging = SNZU_USE_MEM(bool, "dragging");
        bool dragEnded = *dragging && inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP;

        // NOTE: for this to work properly, any box inside of the container needs it's fallthrough flag set.
        // well see if that pans out. Also it's grossly hard to find.
        bool mouseDown = inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN;

        if (mouseDown) {
            *dragOrigin = mouse;
            *dragging = true;
            // FIXME: what happens on the border of mouse not being projectable??
        } else if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_NONE || inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP) {
            *dragging = false;
        }

        {  // loop over all points, check whether they are within the contained zone and update their selection status
            // FIXME: kinda wasteful right now to have points contain full UIInfo structs, but once pt selection and drawing gets added maybe not
            // FIXME: this will also have to change to another variable that isn't selected once that happens, cause selected is persisted
            HMM_Vec2 start = HMM_V2(0, 0);
            HMM_Vec2 end = HMM_V2(0, 0);
            start.X = SNZ_MIN(mouse.X, dragOrigin->X);
            start.Y = SNZ_MIN(mouse.Y, dragOrigin->Y);
            end.X = SNZ_MAX(mouse.X, dragOrigin->X);
            end.Y = SNZ_MAX(mouse.Y, dragOrigin->Y);
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
                bool hovered = _sku_lineContainsPt(l->p1->pos, l->p2->pos, 0.01 * distToCamera, mouse);

                if (mouseDown && hovered) {
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

                bool hovered = _sku_constraintHovered(c, scaleFactor, visualCenter, mouse);

                if (mouseDown && hovered) {
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
            if (!snzu_isNothingFocused()) {
                status->eltUIInfo->selected = false;
            }

            if (mouseDown) {
                if (!shiftPressed && !status->hovered && !status->withinDragZone) {
                    status->eltUIInfo->selected = false;
                }
                if (status->hovered) {
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
            HMM_Vec4 color = ui_colorAccent;
            color.A = 0.2;
            snzr_drawRect(*dragOrigin, mouse,
                          HMM_V2(-100000, -100000), HMM_V2(100000, 100000),
                          color,
                          0, 0, HMM_V4(0, 0, 0, 0),
                          sketchMVP, _snzr_globs.solidTex);  // FIXME: internals should not be hacked at like this
        }
    }  // end selection management scope

    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        _sku_drawManifold(p, cameraPos, model, sketchMVP, scratch);
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        _sku_drawAndBuildConstraint(c, model, cameraPos, scratch, sketchMVP, sound);
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        HMM_Vec2 points[] = {
            l->p1->pos,
            l->p2->pos,
        };
        float thickness = HMM_Lerp(2.0f, l->uiInfo.hoverAnim, 5.0f);
        HMM_Vec4 color = HMM_LerpV4(ui_colorText, l->uiInfo.selectionAnim, ui_colorAccent);
        snzr_drawLine(points, 2, color, thickness, sketchMVP);
    }

    snzu_boxExit(); // exit main parent
    snzu_frameDrawAndGenInteractions(inputs, uiMVP);

    glEnable(GL_DEPTH_TEST);
}