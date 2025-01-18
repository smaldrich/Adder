#include "PoolAlloc.h"
#include "docs.h"
#include "geometry.h"
#include "render3d.h"
#include "serialization2.h"
#include "settings.h"
#include "shortcuts.h"
#include "sketches2.h"
#include "sketchui.h"
#include "snooze.h"
#include "sound.h"
#include "stb/stb_image.h"
#include "timeline.h"
#include "timelineui.h"
#include "ui.h"

snz_Arena main_fontArena;
snz_Arena main_meshArena;
snz_Arena main_tlArena;
snz_Arena main_sketchArena;
PoolAlloc main_pool;

snzu_Instance main_uiInstance;
snzu_Instance main_sceneUIInstance;
snzr_FrameBuffer main_sceneFB;

sc_View main_currentView = SC_VIEW_SCENE;
set_Settings main_settings;
tl_Timeline main_timeline;

void main_init(snz_Arena* scratch, SDL_Window* window) {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    geo_tests();

    main_fontArena = snz_arenaInit(10000000, "main font arena");
    main_sketchArena = snz_arenaInit(10000000, "main sketch arena");
    main_meshArena = snz_arenaInit(10000000, "main mesh arena");
    main_tlArena = snz_arenaInit(10000000, "main tl arena");
    main_pool = poolAllocInit();
    main_uiInstance = snzu_instanceInit();
    snzu_instanceSelect(&main_uiInstance);
    main_sceneUIInstance = snzu_instanceInit();
    main_settings = set_settingsDefault();

    {  // FIXME: move to snz
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

    main_timeline = tl_timelineInit(&main_tlArena);
    {
        geo_BSPTriList* bspTris = NULL;
        geo_MeshFace* faces = NULL;
        geo_Mesh cubeA = geo_cube(&main_meshArena);
        // bspTris = &cubeA.bspTris;
        // faces = cubeA.firstFace;
        {
            geo_Mesh cubeB = geo_cube(&main_meshArena);
            geo_BSPTriListTransform(&cubeB.bspTris, HMM_Rotate_RH(HMM_AngleDeg(30), HMM_V3(1, 1, 1)));
            geo_BSPTriListTransform(&cubeB.bspTris, HMM_Translate(HMM_V3(1, 1, 1)));

            geo_BSPNode* treeA = geo_BSPTriListToBSP(&cubeA.bspTris, &main_meshArena);
            geo_BSPNode* treeB = geo_BSPTriListToBSP(&cubeB.bspTris, &main_meshArena);

            geo_BSPTriList* aClipped = geo_BSPTriListClip(true, &cubeA.bspTris, treeB, &main_meshArena);
            geo_BSPTriList* bClipped = geo_BSPTriListClip(true, &cubeB.bspTris, treeA, &main_meshArena);
            bspTris = geo_BSPTriListJoin(aClipped, bClipped);
            geo_BSPTriListRecoverNonBroken(&bspTris, &main_meshArena);

            geo_MeshFace* bCubeLastFace = cubeB.firstFace;
            for (; bCubeLastFace->next; bCubeLastFace = bCubeLastFace->next) {
            }  // FIXME: gross
            bCubeLastFace->next = cubeA.firstFace;
            // this join destroys the OG cubes bc.the face list has been changed up
            faces = cubeB.firstFace;
        }

        geo_Mesh mesh = (geo_Mesh){
            .bspTris = *bspTris,
            .firstFace = faces,
            .renderMesh = geo_BSPTriListToRenderMesh(*bspTris, scratch),
        };
        geo_BSPTriListToFaceTris(&main_pool, &mesh);
        geo_meshGenerateEdges(&mesh, &main_meshArena, scratch);
        geo_meshGenerateCorners(&mesh, &main_meshArena, scratch);

        tl_timelinePushGeometry(&main_timeline, HMM_V2(-200, 0), mesh);
    }  // end mesh for testing

    tl_timelinePushSketch(&main_timeline, HMM_V2(0, 0), sk_sketchInit(&main_sketchArena));
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

void main_drawTimelineMeshPreview(float dt, HMM_Vec2 panelSize, const ren3d_Mesh* mesh) {
    HMM_Mat4 model = HMM_M4D(1.0f);

    HMM_Mat4 view = HMM_M4D(1.0f);
    float orbitDistance = 5;
    view = HMM_Mul(HMM_Translate(HMM_V3(0, 0, orbitDistance)), view);

    {
        HMM_Mat4* rotated = &view;
        if (!main_settings.timelinePreviewSpinBackground) {
            rotated = &model;
        }

        HMM_Vec3* const spinAngles = SNZU_USE_MEM(HMM_Vec3, "preview spin");
        spinAngles->Y += 0.1 * dt;
        spinAngles->Z += 0.1 * dt;
        *rotated = HMM_Mul(HMM_Rotate_RH(spinAngles->X, HMM_V3(1, 0, 0)), *rotated);
        *rotated = HMM_Mul(HMM_Rotate_RH(spinAngles->Y, HMM_V3(0, 1, 0)), *rotated);
    }

    float aspect = panelSize.X / panelSize.Y;
    HMM_Mat4 proj = HMM_Perspective_RH_NO(HMM_AngleDeg(90), aspect, 0.001, 100000);

    // FIXME: use center of mass of the body, use max extents as well
    view = HMM_InvGeneral(view);
    HMM_Mat4 vp = HMM_MulM4(proj, view);

    // FIXME: this if is fragile bc in more than one plcae and also unintuitive (?)
    if (main_settings.skybox && ui_skyBox != NULL) {
        ren3d_drawSkybox(vp, *ui_skyBox);
    }

    ren3d_drawMesh(mesh, vp, model, HMM_V4(1, 1, 1, 1), HMM_V3(-1, -1, -1), ui_lightAmbient);

    // put a transparent thing over the preview to aid contrast
    snzr_drawRect(
        HMM_V2(-1, -1), HMM_V2(1, 1),
        HMM_V2(-1, -1), HMM_V2(1, 1),
        ui_colorTransparentPanel,
        0, 0, HMM_V4(0, 0, 0, 0),
        HMM_M4D(1.0f),
        _snzr_globs.solidTex);
}

// takes care of camera logic + skybox
void main_sceneBuild(
    tl_Scene* scene,
    HMM_Vec2 panelSize, snzu_Interaction* panelInter,
    HMM_Vec3* outCamPos, HMM_Vec3* outMouseDir,
    HMM_Mat4* outVP) {
    // FIXME: automagic redo of the origin
    SNZ_ASSERT(scene, "scene shouldn't be null"); // FIXME: this is only here cause the VS formatter was being annoying, sorry

    { // apply inputs to move camera around
        HMM_Vec2* const lastMousePos = SNZU_USE_MEM(HMM_Vec2, "lastMousePos");
        if (panelInter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN) {
            *lastMousePos = panelInter->mousePosGlobal;
        }
        if (panelInter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DRAG) {
            HMM_Vec2 diff = HMM_SubV2(panelInter->mousePosGlobal, *lastMousePos);
            if (panelInter->keyMods & KMOD_SHIFT) {
                HMM_Quat cameraRot = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), scene->orbitAngle.X);
                cameraRot = HMM_MulQ(HMM_QFromAxisAngle_RH(HMM_V3(0, 1, 0), scene->orbitAngle.Y), cameraRot);
                HMM_Quat originRot = geo_alignToQuat(geo_alignZero(), scene->orbitOrigin);

                HMM_Quat rotation = HMM_MulQ(originRot, cameraRot);
                diff = HMM_MulV2F(diff, -0.001 * scene->orbitDist);
                HMM_Vec4 diffInSpace = HMM_V4(diff.X, -diff.Y, 0, 1);
                diffInSpace = HMM_Mul(HMM_QToM4(rotation), diffInSpace);
                scene->orbitOrigin.pt = HMM_Add(scene->orbitOrigin.pt, diffInSpace.XYZ);
            } else {
                diff = HMM_V2(diff.Y, diff.X);  // switch so that rotations are repective to their axis
                diff = HMM_Mul(diff, -0.006f);  // sens
                scene->orbitAngle = HMM_AddV2(scene->orbitAngle, diff);

                if (scene->orbitAngle.X < HMM_AngleDeg(-90)) {
                    scene->orbitAngle.X = HMM_AngleDeg(-90);
                } else if (scene->orbitAngle.X > HMM_AngleDeg(90)) {
                    scene->orbitAngle.X = HMM_AngleDeg(90);
                }
            }
        }
        *lastMousePos = panelInter->mousePosGlobal;

        scene->orbitDist += panelInter->mouseScrollY * scene->orbitDist * 0.05;
    }

    { // generate VP / camera pos / mouse dir
        HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, scene->orbitDist));
        view = HMM_MulM4(HMM_Rotate_RH(scene->orbitAngle.X, HMM_V3(1, 0, 0)), view);
        view = HMM_MulM4(HMM_Rotate_RH(scene->orbitAngle.Y, HMM_V3(0, 1, 0)), view);
        view = HMM_MulM4(geo_alignToM4(geo_alignZero(), scene->orbitOrigin), view);
        float aspect = panelSize.X / panelSize.Y;
        HMM_Mat4 proj = HMM_Perspective_RH_NO(HMM_AngleDeg(90), aspect, 0.001, 100000);
        HMM_Mat4 vp = HMM_MulM4(proj, HMM_InvGeneral(view));

        HMM_Vec3 cameraPos = HMM_MulM4V4(view, HMM_V4(0, 0, 0, 1)).XYZ;
        *outCamPos = cameraPos;

        HMM_Vec3 mouseRayNormal = main_rayFromCamera(HMM_AngleDeg(90), view, panelInter->mousePosLocal, panelSize);
        *outMouseDir = mouseRayNormal;

        *outVP = vp;
    }

    if (main_settings.skybox && ui_skyBox != NULL) {
        ren3d_drawSkybox(*outVP, *ui_skyBox);
    }
}

