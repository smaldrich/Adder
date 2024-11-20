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
        sketch = sk_sketchInit();
        sk_Point* originPt = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(0, 0));
        sk_Point* left = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(-1, -1));
        sk_Point* right = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(1, 0));
        sk_Point* up = sk_sketchAddPoint(&sketch, &sketchArena, HMM_V2(0, 1));

        sk_Line* vertical = sk_sketchAddLine(&sketch, &sketchArena, originPt, up);
        sk_sketchAddConstraintDistance(&sketch, &sketchArena, vertical, 0.5);

        sk_Line* leftLine = sk_sketchAddLine(&sketch, &sketchArena, left, originPt);
        sk_sketchAddConstraintAngle(&sketch, &sketchArena, vertical, false, leftLine, true, HMM_AngleDeg(90));
        sk_Line* rightLine = sk_sketchAddLine(&sketch, &sketchArena, originPt, right);
        sk_sketchAddConstraintDistance(&sketch, &sketchArena, rightLine, 1);
        // sk_sketchAddConstraintAngle(&sketch, &sketchArena, rightLine, false, vertical, false, HMM_AngleDeg(90));

        sk_sketchAddLine(&sketch, &sketchArena, up, right);

        sk_sketchSolve(&sketch, originPt, vertical, HMM_AngleDeg(90));

        // sketchOrigin = final->first->a;
        // HMM_Vec3 baseNormal = HMM_V3(0, 0, -1);
        // HMM_Vec3 newNormal = csg_triNormal(final->first->a, final->first->b, final->first->c);

        // HMM_Vec3 axis = HMM_Cross(baseNormal, newNormal);
        // float angle = acosf(HMM_Dot(baseNormal, newNormal) / (HMM_Len(baseNormal) * HMM_Len(newNormal)));
        // sketchOrientation = HMM_QFromAxisAngle_RH(axis, angle);
    }
}

