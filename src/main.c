#define BASE_IMPL
#include "base/allocators.h"
#include "GLAD/include/glad/glad.h"
#define SDL_MAIN_HANDLED
#include "SDL2/include/SDL2/SDL.h"

#include "serialization2.h"
#include "sketches.h"
#include "ui.h"
#include "render.h"

int main(int argc, char* argv[]) {
    assert(argc == 1);
    assert(argv[0]); // to get rid of unused arg warnings

    printf("\n");
    sk_tests();
    ser_tests();

    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    assert(SDL_GL_LoadLibrary(NULL) == 0);
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8);
    uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow("WOAH UI", 0, 0, 700, 500, windowFlags);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    assert(renderer);

    ren_init();

    bool quit = false;
    // float prevTime = 0.0;
    while (!quit) {
        // float time = (float)SDL_GetTicks64() / 1000;
        // float dt = time - prevTime;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        {
            ui_Input input;
            memset(&input, 0, sizeof(input));

            int mx, my;
            SDL_GetMouseState(&mx, &my);
            input.mousePos = HMM_V2(mx, my);

            ui_frameGenInteractions(input);
        }

        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        HMM_Vec2 screenSize = HMM_V2((float)w, (float)h);

        ui_frameStart();

        _ui_Box* background = ui_boxNew("first kid");
        ui_boxSetColor(background, HMM_V4(0, 0, 1, 1));
        ui_boxSetSizeFromStart(background, screenSize);
        ui_boxScope(background) {
            _ui_Box* margins = ui_boxNew("margins");
            ui_boxMarginFromParent(margins, 15);
            ui_boxScope(margins) {
                HMM_Vec4 panelCol = HMM_V4(0, 0.5, 1, 1);
                {
                    _ui_Box* leftPanel = ui_boxNew("leftPanel");
                    ui_boxSetColor(leftPanel, panelCol);
                    float* pct = UI_USE_MEM(float, "pct");
                    if (ui_useMemIsPrevNew()) {
                        *pct = 0.4;
                    }
                    ui_boxFillParent(leftPanel);
                    ui_boxSizePctParent(leftPanel, *pct, UI_AX_X);

                    ui_boxScope(leftPanel) {
                        _ui_Box* blocker = ui_boxNew("blocker");
                        ui_boxFillParent(blocker);
                        ui_boxSizePctParent(blocker, 0.2, UI_AX_Y);
                        ui_boxSetSizeFromStartAx(blocker, UI_AX_X, 1000);
                        ui_boxSetColor(blocker, HMM_V4(1, 1, 1, 1));
                        ui_boxSetInteractionOutput(blocker, NULL, UI_IF_HOVER);
                    }
                }

                {
                    _ui_Box* middleBar = ui_boxNew("middleBar");
                    ui_boxFillParent(middleBar);
                    ui_boxSetSizeFromStartAx(middleBar, UI_AX_X, 2);
                    ui_boxAlignOuter(middleBar, middleBar->prevSibling, UI_AX_X, 1);
                    ui_boxScope(middleBar) {
                        _ui_Box* clickZone = ui_boxNew("clickZone");
                        ui_boxFillParent(clickZone);
                        ui_boxSetSizeFromStartAx(clickZone, UI_AX_X, 6);
                        ui_boxCenter(clickZone, clickZone->parent, UI_AX_X);

                        ui_Interaction* inter = UI_USE_MEM(ui_Interaction, "inter");
                        ui_boxSetInteractionOutput(clickZone, inter, UI_IF_HOVER);
                        float* hoverAnim = UI_USE_MEM(float, "hoverAnim");
                        *hoverAnim = ui_easeExp(*hoverAnim, inter->hovered, 0.075);
                        HMM_Vec4 col = HMM_LerpV4(HMM_V4(1, 1, 1, 0), *hoverAnim, HMM_V4(1, 1, 1, 1));
                        ui_boxSetColor(clickZone, col);
                    }
                }

                {
                    _ui_Box* rightPanel = ui_boxNew("rightPanel");
                    ui_boxSetColor(rightPanel, panelCol);
                    float size = ui_boxSizeRemainingFromStart(rightPanel->parent, UI_AX_X);
                    ui_boxFillParent(rightPanel);
                    ui_boxSetSizeFromEndAx(rightPanel, UI_AX_X, size);
                }
            } // end margins
        } // end parent

        ren_pushCallsFromUITree(background, screenSize);
        ren_flush(w, h, HMM_V4(0, 0, 0, 1));

        SDL_GL_SwapWindow(window);
    }
    return 0;
}