#define BASE_IMPL
#include "base/allocators.h"
#include "GLAD/include/glad/glad.h"
#define SDL_MAIN_HANDLED
#include "SDL2/include/SDL2/SDL.h"

#include "serialization2.h"
#include "sketches.h"
#include "ui.h"

void cb_glad_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam) {
    if (type == GL_DEBUG_TYPE_OTHER) {
        return;
    }
    // hides messages talking about buffer detailed info
    printf("[GL] %i, %s\n", type, message);
    type = source = id = severity = length = (int)(uint64_t)userParam; // to get rid of unused arg warnings
}

void initGl() {
    gladLoadGL();
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS | GL_EQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_MULTISAMPLE);
    glDebugMessageCallback(cb_glad_debug, 0);
}

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
    initGl();

    bool quit = false;
    // float prevTime = 0.0;
    while (!quit) {
        // int w, h;
        // SDL_GL_GetDrawableSize(window, &w, &h);
        // HMM_Vec2 screenSize = HMM_V2((float)w, (float)h);

        // float time = (float)SDL_GetTicks64() / 1000;
        // float dt = time - prevTime;
        // vTime = time;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }
        SDL_GL_SwapWindow(window);
    }
    return 0;
}