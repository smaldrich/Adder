#include "PoolAlloc.h"
#include "csg.h"
#include "docs.h"
#include "render3d.h"
#include "serialization2.h"
#include "sketches2.h"
#include "snooze.h"
#include "stb/stb_image.h"
#include "ui.h"

snz_Arena fontArena;

snzr_FrameBuffer sceneFB;
sk_Sketch sketch;
snz_Arena sketchArena;

ren3d_Mesh mesh;

typedef struct {
    HMM_Vec3 startPt;
    HMM_Vec3 startNormal;
    HMM_Vec3 startVertical;

    HMM_Vec3 endPt;
    HMM_Vec3 endNormal;
    HMM_Vec3 endVertical;
} main_Align;
main_Align sketchAlign;

float main_angleBetweenV3(HMM_Vec3 a, HMM_Vec3 b) {
    return acosf(HMM_Dot(a, b) / (HMM_Len(a) * HMM_Len(b)));
}

HMM_Quat main_alignToQuat(main_Align a) {
    HMM_Vec3 normalCross = HMM_Cross(a.startNormal, a.endNormal);
    float normalAngle = main_angleBetweenV3(a.startNormal, a.endNormal);
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

HMM_Mat4 main_alignToM4(main_Align a) {
    HMM_Quat q = main_alignToQuat(a);
    HMM_Mat4 translate = HMM_Translate(HMM_Sub(a.endPt, a.startPt));
    return HMM_Mul(translate, HMM_QToM4(q));
}

void main_init(snz_Arena* scratch) {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    csg_tests();

    fontArena = snz_arenaInit(10000000, "main font arena");
    sketchArena = snz_arenaInit(10000000, "main sketch arena");

    ui_titleFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-Regular.ttf", 48);
    ui_paragraphFont = snzr_fontInit(&fontArena, scratch, "res/fonts/OpenSans-Light.ttf", 16);
    ui_labelFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-LightItalic.ttf", 20);

    ren3d_init(scratch);
    docs_init();

    snz_arenaClear(scratch);

    sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(500, 500, NULL));

    {
        csg_TriList cubeA = csg_cube(scratch);
        csg_TriList cubeB = csg_cube(scratch);
        csg_triListTransform(&cubeB, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
        csg_triListTransform(&cubeB, HMM_Translate(HMM_V3(1, 1, 1)));

        csg_BSPNode* treeA = csg_triListToBSP(&cubeA, scratch);
        csg_BSPNode* treeB = csg_triListToBSP(&cubeB, scratch);

        csg_TriList* aClipped = csg_bspClipTris(true, &cubeA, treeB, scratch);
        csg_TriList* bClipped = csg_bspClipTris(true, &cubeB, treeA, scratch);
        csg_TriList* final = csg_triListJoin(aClipped, bClipped);
        csg_triListRecoverNonBroken(&final, scratch);

        PoolAlloc pool = poolAllocInit();
        ren3d_Vert* verts = poolAllocAlloc(&pool, 0);
        int64_t vertCount = 0;

        int triIdx = 0;
        for (csg_TriListNode* tri = final->first; tri; tri = tri->next) {
            HMM_Vec3 triNormal = csg_triNormal(tri->a, tri->b, tri->c);
            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(&pool, verts, vertCount, ren3d_Vert) = (ren3d_Vert){
                    .pos = tri->elems[i],
                    .normal = triNormal,
                };
            }
            if (triIdx == 7) {
                sketchAlign = (main_Align){
                    .startPt = HMM_V3(0, 0, 0),
                    .startNormal = HMM_V3(0, 0, 1),
                    .startVertical = HMM_V3(0, 1, 0),

                    .endPt = tri->a,
                    .endNormal = triNormal,
                    .endVertical = HMM_Norm(HMM_Sub(tri->b, tri->a)),
                };
            }
            triIdx++;
        }
        mesh = ren3d_meshInit(verts, vertCount);
        poolAllocDeinit(&pool);
    }  // end mesh for testing

    {
        sketch = sk_sketchInit();
        sk_Point* originPt = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(0, 0));
        sk_Point* left = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(-1, -1));
        sk_Point* right = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(1, 0));
        sk_Point* up = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(0, 1));

        sk_Line* vertical = sk_sketchAddLine(&sketch, &sketchArena, originPt, up);
        sk_sketchAddConstraintDistance(&sketch, &sketchArena, vertical, 0.5);

        sk_Line* leftLine = sk_sketchAddLine(&sketch, &sketchArena, left, originPt);
        sk_sketchAddConstraintAngle(&sketch, &sketchArena, vertical, false, leftLine, true, HMM_AngleDeg(90));
        sk_sketchAddConstraintDistance(&sketch, &sketchArena, leftLine, 0.8);
        sk_Line* rightLine = sk_sketchAddLine(&sketch, &sketchArena, originPt, right);
        sk_sketchAddConstraintDistance(&sketch, &sketchArena, rightLine, 1);
        sk_sketchAddConstraintAngle(&sketch, &sketchArena, rightLine, false, vertical, false, HMM_AngleDeg(120));

        sk_Point* other = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(-1, 1));
        sk_Line* l = sk_sketchAddLine(&sketch, &sketchArena, left, other);
        sk_sketchAddConstraintAngle(&sketch, &sketchArena, l, false, leftLine, false, HMM_AngleDeg(-120));
        // sk_sketchAddConstraintDistance(&sketch, &sketchArena, l, 0.2);

        sk_sketchAddLine(&sketch, &sketchArena, up, right);

        sk_sketchSolve(&sketch, originPt, vertical, HMM_AngleDeg(90));
    }
}

