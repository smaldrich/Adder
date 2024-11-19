#pragma once

#include "snooze.h"

snzr_Font ui_titleFont;
snzr_Font ui_paragraphFont;
snzr_Font ui_labelFont;
// font loading from main.c

#define UI_TEXT_COLOR HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1)
#define UI_ACCENT_COLOR HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1)
#define UI_BACKGROUND_COLOR HMM_V4(1, 1, 1, 1)
#define UI_BORDER_THICKNESS 4

// returns true on the frame it is clicked
bool ui_buttonWithHighlight(bool selected, const char* name) {
    bool out = false;
    HMM_Vec2 size = snzr_strSize(&ui_labelFont, name, strlen(name));

    snzu_boxNew(name);
    snzu_boxSetSizeFromStart(size);
    snzu_boxScope() {
        snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_MOUSE_BUTTONS | SNZU_IF_HOVER);
        out = inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN;

        float* const selectedAnim = SNZU_USE_MEM(float, "selectionAnim");
        snzu_easeExp(selectedAnim, selected, 10);
        float* const hoverAnim = SNZU_USE_MEM(float, "hoverAnim");
        snzu_easeExp(hoverAnim, inter->hovered, 20);

        snzu_boxNew("highlight");
        snzu_boxSetColor(UI_ACCENT_COLOR);
        snzu_boxFillParent();
        snzu_boxSizeFromEndPctParent(0.3, SNZU_AX_Y);
        snzu_boxSizePctParent(*selectedAnim * 0.6, SNZU_AX_X);

        snzu_boxNew("text");
        snzu_boxSetStartFromParentKeepSizeRecurse(HMM_V2((*hoverAnim + *selectedAnim) * 10, 0));
        snzu_boxSetDisplayStr(&ui_labelFont, UI_TEXT_COLOR, name);
        snzu_boxSetSizeFitText();
    }
    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y));

    return out;
}

typedef enum {
    UI_TK_DEFAULT,
    UI_TK_SKETCH_NEW,
    UI_TK_SKETCH_LINE,
    UI_TK_SKETCH_CONSTRAINT_ANGLE,
    UI_TK_SKETCH_CONSTRAINT_DISTANCE,
    UI_TK_EXTRUDE,
} _ui_ToolKind;

static _ui_ToolKind _ui_currentToolKind;

void ui_runTools() {
    snzu_boxNew("tool window");
}
