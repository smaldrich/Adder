#pragma once

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <malloc.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include "GLAD/include/glad/glad.h"
#include "HMM/HandmadeMath.h"
#include "SDL2/include/SDL2/SDL.h"
#include "stb/stb_truetype.h"

// UTILITIES ==================================================================
// UTILITIES ==================================================================
// UTILITIES ==================================================================

// FIXME: logging system / remove all instances of printf
// FIXME: vector macro abstraction

#define SNZ_MIN(a, b) ((a < b) ? a : b)
#define SNZ_MAX(a, b) ((a > b) ? a : b)

// FIXME: macro override for these somehow

// appends a newline :)
// FIXME: some type of way to see cause of failure
#define SNZ_ASSERT(cond, msg) _snz_assertf(cond, "%s", __FILE__, __LINE__, msg)
#define SNZ_ASSERTF(cond, fmt, ...) _snz_assertf(cond, fmt, __FILE__, __LINE__, __VA_ARGS__)

void _snz_assertf(bool cond, const char* fmt, const char* file, int64_t line, ...) {
    if (!cond) {
        va_list args;
        va_start(args, line);
        printf("[%s:%lld]: ", file, line);
        vprintf(fmt, args);
        printf("\n");
        va_end(args);
        assert(false);
    }
}

#define SNZ_OPTION_NAMED(okT, errorT, name) \
    typedef struct {                        \
        okT ok;                             \
        errorT error;                       \
    } name;

#define SNZ_OPTION(okT, errorT) SNZ_OPTION_NAMED(okT, errorT, okT##Opt)

void snz_testPrint(bool result, const char* name) {
    const char* colorCode = (result) ? "\x1B[0;32m" : "\x1B[0;31m";
    const char* resultStr = (result) ? "passed" : "failed";
    printf("\x1B[0m%s\"%s\" %s\x1B[0m\n", colorCode, name, resultStr);
}

void snz_testPrintSection(const char* name) {
    printf("\n    -- %s Tests -- \n", name);
}

// FIXME: multiple def guards

// UTILITIES ==================================================================
// UTILITIES ==================================================================
// UTILITIES ==================================================================

// ARENAS ======================================================================
// ARENAS ======================================================================
// ARENAS ======================================================================

// fixed size, ungrowable
// zeroes memory on free and init
// FIXME: make growable
// FIXME: testing
typedef struct {
    void* start;
    void* end;
    int64_t reserved;
    const char* name;  // used for debug messages only
} snz_Arena;

// returns a pointer to memory that is zeroed
#define SNZ_ARENA_PUSH(bump, T) ((T*)(snz_arenaPush((bump), sizeof(T))))

// returns a pointer to memory that is zeroed
#define SNZ_ARENA_PUSH_ARR(bump, count, T) (T*)(snz_arenaPush((bump), sizeof(T) * (count)))

snz_Arena snz_arenaInit(int64_t size, const char* name) {
    snz_Arena a;
    a.name = name;
    a.reserved = size;
    a.start = calloc(1, size);
    SNZ_ASSERTF(a.start != NULL, "arena alloc for '%s' failed.", a.name);
    a.end = a.start;
    return a;
}

void snz_arenaDeinit(snz_Arena* a) {
    free(a->start);
    memset(a, 0, sizeof(*a));
}

// FIXME: file and line of req.
void* snz_arenaPush(snz_Arena* a, int64_t size) {
    size += (size % sizeof(uint64_t));
    char* o = (char*)(a->end);
    if (!(o + size < (char*)(a->start) + a->reserved)) {
        SNZ_ASSERTF(false,
                    "arena push failed for '%s'. Cap: %lld, Used: %llu, Requested: %llu",
                    a->name, a->reserved, (uint64_t)a->end - (uint64_t)a->start, size);
    }
    a->end = o + size;
    return o;
}

void snz_arenaPop(snz_Arena* a, int64_t size) {
    char* c = (char*)(a->end);
    SNZ_ASSERTF(size <= (c - (char*)(a->start)),
                "arena pop failed for '%s', tried to pop %lld bytes, only %lld remaining",
                a->name, size, (uint64_t)a->end - (uint64_t)a->start);
    a->end = c - size;
    memset(a->end, 0, size);
}

void snz_arenaClear(snz_Arena* a) {
    SNZ_ASSERTF(a->start != NULL,
                "arena clear failed for '%s', start was null (unallocated?)",
                a->name);
    memset(a->start, 0, (int64_t)(a->end) - (int64_t)(a->start));
    a->end = a->start;
}

char* snz_arenaCopyStr(snz_Arena* arena, const char* str) {
    char* chars = SNZ_ARENA_PUSH_ARR(arena, strlen(str) + 1, char);
    strcpy(chars, str);
    return chars;
}

char* snz_arenaFormatStr(snz_Arena* arena, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    uint64_t len = vsnprintf(NULL, 0, fmt, args);
    char* out = SNZ_ARENA_PUSH_ARR(arena, len + 1, char);
    vsprintf_s(out, len + 1, fmt, args);

    va_end(args);
    return out;
}

// ARENAS ======================================================================
// ARENAS ======================================================================
// ARENAS ======================================================================

// RENDER ======================================================================
// RENDER ======================================================================
// RENDER ======================================================================

#define _SNZR_FONT_ATLAS_W 2000
#define _SNZR_FONT_ATLAS_H 2000

typedef struct {
    uint32_t glId;
    uint32_t width;
    uint32_t height;
} snzr_Texture;  // doesn't own or point pixel memory, assumed to be loaded on the gpu already

typedef struct {
    float renderedSize;
    float ascent;
    float descent;  // FIXME: what sign is this
    float lineGap;
    stbtt_pack_range packRange;
    snzr_Texture atlas;
} snzr_Font;

struct {
    HMM_Vec2 screenSize;

    uint32_t rectShaderId;
    uint32_t lineShaderId;
    uint32_t lineShaderSSBOId;

    snzr_Texture solidTex;
} _snzr_globs;

static void _snzr_glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam) {
    // hides messages talking about buffer detailed info
    if (type == GL_DEBUG_TYPE_OTHER) {
        return;
    }
    printf("[GL]: %i, %s\n", type, message);
    type = source = id = severity = length = (int)(uint64_t)userParam;  // to get rid of unused arg warnings
}

// wraps a call in a do while, with another line that asserts no gl errors are present
#define snzr_callGLFnOrError(lineOfCode)                                                    \
    do {                                                                                    \
        lineOfCode;                                                                         \
        int64_t err = glGetError();                                                         \
        if (err != GL_NO_ERROR) {                                                           \
            SNZ_ASSERTF(false, "open gl function %s failed. code: %lld", #lineOfCode, err); \
        }                                                                                   \
    } while (0)

// step kind should be GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
// asserts on failure of any kind, including opening the file and compiling the shader stage
static uint32_t _snzr_loadShaderStep(const char* src, GLenum stepKind, snz_Arena* scratch) {
    int32_t shaderSrcCount = strlen(src);

    uint32_t id = glCreateShader(stepKind);
    glShaderSource(id, 1, (const char* const*)&src, &shaderSrcCount);
    glCompileShader(id);

    int compileSucceded = false;
    char* logBuffer = SNZ_ARENA_PUSH_ARR(scratch, 512, char);  // FIXME: this is gross
    glGetShaderiv(id, GL_COMPILE_STATUS, &compileSucceded);
    if (!compileSucceded) {
        glGetShaderInfoLog(id, 512, NULL, logBuffer);
        printf("[snzr]: Compiling shader stage \"%d\" failied: %s\n", stepKind, logBuffer);
        assert(false);
    };

    return id;
}

// returns the openGL id of the shader
uint32_t snzr_shaderInit(const char* vertChars, const char* fragChars, snz_Arena* scratch) {
    uint32_t vert = _snzr_loadShaderStep(vertChars, GL_VERTEX_SHADER, scratch);
    uint32_t frag = _snzr_loadShaderStep(fragChars, GL_FRAGMENT_SHADER, scratch);
    uint32_t id = glCreateProgram();
    snzr_callGLFnOrError(glAttachShader(id, vert));
    snzr_callGLFnOrError(glAttachShader(id, frag));
    snzr_callGLFnOrError(glLinkProgram(id));
    snzr_callGLFnOrError(glValidateProgram(id));
    return id;
}

// data does not need to be kept alive after this call
// may be null to indicate undefined contents
snzr_Texture snzr_textureInitRBGA(int32_t width, int32_t height, uint8_t* data) {
    snzr_Texture out = {.width = width, .height = height};
    snzr_callGLFnOrError(glGenTextures(1, &out.glId));
    snzr_callGLFnOrError(glBindTexture(GL_TEXTURE_2D, out.glId));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    snzr_callGLFnOrError(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
    return out;
}

// data does not need to be kept alive after this call
snzr_Texture snzr_textureInitGrayscale(int32_t width, int32_t height, uint8_t* data) {
    snzr_Texture out = {.width = width, .height = height};
    snzr_callGLFnOrError(glGenTextures(1, &out.glId));
    snzr_callGLFnOrError(glBindTexture(GL_TEXTURE_2D, out.glId));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    snzr_callGLFnOrError(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    snzr_callGLFnOrError(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data));
    return out;
}

// FIXME: there is no method of freeing gl resources built in, that is bad

typedef struct {
    uint32_t glId;
    uint32_t depthBufferId;
    snzr_Texture texture;
} snzr_FrameBuffer;

// returns the GLID for a new framebuffer, attacted to tex, and with size of tex + a depth buffer
snzr_FrameBuffer snzr_frameBufferInit(snzr_Texture tex) {
    snzr_FrameBuffer out = (snzr_FrameBuffer){
        .depthBufferId = 0,
        .glId = 0,
        .texture = tex,
    };
    snzr_callGLFnOrError(glGenFramebuffers(1, &out.glId));
    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, out.glId));

    snzr_callGLFnOrError(glActiveTexture(GL_TEXTURE0));
    snzr_callGLFnOrError(glBindTexture(GL_TEXTURE_2D, tex.glId));
    snzr_callGLFnOrError(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.glId, 0));

    snzr_callGLFnOrError(glGenRenderbuffers(1, &out.depthBufferId));
    snzr_callGLFnOrError(glBindRenderbuffer(GL_RENDERBUFFER, out.depthBufferId));
    snzr_callGLFnOrError(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32, tex.width, tex.height));
    snzr_callGLFnOrError(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, out.depthBufferId));

    SNZ_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer gen failed.");
    snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    return out;
}

void snzr_frameBufferDeinit(snzr_FrameBuffer* fb) {
    glDeleteFramebuffers(1, &fb->glId);
    glDeleteRenderbuffers(1, &fb->depthBufferId);
    glDeleteTextures(1, &fb->texture.glId);
    memset(fb, 0, sizeof(*fb));
}

#define _SNZR_FONT_FIRST_ASCII 32
#define _SNZR_FONT_ASCII_CHAR_COUNT (255 - _SNZR_FONT_FIRST_ASCII)
#define _SNZR_FONT_UNKNOWN_CHAR 9633  // white box, see: https://www.fileformat.info/info/unicode/char/25a1/index.htm

