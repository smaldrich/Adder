#pragma once

#include "PoolAlloc.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    SC_VIEW_SCENE,
    SC_VIEW_DOCS,
    SC_VIEW_SETTINGS,
    SC_VIEW_SHORTCUTS,
} sc_View;

typedef struct {
    SDL_KeyCode key;
    SDL_Keymod mods;
} _sc_KeyPress;

typedef struct {
    sk_Sketch* activeSketch;
    sc_View* currentView;  // read/write
} _sc_CommandArgs;

typedef bool (*_sc_CommandFunc)(_sc_CommandArgs args);

typedef struct {
    _sc_KeyPress key;
    _sc_CommandFunc func;
    const char* nameLabel;
    const char* keyLabel;
} _sc_Command;

_sc_Command* _sc_commands = NULL;
int64_t _sc_commandCount = 0;
PoolAlloc* _sc_commandPool = NULL;

static _sc_Command* _sc_commandInit(const char* displayName, const char* keyName, SDL_KeyCode code, SDL_Keymod mod, _sc_CommandFunc func) {
    _sc_Command* c = poolAllocPushArray(_sc_commandPool, _sc_commands, _sc_commandCount, _sc_Command);
    *c = (_sc_Command){
        .nameLabel = displayName,
        .keyLabel = keyName,
        .func = func,
        .key = (_sc_KeyPress){
            .key = code,
            .mods = mod,
        },
    };
    return c;
}

bool _scc_goToSettings(_sc_CommandArgs args) {
    *args.currentView = SC_VIEW_SETTINGS;
    return true;
}

bool _scc_goToDocs(_sc_CommandArgs args) {
    *args.currentView = SC_VIEW_DOCS;
    return true;
}

bool _scc_goToShortcuts(_sc_CommandArgs args) {
    *args.currentView = SC_VIEW_SHORTCUTS;
    return true;
}

bool _scc_goToMainScene(_sc_CommandArgs args) {
    *args.currentView = SC_VIEW_SCENE;
    return true;
}

bool _scc_delete(_sc_CommandArgs args) {
    for (sk_Line* l = args.activeSketch->firstLine; l; l = l->next) {
        if (l->uiInfo.selected) {
            l->markedForDelete = true;
        }
    }
    for (sk_Constraint* c = args.activeSketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->uiInfo.selected) {
            c->markedForDelete = true;
        }
    }
    return true;
}

bool _scc_distanceConstraint(_sc_CommandArgs args) {
    int selectedCount = 0;
    sk_Line* firstLine = NULL;

    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->uiInfo.selected) {
            firstLine = line;
            selectedCount++;
        }
    }

    if (selectedCount != 1) {
        return true;
    }

    float currentLength = HMM_Len(HMM_Sub(firstLine->p2->pos, firstLine->p1->pos));
    sk_sketchAddConstraintDistance(args.activeSketch, firstLine, currentLength);
    // FIXME: focus the constraint as soon as it gets added

    return true;
}

bool _scc_angleConstraint(_sc_CommandArgs args) {
    int selectedCount = 0;
    sk_Line* lines[2] = {NULL, NULL};
    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->uiInfo.selected) {
            selectedCount++;
            if (selectedCount > 2) {
                return true;
            }
            lines[selectedCount - 1] = line;
        }
    }

    if (selectedCount != 2) {
        return true;
    }

    float minDist = INFINITY;
    bool isP1OnLine1 = true;
    bool isP1OnLine2 = true;
    for (int i = 0; i < 2; i++) {
        HMM_Vec2 a = lines[0]->pts[i]->pos;
        for (int j = 0; j < 2; j++) {
            HMM_Vec2 b = lines[1]->pts[j]->pos;
            float dist = HMM_Len(HMM_Sub(b, a));
            if (dist < minDist) {
                minDist = dist;
                isP1OnLine1 = i == 0;
                isP1OnLine2 = j == 0;
            }
        }
    }

    float angle1 = sk_angleOfLine(lines[0]->p1->pos, lines[0]->p2->pos, !isP1OnLine1);
    float angle2 = sk_angleOfLine(lines[1]->p1->pos, lines[1]->p2->pos, !isP1OnLine2);

    float diff = angle2 - angle1;
    while (diff > HMM_AngleDeg(180)) {
        diff -= HMM_AngleDeg(360);
    }
    while (diff < HMM_AngleDeg(-180)) {
        diff += HMM_AngleDeg(360);
    }

    if (diff < 0) {
        sk_sketchAddConstraintAngle(args.activeSketch, lines[1], !isP1OnLine2, lines[0], !isP1OnLine1, -diff);
    } else {
        sk_sketchAddConstraintAngle(args.activeSketch, lines[0], !isP1OnLine1, lines[1], !isP1OnLine2, diff);
    }

    // FIXME: focus immediately
    return true;
}

