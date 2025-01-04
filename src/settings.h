#include "snooze.h"
#include "ui.h"

typedef struct {
    bool darkMode;
    bool musicMode;
    bool skybox;
    bool hintWindowAlwaysOpen;
    bool leftBarAlwaysOpen;
    bool timelinePreviewSpinBackground;
} set_Settings;
// NOTE: this is not meant to be a global anywhere, pass specific settings as flags down from wherever this
// is being persisted betw frames.

set_Settings set_settingsDefault() {
    set_Settings out = (set_Settings){
        .darkMode = false,
        .skybox = true,
        .musicMode = true,
        .leftBarAlwaysOpen = true,
        .hintWindowAlwaysOpen = true,
        .timelinePreviewSpinBackground = true,
    };
    return out;
}

// FIXME: save/load settings to disk

// sets members in the settings struct, but won't do anything else, including modifying theme globals etc.
void set_build(set_Settings* settings) {
    ui_menuMargin();
    snzu_boxScope() {
        snzu_boxNew("title");
        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Settings");
        snzu_boxSetSizeFitText(ui_padding);

        ui_switch("dark theme", &settings->darkMode);
        ui_switch("music mode", &settings->musicMode);
        ui_switch("sky box", &settings->skybox);
        ui_switch("keep cheat sheet open", &settings->hintWindowAlwaysOpen);
        ui_switch("keep left bar open", &settings->leftBarAlwaysOpen);
        ui_switch("spin the background in timeline preview", &settings->timelinePreviewSpinBackground);
    }
    // FIXME: UI variable for gap here (?)
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
    snzuc_scrollArea();
}
