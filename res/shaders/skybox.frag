#version 330 core

in vec3 vTextureDir;
uniform sampler2D uTexture;
out vec4 color;

// https://www.reddit.com/r/Unity3D/comments/73wzcv/is_there_a_way_to_make_a_latlong_formatted/?rdt=37665
// world vector to longitude latitude coordinates. singularity at (x=0 && z=0) requires disabling of mips or computing mip level manually
vec2 vector2ll(vec3 v) {
    vec2 vo = vec2(0, 0);
    vo.x = atan(v.x, v.z) / 3.14159265;
    vo.y = -v.y;
    vo = vo * 0.5 + 0.5;
    return vo;
}

void main() {
    color = texture(uTexture, vector2ll(normalize(vTextureDir)));
}