snzr_Font snzr_fontInit(snz_Arena* dataArena, snz_Arena* scratch, const char* path, float size) {
    snzr_Font out = {.renderedSize = size};

    uint8_t* fileData;
    {
        FILE* file = fopen(path, "rb");
        SNZ_ASSERT(file != NULL, "opening font file '%s' failed.");

        fseek(file, 0L, SEEK_END);
        uint64_t size = ftell(file);
        fseek(file, 0L, SEEK_SET);

        fileData = SNZ_ARENA_PUSH_ARR(scratch, size, uint8_t);
        SNZ_ASSERT(fread(fileData, sizeof(uint8_t), size, file) == size, "reading font file failed.");
        fclose(file);
    }

    stbtt_fontinfo font;
    stbtt_InitFont(&font, fileData, stbtt_GetFontOffsetForIndex(fileData, 0));
    stbtt_GetScaledFontVMetrics(fileData, 0, out.renderedSize,
                                &out.ascent,
                                &out.descent,
                                &out.lineGap);

    stbtt_pack_context ctx;
    uint8_t* atlasData = SNZ_ARENA_PUSH_ARR(scratch, _SNZR_FONT_ATLAS_W * _SNZR_FONT_ATLAS_H, uint8_t);
    assert(stbtt_PackBegin(&ctx, atlasData,
                           _SNZR_FONT_ATLAS_W,
                           _SNZR_FONT_ATLAS_H,
                           _SNZR_FONT_ATLAS_W,
                           1,
                           NULL));
    stbtt_PackSetOversampling(&ctx, 1, 1);

    const int glyphCount = _SNZR_FONT_ASCII_CHAR_COUNT + 1;
    int codepoints[glyphCount];
    codepoints[0] = _SNZR_FONT_UNKNOWN_CHAR;
    for (int i = 0; i < _SNZR_FONT_ASCII_CHAR_COUNT; i++) {
        // skip 0, thats the missing glyph
        codepoints[i + 1] = _SNZR_FONT_FIRST_ASCII + i;
    }

    out.packRange = (stbtt_pack_range){
        .font_size = out.renderedSize,
        .array_of_unicode_codepoints = codepoints,
        .num_chars = glyphCount,
        .chardata_for_range = SNZ_ARENA_PUSH_ARR(dataArena, glyphCount, stbtt_packedchar),
    };
    stbtt_PackFontRanges(&ctx, fileData, 0, &out.packRange, 1);
    stbtt_PackEnd(&ctx);

    out.atlas = snzr_textureInitGrayscale(_SNZR_FONT_ATLAS_W, _SNZR_FONT_ATLAS_H, atlasData);
    return out;
}

static void _snzr_init(snz_Arena* scratchArena) {
    {  // initialize gl settings
        gladLoadGL();
        glLoadIdentity();
        snzr_callGLFnOrError(glEnable(GL_DEPTH_TEST));
        snzr_callGLFnOrError(glDepthFunc(GL_LESS | GL_EQUAL));
        snzr_callGLFnOrError(glEnable(GL_BLEND));
        snzr_callGLFnOrError(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        snzr_callGLFnOrError(glEnable(GL_DEBUG_OUTPUT));

        // snzr_callGLFnOrError(glEnable(GL_CULL_FACE));
        // snzr_callGLFnOrError(glCullFace(GL_BACK));
        // snzr_callGLFnOrError(glFrontFace(GL_CCW));
        snzr_callGLFnOrError(glEnable(GL_MULTISAMPLE));
        snzr_callGLFnOrError(glDebugMessageCallback(_snzr_glDebugCallback, 0));

        snzr_callGLFnOrError(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        snzr_callGLFnOrError(glPixelStorei(GL_PACK_ALIGNMENT, 1));
    }

    {
        const char* vertSrc =
            "#version 330 core\n"
            "out vec2 vUv;"
            "out vec2 vCenterFromFragPos;"
            "out vec2 vRectHalfSize;"
            ""
            "uniform mat4 uVP;"
            "uniform float uZ;"
            "uniform vec2 uDstStart;"
            "uniform vec2 uDstEnd;"
            "uniform vec2 uSrcStart;"
            "uniform vec2 uSrcEnd;"
            ""
            "uniform vec2 uClipStart;"
            "uniform vec2 uClipEnd;"
            ""
            "vec2 cornerTable[6] = vec2[]("
            "    vec2(0, 0),"
            "    vec2(0, 1),"
            "    vec2(1, 1),"
            "    vec2(1, 1),"
            "    vec2(1, 0),"
            "    vec2(0, 0)"
            "    );"
            ""
            "void main() {"
            "    vec2 uvPos = cornerTable[gl_VertexID % 6];"
            "    uvPos *= uSrcEnd - uSrcStart;"
            "    uvPos += uSrcStart;"
            "    vUv = uvPos;"
            ""
            "    vec2 pos = cornerTable[gl_VertexID % 6];"
            "    pos *= uDstEnd - uDstStart;"
            "    pos += uDstStart;"
            ""
            "    vec2 startDiff = uClipStart - pos;"
            "    startDiff = max(startDiff, vec2(0, 0));"
            "    vec2 endDiff = uClipEnd - pos;"
            "    endDiff = min(endDiff, vec2(0, 0));"
            ""
            "    vec2 totalDiff = startDiff + endDiff;"
            "    vUv += totalDiff / (uDstEnd - uDstStart) * (uSrcEnd - uSrcStart);"
            "    pos += totalDiff;"
            ""
            "    vRectHalfSize = (uDstEnd - uDstStart) / 2.0;"
            "    vCenterFromFragPos = (uDstStart + vRectHalfSize) - pos;"
            "    gl_Position = uVP * vec4(pos, uZ, 1);"
            "};";

        const char* fragSrc =
            "#version 330 core\n"
            "out vec4 color;"

            "in vec2 vUv;"
            "in vec2 vCenterFromFragPos;"
            "in vec2 vRectHalfSize;"

            "uniform vec4 uColor;"
            "uniform sampler2D uFontTexture;"
            "uniform sampler2D uColorTexture;"
            "uniform float uCornerRadius;"

            "uniform float uBorderThickness;"
            "uniform vec4 uBorderColor;"

            "float roundedRectSDF(float r) {"
            "    vec2 d2 = abs(vCenterFromFragPos) - abs(vRectHalfSize) + vec2(r, r);"
            "    return min(max(d2.x, d2.y), 0.0) + length(max(d2, 0.0)) - r;"
            "}"

            "void main() {"
            "    vec4 textureColor = texture(uColorTexture, vUv);"
            "    vec4 fontColor = vec4(1.0, 1.0, 1.0, texture(uFontTexture, vUv).r);"
            "    color = uColor * textureColor * fontColor;"

            "    float dist = roundedRectSDF(uCornerRadius);"
            "    if(dist > 0) {"
            "        discard;"
            "    } else if(dist > -uBorderThickness) {"
            "        color = uBorderColor;"
            "    }"

            "    if (color.a <= 0.01) { discard; }"
            "};";
        _snzr_globs.rectShaderId = snzr_shaderInit(vertSrc, fragSrc, scratchArena);
    }

    {
        const char* vertSrc =
            "#version 460\n"
            "struct lineVert {"
            "    vec2 pos;"
            "};"

            "layout(std430, binding = 0) buffer vertBuffer {"
            "    lineVert verts[];"
            "};"

            "uniform mat4 uVP;"
            "uniform float uZ;"
            "uniform vec2 uResolution;"
            "uniform float uThickness;"

            "out vec3 vFragPos;"

            "/* STOLEN FROM HERE: https://stackoverflow.com/questions/60440682/drawing-a-line-in-modern-opengl */"
            "void main() {"
            "    int line_i = gl_VertexID / 6;"
            "    int tri_i  = gl_VertexID % 6;"

            "    vec4 va[4];"
            "    for (int i=0; i<4; ++i)"
            "    {"
            "        va[i] = uVP * vec4(verts[line_i+i].pos, uZ, 1);"
            "        va[i].xyz /= va[i].w;"
            "        va[i].xy = (va[i].xy + 1.0) * 0.5 * uResolution;"
            "    }"

            "    vec2 v_line  = normalize(va[2].xy - va[1].xy);"
            "    vec2 nv_line = vec2(-v_line.y, v_line.x);"

            "    vec4 pos;"
            "    if (tri_i == 0 || tri_i == 1 || tri_i == 3)"
            "    {"
            "        vec2 v_pred  = normalize(va[1].xy - va[0].xy);"
            "        vec2 v_miter = normalize(nv_line + vec2(-v_pred.y, v_pred.x));"
            "        pos = va[1];"
            "        pos.xy += v_miter * uThickness * (tri_i == 1 ? -0.5 : 0.5) / dot(v_miter, nv_line);"
            "        vFragPos = vec3(verts[line_i + 1].pos, uZ);"  // FIXME: technically this isn't accounting for the width of the line in determning frag position, but I don't care rn
            "    }"
            "    else"
            "    {"
            "        vec2 v_succ  = normalize(va[3].xy - va[2].xy);"
            "        vec2 v_miter = normalize(nv_line + vec2(-v_succ.y, v_succ.x));"
            "        pos = va[2];"
            "        pos.xy += v_miter * uThickness * (tri_i == 5 ? 0.5 : -0.5) / dot(v_miter, nv_line);"
            "        vFragPos = vec3(verts[line_i + 2].pos, uZ);"  // FIXME: technically this isn't accounting for the width of the line in determning frag position, but I don't care rn
            "    }"
            "    pos.xy = pos.xy / uResolution * 2.0 - 1.0;"
            "    pos.xyz *= pos.w;"
            "    gl_Position = pos;"
            "};";
        const char* fragSrc =
            "#version 330 core\n"
            "out vec4 color;"
            "uniform vec4 uColor;"
            "uniform vec2 uResolution;"
            "in vec3 vFragPos;"
            "uniform vec3 uFalloffOrigin;"
            "uniform float uFalloffOffset;"
            "uniform float uFalloffDuration;"

            "void main() {"
            "    vec3 diffVec = vFragPos - uFalloffOrigin;"
            "    float alpha = min(1, 1 + (1 / uFalloffDuration) * (-length(diffVec) + uFalloffOffset));"
            "    color = vec4(uColor.xyz, alpha * uColor.w);"
            "};";

        // FIXME: issues when lines go off screen
        _snzr_globs.lineShaderId = snzr_shaderInit(vertSrc, fragSrc, scratchArena);
    }

    snzr_callGLFnOrError(glGenBuffers(1, &_snzr_globs.lineShaderSSBOId));
    snzr_callGLFnOrError(glBindBuffer(GL_SHADER_STORAGE_BUFFER, _snzr_globs.lineShaderSSBOId));
    snzr_callGLFnOrError(glBufferData(GL_SHADER_STORAGE_BUFFER, 0, NULL, GL_DYNAMIC_DRAW));

    uint8_t solidTexData[] = {255, 255, 255, 255};
    _snzr_globs.solidTex = snzr_textureInitRBGA(1, 1, solidTexData);
}

void snzr_drawRect(
    HMM_Vec2 start,
    HMM_Vec2 end,
    HMM_Vec2 clipStart,
    HMM_Vec2 clipEnd,
    HMM_Vec4 color,
    float cornerRadius,
    float borderThickness,
    HMM_Vec4 borderColor,
    HMM_Mat4 vp,
    snzr_Texture texture) {
    // FIXME: layering system
    // FIXME: error safe gl calls
    snzr_callGLFnOrError(glUseProgram(_snzr_globs.rectShaderId));

    int loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uVP");
    glUniformMatrix4fv(loc, 1, false, (float*)&vp);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uZ");
    glUniform1f(loc, 0);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uCornerRadius");
    glUniform1f(loc, cornerRadius);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uBorderThickness");
    glUniform1f(loc, borderThickness);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uBorderColor");
    glUniform4f(loc, borderColor.X, borderColor.Y, borderColor.Z, borderColor.A);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uDstStart");
    glUniform2f(loc, start.X, start.Y);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uDstEnd");
    glUniform2f(loc, end.X, end.Y);

    // flip vertically because we assume this is being used in pixel space, where 00 is in the UL corner
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uSrcStart");
    glUniform2f(loc, 0, 1);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uSrcEnd");
    glUniform2f(loc, 1, 0);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uClipStart");
    glUniform2f(loc, clipStart.X, clipStart.Y);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uClipEnd");
    glUniform2f(loc, clipEnd.X, clipEnd.Y);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uColor");
    glUniform4f(loc, color.X, color.Y, color.Z, color.W);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uFontTexture");
    glUniform1i(loc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _snzr_globs.solidTex.glId);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uColorTexture");
    glUniform1i(loc, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture.glId);

    snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, 6));
}

