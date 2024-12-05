#pragma once

#include "GLAD/include/glad/glad.h"
#include "HMM/HandmadeMath.h"
#include "snooze.h"

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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertSize, (void*)(0));  // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vertSize, (void*)(sizeof(HMM_Vec3)));  // normals
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return out;
}

void ren3d_meshDeinit(ren3d_Mesh* mesh) {
    glDeleteVertexArrays(1, &mesh->vaId);
    glDeleteBuffers(1, &mesh->vertexBufferId);
    memset(mesh, 0, sizeof(*mesh));
}

static uint32_t _ren3d_shaderId;
static uint32_t _ren3d_skyboxShaderId;
static ren3d_Mesh _ren3d_skyboxMesh;

void ren3d_init(snz_Arena* scratch) {
    {
        const char* vertSrc = _ren3d_loadFileToStr("res/shaders/3d.vert", scratch);
        const char* fragSrc = _ren3d_loadFileToStr("res/shaders/3d.frag", scratch);
        _ren3d_shaderId = snzr_shaderInit(vertSrc, fragSrc, scratch);
    }

    {
        const char* vertSrc = _ren3d_loadFileToStr("res/shaders/skybox.vert", scratch);
        const char* fragSrc = _ren3d_loadFileToStr("res/shaders/skybox.frag", scratch);
        _ren3d_skyboxShaderId = snzr_shaderInit(vertSrc, fragSrc, scratch);
    }

    {
        HMM_Vec3 verts[] = {
            HMM_V3(-1.0f, 1.0f, -1.0f),
            HMM_V3(-1.0f, -1.0f, -1.0f),
            HMM_V3(1.0f, -1.0f, -1.0f),
            HMM_V3(1.0f, -1.0f, -1.0f),
            HMM_V3(1.0f, 1.0f, -1.0f),
            HMM_V3(-1.0f, 1.0f, -1.0f),

            HMM_V3(-1.0f, -1.0f, 1.0f),
            HMM_V3(-1.0f, -1.0f, -1.0f),
            HMM_V3(-1.0f, 1.0f, -1.0f),
            HMM_V3(-1.0f, 1.0f, -1.0f),
            HMM_V3(-1.0f, 1.0f, 1.0f),
            HMM_V3(-1.0f, -1.0f, 1.0f),

            HMM_V3(1.0f, -1.0f, -1.0f),
            HMM_V3(1.0f, -1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, -1.0f),
            HMM_V3(1.0f, -1.0f, -1.0f),

            HMM_V3(-1.0f, -1.0f, 1.0f),
            HMM_V3(-1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, -1.0f, 1.0f),
            HMM_V3(-1.0f, -1.0f, 1.0f),

            HMM_V3(-1.0f, 1.0f, -1.0f),
            HMM_V3(1.0f, 1.0f, -1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(1.0f, 1.0f, 1.0f),
            HMM_V3(-1.0f, 1.0f, 1.0f),
            HMM_V3(-1.0f, 1.0f, -1.0f),

            HMM_V3(-1.0f, -1.0f, -1.0f),
            HMM_V3(-1.0f, -1.0f, 1.0f),
            HMM_V3(1.0f, -1.0f, -1.0f),
            HMM_V3(1.0f, -1.0f, -1.0f),
            HMM_V3(-1.0f, -1.0f, 1.0f),
            HMM_V3(1.0f, -1.0f, 1.0f),
        };

        _ren3d_skyboxMesh = (ren3d_Mesh){
            .vaId = 0,
            .vertexBufferId = 0,
            .vertCount = sizeof(verts) / sizeof(*verts),
        };
        glGenVertexArrays(1, &_ren3d_skyboxMesh.vaId);
        glBindVertexArray(_ren3d_skyboxMesh.vaId);

        glGenBuffers(1, &_ren3d_skyboxMesh.vertexBufferId);
        glBindBuffer(GL_ARRAY_BUFFER, _ren3d_skyboxMesh.vertexBufferId);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HMM_Vec3), (void*)(0));  // position
        glEnableVertexAttribArray(0);

        glBindVertexArray(0);
    }
}

void ren3d_drawMesh(const ren3d_Mesh* mesh, HMM_Mat4 vp, HMM_Mat4 model, HMM_Vec4 color, HMM_Vec3 lightDir, float ambient) {
    snzr_callGLFnOrError(glUseProgram(_ren3d_shaderId));

    // // FIXME: gl safe uniform loc calls
    int loc = glGetUniformLocation(_ren3d_shaderId, "uVP");
    snzr_callGLFnOrError(glUniformMatrix4fv(loc, 1, false, (float*)&vp));

    loc = glGetUniformLocation(_ren3d_shaderId, "uModel");
    snzr_callGLFnOrError(glUniformMatrix4fv(loc, 1, false, (float*)&model));

    loc = glGetUniformLocation(_ren3d_shaderId, "uColor");
    snzr_callGLFnOrError(glUniform4f(loc, color.X, color.Y, color.Z, color.W));

    loc = glGetUniformLocation(_ren3d_shaderId, "uLightColor");
    snzr_callGLFnOrError(glUniform3f(loc, 1, 1, 1));

    loc = glGetUniformLocation(_ren3d_shaderId, "uLightDir");
    snzr_callGLFnOrError(glUniform3f(loc, lightDir.X, lightDir.Y, lightDir.Z));

    loc = glGetUniformLocation(_ren3d_shaderId, "uAmbient");
    snzr_callGLFnOrError(glUniform1f(loc, ambient));

    snzr_callGLFnOrError(glBindVertexArray(mesh->vaId));
    snzr_callGLFnOrError(glBindBuffer(GL_ARRAY_BUFFER, mesh->vertexBufferId));
    snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, mesh->vertCount));
}

// https://learnopengl.com/Advanced-OpenGL/Cubemaps
// where would I be without this website
// texture expected to be long/lat
void ren3d_drawSkybox(HMM_Mat4 vp, snzr_Texture skyTex) {
    snzr_callGLFnOrError(glUseProgram(_ren3d_skyboxShaderId));

    vp = HMM_Mul(vp, HMM_Scale(HMM_V3(10000, 10000, 10000)));

    int loc = glGetUniformLocation(_ren3d_skyboxShaderId, "uVP");
    snzr_callGLFnOrError(glUniformMatrix4fv(loc, 1, false, (float*)&vp));

    loc = glGetUniformLocation(_ren3d_skyboxShaderId, "uTexture");
    glUniform1i(loc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyTex.glId);

    snzr_callGLFnOrError(glBindVertexArray(_ren3d_skyboxMesh.vaId));
    snzr_callGLFnOrError(glBindBuffer(GL_ARRAY_BUFFER, _ren3d_skyboxMesh.vertexBufferId));
    snzr_callGLFnOrError(glDrawArrays(GL_TRIANGLES, 0, _ren3d_skyboxMesh.vertCount));
}