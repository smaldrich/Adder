#include "PoolAlloc.h"
#include "docs.h"
#include "geometry.h"
#include "render3d.h"
#include "settings.h"
#include "shortcuts.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "sketchui.h"
#include "snooze.h"
#include "sound.h"
#include "stb/stb_image.h"
#include "timeline.h"
#include "timelineui.h"
#include "ui.h"
#include "mesh.h"
#include "meshui.h"
#include "geometry.h"
#include "ser.h"
#include "csg2.h"

snz_Arena main_appLifetimeArena;
snz_Arena main_fontArena;
snz_Arena main_baseMeshArena;
PoolAlloc main_baseMeshPool;
snz_Arena main_sketchArena;

tl_Timeline main_timeline;
snz_Arena main_tlArena;
PoolAlloc main_tlGeneratedPool;
snz_Arena main_tlGeneratedArena;
mesh_Scene main_timelineScene;

snzu_Instance main_uiInstance;
snzu_Instance main_sceneUIInstance;
snzr_FrameBuffer main_sceneFB;

sc_View main_currentView = SC_VIEW_TIMELINE;
mesh_GeoKind main_currentGeoFilter = MESH_GK_FACE;
tl_Op* main_argBarFocusOverride = NULL;
set_Settings main_settings;

#define MAIN_SETTINGS_PATH "settings.adder"

