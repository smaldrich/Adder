#include "PoolAlloc.h"
#include "csg.h"
#include "docs.h"
#include "render3d.h"
#include "serialization2.h"
#include "shortcuts.h"
#include "sketches2.h"
#include "sketchui.h"
#include "snooze.h"
#include "sound.h"
#include "stb/stb_image.h"
#include "ui.h"

snzu_Instance main_uiInstance;

snz_Arena main_fontArena;
PoolAlloc main_pool;

snzr_FrameBuffer main_sceneFB;
sk_Sketch main_sketch;
sk_Line* main_sketchOriginLine;
snzu_Instance main_sketchUIInstance;
snz_Arena main_sketchArena;

ren3d_Mesh main_mesh;

bool main_inDarkMode = true;
bool main_inMusicMode = true;

sku_Align main_sketchAlign;

void main_init(snz_Arena* scratch, SDL_Window* window) {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    csg_tests();

    main_fontArena = snz_arenaInit(10000000, "main font arena");
    main_sketchArena = snz_arenaInit(10000000, "main sketch arena");
    main_pool = poolAllocInit();
    main_uiInstance = snzu_instanceInit();
    snzu_instanceSelect(&main_uiInstance);
    main_sketchUIInstance = snzu_instanceInit();

    { // FIXME: move to snz
        SDL_Surface* s = SDL_LoadBMP("res/textures/icon.bmp");
        char buf[1000] = { 0 };
        const char* err = SDL_GetErrorMsg(buf, 1000);
        printf("%s", err);
        SNZ_ASSERT(s != NULL, "icon load failed.");
        SDL_SetWindowIcon(window, s);
    }

    sound_init();
    ui_init(&main_fontArena, scratch);
    ren3d_init(scratch);
    docs_init();
    sc_init(&main_pool);

    snz_arenaClear(scratch);

    main_sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(500, 500, NULL));

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
                main_sketchAlign = (sku_Align){
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
        main_mesh = ren3d_meshInit(verts, vertCount);
        poolAllocDeinit(&pool);
    }  // end mesh for testing

    {
        main_sketch = sk_sketchInit();
        sk_Point* originPt = sk_sketchAddPoint(&main_sketch, &main_sketchArena, HMM_V2(0, 0));
        sk_Point* left = sk_sketchAddPoint(&main_sketch, &main_sketchArena, HMM_V2(-1, -1));
        sk_Point* right = sk_sketchAddPoint(&main_sketch, &main_sketchArena, HMM_V2(1, 0));
        sk_Point* up = sk_sketchAddPoint(&main_sketch, &main_sketchArena, HMM_V2(0, 1));

        sk_Line* vertical = sk_sketchAddLine(&main_sketch, &main_sketchArena, originPt, up);
        sk_sketchAddConstraintDistance(&main_sketch, &main_sketchArena, vertical, 0.5);

        sk_Line* leftLine = sk_sketchAddLine(&main_sketch, &main_sketchArena, left, originPt);
        sk_sketchAddConstraintAngle(&main_sketch, &main_sketchArena, vertical, false, leftLine, true, HMM_AngleDeg(90));
        sk_sketchAddConstraintDistance(&main_sketch, &main_sketchArena, leftLine, 0.8);
        sk_Line* rightLine = sk_sketchAddLine(&main_sketch, &main_sketchArena, originPt, right);
        sk_sketchAddConstraintDistance(&main_sketch, &main_sketchArena, rightLine, 1);
        sk_sketchAddConstraintAngle(&main_sketch, &main_sketchArena, rightLine, false, vertical, false, HMM_AngleDeg(120));

        sk_Point* other = sk_sketchAddPoint(&main_sketch, &main_sketchArena, HMM_V2(-1, 1));
        sk_Line* l = sk_sketchAddLine(&main_sketch, &main_sketchArena, left, other);
        sk_sketchAddConstraintAngle(&main_sketch, &main_sketchArena, l, false, leftLine, false, HMM_AngleDeg(-120));
        // sk_sketchAddConstraintDistance(&sketch, &sketchArena, l, 0.2);

        sk_sketchAddLine(&main_sketch, &main_sketchArena, up, right);

        main_sketchOriginLine = vertical;
        // main_sketchOriginLine = leftLine;
        sk_sketchSolve(&main_sketch, main_sketchOriginLine->p1, main_sketchOriginLine, HMM_AngleDeg(0));
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

void main_drawDemoScene(HMM_Vec2 panelSize, snz_Arena* scratch, float dt, snzu_Input inputs) {
    snzu_boxNew("inner");
    snzu_boxFillParent();
    snzu_boxSetTexture(main_sceneFB.texture);

    snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);

    HMM_Vec2* const orbitAngle = SNZU_USE_MEM(HMM_Vec2, "orbitAngle");
    // ^^ X meaning rotation around X axis
    float* const orbitDistance = SNZU_USE_MEM(float, "orbitDistance");
    if (snzu_useMemIsPrevNew()) {
        *orbitDistance = 5;
    }
    sku_Align* const orbitOrigin = SNZU_USE_MEM(sku_Align, "orbitOrigin");
    // FIXME: a Look at cmd so that panning isn't so annoying
    // FIXME: automagic redo of the origin

    if (snzu_useMemIsPrevNew()) {
        *orbitOrigin = (sku_Align){
            .startNormal = HMM_V3(0, 0, 1),
            .startPt = HMM_V3(0, 0, 0),
            .startVertical = HMM_V3(0, 1, 0),
            .endNormal = HMM_V3(0, 0, 1),
            .endPt = HMM_V3(0, 0, 0),
            .endVertical = HMM_V3(0, 1, 0),
        };
        *orbitOrigin = main_sketchAlign;
        // Note that the start component of this is indended to stay as what it's initialized to,
        // the end indicates orientation/position of the camera's orbit origin
        // normal points towards the camera when it has a angle of 0, 0
    }

    HMM_Vec2* const lastMousePos = SNZU_USE_MEM(HMM_Vec2, "lastMousePos");
    if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN) {
        *lastMousePos = inter->mousePosGlobal;
    }
    if (inter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DRAG) {
        HMM_Vec2 diff = HMM_SubV2(inter->mousePosGlobal, *lastMousePos);
        if (inter->keyMods & KMOD_SHIFT) {
            HMM_Quat cameraRot = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), orbitAngle->X);
            cameraRot = HMM_MulQ(HMM_QFromAxisAngle_RH(HMM_V3(0, 1, 0), orbitAngle->Y), cameraRot);
            HMM_Quat originRot = sku_alignToQuat(*orbitOrigin);

            HMM_Quat rotation = HMM_MulQ(originRot, cameraRot);

            diff = HMM_MulV2F(diff, -0.001 * (*orbitDistance));
            HMM_Vec4 diffInSpace = HMM_V4(diff.X, -diff.Y, 0, 1);
            diffInSpace = HMM_Mul(HMM_QToM4(rotation), diffInSpace);
            orbitOrigin->endPt = HMM_Add(orbitOrigin->endPt, diffInSpace.XYZ);
        } else {
            diff = HMM_V2(diff.Y, diff.X);  // switch so that rotations are repective to their axis
            diff = HMM_Mul(diff, -0.01f);   // sens
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
    view = HMM_MulM4(sku_alignToM4(*orbitOrigin), view);
    HMM_Vec3 cameraPos = HMM_MulM4V4(view, HMM_V4(0, 0, 0, 1)).XYZ;

    HMM_Vec3 mouseRayNormal = main_rayFromCamera(HMM_AngleDeg(90), view, inter->mousePosLocal, panelSize);

    view = HMM_InvGeneral(view);

    float aspect = panelSize.X / panelSize.Y;
    HMM_Mat4 proj = HMM_Perspective_RH_NO(HMM_AngleDeg(90), aspect, 0.001, 100000);

    uint32_t w = (uint32_t)panelSize.X;
    uint32_t h = (uint32_t)panelSize.Y;
    if (w != main_sceneFB.texture.width || h != main_sceneFB.texture.height) {
        snzr_frameBufferDeinit(&main_sceneFB);
        main_sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(w, h, NULL));
    }

    // FIXME: opengl here is gross
    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, main_sceneFB.glId));
    snzr_callGLFnOrError(glViewport(0, 0, w, h));
    snzr_callGLFnOrError(glClearColor(ui_colorBackground.X, ui_colorBackground.Y, ui_colorBackground.Z, ui_colorBackground.W));
    snzr_callGLFnOrError(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
    HMM_Mat4 vp = HMM_MulM4(proj, view);

    // FIXME: highlight edges :) + debug view of geometry
    ren3d_drawMesh(&main_mesh, vp, model, HMM_V3(-1, -1, -1), ui_lightAmbient);
    ren3d_drawSkybox(vp, ui_skyBox);

    // FIXME: this is gross af
    float soundVal = 0;
    {
        float* const max = SNZU_USE_MEM(float, "max");
        float* const min = SNZU_USE_MEM(float, "min");
        float* const minTime = SNZU_USE_MEM(float, "minTime");
        float* const smooth = SNZU_USE_MEM(float, "smooth");
        float cur = sound_get();
        *max = SNZ_MAX(*max, cur);
        *min = SNZ_MIN(*min, cur);
        snzu_easeExpUnbounded(smooth, cur * (*max - *min), 30);
        *minTime += 0.0001;
        if (*minTime > 1) {
            *minTime = 0;
            *min = cur;
        }
        soundVal = (*smooth / *max - 0.5) * 0.25;
        if (!main_inMusicMode || *max == 0) {
            soundVal = 0;
        }
    }

    snzu_instanceSelect(&main_sketchUIInstance);
    sku_drawSketch(&main_sketch, main_sketchAlign, vp, cameraPos, mouseRayNormal, inputs, soundVal, dt, scratch);
    snzu_instanceSelect(&main_uiInstance);
}

