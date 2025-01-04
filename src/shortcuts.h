#pragma once

#include "PoolAlloc.h"
#include "geometry.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"
#include "timeline.h"

typedef enum {
    SC_VIEW_NONE = 0,
    SC_VIEW_SCENE = 1 << 0,
    SC_VIEW_DOCS = 1 << 1,
    SC_VIEW_SETTINGS = 1 << 2,
    SC_VIEW_SHORTCUTS = 1 << 3,
    SC_VIEW_TIMELINE = 1 << 4,
    SC_VIEW_ALL = SC_VIEW_SCENE | SC_VIEW_DOCS | SC_VIEW_SETTINGS | SC_VIEW_SHORTCUTS | SC_VIEW_TIMELINE,
} sc_View;

typedef struct {
    SDL_KeyCode key;
    SDL_Keymod mods;
} _sc_KeyPress;

typedef struct {
    snz_Arena* scratch;
    sk_Sketch* activeSketch;
    tl_Op* timelineFirstOp;
    sc_View* currentView;  // read/write
    bool firstFrame;
} _sc_CommandArgs;

typedef bool (*sc_CommandFunc)(_sc_CommandArgs args);

typedef struct {
    _sc_KeyPress key;
    sc_CommandFunc func;
    const char* nameLabel;
    const char* keyLabel;
    int64_t availibleViews;
} _sc_Command;

_sc_Command* _sc_commands = NULL;
int64_t _sc_commandCount = 0;
PoolAlloc* _sc_commandPool = NULL;
_sc_Command* _sc_activeCommand = NULL;

sc_CommandFunc sc_getActiveCommand() {
    if (_sc_activeCommand) {
        return _sc_activeCommand->func;
    }
    return NULL;
}

static _sc_Command* _sc_commandInit(const char* displayName, const char* keyName, SDL_KeyCode code, SDL_Keymod mod, int64_t availibleViewMask, sc_CommandFunc func) {
    _sc_Command* c = poolAllocPushArray(_sc_commandPool, _sc_commands, _sc_commandCount, _sc_Command);
    *c = (_sc_Command){
        .nameLabel = displayName,
        .keyLabel = keyName,
        .func = func,
        .key = (_sc_KeyPress){
            .key = code,
            .mods = mod,
        },
        .availibleViews = availibleViewMask,
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

bool _scc_goToTimeline(_sc_CommandArgs args) {
    *args.currentView = SC_VIEW_TIMELINE;
    return true;
}

bool _scc_sketchDelete(_sc_CommandArgs args) {
    for (sk_Point* p = args.activeSketch->firstPoint; p; p = p->next) {
        if (p->sel.selected) {
            p->markedForDelete = true;
        }
    }
    for (sk_Line* l = args.activeSketch->firstLine; l; l = l->next) {
        if (l->sel.selected) {
            l->markedForDelete = true;
        }
    }
    for (sk_Constraint* c = args.activeSketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->uiInfo.sel.selected) {
            c->markedForDelete = true;
        }
    }
    return true;
}

bool _scc_sketchAddDistanceConstraint(_sc_CommandArgs args) {
    int selectedCount = 0;
    sk_Line* firstLine = NULL;

    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->sel.selected) {
            firstLine = line;
            selectedCount++;
        }
    }

    if (selectedCount != 1) {
        return true;
    }

    float currentLength = HMM_Len(HMM_Sub(firstLine->p2->pos, firstLine->p1->pos));
    sk_Constraint* c = sk_sketchAddConstraintDistance(args.activeSketch, firstLine, currentLength);
    c->uiInfo.shouldStartFocus = true;
    const char* str = sk_constraintLabelStr(c, args.scratch);
    ui_textAreaSetStr(&c->uiInfo.textArea, str, strlen(str));
    return true;
}

bool _scc_sketchAddAngleConstraint(_sc_CommandArgs args) {
    int selectedCount = 0;
    sk_Line* lines[2] = { NULL, NULL };
    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->sel.selected) {
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

    sk_Constraint* c = NULL;
    if (diff < 0) {
        c = sk_sketchAddConstraintAngle(args.activeSketch, lines[1], !isP1OnLine2, lines[0], !isP1OnLine1, -diff);
    } else {
        c = sk_sketchAddConstraintAngle(args.activeSketch, lines[0], !isP1OnLine1, lines[1], !isP1OnLine2, diff);
    }
    c->uiInfo.shouldStartFocus = true;
    const char* str = sk_constraintLabelStr(c, args.scratch);
    ui_textAreaSetStr(&c->uiInfo.textArea, str, strlen(str));
    return true;
}

