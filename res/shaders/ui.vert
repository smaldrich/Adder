#version 430

in int gl_VertexID;
out vec2 vUv;
out vec2 vRectCenter;
out vec2 vRectHalfSize;

uniform mat4 uVP;
uniform vec2 uDstStart;
uniform vec2 uDstEnd;
uniform vec2 uSrcStart;
uniform vec2 uSrcEnd;
uniform float uSnap;

vec2 cornerTable[4] = vec2[](
    vec2(0, 0),
    vec2(0, 1),
    vec2(1, 1),
    vec2(1, 0)
    );

void main()
{
    vec2 corner = cornerTable[gl_VertexID];
    corner *= uDstEnd - uDstStart;
    corner += uDstStart;

    vUv = cornerTable[gl_VertexID];
    vUv *= uSrcEnd - uSrcStart;
    vUv += uSrcStart;

    vRectCenter = (uDstEnd + uDstStart) / 2;
    vRectHalfSize = (uDstEnd - uDstStart) / 2;

    vec2 snapped = vec2(int(corner.x), int(corner.y));
    gl_Position = uVP * vec4(mix(corner, snapped, uSnap), 0, 1);
};

