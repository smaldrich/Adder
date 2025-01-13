#version 330 core

uniform mat4 uVP;
uniform vec3 uPos;
uniform vec2 uHalfSize;
out vec2 vTexCoord;

vec2 cornerTable[6] = vec2[](
    vec2(-1, -1),
    vec2(-1, 1),
    vec2(1, 1),
    vec2(1, 1),
    vec2(1, -1),
    vec2(-1, -1)
    );

vec2 uvTable[6] = vec2[](
    vec2(0, 0),
    vec2(0, 1),
    vec2(1, 1),
    vec2(1, 1),
    vec2(1, 0),
    vec2(0, 0)
    );

void main() {
    vec4 pos = vec4(uPos, 1.0f);
    pos = uVP * pos;
    pos.xy += cornerTable[gl_VertexID] * uHalfSize;
    gl_Position = pos;
    vTexCoord = uvTable[gl_VertexID];
}