#include "snooze.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "csg.h"
#include "serialization2.h"
#include "sketches.h"
#include "stb/stb_image.h"

snz_Arena fontArena;

snzr_Font titleFont;
snzr_Font paragraphFont;
snzr_Font labelFont;

#define TEXT_COLOR HMM_V4(60/255.0, 60/255.0, 60/255.0, 1)
#define BACKGROUND_COLOR HMM_V4(1, 1, 1, 1)
#define BORDER_THICKNESS 4

ren3d_Mesh mesh;
snzr_FrameBuffer sceneFB;

void main_init(snz_Arena* scratch) {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    csg_tests();

    fontArena = snz_arenaInit(10000000, "main font arena");

    titleFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-Regular.ttf", 48);
    paragraphFont = snzr_fontInit(&fontArena, scratch, "res/fonts/OpenSans-Light.ttf", 16);
    labelFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-LightItalic.ttf", 20);

    ren3d_init(scratch);

    snz_arenaClear(scratch);
    {
        csg_TriList cubeA = csg_cube(scratch);
        csg_TriList cubeB = csg_cube(scratch);
        csg_triListTransform(&cubeB, HMM_Rotate_RH(30, HMM_V3(1, 1, 1)));
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

        for (csg_TriListNode* tri = final->first; tri; tri = tri->next) {
            HMM_Vec3 triNormal = csg_triNormal(tri->a, tri->b, tri->c);
            for (int i = 0; i < 3; i++) {
                *poolAllocPushArray(&pool, verts, vertCount, ren3d_Vert) = (ren3d_Vert){
                    .pos = tri->elems[i],
                    .normal = triNormal,
                };
            }
        }
        mesh = ren3d_meshInit(verts, vertCount);
        poolAllocDeinit(&pool);
    } // end mesh for testing
    snz_arenaClear(scratch);

    sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(500, 500, NULL));
}

void main_frame(float dt, snz_Arena* scratch) {
    assert(scratch || !scratch);
    assert(dt || !dt);

    HMM_Vec2 rightPanelSize = HMM_V2(0, 0);

    snzu_boxNew("parent");
    snzu_boxFillParent();
    snzu_boxScope() {
        float* const leftPanelSize = SNZU_USE_MEM(float, "leftPanelSize");
        snzu_Interaction* leftPanelInter = SNZU_USE_MEM(snzu_Interaction, "leftPanelInter");
        if (leftPanelInter->dragged) {
            *leftPanelSize = leftPanelInter->mousePosGlobal.X;
        }
        // FIXME: cursor change
        *leftPanelSize = SNZ_MAX(200, SNZ_MIN(*leftPanelSize, snzu_boxGetSize(snzu_boxGetParent()).X - 200));

        rightPanelSize = snzu_boxGetSize(snzu_boxGetParent());
        rightPanelSize.X -= *leftPanelSize;

        snzu_boxNew("leftPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, *leftPanelSize);
        snzu_boxSetColor(BACKGROUND_COLOR);
        snzu_boxScope() {
            snzu_boxNew("title");
            snzu_boxSetDisplayStr(&titleFont, TEXT_COLOR, "// Settings");
            snzu_boxSetSizeFitText();

            snzu_boxNew("paragraph");
            snzu_boxSetDisplayStr(&paragraphFont, TEXT_COLOR, "lrughslidurghsdlirgnsdkjrgnsdlk jn");
            snzu_boxSetSizeFitText();

            snzu_boxNew("label");
            snzu_boxSetDisplayStr(&labelFont, TEXT_COLOR, "Main.cadder");
            snzu_boxSetSizeFitText();
        }
        snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
        snzu_boxClipChildren();

        snzu_boxNew("rightPanel");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_X, rightPanelSize.X); //FIXME: set size remaining util fn
        snzu_boxSetTexture(sceneFB.texture);
        snzu_boxScope() {
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

            orbitPos->Z += inter->mouseScrollY * orbitPos->Z * 0.05;

            HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, orbitPos->Z));
            view = HMM_MulM4(HMM_Rotate_RH(orbitPos->Y * 0.3, HMM_V3(-1, 0, 0)), view);
            view = HMM_MulM4(HMM_Rotate_RH(orbitPos->X * 0.3, HMM_V3(0, -1, 0)), view);
            view = HMM_InvGeneral(view);

            float aspect = rightPanelSize.X / rightPanelSize.Y;
            HMM_Mat4 proj = HMM_Perspective_RH_NO(90, aspect, 0.001, 100000);

            HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));

            uint32_t w = (uint32_t)rightPanelSize.X;
            uint32_t h = (uint32_t)rightPanelSize.Y;
            if (w != sceneFB.texture.width || h != sceneFB.texture.height) {
                snzr_frameBufferDeinit(&sceneFB);
                sceneFB = snzr_frameBufferInit(snzr_textureInitRBGA(w, h, NULL));
            }

            // FIXME: opengl here is gross
            snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, sceneFB.glId));
            snzr_callGLFnOrError(glViewport(0, 0, w, h));
            snzr_callGLFnOrError(glClearColor(1, 1, 1, 1));
            snzr_callGLFnOrError(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
            ren3d_drawMesh(&mesh, HMM_MulM4(proj, view), model, HMM_V3(-1, -1, -1));
        }

        snzu_boxNew("leftPanelBorder");
        snzu_boxFillParent();
        snzu_boxSetStartFromParentAx(*leftPanelSize, SNZU_AX_X);
        snzu_boxSetSizeFromStartAx(SNZU_AX_X, BORDER_THICKNESS);
        snzu_boxSetColor(TEXT_COLOR);
        snzu_boxSetInteractionOutput(leftPanelInter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS);

        snzu_boxNew("upperBorder");
        snzu_boxFillParent();
        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, BORDER_THICKNESS);
        snzu_boxSetColor(TEXT_COLOR);
    }
}

int main() {
    snz_main("CADDER V0.0", main_init, main_frame);
    return 0;
}
