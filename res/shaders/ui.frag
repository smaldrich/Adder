#version 430

layout(origin_upper_left) in vec4 gl_FragCoord;

out vec4 color;

in vec2 vUv;
in vec2 vRectCenter;
in vec2 vRectHalfSize;

uniform vec4 uColor;
uniform sampler2D uFontTexture;
uniform sampler2D uTexture;

uniform float uCornerRadius;
uniform float uBorderRadius;
uniform vec4 uBorderColor;

float roundedRectSDF(vec2 sample_pos, vec2 rect_center, vec2 rect_half_size, float r) {
    vec2 d2 = abs(rect_center - sample_pos) - rect_half_size + vec2(r, r);
    return min(max(d2.x, d2.y), 0.0) + length(max(d2, 0.0)) - r;
}

void main() {
    float dist = roundedRectSDF(gl_FragCoord.xy, vRectCenter, vRectHalfSize, uCornerRadius);
    if (dist > 1) {
        discard;
    } else if(dist > -uBorderRadius) {
        color = uBorderColor;
    } else {
        vec4 texColor = texture(uTexture, vUv);
        vec4 fontColor = vec4(1.0, 1.0, 1.0, texture(uFontTexture, vUv).r);
        color = uColor * fontColor * texColor;

        if (color.a <= 0.01) { discard; }
    }
};
