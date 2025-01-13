#version 330 core

uniform sampler2D uTexture;
out vec4 color;
in vec2 vTexCoord;

void main() {
    color = texture(uTexture, vTexCoord);
}