bool scc_sketchEnterLineMode(_sc_CommandArgs args) {
    if (args.firstFrame) {  // creating a line between two selected pts
        int ptCount = 0;
        sk_Point* pts[2] = { 0 };
        for (sk_Point* p = args.activeSketch->firstPoint; p; p = p->next) {
            if (p->sel.selected) {
                ptCount++;
                if (ptCount > 2) {
                    break;
                }
                pts[ptCount - 1] = p;
            }
        }
        if (ptCount == 2) {
            sk_sketchAddLine(args.activeSketch, pts[0], pts[1]);
            return true;
        }
    }

    // remainder of logic in sketchui.h
    return false;
}

bool _sc_anySelectedInSketch(sk_Sketch* sketch) {
    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        if (p->sel.selected) {
            return true;
        }
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        if (l->sel.selected) {
            return true;
        }
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->uiInfo.sel.selected) {
            return true;
        }
    }
    return false;
}

bool scc_sketchEnterMove(_sc_CommandArgs args) {
    if (args.firstFrame) {
        return !_sc_anySelectedInSketch(args.activeSketch);  // cancels if nothing selected
    }

    // bulk of logic in sketchui.h
    return false;
}

bool scc_sketchEnterRotate(_sc_CommandArgs args) {
    if (args.firstFrame) {
        return !_sc_anySelectedInSketch(args.activeSketch);  // cancels if nothing selected
    }

    // bulk of logic in sketchui.h
    return false;
}

bool _scc_timelineDelete() {
    SNZ_ASSERT(false, "this shit DO NOT WORK!!");
    return true;
}

bool _scc_timelineEnterMove() {
    printf("in timeline tried to move but fuck you\n");
    return true;
}

bool _scc_timelineEnterRotate() {
    printf("in timeline tried to rotate but fuck you\n");
    return true;
}

void sc_init(PoolAlloc* pool) {
    _sc_commandPool = pool;

    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, SC_VIEW_SCENE, _scc_sketchDelete);
    _sc_commandInit("line", "B", SDLK_b, KMOD_NONE, SC_VIEW_SCENE, scc_sketchEnterLineMode);
    _sc_commandInit("move", "G", SDLK_g, KMOD_NONE, SC_VIEW_SCENE, scc_sketchEnterMove);
    _sc_commandInit("rotate", "R", SDLK_r, KMOD_NONE, SC_VIEW_SCENE, scc_sketchEnterRotate);
    _sc_commandInit("distance", "D", SDLK_d, KMOD_NONE, SC_VIEW_SCENE, _scc_sketchAddDistanceConstraint);
    _sc_commandInit("angle", "A", SDLK_a, KMOD_NONE, SC_VIEW_SCENE, _scc_sketchAddAngleConstraint);

    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, SC_VIEW_TIMELINE, _scc_timelineDelete);
    _sc_commandInit("move", "G", SDLK_g, KMOD_NONE, SC_VIEW_TIMELINE, _scc_timelineEnterMove);
    _sc_commandInit("rotate", "R", SDLK_r, KMOD_NONE, SC_VIEW_TIMELINE, _scc_timelineEnterRotate);

    _sc_commandInit("goto shortcuts", "C", SDLK_c, KMOD_LSHIFT, SC_VIEW_ALL, _scc_goToShortcuts);
    _sc_commandInit("goto docs", "D", SDLK_d, KMOD_LSHIFT, SC_VIEW_ALL, _scc_goToDocs);
    _sc_commandInit("goto main scene", "W", SDLK_w, KMOD_LSHIFT, SC_VIEW_ALL, _scc_goToMainScene);
    _sc_commandInit("goto settings", "S", SDLK_s, KMOD_LSHIFT, SC_VIEW_ALL, _scc_goToSettings);
    _sc_commandInit("goto timeline", "T", SDLK_t, KMOD_LSHIFT, SC_VIEW_ALL, _scc_goToTimeline);
}

