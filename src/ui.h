#pragma once

#include "snooze.h"

snzr_Font ui_titleFont;
snzr_Font ui_paragraphFont;
snzr_Font ui_labelFont;
// font loading from main.c

HMM_Vec4 ui_colorText;
HMM_Vec4 ui_colorAccent;
HMM_Vec4 ui_colorErr;
HMM_Vec4 ui_colorBackground;
HMM_Vec4 ui_colorAlmostBackground;
float ui_borderThickness = 4;

void ui_setThemeLight() {
    ui_colorText = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAccent = HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1);
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);;
    ui_colorBackground = HMM_V4(1, 1, 1, 1);
    ui_colorAlmostBackground = HMM_V4(0.9, 0.9, 0.9, 1);
}

void ui_setThemeDark() {
    ui_colorText = HMM_V4(1, 1, 1, 1);
    ui_colorAccent = HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1);
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);;
    ui_colorBackground = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAlmostBackground = HMM_V4(52 / 255.0, 52 / 255.0, 52 / 255.0, 1);
}

void ui_init() {
    ui_setThemeLight();
}

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
        snzu_boxSetColor(ui_colorAccent);
        snzu_boxFillParent();
        snzu_boxSizeFromEndPctParent(0.3, SNZU_AX_Y);
        snzu_boxSizePctParent(*selectedAnim * 0.6, SNZU_AX_X);

        snzu_boxNew("text");
        snzu_boxSetStartFromParentKeepSizeRecurse(HMM_V2((*hoverAnim + *selectedAnim) * 10, 0));
        snzu_boxSetDisplayStr(&ui_labelFont, ui_colorText, name);
        snzu_boxSetSizeFitText();
    }
    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y));

    return out;
}

// constructs at 0, 0
void ui_switch(const char* boxTag, const char* label, bool* const state) {
    float textHeight = ui_labelFont.renderedSize; // FIXME: no mucking like this
    float sliderWidth = 40;
    float innerMargin = 4;

    snzu_boxNew(boxTag);

    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS);
    if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
        *state = !*state;
    }

    snzu_boxScope() {
        snzu_boxNew("switch back");
        snzu_boxSetStart(HMM_V2(0, SNZU_TEXT_PADDING - 0.1 * ui_labelFont.renderedSize)); // FIXME: ew
        snzu_boxSetSizeFromStart(HMM_V2(sliderWidth, textHeight));
        snzu_boxSetCornerRadius(textHeight / 2);

        float* const anim = SNZU_USE_MEM(float, "anim");
        if (snzu_useMemIsPrevNew()) {
            *anim = *state;
        }
        snzu_easeExp(anim, *state, 15);
        snzu_boxSetColor(HMM_Lerp(ui_colorBackground, *anim, ui_colorAccent));

        snzu_boxScope() {
            snzu_boxNew("button");
            snzu_boxSetSizeMarginFromParent(innerMargin);
            snzu_boxSetColor(HMM_Lerp(ui_colorAccent, *anim, ui_colorBackground));
            snzu_boxSetCornerRadius((textHeight - innerMargin * 2) / 2);
            snzu_boxSetStartFromParentAx(*anim * (sliderWidth - textHeight) + innerMargin, SNZU_AX_X);
            snzu_boxSetSizeFromStartAx(SNZU_AX_X, textHeight - innerMargin * 2);
        }

        snzu_boxNew("label");
        snzu_boxSetDisplayStr(&ui_labelFont, ui_colorText, label);
        snzu_boxSetSizeFitText();
        snzu_boxSetPosAfterRecurse(10, SNZU_AX_X); // FIXME: spacing var
    }
    snzu_boxSetSizeFitChildren();
}