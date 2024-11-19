
#include "snooze.h"
#include "ui.h"

static snz_Arena _docs_fileDataArena;

static const char* _docs_filepaths[] = {
    "res/docs/intro.md",
    "res/docs/intro copy.md",
    "res/docs/intro copy 2.md",
    "res/docs/intro copy 3.md",
    "res/docs/intro copy 4.md",
    "res/docs/intro copy 5.md",
};

typedef struct _docs_File _docs_File;
struct _docs_File {
    const char* name;
    const char* text;
    _docs_File* next;
};
static _docs_File* _docs_firstFile;
static _docs_File* _docs_currentFile;

void docs_init() {
    _docs_fileDataArena = snz_arenaInit(1000000, "docs file data arena");

    for (uint64_t i = 0; i < sizeof(_docs_filepaths) / sizeof(*_docs_filepaths); i++) {
        const char* name = _docs_filepaths[i];

        FILE* file = fopen(name, "rb");
        SNZ_ASSERTF(file != NULL, "opening docs file '%s' failed.", name);

        fseek(file, 0L, SEEK_END);
        uint64_t size = ftell(file);
        fseek(file, 0L, SEEK_SET);

        uint8_t* text = SNZ_ARENA_PUSH_ARR(&_docs_fileDataArena, size, uint8_t);
        SNZ_ASSERTF(fread(text, sizeof(uint8_t), size, file) == size, "reading docs file %s failed.", name);
        fclose(file);

        _docs_File* f = SNZ_ARENA_PUSH(&_docs_fileDataArena, _docs_File);
        *f = (_docs_File){
            .name = name,
            .text = (const char*)text,
            .next = _docs_firstFile,
        };
        _docs_firstFile = f;
    }

    _docs_currentFile = _docs_firstFile;
}

void docs_buildPage() {
    snzu_boxNew("docs page");
    snzu_boxFillParent();
    snzu_boxSetColor(UI_BACKGROUND_COLOR);
    snzu_boxScope() {
        snzu_boxNew("margin area");
        HMM_Vec2 parentSize = snzu_boxGetSize(snzu_boxGetParent());
        snzu_boxSetStartFromParentStart(HMM_V2(parentSize.X * 0.1, parentSize.Y * 0.1));
        snzu_boxSetEndFromParentEnd(HMM_V2(parentSize.X * -0.1, parentSize.Y * -0.1));
        snzu_boxScope() {
            snzu_boxNew("scroll area for files");
            snzu_boxFillParent();
            snzu_boxSizePctParent(0.3, SNZU_AX_X);
            snzu_boxScope() {
                snzu_boxNew("margin");
                snzu_boxSetSizeMarginFromParent(20);
                snzu_boxScope() {
                    for (_docs_File* file = _docs_firstFile; file; file = file->next) {
                        bool clicked = ui_buttonWithHighlight(file == _docs_currentFile, file->name);
                        if (clicked) {
                            _docs_currentFile = file;
                        }
                    }
                }
                snzu_boxOrderChildrenInRowRecurse(5, SNZU_AX_Y);
                snzu_boxSetSizeFromStartAx(SNZU_AX_Y, snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y));
            }  // scroll area for files
            snzuc_scrollArea();

            snzu_boxNew("scroll area for text");
            snzu_boxFillParent();
            snzu_boxSizeFromEndPctParent(0.6, SNZU_AX_X);  // FIXME: fill remainder function
            snzu_boxScope() {
                snzu_boxNew("header");
                snzu_boxSetDisplayStr(&ui_titleFont, UI_TEXT_COLOR, _docs_currentFile->name);
                snzu_boxSetSizeFitText();

                snzu_boxNew("text");
                snzu_boxSetDisplayStr(&ui_paragraphFont, UI_TEXT_COLOR, _docs_currentFile->text);
                snzu_boxSetSizeFitText();
            }  // scroll area for files
            snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
            snzuc_scrollArea();
        }  // end box for both scroll areas
    }  // end parent
}  // end docs_buildPage