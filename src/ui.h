#pragma once

#include "snooze.h"
#include "stb/stb_image.h"

snzr_Font ui_titleFont;
snzr_Font ui_paragraphFont;
snzr_Font ui_labelFont;
snzr_Font ui_shortcutFont;
snzr_Font ui_lightLabelFont;

HMM_Vec4 ui_colorText;
HMM_Vec4 ui_colorAccent;
HMM_Vec4 ui_colorErr;
HMM_Vec4 ui_colorBackground;
HMM_Vec4 ui_colorAlmostBackground;
float ui_lightAmbient;
// FIXME: border color instead of defaulting to text color

float ui_cornerRadius = 15;
float ui_borderThickness = 4;
float ui_padding = 5;

snzr_Texture ui_skyBox = { 0 };
snzr_Texture ui_lightSky = { 0 };
snzr_Texture ui_darkSky = { 0 };

void ui_setThemeLight() {
    ui_colorText = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAccent = HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1);
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorBackground = HMM_V4(1, 1, 1, 1);
    ui_colorAlmostBackground = HMM_V4(0.9, 0.9, 0.9, 1);
    ui_lightAmbient = 0.8;
    ui_skyBox = ui_lightSky;
}

void ui_setThemeDark() {
    ui_colorText = HMM_V4(1, 1, 1, 1);
    ui_colorAccent = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorBackground = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAlmostBackground = HMM_V4(52 / 255.0, 52 / 255.0, 52 / 255.0, 1);
    ui_lightAmbient = 0.2;
    ui_skyBox = ui_darkSky;
}

void ui_init(snz_Arena* fontArena, snz_Arena* scratch) {
    // FIXME: load time on this is abhorrent
    int w, h, channels;
    uint8_t* pixels = stbi_load("res/textures/Deep Dusk Equirect.png", &w, &h, &channels, 4);
    SNZ_ASSERT(pixels, "Skybox load failed.");
    ui_darkSky = snzr_textureInitRBGA(w, h, pixels);

    ui_lightSky = _snzr_globs.solidTex; // FIXME: make this public plz

    ui_setThemeDark();

    ui_titleFont = snzr_fontInit(fontArena, scratch, "res/fonts/AzeretMono-Regular.ttf", 48);
    ui_paragraphFont = snzr_fontInit(fontArena, scratch, "res/fonts/OpenSans-Light.ttf", 16);
    ui_labelFont = snzr_fontInit(fontArena, scratch, "res/fonts/AzeretMono-LightItalic.ttf", 20);
    ui_shortcutFont = snzr_fontInit(fontArena, scratch, "res/fonts/AzeretMono-SemiBoldItalic.ttf", 26);
    ui_lightLabelFont = snzr_fontInit(fontArena, scratch, "res/fonts/AzeretMono-ExtraLightItalic.ttf", 26);
}

// returns true on the frame it is clicked
bool ui_buttonWithHighlight(bool selected, const char* name) {
    bool out = false;
    HMM_Vec2 size = snzr_strSize(&ui_labelFont, name, strlen(name), ui_labelFont.renderedSize);

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
        snzu_boxSetSizeFitText(ui_padding);
    }
    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y));

    return out;
}

_snzu_Box* ui_menuMargin() {
    _snzu_Box* box = snzu_boxNew("menu margin");
    HMM_Vec2 parentSize = snzu_boxGetSize(snzu_boxGetParent());
    snzu_boxSetStartFromParentStart(HMM_V2(parentSize.X * 0.1, parentSize.Y * 0.1));
    snzu_boxSetEndFromParentEnd(HMM_V2(parentSize.X * -0.1, parentSize.Y * -0.1));
    return box;
}

// constructs at 0, 0
void ui_switch(const char* boxTag, const char* label, bool* const state) {
    float textHeight = ui_labelFont.renderedSize;  // FIXME: no mucking like this
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
        snzu_boxSetStart(HMM_V2(0, ui_padding - 0.1 * ui_labelFont.renderedSize));  // FIXME: ew
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
        snzu_boxSetSizeFitText(ui_padding);
        snzu_boxSetPosAfterRecurse(10, SNZU_AX_X);  // FIXME: spacing var
    }
    snzu_boxSetSizeFitChildren();
}