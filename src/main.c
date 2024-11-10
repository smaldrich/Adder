#include "snooze.h"
#include "PoolAlloc.h"
#include "render3d.h"
#include "csg.h"
#include "serialization2.h"
#include "sketches.h"

snz_Arena fontArena;

snzr_Font titleFont;
snzr_Font paragraphFont;
snzr_Font labelFont;

#define TEXT_COLOR HMM_V4(60/255.0, 60/255.0, 60/255.0, 1)
#define BACKGROUND_COLOR HMM_V4(1, 1, 1, 1)
#define BORDER_THICKNESS 4

ren3d_Mesh mesh;

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
        csg_TriList* bClipped = csg_bspClipTris(false, &cubeB, treeA, scratch);
        csg_triListInvert(bClipped);
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
}

void main_frame(float dt, snz_Arena* scratch) {
    assert(scratch || !scratch);
    assert(dt || !dt);

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

    float* const time = SNZU_USE_MEM(float, "time");
    *time += dt;

    HMM_Mat4 view = HMM_Translate(HMM_V3(0, 0, 4));
    // view = HMM_Rotate_RH(*time * 30, HMM_V3(0, 1, 0));
    view = HMM_InvGeneral(view);
    HMM_Mat4 proj = HMM_Perspective_RH_NO(90, 1, 0.001, 100000);

    HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
    ren3d_drawMesh(&mesh, HMM_MulM4(proj, view), model, HMM_V3(-1, -1, -1));
}

int main() {
    snz_main("CADDER V0.0", main_init, main_frame);
    return 0;
}