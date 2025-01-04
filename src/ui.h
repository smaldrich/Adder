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
HMM_Vec4 ui_colorTransparentAccent;
HMM_Vec4 ui_colorErr;
HMM_Vec4 ui_colorBackground;
HMM_Vec4 ui_colorAlmostBackground;
HMM_Vec4 ui_colorTransparentPanel;
float ui_lightAmbient;
// FIXME: border color instead of defaulting to text color

float ui_cornerRadius = 15;
float ui_borderThickness = 4;
float ui_padding = 5;
float ui_menuAnimationSpeed = 15;

snzr_Texture* ui_skyBox = NULL;  // may be null, indicates drawing background color
snzr_Texture* ui_lightSky = NULL;
snzr_Texture* ui_darkSky = NULL;

snzr_Texture* ui_sketchPointTexture = NULL;
snzr_Texture* ui_shiftTexture = NULL;

void ui_setThemeLight() {
    ui_colorText = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAccent = HMM_V4(221 / 255.0, 255 / 255.0, 178 / 255.0, 1);
    ui_colorTransparentAccent = ui_colorAccent;
    ui_colorTransparentAccent.A = 0.4;
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorBackground = HMM_V4(1, 1, 1, 1);
    ui_colorAlmostBackground = HMM_V4(0.9, 0.9, 0.9, 1);
    ui_colorTransparentPanel = ui_colorBackground;
    ui_colorTransparentPanel.A = 0.5;
    ui_lightAmbient = 0.8;
    ui_skyBox = ui_lightSky;
}

void ui_setThemeDark() {
    // FIXME: when in dark mode, button with highlight is awful
    // FIXME: darkmode highligh is the same as err color
    // FIXME: when in dark mode, text in the scene is really weird but only when the left bar is moving
    ui_colorText = HMM_V4(1, 1, 1, 1);
    ui_colorAccent = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorTransparentAccent = ui_colorAccent;
    ui_colorTransparentAccent.A = 0.4;
    ui_colorErr = HMM_V4(181 / 255.0, 55 / 255.0, 93 / 255.0, 1);
    ui_colorBackground = HMM_V4(60 / 255.0, 60 / 255.0, 60 / 255.0, 1);
    ui_colorAlmostBackground = HMM_V4(52 / 255.0, 52 / 255.0, 52 / 255.0, 1);
    ui_colorTransparentPanel = ui_colorBackground;
    ui_colorTransparentPanel.A = 0.5;
    ui_lightAmbient = 0.2;
    ui_skyBox = ui_darkSky;
}

// loads in RGBA, asserts on failue.
static snzr_Texture* _ui_texFromFile(const char* path, snz_Arena* outForTexture) {
    int w, h, channels = 0;
    uint8_t* pixels = stbi_load(path, &w, &h, &channels, 4);
    SNZ_ASSERT(pixels, "Texture load failed.");
    snzr_Texture* tex = SNZ_ARENA_PUSH(outForTexture, snzr_Texture);
    *tex = snzr_textureInitRBGA(w, h, pixels);
    stbi_image_free(pixels);
    return tex;
}

void ui_init(snz_Arena* fontArena, snz_Arena* scratch) {
    stbi_set_flip_vertically_on_load(true);
    ui_darkSky = _ui_texFromFile("res/textures/Deep Dusk Equirect.png", fontArena);
    // ui_darkSky = NULL;
    ui_lightSky = _ui_texFromFile("res/textures/Untitled.png", fontArena);

    ui_sketchPointTexture = _ui_texFromFile("res/textures/circle.png", fontArena);
    ui_shiftTexture = _ui_texFromFile("res/textures/shiftIcon.png", fontArena);

    ui_setThemeDark();

    ui_titleFont = snzr_fontInit(fontArena, scratch, "res/fonts/AzeretMono-Regular.ttf", 48);
    ui_paragraphFont = snzr_fontInit(fontArena, scratch, "res/fonts/OpenSans-Light.ttf", 20);
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
    HMM_Vec2 parentSize = snzu_boxGetSizePtr(snzu_boxGetParent());
    snzu_boxSetStartFromParentStart(HMM_V2(parentSize.X * 0.1, parentSize.Y * 0.1));
    snzu_boxSetEndFromParentEnd(HMM_V2(parentSize.X * -0.1, parentSize.Y * -0.1));
    return box;
}