void main_drawDemoScene(HMM_Vec2 panelSize, snz_Arena* scratch) {
    snzu_boxNew("inner");
    snzu_boxFillParent();
    snzu_boxSetTexture(sceneFB.texture);

    snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);
    HMM_Vec3* const orbitPos = SNZU_USE_MEM(HMM_Vec3, "orbitPos");
    if (snzu_useMemIsPrevNew()) {
        orbitPos->Z = 4;
    }

    HMM_Vec2* const lastMousePos = SNZU_USE_MEM(HMM_Vec2, "lastMousePos");

    if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
        *lastMousePos = inter->mousePosGlobal;
    }
    if (inter->dragged) {
        HMM_Vec2 diff = HMM_SubV2(inter->mousePosGlobal, *lastMousePos);
        *lastMousePos = inter->mousePosGlobal;
        orbitPos->XY = HMM_AddV2(orbitPos->XY, diff);
    }

    // FIXME: panning
    // FIXME: live orbit point selection by raycast
    orbitPos->Z += inter->mouseScrollY * orbitPos->Z * 0.05;

    HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, orbitPos->Z));
    view = HMM_MulM4(HMM_Rotate_RH(orbitPos->Y * 0.01, HMM_V3(-1, 0, 0)), view);
    view = HMM_MulM4(HMM_Rotate_RH(orbitPos->X * 0.01, HMM_V3(0, -1, 0)), view);
    view = HMM_InvGeneral(view);

    float aspect = panelSize.X / panelSize.Y;
    HMM_Mat4 proj = HMM_Perspective_RH_NO(HMM_AngleDeg(90), aspect, 0.001, 100000);

    // HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));

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
    // ren3d_drawMesh(&mesh, HMM_MulM4(proj, view), model, HMM_V3(-1, -1, -1));
    // FIXME: highlight edges :) + debug view of geometry

    // HMM_Mat4 sketchVP = HMM_Translate(sketchOrigin);
    // sketchVP = HMM_Mul(sketchVP, HMM_QToM4(sketchOrientation));
    // sketchVP = HMM_Mul(HMM_Mul(proj, view), sketchVP);
    HMM_Mat4 sketchVP = HMM_Mul(proj, view);

    glDisable(GL_DEPTH_TEST);

    // MANIFOLDS
    for (sk_Point* p = sketch.firstPoint; p; p = p->next) {
        sk_ManifoldKind kind = p->manifold.kind;
        if (kind == SK_MK_POINT) {
            continue;
        } else if (kind == SK_MK_CIRCLE) {
            int ptCount = 11;
            HMM_Vec2* pts = SNZ_ARENA_PUSH_ARR(scratch, ptCount, HMM_Vec2);

            float angleRange = HMM_AngleDeg(40);
            HMM_Vec2 diff = HMM_Sub(p->pos, p->manifold.circle.origin);
            float startAngle = atan2f(diff.Y, diff.X);
            for (int i = 0; i < ptCount; i++) {
                float angle = startAngle + (i - (ptCount / 2)) * (angleRange / ptCount);
                pts[i] = HMM_RotateV2(HMM_V2(p->manifold.circle.radius, 0), angle);
                pts[i] = HMM_Add(pts[i], p->manifold.circle.origin);
            }
            snzr_drawLine(pts, ptCount, UI_ACCENT_COLOR, 4, sketchVP);
        } else if (kind == SK_MK_LINE) {
            HMM_Vec2 pts[2] = {
                p->pos,
                HMM_Add(p->pos, HMM_Mul(HMM_Norm(p->manifold.line.direction), 0.4f)),
            };
            snzr_drawLine(pts, 2, UI_ACCENT_COLOR, 4, sketchVP);
        } else if (kind == SK_MK_ANY) {
            HMM_Vec2 pts[4] = {
                HMM_V2(-1, 0),
                HMM_V2(1, 0),
                HMM_V2(0, -1),
                HMM_V2(0, 1),
            };
            for (int i = 0; i < 4; i++) {
                pts[i] = HMM_MulV2F(pts[i], 0.2);
                pts[i] = HMM_RotateV2(pts[i], HMM_AngleDeg(10));
                pts[i] = HMM_AddV2(pts[i], p->pos);
            }
            snzr_drawLine(pts, 2, UI_ACCENT_COLOR, 4, sketchVP);
            snzr_drawLine(&pts[2], 2, UI_ACCENT_COLOR, 4, sketchVP);
        }
    }

    for (sk_Constraint* c = sketch.firstConstraint; c; c = c->nextAllocated) {
        HMM_Vec4 color = UI_TEXT_COLOR;
        if (c->violated) {
            color = UI_RED;
        }

        float angleConstraintVisualOffset = 0.05 * orbitPos->Z;  // FIXME: is this still correct when panning?
        float distConstraintVisualOffset = 0.025 * orbitPos->Z;
        if (c->kind == SK_CK_DISTANCE) {
            HMM_Vec2 p1 = c->line1->p1->pos;
            HMM_Vec2 p2 = c->line1->p2->pos;
            HMM_Vec2 diff = HMM_NormV2(HMM_SubV2(p2, p1));
            HMM_Vec2 offset = HMM_Mul(HMM_V2(diff.Y, -diff.X), distConstraintVisualOffset);
            p1 = HMM_Add(p1, offset);
            p2 = HMM_Add(p2, offset);
            HMM_Vec2 points[] = {p1, p2};
            snzr_drawLine(points, 2, color, 4, sketchVP);
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
                snzr_drawLine(pts, 3, color, 4, sketchVP);
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
                snzr_drawLine(linePts, ptCount, color, 4, sketchVP);
            }
        }  // end constraint kind switch
    }  // end constraint draw loop

    for (sk_Line* l = sketch.firstLine; l; l = l->next) {
        HMM_Vec2 points[] = {
            l->p1->pos,
            l->p2->pos,
        };
        snzr_drawLine(points, 2, UI_TEXT_COLOR, 2, sketchVP);
    }

    glEnable(GL_DEPTH_TEST);
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
