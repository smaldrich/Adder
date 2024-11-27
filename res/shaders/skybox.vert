#version 330 core

layout (location = 0) in vec3 aPos;

out vec3 vTextureDir;

uniform mat4 uVP;

void main() {
    vTextureDir = aPos;
    gl_Position = uVP * vec4(aPos, 1.0);
}