void sc_init(PoolAlloc* pool) {
    _sc_commandPool = pool;

    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, _scc_delete);
    _sc_commandInit("distance", "D", SDLK_d, KMOD_NONE, _scc_distanceConstraint);
    _sc_commandInit("angle", "A", SDLK_a, KMOD_NONE, _scc_angleConstraint);

    _sc_commandInit("goto shortcuts", "C", SDLK_c, KMOD_LSHIFT, _scc_goToShortcuts);
    _sc_commandInit("goto docs", "D", SDLK_d, KMOD_LSHIFT, _scc_goToDocs);
    _sc_commandInit("goto main scene", "W", SDLK_w, KMOD_LSHIFT, _scc_goToMainScene);
    _sc_commandInit("goto settings", "S", SDLK_s, KMOD_LSHIFT, _scc_goToSettings);
    // FIXME: shift icon instead of these
}

void sc_updateAndBuildHintWindow(sk_Sketch* activeSketch, sc_View* outCurrentView) {
    snzu_boxNew("updatesParent");
    snzu_boxSetSizeMarginFromParent(30);

    _sc_Command** const activeCommand = SNZU_USE_MEM(_sc_Command*, "activeCommand");
    {
        snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_NONE);  // FIXME: huh
        if (inter->keyAction == SNZU_ACT_DOWN) {
            _sc_KeyPress kp = (_sc_KeyPress){
                .key = inter->keyCode,
                .mods = inter->keyMods,
            };

            // FIXME: this activates even when typing in a scene textbox bc it's a different instance
            if (!*activeCommand && snzu_isNothingFocused()) {
                for (int i = 0; i < _sc_commandCount; i++) {
                    _sc_Command* c = &_sc_commands[i];
                    // FIXME: left shift only is required thats bad
                    if (kp.key == c->key.key && (kp.mods == c->key.mods)) {
                        *activeCommand = c;
                        break;
                    }
                }
            }

            if (kp.key == SDLK_ESCAPE || !snzu_isNothingFocused()) {
                *activeCommand = NULL;
            }
        }
    }  // command handling

    _sc_CommandArgs args = (_sc_CommandArgs){
        .activeSketch = activeSketch,
        .currentView = outCurrentView,
    };

    snzu_boxScope() {
        if (*activeCommand != NULL) {
            // snzu_boxNew("commandWindow");
            // snzu_boxSetSizeFromStart(HMM_V2(300, 400));
            // snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_TOP);
            // snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
            // snzu_boxSetColor(ui_colorBackground);
            // snzu_boxSetBorder(ui_borderThickness, ui_colorText);
            // snzu_boxSetCornerRadius(ui_cornerRadius);
            // snzu_boxScope() {
            bool done = (*activeCommand)->func(args);
            if (done) {
                *activeCommand = NULL;
            }
            // }
        }

        snzu_boxNew("shortcutWindow");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEnd(HMM_V2(400, 200));
        snzu_boxSetCornerRadius(ui_cornerRadius);
        snzu_boxSetBorder(ui_borderThickness, ui_colorText);
        snzu_boxSetColor(ui_colorBackground);

        snzu_boxScope() {
            snzu_boxNew("margin");
            snzu_boxSetSizeMarginFromParent(20);
            snzu_boxSetSizeMarginFromParentAx(23, SNZU_AX_X);
            snzu_boxScope() {
                for (int i = 0; i < _sc_commandCount; i++) {
                    _sc_Command* c = &_sc_commands[i];
                    snzu_boxNew(c->nameLabel);
                    snzu_boxFillParent();
                    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_lightLabelFont.renderedSize + 2 * ui_padding);
                    snzu_boxScope() {
                        snzu_boxNew("desc");
                        snzu_boxSetDisplayStr(&ui_lightLabelFont, ui_colorText, c->nameLabel);
                        snzu_boxSetSizeFitText(ui_padding);
                        snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
                        snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_CENTER);

                        snzu_boxNew("key");
                        snzu_boxSetDisplayStr(&ui_shortcutFont, ui_colorText, c->keyLabel);
                        snzu_boxSetSizeFitText(ui_padding);
                        snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_LEFT);
                        snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_CENTER);
                    }
                }
            }
            snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y);
            snzuc_scrollArea();
        }  // end hints window
    }  // end entire window parent
}

void sc_buildSettings() {
    ui_menuMargin();
    snzu_boxScope() {
        snzu_boxNew("title");
        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Shortcuts");
        snzu_boxSetSizeFitText(ui_padding);
    }
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
    snzuc_scrollArea();
}