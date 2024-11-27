#version 330 core

in vec3 vTextureDir;
uniform samplerCube uTexture;
out vec4 color;

void main() {
    // color = vec4(vTextureDir, 1);
    color = texture(uTexture, vTextureDir);
}