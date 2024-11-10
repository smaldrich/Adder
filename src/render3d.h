
#include "GLAD/include/glad/glad.h"
#include "HMM/HandmadeMath.h"
#include "snooze.h"

static uint32_t _ren3d_shaderId;

// FIXME: move to snz?
static char* _ren3d_loadFileToStr(const char* path, snz_Arena* scratch) {
    FILE* f = fopen(path, "rb");
    SNZ_ASSERTF(f != NULL, "Opening file failed. Path: %s", path);

    fseek(f, 0L, SEEK_END);
    uint64_t size = ftell(f);
    fseek(f, 0L, SEEK_SET);

    char* chars = SNZ_ARENA_PUSH_ARR(scratch, size, char);
    fread(chars, sizeof(char), size, f);
    fclose(f);
    return chars;
}

void ren3d_init(snz_Arena* scratch) {
    const char* vertSrc = _ren3d_loadFileToStr("res/shaders/3d.vert", scratch);
    const char* fragSrc = _ren3d_loadFileToStr("res/shaders/3d.frag", scratch);
    _ren3d_shaderId = snzr_shaderInit(vertSrc, fragSrc, scratch);
}

typedef struct {
    HMM_Vec3 pos;
    HMM_Vec3 normal;
} ren3d_Vert;

typedef struct {
    uint64_t vertCount;
    uint32_t vaId;
    uint32_t vertexBufferId;
} ren3d_Mesh;

// verts are not retained CPU side, and may be removed immediately following this call
ren3d_Mesh ren3d_meshInit(ren3d_Vert* verts, uint64_t vertCount) {
    ren3d_Mesh out = {
        .vaId = 0,
        .vertexBufferId = 0,
        .vertCount = vertCount,
    };
    // FIXME: safe GL calls here :)
    glGenVertexArrays(1, &out.vaId);
    glBindVertexArray(out.vaId);

    glGenBuffers(1, &out.vertexBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, out.vertexBufferId);
    uint64_t vertSize = sizeof(ren3d_Vert);
    glBufferData(GL_ARRAY_BUFFER, vertCount * vertSize, verts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertSize, (void*)(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vertSize, (void*)(sizeof(HMM_Vec3)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return out;
}

void ren3d_drawMesh(const ren3d_Mesh* mesh, HMM_Mat4 vp, HMM_Mat4 model, HMM_Vec3 lightDir) {
    snzr_callGLFnOrError(glUseProgram(_ren3d_shaderId));

    // // FIXME: gl safe uniform loc calls
    int loc = glGetUniformLocation(_ren3d_shaderId, "uVP");
    snzr_callGLFnOrError(glUniformMatrix4fv(loc, 1, false, (float*)&vp));

    loc = glGetUniformLocation(_ren3d_shaderId, "uModel");
    snzr_callGLFnOrError(glUniformMatrix4fv(loc, 1, false, (float*)&model));

    loc = glGetUniformLocation(_ren3d_shaderId, "uColor");
    snzr_callGLFnOrError(glUniform3f(loc, 0.8, 0.8, 0.8));

    loc = glGetUniformLocation(_ren3d_shaderId, "uLightColor");
    snzr_callGLFnOrError(glUniform3f(loc, 1, 1, 1));

    loc = glGetUniformLocation(_ren3d_shaderId, "uLightDir");
    snzr_callGLFnOrError(glUniform3f(loc, lightDir.X, lightDir.Y, lightDir.Z));

    snzr_callGLFnOrError(glBindVertexArray(mesh->vaId));
    snzr_callGLFnOrError(glBindBuffer(GL_ARRAY_BUFFER, mesh->vertexBufferId));
    snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, mesh->vertCount));
}