float main_getSmoothedSound() {
    // FIXME: this is gross af
    float out = 0;
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
    out = (*smooth / *max - 0.5) * 0.25;
    if (!main_settings.musicMode || *max == 0) {
        out = 0;
    }

    return out;
}

void main_frame(float dt, snz_Arena* scratch, snzu_Input inputs, HMM_Vec2 screenSize) {
    snzu_instanceSelect(&main_uiInstance);
    snzu_frameStart(scratch, screenSize, dt);

    tl_timelineClearOpsMarkedForDelete(&main_timeline);

    if (main_timeline.activeOp) {
        tl_Op* op = main_timeline.activeOp;
        if (op->kind == TL_OPK_SKETCH) {
            sk_Sketch* sketch = &op->val.sketch.sketch;
            sk_sketchClearElementsMarkedForDelete(sketch);
            sk_sketchSolve(sketch);
        }
    } // FIXME: timeline solve/ui update fn (i have no idea where clear elts should go lol)

    if (main_settings.darkMode) {
        ui_setThemeDark();
    } else {
        ui_setThemeLight();
    }

    HMM_Vec2 rightPanelSize = HMM_V2(0, 0);

    snzu_boxNew("parent");
    snzu_boxFillParent();
    snzu_boxScope() {
        float* const leftPanelAnim = SNZU_USE_MEM(float, "leftPanelAnim");
        snzu_Interaction* leftPanelInter = SNZU_USE_MEM(snzu_Interaction, "leftPanelInter");

        // FIXME: on startup this flashes out
        float leftPanelSize = *leftPanelAnim * 200;
        bool target = main_settings.leftBarAlwaysOpen || (leftPanelInter->mousePosGlobal.X < 20) || (leftPanelInter->mousePosGlobal.X < leftPanelSize);
        // ^ FIXME: fixable without doing this: using hover doesn't work because of inner elts. masking hover events
        snzu_easeExp(leftPanelAnim, target, ui_menuAnimationSpeed);

        rightPanelSize = snzu_boxGetSizePtr(snzu_boxGetParent());
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
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SCENE, "demo scene")) {
                        main_currentView = SC_VIEW_SCENE;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_TIMELINE, "timeline")) {
                        main_currentView = SC_VIEW_TIMELINE;
                    }
                }
                snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y);
                snzuc_scrollArea();

                snzu_boxNew("other view list");
                snzu_boxFillParent();
                snzu_boxSizeFromEndPctParent(0.5, SNZU_AX_Y);
                snzu_boxScope() {
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_DOCS, "docs")) {
                        main_currentView = SC_VIEW_DOCS;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SETTINGS, "settings")) {
                        main_currentView = SC_VIEW_SETTINGS;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SHORTCUTS, "shortcuts")) {
                        main_currentView = SC_VIEW_SHORTCUTS;
                    }
                    if (ui_buttonWithHighlight(false, "quit")) {
                        snz_quit();
                    }
                }
                snzu_boxOrderChildrenInRowRecurseAlignEnd(5, SNZU_AX_Y);
            }  // end padding
        }  // end leftpanel
        snzu_boxClipChildren(true);
        // FIXME: some hint in the lower left corner that this menu exists

        float soundVal = main_getSmoothedSound();

        snzu_boxNew("rightPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_X, rightPanelSize.X);  // FIXME: set size remaining util fn
        snzu_boxSetColor(ui_colorBackground);
        snzu_boxScope() {
            if (main_currentView == SC_VIEW_DOCS) {
                docs_buildPage();
            } else if (main_currentView == SC_VIEW_SCENE || main_currentView == SC_VIEW_TIMELINE) {
                uint32_t w = (uint32_t)rightPanelSize.X;
                uint32_t h = (uint32_t)rightPanelSize.Y;
                if (w != main_sceneFB.texture.width || h != main_sceneFB.texture.height) {
                    snzr_frameBufferDeinit(&main_sceneFB);
                    main_sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(w, h, NULL));
                }

                // FIXME: opengl here is gross
                snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, main_sceneFB.glId));
                snzr_callGLFnOrError(glViewport(0, 0, w, h));
                snzr_callGLFnOrError(glClearColor(ui_colorBackground.X, ui_colorBackground.Y, ui_colorBackground.Z, ui_colorBackground.W));
                snzr_callGLFnOrError(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

                snzu_boxNew("scene");
                snzu_boxFillParent();
                snzu_boxSetTexture(main_sceneFB.texture);

                snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
                snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_MOUSE_SCROLL);

                snzu_instanceSelect(&main_sceneUIInstance);
                snzu_frameStart(scratch, HMM_V2(0, 0), dt);

                if (main_currentView == SC_VIEW_SCENE) {
                    tl_Op* op = main_timeline.activeOp;
                    if (op) {
                        HMM_Vec3 cameraPos, mouseDir;
                        HMM_Mat4 vp;
                        main_sceneBuild(&op->scene, HMM_V2(w, h), inter, &cameraPos, &mouseDir, &vp);

                        // FIXME: don't check for kinds because that isn't how this is supposed to work
                        if (op->kind == TL_OPK_SKETCH) {
                            tl_OpSketch* opSketch = &op->val.sketch;
                            geo_Align alignOfSketch = geo_alignZero();
                            sku_drawAndBuildSketch(&opSketch->sketch, alignOfSketch, vp, cameraPos, soundVal, HMM_V2(w, h), scratch);
                            sku_endFrameForUIInstance(inputs, alignOfSketch, vp, cameraPos, mouseDir);
                        } else {
                            if (op->scene.mesh) {
                                geo_meshBuild(op->scene.mesh, vp, cameraPos, mouseDir, inter, HMM_V2(w, h), scratch);
                                snzu_frameDrawAndGenInteractions(inputs, HMM_M4D(1.0f));
                            }
                        }
                    }
                } else if (main_currentView == SC_VIEW_TIMELINE) {
                    if (main_timeline.activeOp) {
                        geo_Mesh* mesh = main_timeline.activeOp->scene.mesh;
                        if (mesh) {
                            ren3d_Mesh* renderMesh = &mesh->renderMesh;
                            main_drawTimelineMeshPreview(dt, HMM_V2(w, h), renderMesh);
                        }
                    }
                    HMM_Mat4 vp = { 0 };
                    snzu_Input inputCopy = inputs;
                    tl_build(&main_timeline, scratch, rightPanelSize, inter->mousePosLocal, soundVal, &inputCopy.mousePos, &vp);
                    snzu_frameDrawAndGenInteractions(inputCopy, vp);
                } else {
                    SNZ_ASSERTF(false, "unreachable. view: %d", main_currentView);
                } // end timeline/scene switch
                snzu_instanceSelect(&main_uiInstance);

                // default scene screen if nothing active in timline.
                // out here so that it is using the correct UI instance.
                if (main_currentView == SC_VIEW_SCENE) {
                    if (!main_timeline.activeOp) {
                        snzu_boxNew("nothing in scene");
                        snzu_boxFillParent();
                        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Nothing Active. Check out the timeline.");
                        // FIXME: link text to go there
                    }
                }
            } else if (main_currentView == SC_VIEW_SETTINGS) {
                set_build(&main_settings);
            } else if (main_currentView == SC_VIEW_SHORTCUTS) {
                sc_buildSettings();
            } else {
                SNZ_ASSERTF(false, "unreachable view case, view was: %d", main_currentView);
            }
        }

        bool openHintWindow = inputs.mousePos.X > (screenSize.X - 20);
        if (main_settings.hintWindowAlwaysOpen) {
            openHintWindow = true;
        }

        sk_Sketch* activeSketch = NULL;
        {
            if (main_timeline.activeOp) {
                if (main_timeline.activeOp->kind == TL_OPK_SKETCH) {
                    activeSketch = &main_timeline.activeOp->val.sketch.sketch;
                }
            }
        }
        sc_updateAndBuildHintWindow(activeSketch, &main_timeline, &main_currentView, scratch, openHintWindow);

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