// returns the normal of the ray starting at cameraPos
HMM_Vec3 main_rayFromCamera(float fov, HMM_Mat4 cameraTransform, HMM_Vec2 mousePosPx, HMM_Vec2 resolution) {
    HMM_Vec2 mousePosProportional = HMM_MulV2F(HMM_DivV2(mousePosPx, resolution), 2);
    mousePosProportional = HMM_Sub(mousePosProportional, HMM_V2(1, 1));

    float aspect = resolution.X / resolution.Y;
    float halfHeight = tanf(fov / 2);
    float halfWidth = halfHeight * aspect;

    HMM_Vec3 v = HMM_V3(halfWidth * mousePosProportional.X, halfHeight * -mousePosProportional.Y, -1);
    v = HMM_MulM4V4(cameraTransform, HMM_V4(v.X, v.Y, v.Z, 0)).XYZ;
    return HMM_Norm(v);
}

float main_gridLineGap(float area, float visibleCount) {
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

HMM_Vec3 main_MulM4V3(HMM_Mat4 m, HMM_Vec3 v) {
    return HMM_Mul(m, HMM_V4(v.X, v.Y, v.Z, 1)).XYZ;
}

void main_drawSketch(HMM_Mat4 vp, HMM_Mat4 model, snz_Arena* scratch, HMM_Vec3 cameraPos, HMM_Vec2 mousePosInPlane) {
    HMM_Mat4 mvp = HMM_Mul(vp, model);

    glDisable(GL_DEPTH_TEST);

    { // grid around the cursor
        HMM_Vec3 mousePos = HMM_V3(mousePosInPlane.X, mousePosInPlane.Y, 0);
        float scaleFactor = HMM_LenV3(HMM_Sub(main_MulM4V3(model, mousePos), cameraPos));

        // FIXME: jarring switches in gaplen
        int lineCount = 13; // FIXME: batch all of these verts into one line
        float lineGap = main_gridLineGap(scaleFactor * 1.5, lineCount);
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
                snzr_drawLineFaded(pts, 2, HMM_V4(0.9, 0.9, 0.9, 1), 1, mvp, fadeOrigin, 0, 0.5 * scaleFactor);
            }
        }
    } // end grid

    // MANIFOLDS
    for (sk_Point* p = sketch.firstPoint; p; p = p->next) {
        sk_ManifoldKind kind = p->manifold.kind;

        HMM_Vec2* pts = NULL;
        int ptCount = 0;

        float distToCamera = HMM_Len(HMM_Sub(cameraPos, main_MulM4V3(model, HMM_V3(p->pos.X, p->pos.Y, 0))));
        float scaleFactor = distToCamera; // FIXME: ortho switch

        if (kind == SK_MK_POINT) {
            continue;
        } else if (kind == SK_MK_CIRCLE) {
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
        } else if (kind == SK_MK_LINE) {
            ptCount = 2;
            HMM_Vec2 ptsArr[2] = {
                p->pos,
                HMM_Add(p->pos, HMM_Mul(HMM_Norm(p->manifold.line.direction), 0.4f * scaleFactor)),
            };
            pts = ptsArr;
        } else if (kind == SK_MK_ANY) {
            // make it really big so the cross line is entirely faded out
            HMM_Vec2 ptsArr[4] = {
                HMM_V2(-1 * scaleFactor, 0),
                HMM_V2(1 * scaleFactor, 0),
                HMM_V2(0, -1 * scaleFactor),
                HMM_V2(0, 1 * scaleFactor),
            };
            ptCount = 4;
            pts = ptsArr;
            for (int i = 0; i < ptCount; i++) {
                pts[i] = HMM_RotateV2(pts[i], HMM_AngleDeg(10));
                pts[i] = HMM_AddV2(pts[i], p->pos);
            }
        } else {
            SNZ_ASSERTF(false, "unreachable. kind was: %d", kind);
        }

        if (kind != SK_MK_CIRCLE) {
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

    // Drawing actual constraints
    for (sk_Constraint* c = sketch.firstConstraint; c; c = c->nextAllocated) {
        HMM_Vec4 color = UI_TEXT_COLOR;
        if (c->violated) {
            color = UI_RED;
        }

        HMM_Vec2 visualCenter = HMM_V2(0, 0);
        float scaleFactor = 0;
        {
            if (c->kind == SK_CK_ANGLE) {
                visualCenter = c->flipLine1 ? c->line1->p2->pos : c->line1->p1->pos;
            } else if (c->kind == SK_CK_DISTANCE) {
                visualCenter = HMM_DivV2F(HMM_Add(c->line1->p1->pos, c->line1->p2->pos), 2);
            } else {
                SNZ_ASSERTF(false, "unreachable case. kind: %d", c->kind);
            }
            HMM_Vec3 transformedCenter = main_MulM4V3(model, HMM_V3(visualCenter.X, visualCenter.Y, 0));
            scaleFactor = HMM_Len(HMM_Sub(cameraPos, transformedCenter));
        }
        float angleConstraintVisualOffset = 0.05 * scaleFactor;
        float distConstraintVisualOffset = 0.025 * scaleFactor;

        if (c->kind == SK_CK_DISTANCE) {
            HMM_Vec2 p1 = c->line1->p1->pos;
            HMM_Vec2 p2 = c->line1->p2->pos;
            HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
            HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), distConstraintVisualOffset);
            p1 = HMM_Add(p1, offset);
            p2 = HMM_Add(p2, offset);
            HMM_Vec2 points[] = { p1, p2 };
            snzr_drawLine(points, 2, color, 4, mvp);

            float drawnHeight = 0.04 * scaleFactor;
            const char* str = snz_arenaFormatStr(scratch, "%.2fm", c->value);
            HMM_Vec2 start = HMM_Add(visualCenter, HMM_Mul(offset, 2.0f));
            start.Y -= drawnHeight / 2;
            // FIXME: move labels if camera is on the other side
            // FIXME: less self intersection on angled lines
            snzr_drawTextScaled(
                start,
                HMM_V2(-100000, -100000), HMM_V2(100000, 100000),
                UI_TEXT_COLOR, str, strlen(str), ui_titleFont,
                mvp, drawnHeight, false, true);

        } else if (c->kind == SK_CK_ANGLE) {
            sk_Point* joint = NULL;
            if (c->line1->p1 == c->line2->p1 || c->line1->p1 == c->line2->p2) {
                joint = c->line1->p1;
            } else if (c->line1->p2 == c->line2->p1 || c->line1->p2 == c->line2->p2) {
                joint = c->line1->p2;
            } else {
                continue;
            }

            if (csg_floatEqual(c->value, HMM_AngleDeg(90))) {
                sk_Point* otherOnLine1 = (c->line1->p1 == joint) ? c->line1->p2 : c->line1->p1;
                sk_Point* otherOnLine2 = (c->line2->p1 == joint) ? c->line2->p2 : c->line2->p1;
                HMM_Vec2 offset1 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine1->pos, joint->pos)), angleConstraintVisualOffset);
                HMM_Vec2 offset2 = HMM_Mul(HMM_Norm(HMM_Sub(otherOnLine2->pos, joint->pos)), angleConstraintVisualOffset);
                HMM_Vec2 pts[] = {
                    HMM_Add(joint->pos, offset1),
                    HMM_Add(joint->pos, HMM_Add(offset1, offset2)),
                    HMM_Add(joint->pos, offset2),
                };
                snzr_drawLine(pts, 3, color, 4, mvp);
            } else {
                sk_Point* otherOnLine1 = (c->line1->p1 == joint) ? c->line1->p2 : c->line1->p1;
                HMM_Vec2 diff = HMM_Sub(otherOnLine1->pos, joint->pos);
                float startAngle = atan2f(diff.Y, diff.X);
                int ptCount = (int)(fabsf(c->value) / HMM_AngleDeg(10)) + 1;
                HMM_Vec2* linePts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);
                for (int i = 0; i < ptCount; i++) {
                    HMM_Vec2 offset = HMM_RotateV2(HMM_V2(angleConstraintVisualOffset, 0), startAngle + (i * c->value / (ptCount - 1)));
                    linePts[i] = HMM_Add(joint->pos, offset);
                }
                snzr_drawLine(linePts, ptCount, color, 4, mvp);
            }
        }  // end constraint kind switch
    }  // end constraint draw loop

    for (sk_Line* l = sketch.firstLine; l; l = l->next) {
        HMM_Vec2 points[] = {
            l->p1->pos,
            l->p2->pos,
        };
        snzr_drawLine(points, 2, UI_TEXT_COLOR, 2, mvp);
    }

    glEnable(GL_DEPTH_TEST);
}