void main_drawSettings() {
    ui_menuMargin();
    snzu_boxScope() {
        snzu_boxNew("title");
        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Settings");
        snzu_boxSetSizeFitText(ui_padding);

        bool prev = main_inDarkMode;
        ui_switch("darkmode", "Dark Theme", &main_inDarkMode);

        if (prev != main_inDarkMode) {
            if (!main_inDarkMode) {
                ui_setThemeLight();
            } else {
                ui_setThemeDark();
                // FIXME: when in dark mode, button with highlight is awful
                // FIXME: when in dark mode, text in the scene is really weird but only when the left bar is moving
            }
        }

        ui_switch("musicmode", "Music Mode", &main_inMusicMode);
    }
    // FIXME: UI variable for gap here
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
    snzuc_scrollArea();
}

typedef enum {
    MAIN_VIEW_SCENE,
    MAIN_VIEW_DOCS,
    MAIN_VIEW_SETTINGS,
    MAIN_VIEW_SHORTCUTS,
} main_View;

main_View main_currentView;

void main_frame(float dt, snz_Arena* scratch, snzu_Input inputs, HMM_Vec2 screenSize) {
    snzu_instanceSelect(&main_uiInstance);
    snzu_frameStart(scratch, screenSize, dt);

    sk_sketchSolve(&main_sketch, main_sketchOriginLine->p1, main_sketchOriginLine, HMM_AngleDeg(0));

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
        snzu_boxSetColor(ui_colorBackground);
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
                    if (ui_buttonWithHighlight(main_currentView == MAIN_VIEW_SETTINGS, "settings")) {
                        main_currentView = MAIN_VIEW_SETTINGS;
                    }
                    if (ui_buttonWithHighlight(main_currentView == MAIN_VIEW_SHORTCUTS, "shortcuts")) {
                        main_currentView = MAIN_VIEW_SHORTCUTS;
                    }
                }
                snzu_boxOrderChildrenInRowRecurseAlignEnd(5, SNZU_AX_Y);
            }  // end padding
        }  // end leftpanel
        snzu_boxClipChildren();
        // FIXME: some hint in the lower left corner that this menu exists

        snzu_boxNew("rightPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_X, rightPanelSize.X);  // FIXME: set size remaining util fn
        snzu_boxSetColor(ui_colorBackground);
        snzu_boxScope() {
            if (main_currentView == MAIN_VIEW_DOCS) {
                docs_buildPage();
            } else if (main_currentView == MAIN_VIEW_SCENE) {
                main_drawDemoScene(rightPanelSize, scratch, dt, inputs);
                sc_updateAndBuildHintWindow();
            } else if (main_currentView == MAIN_VIEW_SETTINGS) {
                main_drawSettings();
            } else if (main_currentView == MAIN_VIEW_SHORTCUTS) {
                sc_buildSettings();
            } else {
                SNZ_ASSERTF(false, "unreachable view case, view was: %d", main_currentView);
            }
        }

        snzu_boxNew("leftPanelBorder");
        snzu_boxFillParent();
        snzu_boxSetStartFromParentAx(leftPanelSize - ui_borderThickness, SNZU_AX_X);
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, ui_borderThickness);
        snzu_boxSetColor(ui_colorText);

        snzu_boxNew("upperBorder");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_borderThickness);
        snzu_boxSetColor(ui_colorText);
    }

    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    snzr_callGLFnOrError(glViewport(0, 0, screenSize.X, screenSize.Y));
    HMM_Mat4 vp = HMM_Orthographic_RH_NO(0, screenSize.X, screenSize.Y, 0, 0, 1000000);
    snzu_frameDrawAndGenInteractions(inputs, vp);
}

int main() {
    snz_main("ADDER V0.0", main_init, main_frame);
    sound_deinit();
    poolAllocDeinit(&main_pool);
    return 0;
}