static const stbtt_packedchar* _snzr_getGylphFromChar(const snzr_Font* font, char c) {
    int glyph = c - _SNZR_FONT_FIRST_ASCII;
    if (glyph < 0) {
        return &font->packRange.chardata_for_range[0];  // first is unknown char
    } else if (glyph >= _SNZR_FONT_ASCII_CHAR_COUNT) {
        return &font->packRange.chardata_for_range[0];
    }
    return &font->packRange.chardata_for_range[glyph + 1];
}

HMM_Vec2 snzr_strSize(const snzr_Font* font, const char* str, uint64_t charCount) {
    float x = 0;
    uint64_t lineCount = 1;
    for (uint64_t i = 0; i < charCount; i++) {
        char c = str[i];
        if (c == '\n') {
            lineCount++;
            continue;
        } else if (c == '\r') {
            continue;
        } else if (c == '\t') {
            continue;  // TODO: what exactly should i be doing here?
        }
        const stbtt_packedchar* glyph = _snzr_getGylphFromChar(font, str[i]);
        x += glyph->xadvance;
    }
    return HMM_V2(x, lineCount * font->renderedSize);
}

// FIXME: disgusting function all the way down
// flipVertical, when false, draws with +down, when true, +up
// when snap is on, rects per char get snapped to integer lines
void snzr_drawTextScaled(HMM_Vec2 start,
                         HMM_Vec2 clipStart,
                         HMM_Vec2 clipEnd,
                         HMM_Vec4 color,
                         const char* str,
                         uint64_t charCount,
                         snzr_Font font,
                         HMM_Mat4 vp,
                         float targetSize,
                         bool snap, bool flipVertical) {
    // FIXME: layering system
    // FIXME: safe gl calls
    snzr_callGLFnOrError(glUseProgram(_snzr_globs.rectShaderId));
    int loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uVP");
    glUniformMatrix4fv(loc, 1, false, (float*)&vp);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uZ");
    glUniform1f(loc, 0);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uColor");
    glUniform4f(loc, color.X, color.Y, color.Z, color.W);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uFontTexture");
    glUniform1i(loc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font.atlas.glId);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uCornerRadius");
    glUniform1f(loc, 0);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uBorderThickness");
    glUniform1f(loc, 0);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uBorderColor");
    glUniform4f(loc, 0, 0, 0, 0);

    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uClipStart");
    glUniform2f(loc, clipStart.X, clipStart.Y);
    loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uClipEnd");
    glUniform2f(loc, clipEnd.X, clipEnd.Y);

    float scaleFactor = targetSize / font.renderedSize;
    float verticalFlip = flipVertical ? -1 : 1;

    HMM_Vec2 drawPos = HMM_V2(start.X, start.Y);
    assert(charCount < INT64_MAX);
    for (const char* c = str; *c != 0; c++) {
        if ((c - str) >= (int64_t)charCount) {
            break;
        }
        if (*c == '\n') {
            drawPos.Y += (verticalFlip) * (font.lineGap + font.ascent - font.descent) * scaleFactor;
            drawPos.X = start.X;
            continue;
        } else if (*c == '\r') {
            continue;
        }

        HMM_Vec2 srcStart = HMM_V2(0, 0);
        HMM_Vec2 srcEnd = HMM_V2(0, 0);
        HMM_Vec2 dstStart = HMM_V2(0, 0);
        HMM_Vec2 dstEnd = HMM_V2(0, 0);
        {
            const stbtt_packedchar* b = _snzr_getGylphFromChar(&font, *c);

            HMM_Vec2 s = HMM_MulV2F(HMM_V2(b->xoff, b->yoff * verticalFlip), scaleFactor);
            HMM_Vec2 e = HMM_MulV2F(HMM_V2(b->xoff2, b->yoff2 * verticalFlip), scaleFactor);
            dstStart = HMM_AddV2(drawPos, s);
            dstEnd = HMM_AddV2(dstStart, HMM_Sub(e, s));

            HMM_Vec2 uvScale = HMM_V2(1.0f / font.atlas.width, 1.0f / font.atlas.height);
            srcStart = HMM_MulV2(HMM_V2(b->x0, b->y0), uvScale);
            srcEnd = HMM_MulV2(HMM_V2(b->x1, b->y1), uvScale);

            drawPos.X += b->xadvance * scaleFactor;
        }
        if (snap) {
            dstStart.X = (int)dstStart.X;
            dstStart.Y = (int)dstStart.Y;
            dstEnd.X = (int)dstEnd.X;
            dstEnd.Y = (int)dstEnd.Y;
        }

        loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uSrcStart");
        glUniform2f(loc, srcStart.X, srcStart.Y);
        loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uSrcEnd");
        glUniform2f(loc, srcEnd.X, srcEnd.Y);
        loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uDstStart");
        glUniform2f(loc, dstStart.X, dstStart.Y);
        loc = glGetUniformLocation(_snzr_globs.rectShaderId, "uDstEnd");
        glUniform2f(loc, dstEnd.X, dstEnd.Y);
        snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, 6));
    }
}

// default to use for UI in 2d pixel space
void snzr_drawText(HMM_Vec2 start,
                   HMM_Vec2 clipStart,
                   HMM_Vec2 clipEnd,
                   HMM_Vec4 color,
                   const char* str,
                   uint64_t charCount,
                   snzr_Font font,
                   HMM_Mat4 vp) {
    snzr_drawTextScaled(start, clipStart, clipEnd, color, str, charCount, font, vp, font.renderedSize, true, false);
}

// FIXME: disgusting hack on the shader for this
void snzr_drawLineFaded(
    HMM_Vec2* pts,
    uint64_t ptCount,
    HMM_Vec4 color,
    float thickness,
    HMM_Mat4 vp,
    HMM_Vec3 falloffOrigin,
    float falloffOffset,
    float falloffDuration) {
    if (ptCount < 2) {
        return;
    }
    snzr_callGLFnOrError(glUseProgram(_snzr_globs.lineShaderId));

    int loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uColor");
    glUniform4f(loc, color.X, color.Y, color.Z, color.W);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uThickness");
    glUniform1f(loc, thickness);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uVP");
    glUniformMatrix4fv(loc, 1, false, (float*)&vp);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uZ");
    glUniform1f(loc, 0);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uResolution");
    glUniform2f(loc, _snzr_globs.screenSize.X, _snzr_globs.screenSize.Y);

    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uFalloffOrigin");
    glUniform3f(loc, falloffOrigin.X, falloffOrigin.Y, falloffOrigin.Z);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uFalloffOffset");
    glUniform1f(loc, falloffOffset);
    loc = glGetUniformLocation(_snzr_globs.lineShaderId, "uFalloffDuration");
    glUniform1f(loc, falloffDuration);

    snzr_callGLFnOrError(glBindBuffer(GL_SHADER_STORAGE_BUFFER, _snzr_globs.lineShaderSSBOId));
    snzr_callGLFnOrError(glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(HMM_Vec2) * (ptCount + 2), NULL, GL_DYNAMIC_DRAW));

    HMM_Vec2 startMiter = HMM_Sub(pts[1], pts[0]);
    startMiter = HMM_Mul(HMM_Norm(startMiter), 0.001f);
    startMiter = HMM_Sub(pts[0], startMiter);
    snzr_callGLFnOrError(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(HMM_Vec2), &startMiter));
    snzr_callGLFnOrError(glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(HMM_Vec2), ptCount * sizeof(HMM_Vec2), pts));
    HMM_Vec2 endMiter = HMM_Sub(pts[ptCount - 1], pts[ptCount - 2]);
    endMiter = HMM_Mul(HMM_Norm(endMiter), 0.001f);
    endMiter = HMM_Add(pts[ptCount - 1], endMiter);
    snzr_callGLFnOrError(glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(HMM_Vec2) * (ptCount + 1), sizeof(HMM_Vec2), &endMiter));

    snzr_callGLFnOrError(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, _snzr_globs.lineShaderSSBOId));
    snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, (ptCount - 1) * 6));
}

// end miters automatically added, pointing straight away
void snzr_drawLine(HMM_Vec2* pts, uint64_t ptCount, HMM_Vec4 color, float thickness, HMM_Mat4 vp) {
    snzr_drawLineFaded(pts, ptCount, color, thickness, vp, HMM_V3(0, 0, 0), INFINITY, INFINITY);
}

// RENDER ======================================================================
// RENDER ======================================================================
// RENDER ======================================================================

// UI ==========================================================================
// UI ==========================================================================
// UI ==========================================================================

#define SNZU_TEXT_PADDING 5
// FIXME: ^ these should be part of the component lib, not hardcoded

// TODO: new as parent
// TODO: easing functions
// TODO: test shiz
// TODO: utilities
// TODO: rounding, borders
// TODO: textures
// TODO: singleheader ify
// TODO: docs pass

// TODO: no error if children extend past their parents
// w/ the layer system in place, this can probably be automatically applied

// TODO: layer system
// the idea is an enum value stored per box which determines the 'layer' of each thing under it
// layers would be something like 'default' 'nav bar' 'popout' 'tooltip', where later layes are rendered on top
// of earlier ones and recieve events soonest
// layer enums would prolly just be ignored after the outermost one has been set

typedef enum {
    SNZU_AX_X,
    SNZU_AX_Y,
    SNZU_AX_COUNT
} snzu_Axis;

