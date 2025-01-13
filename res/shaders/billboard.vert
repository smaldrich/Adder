#version 330 core

uniform mat4 uVP;
uniform vec3 uPos;
uniform vec2 uHalfSize;
uniform vec2 uResolution;
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
    vec4 pos = uVP * vec4(uPos, 1.0f);
    pos.xy += (cornerTable[gl_VertexID] * (uHalfSize / uResolution)) * pos.w;
    gl_Position = pos;
    vTexCoord = uvTable[gl_VertexID];
}