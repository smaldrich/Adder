#pragma once
#include "GLAD/include/glad/glad.h"
#include "ui.h"

typedef struct {
    HMM_Vec2 dstStart;
    HMM_Vec2 dstEnd;
    float z;
    HMM_Mat4 vp;
    HMM_Vec4 color;
} _ren_CallParamsUI;

typedef enum {
    _REN_CK_UI,
    _REN_CK_COUNT
} _ren_CallKind;

typedef struct _ren_Call _ren_Call;
struct _ren_Call {
    _ren_CallKind kind;
    _ren_Call* next;
    union {
        _ren_CallParamsUI ui;
    };
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t channels; // either 1 or 4
    uint8_t* data;
    uint32_t glId;
} ren_Texture;

typedef struct {
    uint32_t shaderIds[_REN_CK_COUNT];
    _ren_Call* firstCall;
    _ren_Call* lastCall;
    BumpAlloc arena;

    ren_Texture solidTex;
    uint32_t squareVAId; // centered on 0, 0 with side lengths of 1
} _ren_Globs;
_ren_Globs _ren_globs;

int32_t _ren_initShader(const char* vertSrc, const char* fragSrc) {
    uint32_t v = glCreateShader(GL_VERTEX_SHADER);
    uint64_t vertSize = strlen(vertSrc);
    assert(vertSize < INT32_MAX);
    int smallVertSize = (int)vertSize;
    glShaderSource(v, 1, (const char* const*)&vertSrc, &smallVertSize);
    glCompileShader(v);

    uint32_t f = glCreateShader(GL_FRAGMENT_SHADER);
    uint64_t fragSize = strlen(fragSrc);
    assert(fragSize < INT32_MAX);
    int smallFragSize = (int)fragSize;  // CLEANUP: these could overflow
    glShaderSource(f, 1, (const char* const*)&fragSrc, &smallFragSize);
    glCompileShader(f);

    bool compFailed = false;
    int err = 0;
    glGetShaderiv(v, GL_COMPILE_STATUS, &err);
    if (!err) {
        compFailed = true;
    };

    glGetShaderiv(f, GL_COMPILE_STATUS, &err);
    if (!err) {
        compFailed = true;
    };

    if (!compFailed) {
        int id = glCreateProgram();
        glAttachShader(id, v);
        glAttachShader(id, f);
        glLinkProgram(id);
        glValidateProgram(id);
        return id;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return 0;
}

ren_Texture ren_initTexture(int w, int h, int channels, void* data) {
    ren_Texture t = {
        .width = w,
        .height = h,
        .channels = channels,
        .data = data };
    glGenTextures(1, &t.glId);
    glBindTexture(GL_TEXTURE_2D, t.glId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (t.channels == 1) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // TODO: what are these doing
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, t.width, t.height, 0, GL_RED, GL_UNSIGNED_BYTE, t.data);
    } else if (t.channels == 4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data);
    } else {
        assert(false);
    }
    return t;
}

void _ren_glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam) {
    // hides messages talking about buffer detailed info
    if (type == GL_DEBUG_TYPE_OTHER) {
        return;
    }
    printf("[GL] %i, %s\n", type, message);
    type = source = id = severity = length = (int)(uint64_t)userParam; // to get rid of unused arg warnings
}

const char* _ren_loadToFileToStr(const char* path, BumpAlloc* arena) {
    FILE* file = fopen(path, "r");
    assert(file);
    fseek(file, 0L, SEEK_END);
    uint64_t size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    char* text = BUMP_PUSH_ARR(arena, size, char);
    fread(text, sizeof(char), size, file);
    fclose(file);
    *BUMP_PUSH_NEW(arena, char) = '\0';
    return text;
}

void ren_init() {
    gladLoadGL();
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS | GL_EQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_MULTISAMPLE);
    glDebugMessageCallback(_ren_glDebugCallback, 0);

    { // solid tex
        uint8_t imgData[4] = { 0 };
        imgData[0] = 0xFF;
        imgData[1] = 0xFF;
        imgData[2] = 0xFF;
        imgData[3] = 0xFF;
        _ren_globs.solidTex = ren_initTexture(1, 1, 4, imgData);
    }

    { // std. square geometry
        glGenVertexArrays(1, &_ren_globs.squareVAId);
        glBindVertexArray(_ren_globs.squareVAId);

        uint32_t idata[] = { 0, 1, 2, 2, 3, 0 };
        uint32_t dataSize = sizeof(idata);
        uint32_t ibId = 0;
        glGenBuffers(1, &ibId);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, dataSize, idata, GL_STATIC_DRAW);

        float vdata[] = { -0.5, -0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5 };
        dataSize = sizeof(vdata);
        uint32_t vbId = 0;
        glGenBuffers(1, &vbId);
        glBindBuffer(GL_ARRAY_BUFFER, vbId);
        glBufferData(GL_ARRAY_BUFFER, dataSize, vdata, GL_STATIC_DRAW);

        uint32_t vertSize = 2 * sizeof(float);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, vertSize, (void*)(0));
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    _ren_globs.arena = bump_allocate(1000000, "render arena");
    _ren_globs.shaderIds[_REN_CK_UI] = _ren_initShader(
        _ren_loadToFileToStr("res/shaders/ui.vert", &_ren_globs.arena),
        _ren_loadToFileToStr("res/shaders/ui.frag", &_ren_globs.arena));

    bump_clear(&_ren_globs.arena);
}

