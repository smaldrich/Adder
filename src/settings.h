#pragma once

#include "snooze.h"
#include "ui.h"
#include "ser.h"

typedef struct {
    bool darkMode;
    bool musicMode;
    bool skybox;
    bool hintWindowAlwaysOpen;
    bool leftBarAlwaysOpen;
    bool timelinePreviewSpinBackground;
    bool squishyCamera;
    bool crosshair;
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

        const char* tags[] = {
            "dark theme",
            "music mode",
            "sky box",
            "keep cheat sheet open",
            "keep left bar open",
            "spin the background in timeline preview",
            "squishy camera",
            "crosshair",
        };
        int tagCount = sizeof(tags) / sizeof(*tags);

        snzu_boxNew()
            float labelColWidth = 0;
        for (int i = 0; i < tagCount; i++) {
        }

        ui_switch("dark theme", &settings->darkMode);
        ui_switch("music mode", &settings->musicMode);
        ui_switch("sky box", &settings->skybox);
        ui_switch("keep cheat sheet open", &settings->hintWindowAlwaysOpen);
        ui_switch("keep left bar open", &settings->leftBarAlwaysOpen);
        ui_switch("spin the background in timeline preview", &settings->timelinePreviewSpinBackground);
        ui_switch("squishy camera", &settings->squishyCamera);
        ui_switch("crosshair", &settings->crosshair);

        const char* strs[] = {
            "Red",
            "Light",
            "Space",
        };
        int64_t x = 0;
        snzu_boxNew("dropdown");
        snzu_boxSetDisplayStr(&ui_labelFont, ui_colorText, "AHHHHHHHHHH");
        snzu_boxSetSizeFitText(ui_padding);
        ui_dropdown(strs, 3, &x);
    }
    // FIXME: UI variable for gap here (?)
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
    snzuc_scrollArea();
}