// constructs at 0, 0
void ui_switch(const char* label, bool* const state) {
    float textHeight = ui_labelFont.renderedSize;  // FIXME: no mucking like this
    float sliderWidth = 40;
    float innerMargin = 4;

    snzu_boxNew(label);

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

#define UI_TEXTAREA_MAX_CHARS 255
typedef struct {
    int64_t cursorPos;
    int64_t selectionStart;

    bool wasFocused;
    bool firstClickForFocus;  // needs to be persisted so that other things don't trigger while mouse is down

    const snzr_Font* font;

    int64_t charCount;
    char chars[UI_TEXTAREA_MAX_CHARS];

    snzu_Interaction inter;
} ui_TextArea;
// FIXME: testing to make sure the null char at the end works

static void _ui_textAreaAssertValid(ui_TextArea* text) {
    SNZ_ASSERTF(text->charCount >= 0, "textarea charCount out of bounds. was: %lld", text->charCount);
    SNZ_ASSERTF(text->charCount < UI_TEXTAREA_MAX_CHARS, "textarea charCount out of bounds. was: %lld", text->charCount);
    SNZ_ASSERTF(text->cursorPos >= 0, "textarea cursor out of bounds. was: %lld", text->cursorPos);
    SNZ_ASSERTF(text->cursorPos <= text->charCount, "textarea cursor out of bounds. was: %lld", text->cursorPos);
    SNZ_ASSERTF(text->selectionStart >= -1, "textarea selection start out of bounds. was: %lld", text->selectionStart);
    SNZ_ASSERTF(text->selectionStart <= text->charCount, "textarea selection start out of bounds. was: %lld", text->selectionStart);
    SNZ_ASSERT(text->font != NULL, "text area font was NULL");
}

static void _ui_textAreaNormalizeCursor(ui_TextArea* text) {
    if (text->cursorPos < 0) {
        text->cursorPos = 0;
    } else if (text->cursorPos > text->charCount) {
        text->cursorPos = text->charCount;
    }
}

// returns whether it was a successful insert (too long is a failure)
static bool _ui_textAreaInsert(ui_TextArea* text, char* insertChars, int64_t insertLen, int64_t insertPos) {
    _ui_textAreaAssertValid(text);

    if (text->charCount + insertLen >= UI_TEXTAREA_MAX_CHARS) {
        return false;
    }

    // FIXME: fancy asserts
    assert(insertPos >= 0);
    assert(insertPos <= text->charCount);

    memmove(&text->chars[insertPos + insertLen], &text->chars[insertPos], UI_TEXTAREA_MAX_CHARS - insertPos - insertLen);
    for (int64_t i = 0; i < insertLen; i++) {
        text->chars[insertPos + i] = insertChars[i];
    }
    text->charCount += insertLen;
    return true;
}

static bool _ui_textAreaRemove(ui_TextArea* text, int64_t removePos, int64_t removeLen) {
    _ui_textAreaAssertValid(text);

    if (text->charCount - removeLen < 0) {
        return false;
    } else if (removePos > text->charCount - removeLen) {
        return false;
    } else if (removePos < 0) {
        return false;  // FIXME: this case should really clamp removepos to be valid and sub it from len
    }

    memmove(&text->chars[removePos], &text->chars[removePos + removeLen], UI_TEXTAREA_MAX_CHARS - removePos - removeLen);
    text->charCount -= removeLen;
    return true;
}

// doesn't account for newlines, end result is clamped to be a valid index within str
static int64_t _ui_textAreaIndexFromCursorPos(ui_TextArea* text, float cursorRelativeToStart, float textHeight) {
    // TODO: offset click zones to make right char boxes extend a little left
    _ui_textAreaAssertValid(text);

    for (int i = 0; i < text->charCount; i++) {
        // TODO: this could be incrementally calculated but i don't care
        float charX = snzr_strSize(text->font, text->chars, i + 1, textHeight).X;
        if (cursorRelativeToStart < charX) {
            return i;
        }
    }
    return text->charCount;
}

static void _ui_textAreaClearSelection(ui_TextArea* text) {
    bool startIsSmaller = text->selectionStart < text->cursorPos;
    int64_t lowerBound = (startIsSmaller ? text->selectionStart : text->cursorPos);
    int64_t upperBound = (startIsSmaller ? text->cursorPos : text->selectionStart);
    _ui_textAreaRemove(text, lowerBound, upperBound - lowerBound);
    text->cursorPos = lowerBound;
    text->selectionStart = -1;
    _ui_textAreaNormalizeCursor(text);
}

// dir = true is to the right, false to the left // returns index of the beginning of the next or previous word
static int64_t _ui_textAreaNextWordFromCursor(ui_TextArea* text, bool dir) {
    _ui_textAreaAssertValid(text);

    if (dir) {
        bool endedChars = false;
        char initial = text->chars[text->cursorPos];
        if (!isalnum(initial) && !isspace(initial)) {
            endedChars = true;
        }
        for (int64_t i = (text->cursorPos + 1); i < text->charCount; i++) {
            char c = text->chars[i];
            if (!endedChars) {
                // skip all alnums
                if (isalnum(c) || c == '_') {
                    continue;
                }
                endedChars = true;
            }

            // skip all whitespace
            if (!isspace(c)) {
                return i;
            }
        }
        return text->charCount;
    } else {
        bool endedWhitespace = false;
        bool endWithinWord = false;
        for (int64_t i = (text->cursorPos - 1); i >= 0; i--) {
            char c = text->chars[i];
            // skip all whitespace
            if (!endedWhitespace) {
                if (isspace(c)) {
                    continue;
                }
                endedWhitespace = true;
            }

            // skip all alnums
            if (isalnum(c) || c == '_') {
                endWithinWord = true;
                continue;
            }

            if (!endWithinWord) {
                // return on the invalid char if we have not seen any alnums so far, it's puncuation and it's own word
                return i;
            }
            // otherwise, the breaking char is terminating a word, which case we should end within the word
            return i + 1;
        }
        return 0;
    }
}

// str may be null
void ui_textAreaInit(ui_TextArea* area, const char* str) {
    memset(area, 0, sizeof(*area));
    if (str != NULL) {
        uint64_t len = strlen(str);
        SNZ_ASSERTF(
            len < UI_TEXTAREA_MAX_CHARS - 1,
            "Initializing text area failed. Too many chars in str. were %llu, expected less than %d.",
            len, UI_TEXTAREA_MAX_CHARS - 1);
        area->charCount = len;
        strcpy(area->chars, str);
    }
    area->selectionStart = -1;  // FIXME: sorry it's non zero. worked out easier.
}

void ui_textAreaSetStr(ui_TextArea* area, const char* str, uint64_t len) {
    SNZ_ASSERTF(len < UI_TEXTAREA_MAX_CHARS - 1, "set failed. len was > %d - 1, the max number of chars a textbox can fit.", UI_TEXTAREA_MAX_CHARS);
    strcpy(area->chars, str);
    area->charCount = strlen(str);
    area->cursorPos = SNZ_MIN(area->cursorPos, area->charCount);
    // FIXME: more intensive validation checks?
}

// FIXME: bettery recovery than just not applying when it changes
// FIXME: max char count should not change at any point, it is likely to mess up some internal state severely, and thats bad
// charCount and chars are read/write vars
// use ui_textAreaInit before passing in a textArea struct
// text is expected to be usememd
void ui_textArea(ui_TextArea* const text, const snzr_Font* font, float textHeight, HMM_Vec4 textColor, bool setFocused) {
    /*
    FEATURES:
    [X] selection zones
    [X] select all on initial click
    [X] edits work with selections
    [X] click to move cursor
    [X] drag to select
    [X] shift + arrows to select
    [X] ctrl arrows to move betw words
    [ ] home/end
    [ ] ctrl+A
    [ ] copy + paste to clipboard
    [ ] multiple line support
    [ ] scrolling view to fit more text/view cursor
    [ ] FIXME: test cases for the whole thing lol
    */

    _snzu_Box* container = snzu_getSelectedBox();
    // snzu_boxClipChildren();
    float padding = 0.1 * textHeight;

    text->font = font;
    _ui_textAreaAssertValid(text);

    snzu_boxSetInteractionOutput(&text->inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS | SNZU_IF_ALLOW_EVENT_FALLTHROUGH);

    if (text->inter.doubleClicked || setFocused) {
        text->firstClickForFocus = true;
        snzu_boxSetFocused();

        if (text->charCount != 0) {
            text->selectionStart = 0;
        }
        text->cursorPos = text->charCount;
    } else if (text->inter.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
        text->firstClickForFocus = false;
        if (text->wasFocused) {
            snzu_boxSetFocused();
            float mouseX = text->inter.mousePosLocal.X - padding;
            text->selectionStart = _ui_textAreaIndexFromCursorPos(text, mouseX, textHeight);
            text->cursorPos = text->selectionStart;
        }
    }

    bool focused = snzu_boxFocused();
    text->wasFocused = focused;
    if (focused) {
        if (!text->firstClickForFocus && text->inter.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DRAG) {
            float mouseX = text->inter.mousePosLocal.X - padding;
            text->cursorPos = _ui_textAreaIndexFromCursorPos(text, mouseX, textHeight);
        }

        if (!text->firstClickForFocus && text->inter.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP) {
            if (text->selectionStart == text->cursorPos) {
                text->selectionStart = -1;
            }
        }

        if (text->inter.keyAction == SNZU_ACT_DOWN) {
            if (text->inter.keyCode == SDLK_ESCAPE || text->inter.keyCode == SDLK_RETURN) {
                snzu_clearFocus();
                text->selectionStart = -1;
            }
        }
    }

    // only process keystrokes when focused
    if (focused) {
        bool selecting = text->selectionStart != -1 && text->selectionStart != text->cursorPos;
        if (text->inter.keyChars[0] != '\0') {
            if (selecting) {
                _ui_textAreaClearSelection(text);
            }

            // TODO: do we need a more sophisticated check?
            // TODO: support >1 char/unicode shit
            if (_ui_textAreaInsert(text, &(text->inter.keyChars[0]), 1, text->cursorPos)) {
                text->cursorPos++;
            }
        } else if (text->inter.keyAction == SNZU_ACT_DOWN) {
            if (text->inter.keyCode == SDLK_BACKSPACE) {
                if (selecting) {
                    _ui_textAreaClearSelection(text);
                } else {
                    bool removed = _ui_textAreaRemove(text, text->cursorPos - 1, 1);
                    if (removed) {
                        text->cursorPos--;
                    }
                }
            } else if (text->inter.keyCode == SDLK_DELETE) {
                if (selecting) {
                    _ui_textAreaClearSelection(text);
                } else {
                    _ui_textAreaRemove(text, text->cursorPos, 1);
                }
            } else if (text->inter.keyCode == SDLK_LEFT || text->inter.keyCode == SDLK_RIGHT) {
                bool dir = (text->inter.keyCode == SDLK_RIGHT);  // true when right, false when left
                int64_t initialCursorPos = text->cursorPos;

                bool selectionHandled = false;
                if (text->inter.keyMods & KMOD_CTRL) {
                    text->cursorPos = _ui_textAreaNextWordFromCursor(text, dir);
                } else if (text->inter.keyMods & KMOD_SHIFT) {
                    text->cursorPos += (dir ? 1 : -1);
                } else if (selecting) {
                    int64_t min = SNZ_MIN(text->cursorPos, text->selectionStart);
                    int64_t max = SNZ_MAX(text->cursorPos, text->selectionStart);
                    text->cursorPos = (dir) ? max : min;
                    text->selectionStart = -1;
                    selectionHandled = true;
                } else {
                    text->cursorPos += (dir ? 1 : -1);
                }

                // error here when ctrl + right normally
                if (!selectionHandled && (text->inter.keyMods & KMOD_SHIFT)) {
                    if (text->selectionStart == -1) {
                        text->selectionStart = initialCursorPos;
                    }
                }
            }
        }  // end button press checks

        _ui_textAreaNormalizeCursor(text);
        _ui_textAreaAssertValid(text);
    }  // end focus check to process keys

    _ui_textAreaAssertValid(text);

    HMM_Vec2 size = snzr_strSize(text->font, text->chars, text->charCount, textHeight);
    size = HMM_Add(size, HMM_Mul(HMM_V2(padding, padding), 2.0f));
    snzu_boxSetSizeFromStart(size);

    snzu_boxScope() {
        if (focused) {
            if (text->selectionStart != -1 && text->selectionStart != text->cursorPos) {
                snzu_boxNew("selectionBox");
                snzu_boxSetSizeMarginFromParent(padding);
                float startOffset = snzr_strSize(text->font, text->chars, text->selectionStart, textHeight).X;
                float endOffset = snzr_strSize(text->font, text->chars, text->cursorPos, textHeight).X;
                if (endOffset < startOffset) {
                    float temp = startOffset;
                    startOffset = endOffset;
                    endOffset = temp;
                }
                snzu_boxSetStartAx(container->start.X + padding + startOffset, SNZU_AX_X);
                snzu_boxSetEndAx(container->start.X + padding + endOffset, SNZU_AX_X);
                snzu_boxSetColor(ui_colorAccent);
            }
        }

        snzu_boxNew("text");
        snzu_boxFillParent();
        snzu_boxSetDisplayStrLen(text->font, textColor, text->chars, text->charCount);
        snzu_boxSetDisplayStrMode(textHeight, true);
        snzu_boxSetSizeFitText(padding);  // aligns text left

        if (focused) {
            snzu_boxNew("cursor");
            float cursorStartX = (snzr_strSize(text->font, text->chars, text->cursorPos, textHeight).X) + padding;
            snzu_boxFillParent();
            snzu_boxSetStartAx(container->start.X + cursorStartX, SNZU_AX_X);
            snzu_boxSetSizeFromStartAx(SNZU_AX_X, padding);
            snzu_boxSetColor(ui_colorText);
        }
    }  // end inners
}  // end text area

typedef struct {
    bool tempSelected;
    bool selected;
    float selectionAnim;
    float hoverAnim;
} ui_SelectionState;

typedef struct ui_SelectionStatus ui_SelectionStatus;
struct ui_SelectionStatus {
    bool hovered;
    bool withinDragZone;
    snzu_Action mouseAct;
    ui_SelectionState* state;
    ui_SelectionStatus* next;
};

typedef struct {
    bool wasDragging;
    bool dragging;
    HMM_Vec2 dragOrigin;
} ui_SelectionRegion;

// mouseaction should not contain data if any status has mouseaction data
// If the inners are just using stock inters, shouldn't be an issue because they will capture mouse actions
void ui_selectionRegionUpdate(ui_SelectionRegion* region, snzu_Action regionMouseAction, HMM_Vec2 mousePos, bool shiftPressed, ui_SelectionStatus* firstStatus, bool allowDragging) {
    if (regionMouseAction == SNZU_ACT_DOWN) {
        region->dragging = true;
        region->dragOrigin = mousePos;
    }

    // FIXME: trigger on !down or dragging??
    if (regionMouseAction == SNZU_ACT_UP || !allowDragging || !snzu_isNothingFocused()) {
        region->dragging = false;
    }

    bool dragEnded = region->wasDragging && !region->dragging;
    region->wasDragging = region->dragging;

    bool anyMouseDown = regionMouseAction == SNZU_ACT_DOWN;
    for (ui_SelectionStatus* s = firstStatus; s; s = s->next) {
        if (s->mouseAct == SNZU_ACT_DOWN) {
            anyMouseDown = true;
            break;
        }
    }

    for (ui_SelectionStatus* s = firstStatus; s; s = s->next) {
        s->state->tempSelected = s->withinDragZone && region->dragging;

        if (!snzu_isNothingFocused()) {
            s->state->selected = false;
        } else if (dragEnded && s->withinDragZone) {
            s->state->selected = true;
        }

        if (s->mouseAct == SNZU_ACT_DOWN && s->hovered) {
            s->state->selected = !s->state->selected;
        } else if (anyMouseDown && !shiftPressed) {
            s->state->selected = false;
        }
    }
}

void ui_selectionRegionUpdateIgnoreMouse(ui_SelectionRegion* region, ui_SelectionStatus* firstStatus) {
    region->dragging = false;
    region->wasDragging = false;

    for (ui_SelectionStatus* s = firstStatus; s; s = s->next) {
        if (!snzu_isNothingFocused()) {
            s->state->selected = false;
        } else if (s->state->tempSelected) {
            s->state->tempSelected = false;
            s->state->selected = true;
        }
        s->hovered = false;
    }
}

void ui_selectionRegionAnimate(ui_SelectionStatus* firstStatus) {
    for (ui_SelectionStatus* s = firstStatus; s; s = s->next) {
        snzu_easeExp(&s->state->hoverAnim, s->hovered, 15);
        snzu_easeExp(&s->state->selectionAnim, s->state->selected || s->state->tempSelected, 15);
    }
}