// allocates and pushes to the global list of calls
_ren_Call* _ren_pushCall() {
    _ren_Call* out = BUMP_PUSH_NEW(&_ren_globs.arena, _ren_Call);
    if (!_ren_globs.firstCall) {
        _ren_globs.firstCall = out;
        _ren_globs.lastCall = out;
    } else {
        _ren_globs.lastCall->next = out;
        _ren_globs.lastCall = out;
    }
    return out;
}

void ren_pushCallUI(HMM_Vec2 dstStart, HMM_Vec2 dstEnd, float z, HMM_Mat4 vp, HMM_Vec4 color) {
    _ren_Call* c = _ren_pushCall();
    c->kind = _REN_CK_UI;
    c->ui = (_ren_CallParamsUI){
        .dstStart = dstStart,
        .dstEnd = dstEnd,
        .z = z,
        .vp = vp,
        .color = color,
    };
}

void _ren_pushCallsFromUITree(ui_Box* box, HMM_Mat4 vp) {
    for (ui_Box* b = box; b; b = b->nextSibling) {
        ren_pushCallUI(b->start, b->end, b->z, vp, b->color);
        if (b->firstChild) {
            _ren_pushCallsFromUITree(b->firstChild, vp);
        }
    }
}

void ren_pushCallsFromUITree(ui_Box* box, HMM_Vec2 screenSize) {
    HMM_Mat4 vp = HMM_Orthographic_RH_NO(0, screenSize.X, screenSize.Y, 0, -1000, 1000);
    // we are using a depth test of LESS, so negative values will appear in front in NDC
    // this VP then puts coords closer to negative values closer to + in NDC
    // which makes the depth test work as expected // NOTE: i tried change the depth test func but it didn't work at all and I'm lazy
    _ren_pushCallsFromUITree(box, vp);
}

void ren_flush(uint32_t screenW, uint32_t screenH, HMM_Vec4 clearColor) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // target the screen // TODO: framebuffers
    glViewport(0, 0, screenW, screenH);
    glClearColor(clearColor.R, clearColor.G, clearColor.B, clearColor.A); // clear screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uint32_t currentKind = -1;
    for (_ren_Call* call = _ren_globs.firstCall; call; call = call->next) {
        assert(call->kind >= 0 && call->kind < _REN_CK_COUNT);
        if (call->kind != currentKind) {
            glUseProgram(_ren_globs.shaderIds[call->kind]);
            currentKind = call->kind;
        }
        uint32_t shaderId = _ren_globs.shaderIds[call->kind];

        if (call->kind == _REN_CK_UI) {
            int loc = 0;
            loc = glGetUniformLocation(shaderId, "uVP"); // TODO: cache uniform locs per shader
            glUniformMatrix4fv(loc, 1, false, (float*)&call->ui.vp);
            loc = glGetUniformLocation(shaderId, "uSrcStart");
            glUniform2f(loc, 0, 0);
            loc = glGetUniformLocation(shaderId, "uSrcEnd");
            glUniform2f(loc, 1, 1); // TODO: textures
            loc = glGetUniformLocation(shaderId, "uDstStart");
            glUniform2f(loc, call->ui.dstStart.X, call->ui.dstStart.Y);
            loc = glGetUniformLocation(shaderId, "uDstEnd");
            glUniform2f(loc, call->ui.dstEnd.X, call->ui.dstEnd.Y);
            loc = glGetUniformLocation(shaderId, "uZ");
            glUniform1f(loc, call->ui.z);
            loc = glGetUniformLocation(shaderId, "uColor");
            glUniform4f(loc, call->ui.color.R, call->ui.color.G, call->ui.color.B, call->ui.color.A);
            // loc = glGetUniformLocation(shaderId, "uSnap");
            // glUniform1f(loc, call->snap);
            loc = glGetUniformLocation(shaderId, "uTexture");
            glUniform1i(loc, 0);
            glActiveTexture(GL_TEXTURE0 + 0);
            glBindTexture(GL_TEXTURE_2D, _ren_globs.solidTex.glId);
            loc = glGetUniformLocation(shaderId, "uFontTexture");
            glUniform1i(loc, 1);
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, _ren_globs.solidTex.glId);
            // loc = glGetUniformLocation(shaderId, "uCornerRadius");
            // glUniform1f(loc, call->cornerRadius);
            // loc = glGetUniformLocation(shaderId, "uBorderColor");
            // glUniform4f(loc, call->borderColor.R, call->borderColor.G, call->borderColor.B, call->borderColor.A);
            // loc = glGetUniformLocation(shaderId, "uBorderRadius");
            // glUniform1f(loc, call->borderThickness);

            glBindVertexArray(_ren_globs.squareVAId);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            glBindVertexArray(0);
        } else {
            assert(false);
        }
    }
    bump_clear(&_ren_globs.arena);
    _ren_globs.firstCall = NULL;
    _ren_globs.lastCall = NULL;
}