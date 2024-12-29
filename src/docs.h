
#include "snooze.h"
#include "ui.h"

static snz_Arena _docs_fileDataArena;

static const char* _docs_filepaths[] = {
    "res/docs/sketches.md",
    "res/docs/starting.md",
    "res/docs/workflow.md",
    "res/docs/adder.md",
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
    ui_menuMargin();
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
        snzu_boxClipChildren(false);


        snzu_boxNew("scroll area for text");
        snzu_boxFillParent();
        snzu_boxSizeFromEndPctParent(0.6, SNZU_AX_X);  // FIXME: fill remainder function
        snzu_boxScope() {
            snzu_boxNew("header");
            snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, _docs_currentFile->name);
            snzu_boxSetSizeFitText(ui_padding);

            snzu_boxNew("text");
            snzu_boxSetDisplayStr(&ui_paragraphFont, ui_colorText, _docs_currentFile->text);
            snzu_boxSetSizeFitText(ui_padding);
        }  // scroll area for files
        snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y);
        snzuc_scrollArea();
        snzu_boxClipChildren(false);
    }  // end box for both scroll areas // margin
}  // end docs_buildPage