void main_drawDemoScene(HMM_Vec2 panelSize, snz_Arena* scratch) {
    snzu_boxNew("inner");
    snzu_boxFillParent();
    snzu_boxSetTexture(sceneFB.texture);

    snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);

    HMM_Vec2* const orbitAngle = SNZU_USE_MEM(HMM_Vec2, "orbitAngle");
    // ^^ X meaning rotation around X axis
    float* const orbitDistance = SNZU_USE_MEM(float, "orbitDistance");
    if (snzu_useMemIsPrevNew()) {
        *orbitDistance = 5;
    }
    main_Align* const orbitOrigin = SNZU_USE_MEM(main_Align, "orbitOrigin");
    // FIXME: a Look at cmd so that panning isn't so annoying
    // FIXME: automagic redo of the origin

    if (snzu_useMemIsPrevNew()) {
        *orbitOrigin = (main_Align){
            .startNormal = HMM_V3(0, 0, 1),
            .startPt = HMM_V3(0, 0, 0),
            .startVertical = HMM_V3(0, 1, 0),
            .endNormal = HMM_V3(0, 0, 1),
            .endPt = HMM_V3(0, 0, 0),
            .endVertical = HMM_V3(0, 1, 0),
        };
        *orbitOrigin = sketchAlign;
        // Note that the start component of this is indended to stay as what it's initialized to,
        // the end indicates orientation/position of the camera's orbit origin
        // normal points towards the camera when it has a angle of 0, 0
    }

    HMM_Vec2* const lastMousePos = SNZU_USE_MEM(HMM_Vec2, "lastMousePos");
    if (inter->mouseActions[SNZU_MB_MIDDLE] == SNZU_ACT_DOWN) {
        *lastMousePos = inter->mousePosGlobal;
    }
    if (inter->mouseActions[SNZU_MB_MIDDLE] == SNZU_ACT_DRAG) {
        HMM_Vec2 diff = HMM_SubV2(inter->mousePosGlobal, *lastMousePos);
        if (inter->keyMods & KMOD_SHIFT) {
            HMM_Quat cameraRot = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), orbitAngle->X);
            cameraRot = HMM_MulQ(HMM_QFromAxisAngle_RH(HMM_V3(0, 1, 0), orbitAngle->Y), cameraRot);
            HMM_Quat originRot = main_alignToQuat(*orbitOrigin);

            HMM_Quat rotation = HMM_MulQ(originRot, cameraRot);

            diff = HMM_MulV2F(diff, -0.001 * (*orbitDistance));
            HMM_Vec4 diffInSpace = HMM_V4(diff.X, -diff.Y, 0, 1);
            diffInSpace = HMM_Mul(HMM_QToM4(rotation), diffInSpace);
            orbitOrigin->endPt = HMM_Add(orbitOrigin->endPt, diffInSpace.XYZ);
        } else {
            diff = HMM_V2(diff.Y, diff.X); // switch so that rotations are repective to their axis
            diff = HMM_Mul(diff, -0.01f); // sens
            *orbitAngle = HMM_AddV2(*orbitAngle, diff);

            if (orbitAngle->X < HMM_AngleDeg(-90)) {
                orbitAngle->X = HMM_AngleDeg(-90);
            } else if (orbitAngle->X > HMM_AngleDeg(90)) {
                orbitAngle->X = HMM_AngleDeg(90);
            }
        }
    }
    *lastMousePos = inter->mousePosGlobal;

    *orbitDistance += inter->mouseScrollY * (*orbitDistance) * 0.05;

    HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, *orbitDistance));
    view = HMM_MulM4(HMM_Rotate_RH(orbitAngle->X, HMM_V3(1, 0, 0)), view);
    view = HMM_MulM4(HMM_Rotate_RH(orbitAngle->Y, HMM_V3(0, 1, 0)), view);
    view = HMM_MulM4(main_alignToM4(*orbitOrigin), view);
    HMM_Vec3 cameraPos = HMM_MulM4V4(view, HMM_V4(0, 0, 0, 1)).XYZ;

    HMM_Vec3 rayNormal = main_rayFromCamera(HMM_AngleDeg(90), view, inter->mousePosLocal, panelSize);

    view = HMM_InvGeneral(view);

    float aspect = panelSize.X / panelSize.Y;
    HMM_Mat4 proj = HMM_Perspective_RH_NO(HMM_AngleDeg(90), aspect, 0.001, 100000);

    uint32_t w = (uint32_t)panelSize.X;
    uint32_t h = (uint32_t)panelSize.Y;
    if (w != sceneFB.texture.width || h != sceneFB.texture.height) {
        snzr_frameBufferDeinit(&sceneFB);
        sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(w, h, NULL));
    }


    // FIXME: opengl here is gross
    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, sceneFB.glId));
    snzr_callGLFnOrError(glViewport(0, 0, w, h));
    snzr_callGLFnOrError(glClearColor(1, 1, 1, 1));
    snzr_callGLFnOrError(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
    HMM_Mat4 vp = HMM_MulM4(proj, view);

    // FIXME: highlight edges :) + debug view of geometry
    ren3d_drawMesh(&mesh, vp, model, HMM_V3(-1, -1, -1));

    float t = 0;
    bool hit = csg_planeLineIntersection(sketchAlign.endPt, sketchAlign.endNormal, cameraPos, rayNormal, &t);
    HMM_Vec3 point = HMM_Add(cameraPos, HMM_Mul(rayNormal, t));
    if (!hit) {
        point = HMM_V3(INFINITY, INFINITY, INFINITY);
    }
    point = HMM_Sub(point, sketchAlign.endPt);
    HMM_Vec3 xAxis = HMM_Cross(sketchAlign.endVertical, sketchAlign.endNormal);
    float x = HMM_Dot(point, xAxis);
    float y = HMM_Dot(point, sketchAlign.endVertical);

    main_drawSketch(vp, main_alignToM4(sketchAlign), scratch, cameraPos, HMM_V2(x, y));
}