typedef enum {
    SNZU_ALIGN_MIN = 0,
    SNZU_ALIGN_CENTER = 1,
    SNZU_ALIGN_MAX = 2,
    SNZU_ALIGN_TOP = SNZU_ALIGN_MIN,
    SNZU_ALIGN_BOTTOM = SNZU_ALIGN_MAX,
    SNZU_ALIGN_LEFT = SNZU_ALIGN_MIN,
    SNZU_ALIGN_RIGHT = SNZU_ALIGN_MAX,
} snzu_Align;

typedef enum {
    SNZU_IF_NONE = 0,
    SNZU_IF_HOVER = (1 << 0),
    SNZU_IF_MOUSE_BUTTONS = (1 << 1),
    SNZU_IF_MOUSE_SCROLL = (1 << 2),
} snzu_InteractionFlags;

typedef enum {
    SNZU_MB_LEFT,
    SNZU_MB_RIGHT,
    SNZU_MB_MIDDLE,
    SNZU_MB_COUNT
} snzu_MouseButton;

typedef enum {
    SNZU_ACT_NONE,
    SNZU_ACT_DOWN,
    SNZU_ACT_DRAG,
    SNZU_ACT_UP,
} snzu_Action;

#define _SNZU_TEXT_INPUT_CHAR_MAX 4

typedef struct snzu_Interaction snzu_Interaction;
struct snzu_Interaction {
    bool hovered;
    HMM_Vec2 mousePosLocal;  // mouse pos relative to the start of this box
    HMM_Vec2 mousePosGlobal;
    float mouseScrollY;
    snzu_Action mouseActions[SNZU_MB_COUNT];  // index into this using the snzu_MouseButton enum

    char keyChars[_SNZU_TEXT_INPUT_CHAR_MAX];
    uint64_t keyCode;
    uint64_t keyMods;
    snzu_Action keyAction;

    bool dragged;
    HMM_Vec2 dragBeginningLocal;
    HMM_Vec2 dragBeginningGlobal;
};

// FIXME: opaque ptr type

typedef struct _snzu_Box _snzu_Box;
struct _snzu_Box {
    uint64_t pathHash;
    const char* tag;

    HMM_Vec2 start;
    HMM_Vec2 end;
    HMM_Vec4 color;
    float cornerRadius;
    float borderThickness;
    HMM_Vec4 borderColor;
    snzr_Texture texture;

    const char* displayStr;
    uint64_t displayStrLen;
    const snzr_Font* font;
    HMM_Vec4 displayStrColor;

    HMM_Vec2 clippedStart;
    HMM_Vec2 clippedEnd;
    bool clipChildren;

    snzu_Interaction* interactionTarget;
    snzu_InteractionFlags interactionMask;

    _snzu_Box* firstChild;
    _snzu_Box* lastChild;
    _snzu_Box* nextSibling;
    _snzu_Box* prevSibling;
    _snzu_Box* parent;  // expected to always be nonnull, except for the trees first parent (which build code should not operate on)
};

typedef struct {
    uint64_t lastFrameTouched;
    uint64_t allocSize;
    void* alloc;
    uint64_t pathHash;
    bool inUse;
} _snzu_useMemAllocNode;
#define _SNZU_USEMEM_MAX_ALLOCS 8000

typedef struct {
    HMM_Vec2 mousePos;
    bool mouseStates[SNZU_MB_COUNT];
    char charsEntered[_SNZU_TEXT_INPUT_CHAR_MAX];
    float mouseScrollY;
    uint64_t keyCode;
    uint64_t keyMods;
    snzu_Action keyAction;
} snzu_Input;

typedef struct {
    _snzu_Box treeParent;
    _snzu_Box* currentParentBox;
    _snzu_Box* selectedBox;
    snz_Arena* frameArena;

    _snzu_useMemAllocNode useMemAllocs[_SNZU_USEMEM_MAX_ALLOCS];
    bool useMemIsLastAllocTouchedNew;
    uint64_t currentFrameIdx;
    float timeSinceLastFrame;

    snzu_Input currentInputs;
    snzu_Input previousInputs;
    snzu_Action mouseActions[SNZU_MB_COUNT];

    uint64_t mouseCapturePathHash;  // zero indicates unset

    // persistant, set to null whenever a click happens
    // useful to make sure keyboard input only goes to one place
    uint64_t focusedPathHash;
} _snzu_Globs;
static _snzu_Globs _snzu_globs;

static uint64_t _snzu_generatePathHash(uint64_t parentPathHash, const char* tag) {
    // stolen, https://stackoverflow.com/questions/7666509/hash-function-for-string
    uint32_t tagHash = 5381;
    uint8_t c;
    while ((c = *tag++)) {
        tagHash = ((tagHash << 5) + tagHash) + c; /* hash * 33 + c */
    }
    return tagHash + parentPathHash;  // FIXME: is this problematic at all?
}

// returns an initially zeroed piece of memory that will persist between frames
// memory is automatically freed when it is not used (with the same size) for a frame
// tag must be unique with all siblings inside the current parent box
// size is ignored on a repeat allocation // FIXME: shouldn't be tho :)
// TODO: invalid access unit tests
// TODO: unit tests when input is done
void* snzu_useMem(uint64_t size, const char* tag) {
    _snzu_Box* pathTarget = _snzu_globs.selectedBox;
    uint64_t pathHash = _snzu_generatePathHash(pathTarget->pathHash, tag);

    // first check if there is a node out there that correllates with this ones tag
    _snzu_useMemAllocNode* firstFree = NULL;
    for (uint64_t i = 0; i < _SNZU_USEMEM_MAX_ALLOCS; i++) {
        _snzu_useMemAllocNode* node = &_snzu_globs.useMemAllocs[i];
        if (!node->inUse) {
            if (!firstFree) {
                firstFree = node;
            }
            continue;
        } else if (node->pathHash == pathHash) {
            if (node->lastFrameTouched != _snzu_globs.currentFrameIdx - 1) {
                // FIXME: this shit needs a better error message
                // like include the parent trace, not just the last level
                SNZ_ASSERTF(false, "double usememmed tag '%s'", tag);
            }
            node->lastFrameTouched = _snzu_globs.currentFrameIdx;
            _snzu_globs.useMemIsLastAllocTouchedNew = false;
            return node->alloc;
        }
    }
    SNZ_ASSERT(firstFree != NULL, "use mem failed, no more free nodes.");
    // no node out there matches, we need to make a new one
    memset(firstFree, 0, sizeof(*firstFree));
    firstFree->inUse = true;
    firstFree->pathHash = pathHash;
    firstFree->allocSize = size;
    firstFree->lastFrameTouched = _snzu_globs.currentFrameIdx;
    firstFree->alloc = calloc(1, size);
    SNZ_ASSERT(firstFree->alloc, "allocating new usemem alloc failed.");
    _snzu_globs.useMemIsLastAllocTouchedNew = true;
    return firstFree->alloc;
}

// returns whether the last returned call to snzu_useMem this frame was newly allocated or persisted
bool snzu_useMemIsPrevNew() {
    return _snzu_globs.useMemIsLastAllocTouchedNew;
}

// sets the in use flag on each useMem node that has not been touched on the current frame
static void _snzu_useMemClearOld() {
    for (uint64_t i = 0; i < _SNZU_USEMEM_MAX_ALLOCS; i++) {
        _snzu_useMemAllocNode* node = &_snzu_globs.useMemAllocs[i];
        if (!node->inUse) {
            continue;
        }
        SNZ_ASSERT(node->lastFrameTouched <= _snzu_globs.currentFrameIdx, "usemem node somehow more recent than frame");
        if (node->lastFrameTouched < _snzu_globs.currentFrameIdx) {
            node->inUse = false;
        }
    }
}

#define SNZU_USE_MEM(T, tag) ((T*)snzu_useMem(sizeof(T), (tag)))
#define SNZU_USE_ARRAY(T, count, tag) ((T*)snzu_useMem(sizeof(T) * (count), (tag)))

// TODO: formatted version
_snzu_Box* snzu_boxNew(const char* tag) {
    SNZ_ASSERT(_snzu_globs.currentParentBox != NULL, "creating a new box, parent was null");
    _snzu_Box* b = SNZ_ARENA_PUSH(_snzu_globs.frameArena, _snzu_Box);
    b->tag = tag;
    b->texture = _snzr_globs.solidTex;

    b->parent = _snzu_globs.currentParentBox;
    if (_snzu_globs.currentParentBox->lastChild) {
        _snzu_globs.currentParentBox->lastChild->nextSibling = b;
        b->prevSibling = _snzu_globs.currentParentBox->lastChild;
        _snzu_globs.currentParentBox->lastChild = b;
    } else {
        _snzu_globs.currentParentBox->firstChild = b;
        _snzu_globs.currentParentBox->lastChild = b;
    }
    _snzu_globs.selectedBox = b;

    b->pathHash = _snzu_generatePathHash(b->parent->pathHash, tag);
    for (_snzu_Box* sibling = b->parent->firstChild; sibling; sibling = sibling->nextSibling) {
        if (sibling == b) {
            continue;
        } else if (sibling->pathHash == b->pathHash) {
            // FIXME: would be nice to have a parent trace
            SNZ_ASSERTF(false, "could not make new box, tag '%s' was already used in parent.\n", tag);
        }
    }

    return b;
}

static void _snzu_beginFrame(snz_Arena* frameArena, HMM_Vec2 screenSize, float dt) {
    _snzu_globs.frameArena = frameArena;

    _snzu_useMemClearOld();
    _snzu_globs.useMemIsLastAllocTouchedNew = false;
    _snzu_globs.currentFrameIdx++;

    _snzu_globs.timeSinceLastFrame = dt;

    memset(&_snzu_globs.treeParent, 0, sizeof(_snzu_globs.treeParent));
    _snzu_globs.treeParent.pathHash = 6969420;
    _snzu_globs.currentParentBox = &_snzu_globs.treeParent;
    _snzu_globs.currentParentBox->end = screenSize;
}

static _snzu_Box* _snzu_findBoxByPathHash(uint64_t pathHash, _snzu_Box* parent) {
    if (parent->pathHash == pathHash) {
        return parent;
    }

    for (_snzu_Box* child = parent->firstChild; child; child = child->nextSibling) {
        _snzu_Box* found = _snzu_findBoxByPathHash(pathHash, child);
        if (found != NULL) {
            return found;
        }
    }

    return NULL;
}

