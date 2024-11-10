#version 330 core

uniform mat4 uVP;
uniform mat4 uModel;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

out vec3 vNormal;

void main() {
    gl_Position = uVP * uModel * vec4(position, 1);
    vNormal = normal;
}