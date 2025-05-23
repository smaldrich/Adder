#pragma once

#include "snooze.h"
#include "ui.h"
#include "ser.h"

typedef struct {
    // serialized
    bool darkMode;
    bool musicMode;
    bool skybox;
    bool hintWindowAlwaysOpen;
    bool leftBarAlwaysOpen;
    bool timelinePreviewSpinBackground;
    bool squishyCamera;
    bool crosshair;
    bool geometryFilter;

    // non serialized
    bool debugMode;
} set_Settings;
// NOTE: this is not meant to be a global anywhere, pass specific settings as flags down from wherever this
// is being persisted betw frames.

void set_settingsSpec() {
    ser_addStruct(set_Settings, false);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), darkMode);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), musicMode);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), skybox);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), hintWindowAlwaysOpen);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), leftBarAlwaysOpen);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), timelinePreviewSpinBackground);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), squishyCamera);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), crosshair);
    ser_addStructField(set_Settings, ser_tBase(SER_TK_UINT8), geometryFilter);
}

set_Settings set_settingsDefault() {
    set_Settings out = (set_Settings){
        .darkMode = false,
        .skybox = true,
        .musicMode = true,
        .leftBarAlwaysOpen = false,
        .hintWindowAlwaysOpen = false,
        .timelinePreviewSpinBackground = true,
        .squishyCamera = true,
        .crosshair = true,
        .geometryFilter = true,

        .debugMode = true,
    };
    return out;
}

// sets members in the settings struct, but won't do anything else, including modifying theme globals etc.
void set_build(set_Settings* settings) {
    ui_menuMargin();
    snzu_boxScope() {
        snzu_boxNew("title");
        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Settings");
        snzu_boxSetSizeFitText(ui_padding);

        snzu_boxNew("gap");
        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, 10);

        const char* labels[] = {
            "theme",
            "music mode",
            "sky box",
            "keep cheat sheet open",
            "keep left bar open",
            "spin the background in timeline preview",
            "squishy camera",
            "crosshair",
            "geometry filter",
            "debug mode"
        };

        snzu_boxNew("holder");
        snzu_boxFillParent();
        snzu_boxScope() {
            snzu_boxNew("left side");
            snzu_boxFillParent();
            snzu_boxScope() {
                for (uint64_t i = 0; i < (sizeof(labels) / sizeof(*labels)); i++) {
                    snzu_boxNewF("%d", i);
                    snzu_boxFillParent();
                    float heightWithPadding = ui_labelFont.renderedSize + 4 * ui_padding;
                    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, heightWithPadding);
                    if (i % 2 == 1) {
                        snzu_boxSetColor(ui_colorAlmostBackground);
                    }
                    snzu_boxScope() {
                        snzu_boxNewF("text", i);
                        snzu_boxSetSizeMarginFromParent(2 * ui_padding);
                        snzu_boxSetDisplayStr(&ui_labelFont, ui_colorText, labels[i]);
                        snzu_boxSetSizeFitText(ui_padding);
                    }
                }
            }
            snzu_boxOrderChildrenInRowRecurse(0, SNZU_AX_Y, SNZU_ALIGN_LEFT);
            float size = snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y);
            snzu_boxSetSizeFromStartAx(SNZU_AX_Y, size);
        }
        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y));
        snzu_boxScope() {
            snzu_boxNew("right side");
            snzu_boxFillParent();
            snzu_boxSizeFromEndPctParent(0.25, SNZU_AX_X);
            snzu_boxMoveKeepSizeRecurse(HMM_V2(0, ui_padding));
            snzu_boxScope() {
                const char* strs[] = {
                    "Light",
                    "Dark",
                    "who knows what this does",
                };
                int x = settings->darkMode;
                ui_dropdown("theme", strs, 3, &x);
                settings->darkMode = (bool)x;

                ui_switch("music", &settings->musicMode);
                ui_switch("sky", &settings->skybox);
                ui_switch("cheat sheet", &settings->hintWindowAlwaysOpen);
                ui_switch("left bar", &settings->leftBarAlwaysOpen);
                ui_switch("timeline preview", &settings->timelinePreviewSpinBackground);
                ui_switch("squishy camera", &settings->squishyCamera);
                ui_switch("crosshair", &settings->crosshair);
                ui_switch("geometryFilter", &settings->geometryFilter);
                ui_switch("debug mode", &settings->debugMode);
            }
            snzu_boxOrderChildrenInRowRecurse(2 * ui_padding, SNZU_AX_Y, SNZU_ALIGN_LEFT);
        }
    }
    // FIXME: UI variable for gap here (?)
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y, SNZU_ALIGN_LEFT);
    snzuc_scrollArea();
}