static void _snzu_drawBoxAndChildren(_snzu_Box* parent, HMM_Vec2 clipStart, HMM_Vec2 clipEnd, HMM_Mat4 vp) {
    HMM_Vec2 newClipStart = clipStart;
    HMM_Vec2 newClipEnd = clipEnd;
    if (newClipStart.X < parent->start.X) {
        newClipStart.X = parent->start.X;
    }
    if (newClipEnd.X > parent->end.X) {
        newClipEnd.X = parent->end.X;
    }
    if (newClipStart.Y < parent->start.Y) {
        newClipStart.Y = parent->start.Y;
    }
    if (newClipEnd.Y > parent->end.Y) {
        newClipEnd.Y = parent->end.Y;
    }

    // always forward a clip to the parents clipped rect, because it's used for processing
    // inputs and shouldn't be larger than the parent. Only apply the change to children
    // if the clipChildren flag is set
    parent->clippedStart = newClipStart;
    parent->clippedEnd = newClipEnd;
    if (parent->clipChildren) {
        clipStart = newClipStart;
        clipEnd = newClipEnd;
    }

    snzr_drawRect(
        parent->start, parent->end,
        clipStart, clipEnd,
        parent->color,
        parent->cornerRadius,
        parent->borderThickness, parent->borderColor,
        vp,
        parent->texture);

    if (parent->displayStr != NULL) {
        HMM_Vec2 textPos = HMM_DivV2F(HMM_AddV2(parent->start, parent->end), 2);  // set to the midpoint of the box
        HMM_Vec2 textSize = snzr_strSize(parent->font, parent->displayStr, parent->displayStrLen);
        textPos = HMM_SubV2(textPos, HMM_DivV2F(textSize, 2));
        textPos = HMM_AddV2(textPos, HMM_V2(0, parent->font->ascent));
        snzr_drawText(
            textPos,
            clipStart, clipEnd,
            parent->displayStrColor,
            parent->displayStr, parent->displayStrLen,
            *parent->font,
            vp);
    }

    for (_snzu_Box* child = parent->firstChild; child; child = child->nextSibling) {
        _snzu_drawBoxAndChildren(child, clipStart, clipEnd, vp);
    }
}

static void _snzu_genInteractionsForBoxAndChildren(_snzu_Box* box, uint64_t* remainingInteractionFlags) {
    for (_snzu_Box* child = box->lastChild; child; child = child->prevSibling) {
        _snzu_genInteractionsForBoxAndChildren(child, remainingInteractionFlags);
    }

    if (box->interactionTarget) {
        snzu_Interaction* inter = box->interactionTarget;

        bool boxDragged = inter->dragged;
        HMM_Vec2 boxDragStartLocal = inter->dragBeginningLocal;
        HMM_Vec2 boxDragStartGlobal = inter->dragBeginningGlobal;
        memset(inter, 0, sizeof(*inter));
        inter->dragged = boxDragged;
        inter->dragBeginningLocal = boxDragStartLocal;
        inter->dragBeginningGlobal = boxDragStartGlobal;

        inter->mousePosGlobal = _snzu_globs.currentInputs.mousePos;
        inter->mousePosLocal = HMM_Sub(_snzu_globs.currentInputs.mousePos, box->start);

        // keyboard inputs -- it's up to the obj to manage if they should be consumed
        for (uint64_t i = 0; i < _SNZU_TEXT_INPUT_CHAR_MAX; i++) {
            inter->keyChars[i] = _snzu_globs.currentInputs.charsEntered[i];
        }
        inter->keyCode = _snzu_globs.currentInputs.keyCode;
        inter->keyAction = _snzu_globs.currentInputs.keyAction;
        inter->keyMods = _snzu_globs.currentInputs.keyMods;
    }

    bool containsMouse = false;
    HMM_Vec2 mousePos = _snzu_globs.currentInputs.mousePos;
    if (box->clippedStart.X < mousePos.X && box->clippedEnd.X > mousePos.X) {
        if (box->clippedStart.Y < mousePos.Y && box->clippedEnd.Y > mousePos.Y) {
            containsMouse = true;
        }
    }

    bool captureAllowsMouseEvents = true;
    if (_snzu_globs.mouseCapturePathHash != 0) {
        captureAllowsMouseEvents = false;
        if (box->pathHash == _snzu_globs.mouseCapturePathHash) {
            captureAllowsMouseEvents = true;
        }
    }

    if (containsMouse && captureAllowsMouseEvents) {
        if (box->interactionMask & SNZU_IF_HOVER) {
            if ((*remainingInteractionFlags) & SNZU_IF_HOVER) {
                (*remainingInteractionFlags) ^= SNZU_IF_HOVER;
                if (box->interactionTarget != NULL) {
                    box->interactionTarget->hovered = true;
                }
            }
        }

        if (box->interactionMask & SNZU_IF_MOUSE_BUTTONS) {
            if ((*remainingInteractionFlags) & SNZU_IF_MOUSE_BUTTONS) {
                (*remainingInteractionFlags) ^= SNZU_IF_MOUSE_BUTTONS;

                if (_snzu_globs.mouseCapturePathHash == 0) {
                    // when a mousedown is captured, save the pathhash of the box
                    // this is maintained until a mouseup happens
                    // TODO: this system is completely fucked and very hard to locate when reading thru code
                    // document well or refactor to something better
                    if (_snzu_globs.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN ||
                        _snzu_globs.mouseActions[SNZU_MB_RIGHT] == SNZU_ACT_DOWN ||
                        _snzu_globs.mouseActions[SNZU_MB_MIDDLE] == SNZU_ACT_DOWN) {
                        _snzu_globs.mouseCapturePathHash = box->pathHash;
                    }
                }

                if (box->interactionTarget != NULL) {
                    snzu_Interaction* inter = box->interactionTarget;
                    memcpy(inter->mouseActions, _snzu_globs.mouseActions, sizeof(_snzu_globs.mouseActions));
                }
            }  // end flag checks
        }  // end mouse button checks

        if (box->interactionMask & SNZU_IF_MOUSE_SCROLL) {
            if ((*remainingInteractionFlags) & SNZU_IF_MOUSE_SCROLL) {
                (*remainingInteractionFlags) ^= SNZU_IF_MOUSE_SCROLL;
                if (box->interactionTarget != NULL) {
                    box->interactionTarget->mouseScrollY = _snzu_globs.currentInputs.mouseScrollY;
                }
            }
        }
    }  // end collision check
}

static void _snzu_drawFrameAndGenInterations(snzu_Input input, HMM_Vec2 screenSize) {
    _snzu_globs.currentInputs = input;

    HMM_Mat4 vp;
    vp = HMM_Orthographic_RH_NO(0, screenSize.X, screenSize.Y, 0, 0, 1000);
    _snzu_drawBoxAndChildren(&_snzu_globs.treeParent, HMM_V2(0, 0), screenSize, vp);

    // compute mouse actions for this frame
    bool wasMouseUp = false;
    for (uint64_t i = 0; i < SNZU_MB_COUNT; i++) {
        bool cur = _snzu_globs.currentInputs.mouseStates[i];
        bool prev = _snzu_globs.previousInputs.mouseStates[i];
        if (!cur && prev) {
            _snzu_globs.mouseActions[i] = SNZU_ACT_UP;
            wasMouseUp = true;
        } else if (cur && !prev) {
            _snzu_globs.mouseActions[i] = SNZU_ACT_DOWN;
        } else if (cur && prev) {
            _snzu_globs.mouseActions[i] = SNZU_ACT_DRAG;
        } else {
            _snzu_globs.mouseActions[i] = SNZU_ACT_NONE;
        }
    }

    if (_snzu_globs.mouseActions[0] == SNZU_ACT_DOWN) {
        _snzu_globs.focusedPathHash = 0;  // set null before the frame is processed so that any new focus is aquired correctly
    }

    uint64_t interactionFlags = ~SNZU_IF_NONE;
    _snzu_genInteractionsForBoxAndChildren(&_snzu_globs.treeParent, &interactionFlags);

    if (_snzu_globs.mouseCapturePathHash != 0) {
        _snzu_Box* box = _snzu_findBoxByPathHash(_snzu_globs.mouseCapturePathHash, &_snzu_globs.treeParent);
        if (box != NULL) {
            snzu_Interaction* inter = box->interactionTarget;
            if (inter != NULL) {
                if (_snzu_globs.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
                    inter->dragBeginningGlobal = inter->mousePosGlobal;
                    inter->dragBeginningLocal = inter->mousePosLocal;
                    inter->dragged = true;
                } else if (_snzu_globs.mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP) {
                    inter->dragBeginningGlobal = HMM_V2(0, 0);
                    inter->dragBeginningLocal = HMM_V2(0, 0);
                    inter->dragged = false;
                }
            }
        }
    }

    if (wasMouseUp) {
        _snzu_globs.mouseCapturePathHash = 0;  // RMB clears this focus, which can fuck up a LMB drag, it doesn't get cleared on mouseup bc 'nothing is dragged'
    }
    _snzu_globs.previousInputs = _snzu_globs.currentInputs;
}

void snzu_boxEnter() {
    _snzu_globs.currentParentBox = _snzu_globs.selectedBox;
    _snzu_globs.selectedBox = _snzu_globs.selectedBox;
}
void snzu_boxExit() {
    _snzu_globs.currentParentBox = _snzu_globs.currentParentBox->parent;
    SNZ_ASSERT(_snzu_globs.currentParentBox != NULL, "exiting box makes parent null. (exiting past where the tree should have started)");
    _snzu_globs.selectedBox = _snzu_globs.currentParentBox->lastChild;
}

void snzu_boxSelect(_snzu_Box* box) {
    _snzu_globs.selectedBox = box;
}

_snzu_Box* snzu_getSelectedBox() {
    return _snzu_globs.selectedBox;
}

_snzu_Box* snzu_boxGetParent() {
    return _snzu_globs.selectedBox->parent;
}

#define _snzu_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define snzu_boxScope() _snzu_defer(snzu_boxEnter(), snzu_boxExit())

// outInter will be updated with interaction data at the end of the frame, when genInputsForFrame is called.
// reccommended that the mem is useMemd
// outInter may be null, for cases when just blocking interactions is what you are after
// interactionMask should be a set of snzu_InteractionFlags ored together, only interactions in that set will be reported
void snzu_boxSetInteractionOutput(snzu_Interaction* outInter, uint64_t interactionMask) {
    _snzu_globs.selectedBox->interactionTarget = outInter;
    _snzu_globs.selectedBox->interactionMask = interactionMask;
}

void snzu_boxSetFocused() {
    _snzu_globs.focusedPathHash = _snzu_globs.selectedBox->pathHash;
}

bool snzu_boxFocused() {
    return _snzu_globs.focusedPathHash == _snzu_globs.selectedBox->pathHash;
}

void snzu_clearFocus() {
    _snzu_globs.focusedPathHash = 0;
}

bool snzu_isNothingFocused() {
    return _snzu_globs.focusedPathHash == 0;
}

void snzu_boxClipChildren() {
    _snzu_globs.selectedBox->clipChildren = true;
}

void snzu_boxSetColor(HMM_Vec4 color) {
    _snzu_globs.selectedBox->color = color;
}

void snzu_boxSetCornerRadius(float radiusPx) {
    _snzu_globs.selectedBox->cornerRadius = radiusPx;
}

void snzu_boxSetBorder(float px, HMM_Vec4 col) {
    _snzu_globs.selectedBox->borderThickness = px;
    _snzu_globs.selectedBox->borderColor = col;
}

void snzu_boxSetTexture(snzr_Texture texture) {
    _snzu_globs.selectedBox->color = HMM_V4(1, 1, 1, 1);
    _snzu_globs.selectedBox->texture = texture;
}