typedef enum {
    MAIN_VIEW_SCENE,
    MAIN_VIEW_DOCS,
} main_View;

main_View main_currentView;

void main_frame(float dt, snz_Arena* scratch) {
    assert(scratch || !scratch);
    assert(dt || !dt);

    HMM_Vec2 rightPanelSize = HMM_V2(0, 0);

    snzu_boxNew("parent");
    snzu_boxFillParent();
    snzu_boxScope() {
        float* const leftPanelAnim = SNZU_USE_MEM(float, "leftPanelAnim");
        snzu_Interaction* leftPanelInter = SNZU_USE_MEM(snzu_Interaction, "leftPanelInter");

        // FIXME: on startup this flashes out
        float leftPanelSize = *leftPanelAnim * 200;
        bool target = (leftPanelInter->mousePosGlobal.X < 20) || (leftPanelInter->mousePosGlobal.X < leftPanelSize);
        // ^ Just using hover don't work because of inner elts. masking hover events
        snzu_easeExp(leftPanelAnim, target, 10);

        rightPanelSize = snzu_boxGetSize(snzu_boxGetParent());
        rightPanelSize.X -= leftPanelSize;

        snzu_boxNew("leftPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, leftPanelSize);
        snzu_boxSetColor(UI_BACKGROUND_COLOR);
        snzu_boxSetInteractionOutput(leftPanelInter, SNZU_IF_HOVER);
        snzu_boxScope() {
            snzu_boxNew("padding");
            snzu_boxSetSizeMarginFromParent(20);
            snzu_boxScope() {
                snzu_boxNew("scene list");
                snzu_boxFillParent();
                snzu_boxSizePctParent(0.5, SNZU_AX_Y);
                snzu_boxScope() {
                    if (ui_buttonWithHighlight(main_currentView == MAIN_VIEW_SCENE, "demo scene")) {
                        main_currentView = MAIN_VIEW_SCENE;
                    }
                }
                snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y);
                snzuc_scrollArea();

                snzu_boxNew("other view list");
                snzu_boxFillParent();
                snzu_boxSizeFromEndPctParent(0.5, SNZU_AX_Y);
                snzu_boxScope() {
                    if (ui_buttonWithHighlight(main_currentView == MAIN_VIEW_DOCS, "docs")) {
                        main_currentView = MAIN_VIEW_DOCS;
                    }
                }
                snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y);
                // FIXME: bottom align
            }  // end padding
        }  // end leftpanel
        snzu_boxClipChildren();
        // FIXME: some hint in the lower left corner that this menu exists

        snzu_boxNew("rightPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_X, rightPanelSize.X);  // FIXME: set size remaining util fn
        snzu_boxScope() {
            if (main_currentView == MAIN_VIEW_DOCS) {
                docs_buildPage();
            } else if (main_currentView == MAIN_VIEW_SCENE) {
                main_drawDemoScene(rightPanelSize, scratch);
            } else {
                SNZ_ASSERTF(false, "unreachable view case, view was: %d", main_currentView);
            }
        }

        snzu_boxNew("leftPanelBorder");
        snzu_boxFillParent();
        snzu_boxSetStartFromParentAx(leftPanelSize - UI_BORDER_THICKNESS, SNZU_AX_X);
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, UI_BORDER_THICKNESS);
        snzu_boxSetColor(UI_TEXT_COLOR);

        snzu_boxNew("upperBorder");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, UI_BORDER_THICKNESS);
        snzu_boxSetColor(UI_TEXT_COLOR);
    }
}

int main() {
    snz_main("ADDER V0.0", main_init, main_frame);
    return 0;
}
