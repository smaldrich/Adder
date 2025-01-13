#version 330 core

uniform sampler2D uTexture;
uniform vec4 uColor;
out vec4 color;
in vec2 vTexCoord;

void main() {
    color = uColor * texture(uTexture, vTexCoord);
}