// string should last until the end of the frome
// font must also last
void snzu_boxSetDisplayStrLen(const snzr_Font* font, HMM_Vec4 color, const char* str, uint64_t strLen) {
    _snzu_globs.selectedBox->font = font;
    _snzu_globs.selectedBox->displayStrColor = color;
    _snzu_globs.selectedBox->displayStr = str;
    _snzu_globs.selectedBox->displayStrLen = strLen;
}

// str is null terminated, must last until the end of the frame
// font must alos last
void snzu_boxSetDisplayStr(const snzr_Font* font, HMM_Vec4 color, const char* str) {
    snzu_boxSetDisplayStrLen(font, color, str, strlen(str));
}

void snzu_boxSetDisplayStrF(snzr_Font* font, const char* fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    int l = vsnprintf(NULL, 0, fmt, argp);
    char* chars = SNZ_ARENA_PUSH_ARR(_snzu_globs.frameArena, l + 1, char);
    vsnprintf(chars, l + 1, fmt, argp);
    _snzu_globs.selectedBox->displayStr = chars;
    _snzu_globs.selectedBox->displayStrLen = l;
    _snzu_globs.selectedBox->font = font;
    va_end(argp);
}

void snzu_boxSetStart(HMM_Vec2 newStart) {
    _snzu_globs.selectedBox->start = newStart;
}

void snzu_boxSetStartFromParentStart(HMM_Vec2 offset) {
    HMM_Vec2 finalPos = HMM_AddV2(_snzu_globs.selectedBox->parent->start, offset);
    _snzu_globs.selectedBox->start = finalPos;
}

void snzu_boxSetStartAx(float newStart, snzu_Axis ax) {
    _snzu_globs.selectedBox->start.Elements[ax] = newStart;
}

void snzu_boxSetStartFromParentAx(float offset, snzu_Axis ax) {
    float finalPos = _snzu_globs.selectedBox->parent->start.Elements[ax] + offset;
    _snzu_globs.selectedBox->start.Elements[ax] = finalPos;
}

void snzu_boxSetEnd(HMM_Vec2 newEnd) {
    _snzu_globs.selectedBox->end = newEnd;
}

void snzu_boxSetEndFromParentEnd(HMM_Vec2 offset) {
    HMM_Vec2 finalPos = HMM_AddV2(_snzu_globs.selectedBox->parent->end, offset);
    _snzu_globs.selectedBox->end = finalPos;
}

void snzu_boxSetEndAx(float newEnd, snzu_Axis ax) {
    _snzu_globs.selectedBox->end.Elements[ax] = newEnd;
}

void snzu_boxSetSizeFromStart(HMM_Vec2 newSize) {
    _snzu_globs.selectedBox->end = HMM_Add(_snzu_globs.selectedBox->start, newSize);
}

void snzu_boxSetSizeFromEnd(HMM_Vec2 newSize) {
    _snzu_globs.selectedBox->start = HMM_Sub(_snzu_globs.selectedBox->end, newSize);
}

// FIXME: these should have the same arg order as snzu_boxSetStartAx
void snzu_boxSetSizeFromStartAx(snzu_Axis ax, float newSize) {
    _snzu_globs.selectedBox->end.Elements[ax] = _snzu_globs.selectedBox->start.Elements[ax] + newSize;
}

void snzu_boxSetSizeFromEndAx(snzu_Axis ax, float newSize) {
    _snzu_globs.selectedBox->start.Elements[ax] = _snzu_globs.selectedBox->end.Elements[ax] - newSize;
}

HMM_Vec2 snzu_boxGetSize(_snzu_Box* box) {
    return HMM_Sub(box->end, box->start);
}

static void _snzu_boxSetStartKeepSizeRecurse(_snzu_Box* box, HMM_Vec2 diff) {
    box->start = HMM_AddV2(box->start, diff);
    box->end = HMM_AddV2(box->end, diff);
    for (_snzu_Box* child = box->firstChild; child; child = child->nextSibling) {
        _snzu_boxSetStartKeepSizeRecurse(child, diff);
    }
}

void snzu_boxSetStartKeepSizeRecursePtr(_snzu_Box* box, HMM_Vec2 newStart) {
    HMM_Vec2 diff = HMM_SubV2(newStart, box->start);
    _snzu_boxSetStartKeepSizeRecurse(box, diff);
}

// TODO: are there bad perf implications for this??
// also moves all children
void snzu_boxSetStartKeepSizeRecurse(HMM_Vec2 newStart) {
    snzu_boxSetStartKeepSizeRecursePtr(_snzu_globs.selectedBox, newStart);
}

void snzu_boxSetStartFromParentKeepSizeRecurse(HMM_Vec2 offset) {
    HMM_Vec2 final = HMM_AddV2(offset, _snzu_globs.selectedBox->parent->start);
    snzu_boxSetStartKeepSizeRecursePtr(_snzu_globs.selectedBox, final);
}

// margin in pixels
void snzu_boxSetSizeMarginFromParent(float m) {
    _snzu_globs.selectedBox->start = HMM_Add(_snzu_globs.selectedBox->parent->start, HMM_V2(m, m));
    _snzu_globs.selectedBox->end = HMM_Sub(_snzu_globs.selectedBox->parent->end, HMM_V2(m, m));
}

void snzu_boxSetSizeMarginFromParentAx(float px, snzu_Axis axis) {
    _snzu_globs.selectedBox->start.Elements[axis] = _snzu_globs.selectedBox->parent->start.Elements[axis] + px;
    _snzu_globs.selectedBox->end.Elements[axis] = _snzu_globs.selectedBox->parent->end.Elements[axis] - px;
}

void snzu_boxFillParent() {
    _snzu_globs.selectedBox->start = _snzu_globs.selectedBox->parent->start;
    _snzu_globs.selectedBox->end = _snzu_globs.selectedBox->parent->end;
}

void snzu_boxSizePctParent(float pct, snzu_Axis ax) {
    HMM_Vec2 newSize = snzu_boxGetSize(_snzu_globs.selectedBox->parent);
    snzu_boxSetSizeFromStartAx(ax, newSize.Elements[ax] * pct);
}

void snzu_boxSizeFromEndPctParent(float pct, snzu_Axis ax) {
    HMM_Vec2 newSize = snzu_boxGetSize(_snzu_globs.selectedBox->parent);
    snzu_boxSetSizeFromEndAx(ax, newSize.Elements[ax] * pct);
}

// TODO: unit test this
// maintains size but moves the box to be centered with other along ax
void snzu_boxCenter(_snzu_Box* other, snzu_Axis ax) {
    float boxCenter = (_snzu_globs.selectedBox->start.Elements[ax] + _snzu_globs.selectedBox->end.Elements[ax]) / 2.0f;
    float otherCenter = (other->start.Elements[ax] + other->end.Elements[ax]) / 2.0f;
    float diff = otherCenter - boxCenter;
    _snzu_globs.selectedBox->start.Elements[ax] += diff;
    _snzu_globs.selectedBox->end.Elements[ax] += diff;
}

// TODO: unit test this
// aligns box to be immediately next to and outside of other along ax, (align left is to the left of other)
void snzu_boxAlignOuter(_snzu_Box* other, snzu_Axis ax, snzu_Align align) {
    HMM_Vec2 boxSize = snzu_boxGetSize(_snzu_globs.selectedBox);

    float sidePos = 0;
    if (align == SNZU_ALIGN_MIN) {
        sidePos = other->start.Elements[ax];
        _snzu_globs.selectedBox->end.Elements[ax] = sidePos;
        snzu_boxSetSizeFromEnd(boxSize);
    } else if (SNZU_ALIGN_MAX) {
        sidePos = other->end.Elements[ax];
        _snzu_globs.selectedBox->start.Elements[ax] = sidePos;
        snzu_boxSetSizeFromStart(boxSize);
    } else {
        SNZ_ASSERTF(false, "invalid align value: %d", align);
    }
}

// maintains size, only affects coords on the targeted axis
void snzu_boxAlignInParent(snzu_Axis ax, snzu_Align align) {
    float initialSize = snzu_boxGetSize(_snzu_globs.selectedBox).Elements[ax];
    if (align == SNZU_ALIGN_MIN) {
        _snzu_globs.selectedBox->start.Elements[ax] = _snzu_globs.selectedBox->parent->start.Elements[ax];
    } else if (align == SNZU_ALIGN_MAX) {
        _snzu_globs.selectedBox->start.Elements[ax] = _snzu_globs.selectedBox->parent->end.Elements[ax] - initialSize;
    } else if (align == SNZU_ALIGN_CENTER) {
        _snzu_Box* par = _snzu_globs.selectedBox->parent;
        float midpt = (par->start.Elements[ax] + par->end.Elements[ax]) / 2.0f;
        _snzu_globs.selectedBox->start.Elements[ax] = midpt - (initialSize / 2);
    } else {
        SNZ_ASSERTF(false, "invalid align value: %d", align);
    }
    snzu_boxSetSizeFromStartAx(ax, initialSize);
}

// stacks this box after it's previous sibling (if none, the parents origin), used to make rows of things easy
// only inserts a gap after a sibling, not if it is the first element
// maintains size and relative positioning of all inner boxes
void snzu_boxSetPosAfterRecurse(float gap, snzu_Axis ax) {
    HMM_Vec2 newPos = _snzu_globs.selectedBox->parent->start;

    if (_snzu_globs.selectedBox->prevSibling != NULL) {
        newPos.Elements[ax] = _snzu_globs.selectedBox->prevSibling->end.Elements[ax] + gap;
    } else {
        newPos.Elements[ax] = _snzu_globs.selectedBox->parent->start.Elements[ax];
    }
    snzu_boxSetStartKeepSizeRecurse(newPos);
}

// recursively moves every box within the currently selected box to be ordered in a row, with the last one aligned to the bottom/end
void snzu_boxOrderChildrenInRowRecurseAlignEnd(float gap, snzu_Axis ax) {
    for (_snzu_Box* child = _snzu_globs.selectedBox->lastChild; child; child = child->prevSibling) {
        float newEnd = 0;
        if (child->nextSibling) {
            newEnd = child->nextSibling->start.Elements[ax] - gap;
        } else {
            newEnd = _snzu_globs.selectedBox->end.Elements[ax];
        }

        HMM_Vec2 start = _snzu_globs.selectedBox->start;
        start.Elements[ax] = newEnd - (child->end.Elements[ax] - child->start.Elements[ax]);
        snzu_boxSetStartKeepSizeRecursePtr(child, start);
    }
}

// recursively moves every box within the currently selected box to be ordered in a row
void snzu_boxOrderChildrenInRowRecurse(float gap, snzu_Axis ax) {
    _snzu_Box* initiallySelected = _snzu_globs.selectedBox;
    for (_snzu_Box* child = _snzu_globs.selectedBox->firstChild; child; child = child->nextSibling) {
        snzu_boxSelect(child);
        snzu_boxSetPosAfterRecurse(gap, ax);
    }
    _snzu_globs.selectedBox = initiallySelected;
}

