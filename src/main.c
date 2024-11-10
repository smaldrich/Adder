#include "snooze.h"
#include "PoolAlloc.h"
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

void main_init(snz_Arena* scratch) {
    fontArena = snz_arenaInit(10000000, "main font arena");

    titleFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-Regular.ttf", 48);
    paragraphFont = snzr_fontInit(&fontArena, scratch, "res/fonts/OpenSans-Light.ttf", 16);
    labelFont = snzr_fontInit(&fontArena, scratch, "res/fonts/AzeretMono-LightItalic.ttf", 20);
}

void main_frame(float dt, snz_Arena* scratch) {
    assert(scratch || !scratch);
    assert(dt || !dt);

    snzu_boxNew("parent");
    snzu_boxFillParent();
    snzu_boxSetColor(BACKGROUND_COLOR);
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
}

int main() {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    csg_tests();

    snz_main("CADDER V0.0", main_init, main_frame);

    return 0;
}