// immediately sets the active cmd to null, so make sure you don't trample shit
// FIXME: this is bad but so is making a buffer system
void sc_cancelActiveCommand() {
    _sc_activeCommand = NULL;
}

// aligns to the TL of parent, no padding, tagged with cmd label
static void _sc_buildCommandShortcutBox(_sc_Command* cmd, HMM_Vec4 textColor) {
    snzu_boxNew(cmd->nameLabel);
    snzu_boxFillParent();
    snzu_boxScope() {
        snzu_boxNew("icons container");
        snzu_boxFillParent();
        snzu_boxScope() {
            // FIXME: both :)
            if (cmd->key.mods & KMOD_LSHIFT) {
                snzu_boxNew("icon");
                float aspect = (float)ui_shiftTexture->width / (float)ui_shiftTexture->height;
                float height = ui_shortcutFont.ascent;
                snzu_boxSetStartFromParentKeepSizeRecurse(HMM_V2(0, height * 0.1));  // FIXME: this
                snzu_boxSetSizeFromStart(HMM_V2(aspect * height, height));
                snzu_boxSetTexture(*ui_shiftTexture);
                snzu_boxSetColor(textColor);
            }
        }
        snzu_boxSetSizeFitChildren();

        snzu_boxNew("char");
        snzu_boxFillParent();
        snzu_boxSetDisplayStr(&ui_shortcutFont, textColor, cmd->keyLabel);
        snzu_boxSetSizeFitText(0);
    }
    snzu_boxOrderChildrenInRowRecurse(1, SNZU_AX_X);
    snzu_boxSetSizeFitChildren();
}

