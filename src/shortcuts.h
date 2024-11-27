#include "PoolAlloc.h"
#include "snooze.h"
#include "ui.h"

typedef struct {
    SDL_KeyCode key;
    SDL_Keymod mods;
} _sc_KeyPress;

typedef bool (*_sc_CommandFunc)();

typedef struct {
    _sc_KeyPress key;
    _sc_CommandFunc func;
    const char* nameLabel;
    const char* keyLabel;
    bool immediate;
} _sc_Command;

_sc_Command* _sc_commands = NULL;
int64_t _sc_commandCount = 0;
PoolAlloc* _sc_commandPool = NULL;

static _sc_Command* _sc_commandInit(const char* displayName, const char* keyName, SDL_KeyCode code, SDL_Keymod mod, bool immediate, _sc_CommandFunc func) {
    _sc_Command* c = poolAllocPushArray(_sc_commandPool, _sc_commands, _sc_commandCount, _sc_Command);
    *c = (_sc_Command){
        .nameLabel = displayName,
        .keyLabel = keyName,
        .func = func,
        .immediate = immediate,
        .key = (_sc_KeyPress){
            .key = code,
            .mods = mod,
        },
    };
    return c;
}

bool _scc_delete() {
    return true;
}

bool _scc_distanceConstraint() {
    bool cancelled = ui_buttonWithHighlight(true, "exit this");
    snzu_boxOrderSiblingsInRowRecurse(10, SNZU_AX_Y);
    return cancelled;
}

void sc_init(PoolAlloc* pool) {
    _sc_commandPool = pool;
    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, true, _scc_delete);
    _sc_commandInit("distance", "D", SDLK_d, KMOD_NONE, false, _scc_distanceConstraint);
}

void sc_updateAndBuildHintWindow() {
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

            if (!*activeCommand) {

                for (int i = 0; i < _sc_commandCount; i++) {
                    _sc_Command* c = &_sc_commands[i];
                    if (kp.key == c->key.key && kp.mods == c->key.mods) {
                        *activeCommand = c;
                        break;
                    }
                }

            }

            if (kp.key == SDLK_ESCAPE) {
                *activeCommand = NULL;
            }
        }
    }  // command handling

    snzu_boxScope() {
        if (*activeCommand != NULL) {
            if ((*activeCommand)->immediate) {
                (*activeCommand)->func();
                *activeCommand = NULL;
            } else {
                snzu_boxNew("commandWindow");
                snzu_boxSetSizeFromStart(HMM_V2(300, 400));
                snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_TOP);
                snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
                snzu_boxSetColor(ui_colorBackground);
                snzu_boxSetBorder(ui_borderThickness, ui_colorText);
                snzu_boxSetCornerRadius(ui_cornerRadius);
                snzu_boxScope() {
                    bool done = (*activeCommand)->func();
                    if (done) {
                        *activeCommand = NULL;
                    }
                }  // end immidiate check
            }  // end command window
        }

        snzu_boxNew("shortcutWindow");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEnd(HMM_V2(300, 200));
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
                    snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_lightLabelFont.renderedSize + 2 * SNZU_TEXT_PADDING);
                    snzu_boxScope() {
                        snzu_boxNew("desc");
                        snzu_boxSetDisplayStr(&ui_lightLabelFont, ui_colorText, c->nameLabel);
                        snzu_boxSetSizeFitText();
                        snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
                        snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_CENTER);

                        snzu_boxNew("key");
                        snzu_boxSetDisplayStr(&ui_shortcutFont, ui_colorText, c->keyLabel);
                        snzu_boxSetSizeFitText();
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
        snzu_boxSetSizeFitText();
    }
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
    snzuc_scrollArea();
}