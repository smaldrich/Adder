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
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        HMM_Vec2 screenSize = HMM_V2((float)w, (float)h);

        ui_frameStart();

        HMM_Mat4 vp = HMM_Orthographic_RH_NO(0, screenSize.X, screenSize.Y, 0, 0, 1000);
        ren_pushCallUI(HMM_V2(100, 100), HMM_V2(200, 200), 0, vp, HMM_V4(0, 0, 1, 1));
        ren_flush(w, h, HMM_V4(0, 1, 0, 1));

        // float time = (float)SDL_GetTicks64() / 1000;
        // float dt = time - prevTime;
        // vTime = time;

        SDL_GL_SwapWindow(window);
    }
    return 0;
}