void main_init(snz_Arena* scratch, SDL_Window* window) {
    SNZ_ASSERT(window || !window, "huh"); //  getting rid of unused arg warning

    _poolAllocTests();
    sk_tests();
    fflush(_snz_logFile);
    skt_tests();
    fflush(_snz_logFile);
    ser_tests();
    fflush(_snz_logFile);
    csg_tests();
    fflush(_snz_logFile);

    main_appLifetimeArena = snz_arenaInit(100000, "main app lifetime arena");
    main_fontArena = snz_arenaInit(10000000, "main font arena");
    main_sketchArena = snz_arenaInit(10000000, "main sketch arena");
    main_baseMeshArena = snz_arenaInit(1000000000, "main base mesh arena");
    main_baseMeshPool = poolAllocInit();

    main_tlArena = snz_arenaInit(10000000, "main tl arena");
    main_tlGeneratedArena = snz_arenaInit(1000000000, "main tl gen arena");
    main_tlGeneratedPool = poolAllocInit();

    main_uiInstance = snzu_instanceInit();
    snzu_instanceSelect(&main_uiInstance);
    main_sceneUIInstance = snzu_instanceInit();
    main_settings = set_settingsDefault(); // loaded from disk below

    sound_init();
    ui_init(&main_fontArena, scratch); // tex loads happen here
    ren3d_init(scratch);
    docs_init();
    sc_init(&main_baseMeshPool);

    {
        ser_begin(&main_appLifetimeArena);
        set_settingsSpec();
        ser_end();

        FILE* f = fopen(MAIN_SETTINGS_PATH, "r");
        if (f) {
            set_Settings* loaded = NULL;
            ser_ReadError err = ser_read(f, set_Settings, scratch, scratch, (void**)&loaded);
            if (!err == SER_RE_OK) {
                main_settings = set_settingsDefault();
                SNZ_LOGF("Loading settings file failed. Code: %d.", err);
            }
            main_settings = *loaded;
        }
        fclose(f);
    }

    snz_arenaClear(scratch);

    main_sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(500, 500, NULL));

    main_timeline = tl_timelineInit(&main_tlArena, &main_tlGeneratedArena, &main_tlGeneratedPool);
    {
        mesh_FaceSlice faces = mesh_stlFileToFaces("res/demos/bracket.stl", &main_baseMeshArena, scratch, &main_baseMeshPool);
        tl_timelinePushBaseGeometry(&main_timeline, HMM_V2(0, -200), faces);

        snz_arenaClear(scratch);

        faces = mesh_stlFileToFaces("testing/union.stl", &main_baseMeshArena, scratch, &main_baseMeshPool);
        tl_timelinePushBaseGeometry(&main_timeline, HMM_V2(-400, -200), faces);

        faces = mesh_stlFileToFaces("testing/difference.stl", &main_baseMeshArena, scratch, &main_baseMeshPool);
        tl_timelinePushBaseGeometry(&main_timeline, HMM_V2(-300, -200), faces);

        faces = mesh_cube(&main_baseMeshArena);
        tl_timelinePushBaseGeometry(&main_timeline, HMM_V2(0, 0), faces);
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
    mesh_Scene* scene,
    HMM_Vec2 panelSize, snzu_Interaction* panelInter, float dt,
    HMM_Vec3* outCamPos, HMM_Vec3* outMouseDir,
    HMM_Mat4* outVP) {
    // FIXME: automagic redo of the origin
    // FIXME: crosshair of the camera
    HMM_Vec2* const smoothedOrbitAngle = SNZU_USE_MEM(HMM_Vec2, "smoothOrbitAngle");
    float* const smoothedOrbitDist = SNZU_USE_MEM(float, "smoothOrbitDist");
    geo_Align* const smoothedOrbitOrigin = SNZU_USE_MEM(geo_Align, "smoothOrbitOrigin");

    { // apply inputs to move camera around
        HMM_Vec2* const lastMousePos = SNZU_USE_MEM(HMM_Vec2, "lastMousePos");
        if (panelInter->mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN) {
            *lastMousePos = panelInter->mousePosGlobal;
        }
        // FIXME: both pan and rotate should keep a point on the geometry on the cursor instead of just diffing like they are
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
                diff = HMM_Mul(diff, -0.005f);  // sens
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

    { // smoothing of camera pos
        float smoothPct = dt * 25;
        smoothPct = SNZ_MIN(smoothPct, 1);
        if (!main_settings.squishyCamera) {
            smoothPct = 1;
        }
        *smoothedOrbitDist = HMM_Lerp(*smoothedOrbitDist, smoothPct, scene->orbitDist);
        // FIXME: not wrapping angles here, so sometimes it goes insane
        *smoothedOrbitAngle = HMM_Lerp(*smoothedOrbitAngle, smoothPct, scene->orbitAngle);
        smoothedOrbitOrigin->pt = HMM_Lerp(smoothedOrbitOrigin->pt, smoothPct, scene->orbitOrigin.pt);

        smoothedOrbitOrigin->vertical = HMM_Norm(HMM_Lerp(smoothedOrbitOrigin->vertical, smoothPct, scene->orbitOrigin.vertical));
        // FIXME: sometimes this gets 'caught' if the target jumps 180, interpolation doesn't change the angle, so it just stays opposite
        smoothedOrbitOrigin->normal = HMM_Norm(HMM_Lerp(smoothedOrbitOrigin->normal, smoothPct, scene->orbitOrigin.normal));
    }

    { // generate VP / camera pos / mouse dir
        HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, *smoothedOrbitDist));
        view = HMM_MulM4(HMM_Rotate_RH(smoothedOrbitAngle->X, HMM_V3(1, 0, 0)), view);
        view = HMM_MulM4(HMM_Rotate_RH(smoothedOrbitAngle->Y, HMM_V3(0, 1, 0)), view);
        view = HMM_MulM4(geo_alignToM4(geo_alignZero(), *smoothedOrbitOrigin), view);
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

    if (main_timeline.activeOp) {
        tl_Op* op = main_timeline.activeOp;
        if (op->kind == TL_OPK_SKETCH) {
            sk_Sketch* sketch = &op->val.sketch;
            sk_sketchClearElementsMarkedForDelete(sketch);
            sk_sketchSolve(sketch);
        }
    }

    if (main_argBarFocusOverride && main_argBarFocusOverride->markedForDeletion) {
        main_argBarFocusOverride = NULL;
    }
    tl_timelineCullOpsMarkedForDelete(&main_timeline);

    if (main_settings.darkMode) {
        ui_setThemeDark();
    } else {
        ui_setThemeLight();
    }

    snzu_boxNew("parent");
    snzu_boxFillParent();
    snzu_boxScope() {
        float soundVal = main_getSmoothedSound();
        snzu_Interaction* leftPanelInter = SNZU_USE_MEM(snzu_Interaction, "leftPanelInter");
        HMM_Vec2 rightPanelSize = HMM_V2(0, 0);
        float leftPanelSize = 0;
        float* const leftPanelStayClosedTimer = SNZU_USE_MEM(float, "leftPanelClosedTimer"); // in seconds,  to forcibly close the left panel when the user presses a button to change view

        {
            *leftPanelStayClosedTimer -= dt;
            bool target = main_settings.leftBarAlwaysOpen || (leftPanelInter->hovered && (*leftPanelStayClosedTimer <= 0));
            float* const leftPanelAnim = SNZU_USE_MEM(float, "leftPanelAnim");
            snzu_easeExp(leftPanelAnim, target, ui_menuAnimationSpeed);
            leftPanelSize = *leftPanelAnim * 200;

            rightPanelSize = snzu_boxGetSizePtr(snzu_boxGetParent());
            rightPanelSize.X -= leftPanelSize;
        }

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
                    tl_Op* op = main_timeline.activeOp; // FIXME: hell is tl.activeOp
                    if (op) {
                        HMM_Vec3 cameraPos, mouseDir;
                        HMM_Mat4 vp;
                        main_sceneBuild(&main_timelineScene, HMM_V2(w, h), inter, dt, &cameraPos, &mouseDir, &vp);

                        if (op->kind == TL_OPK_SKETCH) {
                            sk_Sketch* opSketch = &op->val.sketch;
                            geo_Align alignOfSketch = geo_alignZero();
                            sku_drawAndBuildSketch(opSketch, alignOfSketch, vp, cameraPos, soundVal, HMM_V2(w, h), scratch);
                            sku_endFrameForUIInstance(inputs, alignOfSketch, vp, cameraPos, mouseDir);
                        } else {
                            mesh_GeoKind filter = main_currentGeoFilter;
                            if (main_settings.geometryFilter == false) {
                                filter = MESH_GK_CORNER | MESH_GK_EDGE | MESH_GK_FACE;
                            }
                            meshu_sceneBuild(filter, &main_timelineScene, vp, cameraPos, mouseDir, inter, HMM_V2(w, h), scratch);
                            snzu_frameDrawAndGenInteractions(inputs, HMM_M4D(1.0f));
                        }

                        // draw crosshair
                        if (main_settings.crosshair) {
                            // FIXME: looks jumpy with camera in squishy mode
                            // FIXME: I don't like gl here
                            glDisable(GL_DEPTH_TEST);
                            geo_Align origin = main_timelineScene.orbitOrigin;
                            float scale = main_timelineScene.orbitDist * 0.05;
                            HMM_Vec4 pts[2] = { 0 };
                            pts[0].XYZ = HMM_Add(origin.pt, HMM_Mul(origin.vertical, scale));
                            pts[1].XYZ = origin.pt;
                            // pts[2].XYZ = HMM_Add(origin.pt, HMM_Mul(origin.normal, scale));
                            snzr_drawLine(pts, sizeof(pts) / sizeof(*pts), ui_colorTransparentAccent, 5, vp);
                            glEnable(GL_DEPTH_TEST);
                        }
                    }
                } else if (main_currentView == SC_VIEW_TIMELINE) {
                    if (main_timeline.activeOp) {
                        main_drawTimelineMeshPreview(dt, HMM_V2(w, h), &main_timelineScene.renderMesh);
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
                    tl_Op* activeOp = main_timeline.activeOp;
                    if (!activeOp) {
                        snzu_boxNew("nothing in scene");
                        snzu_boxFillParent();
                        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Nothing Active. Check out the timeline.");
                        // FIXME: link text to go there
                    } else if (main_settings.debugMode) {
                        snzu_boxNew("debug menu");
                        snzu_boxFillParent();
                        snzu_boxScope() {
                            ui_debugLabel("op uid", scratch, "%lld", activeOp->uniqueId);
                            ui_debugLabel("faces", scratch, "%lld", main_timelineScene.faces.count);
                            ui_debugLabel("edges", scratch, "%lld", main_timelineScene.edges.count);
                            ui_debugLabel("corners", scratch, "%lld", main_timelineScene.corners.count);

                            int64_t triCount = 0;
                            int64_t selectedTriCount = 0;
                            for (int64_t i = 0; i < main_timelineScene.faces.count; i++) {
                                mesh_SceneGeo* f = &main_timelineScene.faces.elems[i];
                                triCount += f->faceTris.count;
                                if (f->sel.selected) {
                                    selectedTriCount += f->faceTris.count;
                                }
                            }
                            ui_debugLabel("tri count", scratch, "%lld", triCount);
                            ui_debugLabel("tris on face(s)", scratch, "%lld", selectedTriCount);

                            ui_debugLabel("frame time", scratch, "%.4f", dt);
                        }
                        snzu_boxOrderChildrenInRowRecurse(0, SNZU_AX_Y, SNZU_ALIGN_LEFT);
                    }
                }
            } else if (main_currentView == SC_VIEW_SETTINGS) {
                set_build(&main_settings);
            } else if (main_currentView == SC_VIEW_SHORTCUTS) {
                sc_buildSettings();
            } else {
                SNZ_ASSERTF(false, "unreachable view case, view was: %d", main_currentView);
            }

            tl_Op* selected = main_timeline.activeOp;
            if (main_argBarFocusOverride) {
                selected = main_argBarFocusOverride;
            }
            tl_argbarBuild(&main_timeline, selected, &main_argBarFocusOverride);
        } // end right panel

        snzu_boxNew("leftPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, leftPanelSize);
        snzu_boxSetColor(ui_colorBackground);
        snzu_boxScope() {
            snzu_boxNew("padding");
            snzu_boxSetSizeMarginFromParent(20);
            snzu_boxScope() {
                snzu_boxNew("scene list");
                snzu_boxFillParent();
                snzu_boxSizePctParent(0.5, SNZU_AX_Y);
                snzu_boxScope() {
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SCENE, "demo scene")) {
                        *leftPanelStayClosedTimer = 0.25;
                        main_currentView = SC_VIEW_SCENE;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_TIMELINE, "timeline")) {
                        *leftPanelStayClosedTimer = 0.25;
                        main_currentView = SC_VIEW_TIMELINE;
                    }
                }
                snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y, SNZU_ALIGN_LEFT);
                snzuc_scrollArea();

                snzu_boxNew("other view list");
                snzu_boxFillParent();
                snzu_boxSizeFromEndPctParent(0.5, SNZU_AX_Y);
                snzu_boxScope() {
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_DOCS, "docs")) {
                        *leftPanelStayClosedTimer = 0.25;
                        main_currentView = SC_VIEW_DOCS;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SETTINGS, "settings")) {
                        *leftPanelStayClosedTimer = 0.25;
                        main_currentView = SC_VIEW_SETTINGS;
                    }
                    if (ui_buttonWithHighlight(main_currentView == SC_VIEW_SHORTCUTS, "shortcuts")) {
                        *leftPanelStayClosedTimer = 0.25;
                        main_currentView = SC_VIEW_SHORTCUTS;
                    }
                    if (ui_buttonWithHighlight(false, "quit")) {
                        snz_quit();
                    }
                }
                snzu_boxOrderChildrenInRowRecurseAlignEnd(5, SNZU_AX_Y);
            }  // end padding

            snzu_boxNew("leftPanelBorder");
            snzu_boxFillParent();
            snzu_boxSetSizeFromEndAx(SNZU_AX_X, ui_borderThickness);
            snzu_boxSetColor(ui_colorText);
        }  // end leftpanel
        snzu_boxClipChildren(true);
        // FIXME: some hint in the lower left corner that this menu exists, same w/ right panel

        if (leftPanelSize < 10) {
            ui_hiddenPanelIndicator(0, true, "leftHoverIndicator");
        }

        snzu_boxNew("leftHoverDetector");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, leftPanelSize + 20);
        snzu_boxSetInteractionOutput(leftPanelInter, SNZU_IF_HOVER | SNZU_IF_ALLOW_EVENT_FALLTHROUGH);

        bool openHintWindow = inputs.mousePos.X > (screenSize.X - 20);
        if (main_settings.hintWindowAlwaysOpen) {
            openHintWindow = true;
        }

        sk_Sketch* activeSketch = NULL;
        if (main_timeline.activeOp) {
            if (main_timeline.activeOp->kind == TL_OPK_SKETCH) {
                activeSketch = &main_timeline.activeOp->val.sketch;
            }
        }
        sc_updateAndBuildHintWindow(
            activeSketch,
            &main_timeline,
            &main_currentView,
            &main_currentGeoFilter,
            &main_timelineScene,
            &main_argBarFocusOverride,
            scratch,
            openHintWindow);

        snzu_boxNew("capsIndicator");
        snzu_boxFillParent();
        snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_ALLOW_EVENT_FALLTHROUGH);
        if (inter->keyMods & KMOD_CAPS) {
            snzu_boxScope() {
                snzu_boxNew("text");
                snzu_boxSetDisplayStr(&ui_labelFont, ui_colorErr, "Caps lock on");
                snzu_boxSetSizeFitText(ui_padding);
                snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_CENTER);
                snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_BOTTOM);

                snzu_boxNew("bar");
                snzu_boxFillParent();
                snzu_boxSetColor(ui_colorErr);
                snzu_boxSetSizeFromEndAx(SNZU_AX_Y, ui_borderThickness);
            }
        }
    }

    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    snzr_callGLFnOrError(glViewport(0, 0, screenSize.X, screenSize.Y));
    HMM_Mat4 vp = HMM_Orthographic_RH_NO(0, screenSize.X, screenSize.Y, 0, 0, 1000000);
    snzu_frameDrawAndGenInteractions(inputs, vp);
}

int main() {
    snz_main("ADDER V0.0", "res/textures/icon.bmp", main_init, main_frame);
    sound_deinit();
    poolAllocDeinit(&main_baseMeshPool);

    // saving settings to file
    FILE* f = fopen(MAIN_SETTINGS_PATH, "w");
    if (!f) {
        SNZ_LOGF("Opening settings file to write at %s failed.", MAIN_SETTINGS_PATH);
    }
    if (f) {
        SNZ_ASSERT(ser_write(f, set_Settings, &main_settings, &main_appLifetimeArena) == SER_WE_OK,
            "Writing settings failed.");
    }

    return 0;
}