// recursively moves every box within the currently selected boxes parent to be ordered in a row
void snzu_boxOrderSiblingsInRowRecurse(float gap, snzu_Axis ax) {
    _snzu_Box* initiallySelected = _snzu_globs.selectedBox;
    snzu_boxSelect(_snzu_globs.selectedBox->parent);
    snzu_boxOrderChildrenInRowRecurse(gap, ax);
    snzu_boxSelect(initiallySelected);
}

// changes only end
void snzu_boxSetSizeFitText() {
    const snzr_Font* font = _snzu_globs.selectedBox->font;
    HMM_Vec2 size = snzr_strSize(font, _snzu_globs.selectedBox->displayStr, _snzu_globs.selectedBox->displayStrLen);
    size = HMM_AddV2(size, HMM_V2(SNZU_TEXT_PADDING * 2, SNZU_TEXT_PADDING * 2));
    _snzu_globs.selectedBox->end = HMM_AddV2(_snzu_globs.selectedBox->start, size);
}

float snzu_boxGetMaxChildSizeAx(snzu_Axis ax) {
    float maxSize = 0;
    for (_snzu_Box* child = _snzu_globs.selectedBox->firstChild; child; child = child->nextSibling) {
        float size = child->end.Elements[ax] - child->start.Elements[ax];
        if (size > maxSize) {
            maxSize = size;
        }
    }
    return maxSize;
}

// calculates size based on extent of the children relative to the parent at call time
float snzu_boxGetSizeToFitChildrenAx(snzu_Axis ax) {
    float max = 0;
    float parentStart = _snzu_globs.selectedBox->start.Elements[ax];
    for (_snzu_Box* child = _snzu_globs.selectedBox->firstChild; child; child = child->nextSibling) {
        float dist = child->start.Elements[ax] - parentStart;
        if (dist > max) {
            max = dist;
        }
        dist = child->end.Elements[ax] - parentStart;
        if (dist > max) {
            max = dist;
        }
    }
    return max;
}

// see snzu_boxGetSizeToFitChildrenAx, sets size from start
void snzu_boxSetSizeFitChildren() {
    float x = snzu_boxGetSizeToFitChildrenAx(SNZU_AX_X);
    float y = snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y);
    snzu_boxSetSizeFromStart(HMM_V2(x, y));
}

void snzu_easeExpUnbounded(float* in, float target, float pctPerSec) {
    float diff = target - *in;
    diff *= pctPerSec * _snzu_globs.timeSinceLastFrame;
    *in += diff;
}

// eases a float closer to target (which should be between 0 and 1) (in will be clamped to this range, should not be null)
void snzu_easeExp(float* in, float target, float pctPerSec) {
    float diff = target - *in;
    diff *= pctPerSec * _snzu_globs.timeSinceLastFrame;
    *in += diff;
    if (*in > 1) {
        *in = 1;
    } else if (*in < 0) {
        *in = 0;
    }
}

void snzu_boxHighlightByAnim(float* anim, HMM_Vec4 baseColor, float diff) {
    HMM_Vec4 highlightCol = HMM_AddV4(baseColor, HMM_V4(diff, diff, diff, 0));
    snzu_boxSetColor(HMM_LerpV4(baseColor, *anim, highlightCol));
}

// UI ==========================================================================
// UI ==========================================================================
// UI ==========================================================================

// MAIN LOOP HANDLING + SDL ====================================================
// MAIN LOOP HANDLING + SDL ====================================================
// MAIN LOOP HANDLING + SDL ====================================================

typedef void (*snz_InitFunc)(snz_Arena* scratch, SDL_Window* window);
typedef void (*snz_FrameFunc)(float dt, snz_Arena* frameArena);

void snz_main(const char* windowTitle, snz_InitFunc initFunc, snz_FrameFunc frameFunc) {
    SDL_Window* window = NULL;
    {
        // initialize SDL and open window
        SNZ_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0, "sdl initialization failed.");
        SNZ_ASSERT(SDL_GL_LoadLibrary(NULL) == 0, "sdl loading opengl failed.");
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8);
        uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
        window = SDL_CreateWindow(windowTitle, 100, 100, 700, 500, windowFlags);
        SNZ_ASSERT(window, "sdl window creation failed.");
        SDL_GLContext context = SDL_GL_CreateContext(window);
        SNZ_ASSERT(context, "sdl gl context creation failed");
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        SNZ_ASSERT(renderer, "sdl renderer creation failed");
    }

    snz_Arena frameArena = snz_arenaInit(100000000, "snz frame arena");

    _snzr_init(&frameArena);
    snz_arenaClear(&frameArena);
    initFunc(&frameArena, window);
    snz_arenaClear(&frameArena);

    bool quit = false;
    float prevTime = 0.0;
    while (!quit) {
        float time = (float)SDL_GetTicks64() / 1000;
        float dt = time - prevTime;
        prevTime = time;

        int screenW, screenH;
        SDL_GL_GetDrawableSize(window, &screenW, &screenH);
        _snzr_globs.screenSize = HMM_V2(screenW, screenH);

        int mouseX, mouseY;
        uint32_t mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);

        snzu_Input uiInputs = (snzu_Input){
            .mousePos = HMM_V2(mouseX, mouseY),
            .mouseStates[SNZU_MB_LEFT] = (SDL_BUTTON(SDL_BUTTON_LEFT) & mouseButtons),
            .mouseStates[SNZU_MB_RIGHT] = (SDL_BUTTON(SDL_BUTTON_RIGHT) & mouseButtons),
            .mouseStates[SNZU_MB_MIDDLE] = (SDL_BUTTON(SDL_BUTTON_MIDDLE) & mouseButtons),
            .keyMods = SDL_GetModState(),
            // chars entered and keycode/action taken care of via event polling loop
        };

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_MOUSEWHEEL) {
                uiInputs.mouseScrollY = e.wheel.preciseY;
            } else if (e.type == SDL_KEYDOWN) {
                uiInputs.keyAction = SNZU_ACT_DOWN;
                uiInputs.keyCode = e.key.keysym.sym;
            } else if (e.type == SDL_KEYUP) {
                uiInputs.keyAction = SNZU_ACT_UP;
                uiInputs.keyCode = e.key.keysym.sym;
            } else if (e.type == SDL_TEXTINPUT) {
                _STATIC_ASSERT(_SNZU_TEXT_INPUT_CHAR_MAX < SDL_TEXTINPUTEVENT_TEXT_SIZE);
                for (uint64_t i = 0; i < _SNZU_TEXT_INPUT_CHAR_MAX; i++) {
                    uiInputs.charsEntered[i] = e.text.text[i];
                }
            }
        }  // end event polling

        _snzu_beginFrame(&frameArena, HMM_V2(screenW, screenH), dt);

        snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        snzr_callGLFnOrError(glViewport(0, 0, screenW, screenH));
        snzr_callGLFnOrError(glClearColor(1, 1, 1, 1));
        snzr_callGLFnOrError(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        frameFunc(dt, &frameArena);

        snzr_callGLFnOrError(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        snzr_callGLFnOrError(glViewport(0, 0, screenW, screenH));
        _snzu_drawFrameAndGenInterations(uiInputs, HMM_V2(screenW, screenH));

        snz_arenaClear(&frameArena);
        SDL_GL_SwapWindow(window);
    }  // end main loop

    snz_arenaDeinit(&frameArena);

    // FIXME: gc gpu resources, all allocated arenas, etc.
}

// UI COMPONENTS ===============================================================
// UI COMPONENTS ===============================================================
// UI COMPONENTS ===============================================================

typedef struct {
    char* chars;
    int64_t charCount;
    int64_t maxCharCount;

    int64_t cursorPos;
    int64_t selectionStart;

    bool wasFocused;
    bool firstClickForFocus;

    const snzr_Font* font;
} snzuc_TextArea;

static void _snzuc_textAreaAssertValid(snzuc_TextArea* text) {
    SNZ_ASSERTF(text->maxCharCount, "textarea maxCharCount negative or zero. was: %lld", text->maxCharCount);
    SNZ_ASSERTF(text->charCount >= 0, "textarea charCount out of bounds. was: %lld", text->charCount);
    SNZ_ASSERTF(text->charCount <= text->maxCharCount, "textarea charCount out of bounds. was: %lld", text->charCount);
    SNZ_ASSERTF(text->cursorPos >= 0, "textarea cursor out of bounds. was: %lld", text->cursorPos);
    SNZ_ASSERTF(text->cursorPos <= text->charCount, "textarea cursor out of bounds. was: %lld", text->cursorPos);
    SNZ_ASSERTF(text->selectionStart >= -1, "textarea selection start out of bounds. was: %lld", text->selectionStart);
    SNZ_ASSERTF(text->selectionStart <= text->charCount, "textarea selection start out of bounds. was: %lld", text->selectionStart);
    SNZ_ASSERT(text->font != NULL, "text area font was NULL");
}

static void _snzuc_textAreaNormalizeCursor(snzuc_TextArea* text) {
    if (text->cursorPos < 0) {
        text->cursorPos = 0;
    } else if (text->cursorPos > text->charCount) {
        text->cursorPos = text->charCount;
    }
}

// returns whether it was a successful insert (too long is a failure)
bool snzuc_textAreaInsert(snzuc_TextArea* text, char* insertChars, int64_t insertLen, int64_t insertPos) {
    _snzuc_textAreaAssertValid(text);

    if (text->charCount + insertLen > text->maxCharCount) {
        return false;
    }

    assert(insertPos >= 0);
    assert(insertPos <= text->charCount);

    memmove(&text->chars[insertPos + insertLen], &text->chars[insertPos], text->maxCharCount - insertPos - insertLen);
    for (int64_t i = 0; i < insertLen; i++) {
        text->chars[insertPos + i] = insertChars[i];
    }
    text->charCount += insertLen;
    return true;
}

bool snzuc_textAreaRemove(snzuc_TextArea* text, int64_t removePos, int64_t removeLen) {
    _snzuc_textAreaAssertValid(text);

    if (text->charCount - removeLen < 0) {
        return false;
    } else if (removePos > text->charCount - removeLen) {
        return false;
    } else if (removePos < 0) {
        return false;  // FIXME: this case should really clamp removepos to be valid and sub it from len
    }

    memmove(&text->chars[removePos], &text->chars[removePos + removeLen], text->maxCharCount - removePos - removeLen);
    text->charCount -= removeLen;
    return true;
}

// doesn't account for newlines, end result is clamped to be a valid index within str
static int64_t _snzuc_textAreaIndexFromCursorPos(snzuc_TextArea* text, float cursorRelativeToStart) {
    // TODO: offset click zones to make right char boxes extend a little left
    _snzuc_textAreaAssertValid(text);

    for (int i = 0; i < text->charCount; i++) {
        // TODO: this could be incrementally calculated but i don't care
        float charX = snzr_strSize(text->font, text->chars, i + 1).X;
        if (cursorRelativeToStart < charX) {
            return i;
        }
    }
    return text->charCount;
}