void sc_updateAndBuildHintWindow(sk_Sketch* activeSketch, tl_Op* tlFirstOp, sc_View* outCurrentView, snz_Arena* scratch, bool targetOpen) {
    snzu_boxNew("updatesParent");
    snzu_boxFillParent();

    _sc_CommandArgs args = (_sc_CommandArgs){
        .scratch = scratch,
        .activeSketch = activeSketch,
        .timelineFirstOp = tlFirstOp,
        .currentView = outCurrentView,
        .firstFrame = false,
    };

    {
        snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_NONE);  // FIXME: huh
        if (inter->keyAction == SNZU_ACT_DOWN) {
            _sc_KeyPress kp = (_sc_KeyPress){
                .key = inter->keyCode,
                .mods = inter->keyMods,
            };

            // FIXME: this activates even when typing in a scene textbox bc it's a different instance
            if (snzu_isNothingFocused()) {
                for (int i = 0; i < _sc_commandCount; i++) {
                    _sc_Command* c = &_sc_commands[i];
                    if (!(c->availibleViews & *outCurrentView)) {
                        continue;
                    }
                    // FIXME: left shift only is required thats bad
                    if (kp.key == c->key.key && (kp.mods == c->key.mods)) {
                        _sc_activeCommand = c;
                        args.firstFrame = true;
                        break;
                    }
                }
            }
            // FIXME: some indication if a cmd failed or not

            if (kp.key == SDLK_ESCAPE || !snzu_isNothingFocused()) {
                _sc_activeCommand = NULL;
                sk_sketchDeselectAll(args.activeSketch);
            }
        }
    }  // command handling

    // to animate cmds on use in the hint window
    // here to still work before a command is used if it's instant
    _sc_Command* commandJustUsed = NULL;
    {
        _sc_Command** const prevCmd = SNZU_USE_MEM(_sc_Command*, "prevcmd");
        if (_sc_activeCommand != *prevCmd) {
            commandJustUsed = _sc_activeCommand;
        }
        *prevCmd = _sc_activeCommand;
    }

    snzu_boxScope() {
        if (_sc_activeCommand != NULL) {
            bool invalidated = !(_sc_activeCommand->availibleViews & *outCurrentView);
            bool done = _sc_activeCommand->func(args);
            if (invalidated || done) {
                _sc_activeCommand = NULL;
                sk_sketchDeselectAll(args.activeSketch);
            }
        }

        snzu_boxNew("shortcutWindow");
        {
            HMM_Vec4 col = ui_colorBackground;
            col.A = 0.5;
            snzu_boxSetColor(col);
        }

        bool buildInners = true;
        snzu_Interaction* hoverInter = SNZU_USE_MEM(snzu_Interaction, "hintwindowinter");
        {
            float* const openAnim = SNZU_USE_MEM(float, "openanim");
            ;
            if (hoverInter->hovered) {
                targetOpen = true;
            }
            snzu_easeExp(openAnim, targetOpen, ui_menuAnimationSpeed);

            if (*openAnim < geo_EPSILON) {
                buildInners = false;
            }

            snzu_boxFillParent();
            snzu_boxSetSizeFromEndAx(SNZU_AX_X, *openAnim * 375);
        }

        if (buildInners) {
            snzu_boxScope() {
                snzu_boxNew("margin");
                snzu_boxSetSizeMarginFromParent(20);
                snzu_boxSetSizeMarginFromParentAx(23, SNZU_AX_X);
                snzu_boxScope() {
                    if (_sc_activeCommand != NULL) {
                        snzu_boxNew("active cmd area");
                        snzu_boxFillParent();
                        snzu_boxScope() {
                            snzu_boxNew("active cmd");
                            snzu_boxFillParent();
                            snzu_boxSetDisplayStr(&ui_lightLabelFont, ui_colorAccent, snz_arenaFormatStr(scratch, "// %s", _sc_activeCommand->nameLabel));
                            snzu_boxSetSizeFitText(1);

                            snzu_boxNew("esc");
                            snzu_boxFillParent();
                            snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_lightLabelFont.renderedSize);
                            snzu_boxScope() {
                                snzu_boxNew("desc");
                                snzu_boxFillParent();
                                snzu_boxSetDisplayStr(&ui_lightLabelFont, ui_colorText, "cancel");
                                snzu_boxSetSizeFitText(1);
                                snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);

                                snzu_boxNew("key");
                                snzu_boxFillParent();
                                snzu_boxSetDisplayStr(&ui_shortcutFont, ui_colorText, "ESC");
                                snzu_boxSetSizeFitText(1);
                                snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_LEFT);
                            }
                        }
                        snzu_boxOrderChildrenInRowRecurse(4, SNZU_AX_Y);
                        snzu_boxSetSizeFitChildren();
                        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSize().Y + 10);
                    }

                    for (int i = 0; i < _sc_commandCount; i++) {
                        _sc_Command* c = &_sc_commands[i];
                        if (!(c->availibleViews & *outCurrentView)) {
                            continue;
                        }

                        snzu_boxNew(c->nameLabel);
                        float* const useAnim = SNZU_USE_MEM(float, "useanim");
                        snzu_easeExp(useAnim, 0, 5);
                        if (commandJustUsed == c) {
                            *useAnim = 1;
                        }

                        if (c == _sc_activeCommand) {
                            continue;
                        }
                        snzu_boxFillParent();
                        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_lightLabelFont.renderedSize);
                        snzu_boxScope() {
                            HMM_Vec4 color = HMM_Lerp(ui_colorText, *useAnim, ui_colorAccent);

                            snzu_boxNew("desc");
                            snzu_boxSetDisplayStr(&ui_lightLabelFont, color, c->nameLabel);
                            snzu_boxSetSizeFitText(1);
                            snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
                            snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_CENTER);

                            _sc_buildCommandShortcutBox(c, color);
                        }
                    }  // end cmd loop
                }  // end margin box
                snzu_boxOrderChildrenInRowRecurse(4, SNZU_AX_Y);
                snzuc_scrollArea();
                snzu_boxClipChildren(false);

                snzu_boxNew("left border");
                snzu_boxFillParent();
                snzu_boxSetSizeFromStartAx(SNZU_AX_X, ui_borderThickness);
                snzu_boxSetColor(ui_colorText);

                snzu_boxNew("hover detector gross");  // FIXME: ew
                snzu_boxFillParent();
                snzu_boxSetInteractionOutput(hoverInter, SNZU_IF_HOVER | SNZU_IF_ALLOW_EVENT_FALLTHROUGH);
            }  // end hints window
            snzu_boxClipChildren(true);
        }
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