static void _snzuc_textAreaClearSelection(snzuc_TextArea* text) {
    bool startIsSmaller = text->selectionStart < text->cursorPos;
    int64_t lowerBound = (startIsSmaller ? text->selectionStart : text->cursorPos);
    int64_t upperBound = (startIsSmaller ? text->cursorPos : text->selectionStart);
    snzuc_textAreaRemove(text, lowerBound, upperBound - lowerBound);
    text->cursorPos = lowerBound;
    text->selectionStart = -1;
    _snzuc_textAreaNormalizeCursor(text);
}

// dir = true is to the right, false to the left // returns index of the beginning of the next or previous word
static int64_t _snzuc_textAreaNextWordFromCursor(snzuc_TextArea* text, bool dir) {
    _snzuc_textAreaAssertValid(text);

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

// FIXME: max char count should not change at any point, as it is only used to allocate on the first frame.
// FIXME: bettery recovery than just not applying when it changes
void snzuc_textArea(_snzu_Box* container, int64_t maxCharCount, const snzr_Font* font) {
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
    [ ] return chars
    */

    snzu_boxSelect(container);  // TODO: it's unclear how this affects boxnew, fix please
    snzu_boxClipChildren();

    char* const chars = SNZU_USE_ARRAY(char, maxCharCount, "chars");
    snzuc_TextArea* const text = SNZU_USE_MEM(snzuc_TextArea, "struct");
    if (snzu_useMemIsPrevNew()) {
        text->chars = chars;
        text->selectionStart = -1;
        text->maxCharCount = maxCharCount;
    }
    text->font = font;
    _snzuc_textAreaAssertValid(text);

    snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS);

    if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DOWN) {
        text->firstClickForFocus = false;
        if (!text->wasFocused) {
            if (text->charCount != 0) {
                text->selectionStart = 0;
            }
            text->cursorPos = text->charCount;
            text->firstClickForFocus = true;
        } else {
            float mouseX = inter->mousePosLocal.X - SNZU_TEXT_PADDING;
            text->selectionStart = _snzuc_textAreaIndexFromCursorPos(text, mouseX);
            text->cursorPos = text->selectionStart;
        }
        snzu_boxSetFocused();
    }

    if (!text->firstClickForFocus && inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_DRAG) {
        float mouseX = inter->mousePosLocal.X - SNZU_TEXT_PADDING;
        text->cursorPos = _snzuc_textAreaIndexFromCursorPos(text, mouseX);
    }

    if (!text->firstClickForFocus && inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP) {
        if (text->selectionStart == text->cursorPos) {
            text->selectionStart = -1;
        }
    }

    if (inter->keyAction == SNZU_ACT_DOWN) {
        if (inter->keyCode == SDLK_ESCAPE || inter->keyCode == SDLK_RETURN) {
            if (snzu_boxFocused()) {
                snzu_clearFocus();
                text->selectionStart = -1;
            }
        }
    }

    bool focused = snzu_boxFocused();
    text->wasFocused = focused;

    float* focusedAnim = SNZU_USE_MEM(float, "focusedAnim");
    snzu_easeExp(focusedAnim, focused, 20);
    snzu_boxHighlightByAnim(focusedAnim, HMM_V4(0.2, 0.2, 0.2, 1), 0.1);
    snzu_boxSetCornerRadius(7 + 5 * *focusedAnim);
    snzu_boxSetBorder(*focusedAnim * 3, HMM_V4(0, 0, 0, 0.5));

    // only process keystrokes when focused
    if (focused) {
        bool selecting = text->selectionStart != -1 && text->selectionStart != text->cursorPos;
        if (inter->keyChars[0] != '\0') {
            if (selecting) {
                _snzuc_textAreaClearSelection(text);
            }

            // TODO: do we need a more sophisticated check?
            // TODO: support >1 char/unicode shit
            if (snzuc_textAreaInsert(text, &(inter->keyChars[0]), 1, text->cursorPos)) {
                text->cursorPos++;
            }
        } else if (inter->keyAction == SNZU_ACT_DOWN) {
            if (inter->keyCode == SDLK_BACKSPACE) {
                if (selecting) {
                    _snzuc_textAreaClearSelection(text);
                } else {
                    bool removed = snzuc_textAreaRemove(text, text->cursorPos - 1, 1);
                    if (removed) {
                        text->cursorPos--;
                    }
                }
            } else if (inter->keyCode == SDLK_DELETE) {
                if (selecting) {
                    _snzuc_textAreaClearSelection(text);
                } else {
                    snzuc_textAreaRemove(text, text->cursorPos, 1);
                }
            } else if (inter->keyCode == SDLK_LEFT || inter->keyCode == SDLK_RIGHT) {
                bool dir = (inter->keyCode == SDLK_RIGHT);  // true when right, false when left
                int64_t initialCursorPos = text->cursorPos;

                bool selectionHandled = false;
                if (inter->keyMods & KMOD_CTRL) {
                    text->cursorPos = _snzuc_textAreaNextWordFromCursor(text, dir);
                } else if (inter->keyMods & KMOD_SHIFT) {
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
                if (!selectionHandled && (inter->keyMods & KMOD_SHIFT)) {
                    if (text->selectionStart == -1) {
                        text->selectionStart = initialCursorPos;
                    }
                }
            }
        }  // end button press checks

        _snzuc_textAreaNormalizeCursor(text);
        _snzuc_textAreaAssertValid(text);
    }  // end focus check to process keys

    snzu_boxScope() {
        if (focused) {
            if (text->selectionStart != -1 && text->selectionStart != text->cursorPos) {
                snzu_boxNew("selectionBox");
                snzu_boxSetSizeMarginFromParent(SNZU_TEXT_PADDING);
                float startOffset = snzr_strSize(text->font, text->chars, text->selectionStart).X;
                float endOffset = snzr_strSize(text->font, text->chars, text->cursorPos).X;
                if (endOffset < startOffset) {
                    float temp = startOffset;
                    startOffset = endOffset;
                    endOffset = temp;
                }
                snzu_boxSetStartAx(container->start.X + SNZU_TEXT_PADDING + startOffset, SNZU_AX_X);
                snzu_boxSetEndAx(container->start.X + SNZU_TEXT_PADDING + endOffset, SNZU_AX_X);
                snzu_boxSetColor(HMM_V4(47 / 255.0, 145 / 255.0, 237 / 255.0, 0.6));
            }
        }

        // FIXME: clipping for text and innards
        snzu_boxNew("text");
        snzu_boxFillParent();
        snzu_boxSetDisplayStrLen(text->font, HMM_V4(1, 1, 1, 1), text->chars, text->charCount);
        snzu_boxSetSizeFitText();  // aligns text left

        if (focused) {
            snzu_boxNew("cursor");
            float cursorStartX = snzr_strSize(text->font, text->chars, text->cursorPos).X + SNZU_TEXT_PADDING;
            snzu_boxSetSizeMarginFromParent(8);  // TODO: make font based + fix alignment
            snzu_boxSetStartAx(container->start.X + cursorStartX, SNZU_AX_X);
            snzu_boxSetSizeFromStartAx(SNZU_AX_X, 1);
            snzu_boxSetColor(HMM_V4(1, 1, 1, 1));
        }
    }  // end inners

    _snzuc_textAreaAssertValid(text);
}  // end text area

// outbox may be null - title used for the boxes id and display name
bool snzuc_button(const snzr_Font* font, const char* title) {
    snzu_boxNew(title);
    snzu_boxSetDisplayStr(font, HMM_V4(1, 1, 1, 1), title);
    snzu_boxSetSizeFitText();
    snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "interaction");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_MOUSE_BUTTONS | SNZU_IF_HOVER);

    float* anim = SNZU_USE_MEM(float, "hover anim");
    snzu_easeExp(anim, inter->hovered, 20);
    snzu_boxHighlightByAnim(anim, HMM_V4(0.2, 0.2, 0.2, 1), 0.3);
    snzu_boxSetCornerRadius(*anim * 5 + 7);

    if (inter->mouseActions[SNZU_MB_LEFT] == SNZU_ACT_UP) {
        return true;
    }
    return false;
}

// FIXME: margin prop for inners
void snzuc_scrollArea() {
    snzu_boxClipChildren();

    snzu_Interaction* const inter = SNZU_USE_MEM(snzu_Interaction, "inter");
    snzu_boxSetInteractionOutput(inter, SNZU_IF_MOUSE_SCROLL | SNZU_IF_HOVER | SNZU_IF_MOUSE_BUTTONS);

    float* const scrollPosPx = SNZU_USE_MEM(float, "scrollPosPx");
    *scrollPosPx -= inter->mouseScrollY * 20;

    _snzu_Box* container = snzu_getSelectedBox();
    float containerHeight = snzu_boxGetSize(container).Y;
    float innerHeight = snzu_boxGetSizeToFitChildrenAx(SNZU_AX_Y);

    snzu_boxScope() {  // enter container
        snzu_boxNew("scrollBarContainer");
        snzu_boxFillParent();
        snzu_boxSetSizeFromEndAx(SNZU_AX_X, 20);
        snzu_boxScope() {
            snzu_boxNew("scrollHandle");
            snzu_boxFillParent();

            snzu_Interaction* const handleInter = SNZU_USE_MEM(snzu_Interaction, "handleInter");
            snzu_boxSetInteractionOutput(handleInter, SNZU_IF_MOUSE_BUTTONS | SNZU_IF_HOVER);
            if (handleInter->dragged) {
                float nPos = handleInter->mousePosGlobal.Y - container->start.Y;
                // FIXME: this breaks with nested scroll areas because the container moves after this every frame, so the dragging looks wierd
                nPos -= handleInter->dragBeginningLocal.Y;
                nPos = nPos / containerHeight * innerHeight;
                *scrollPosPx = nPos;
            }

            *scrollPosPx = fminf(innerHeight - containerHeight, *scrollPosPx);
            if (*scrollPosPx < 0) {
                *scrollPosPx = 0;
            }

            float scrollBarStart = (*scrollPosPx) / innerHeight * containerHeight;
            float scrollBarSize = containerHeight / innerHeight * containerHeight;
            if (scrollBarSize > 0 && scrollBarSize < containerHeight) {
                snzu_boxSetStartFromParentAx(scrollBarStart, SNZU_AX_Y);
                snzu_boxSetSizeFromStartAx(SNZU_AX_Y, scrollBarSize);
                snzu_boxSetColor(HMM_V4(0.8, 0.8, 0.8, 0.4));
            }
        }
    }  // exit container

    // FIXME: this is bad going into the guts of the lib like this
    for (_snzu_Box* child = container->firstChild; child; child = child->nextSibling) {
        if (child == container->lastChild) {  // skip the scroll bar casuse that shit should not be scrollin
            continue;
        }
        snzu_boxSetStartKeepSizeRecursePtr(child, HMM_AddV2(child->start, HMM_V2(0, -(*scrollPosPx))));
    }

    snzu_boxSelect(container);
}

// UI COMPONENTS ===============================================================
// UI COMPONENTS ===============================================================
// UI COMPONENTS ===============================================================