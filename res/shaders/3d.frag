#version 330 core

out vec4 color;

in vec4 gl_FragCoord;
in vec3 vNormal;

uniform vec4 uColor;

uniform vec3 uLightDir;
uniform vec3 uLightColor;

uniform float uAmbient;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);

    vec3 ambient = uLightColor * uAmbient;

    float diff = clamp(dot(-normal, lightDir), 0.0, 1.0);
    vec3 diffuse = uLightColor * 0.15 * diff;

    color = vec4(diffuse + ambient, 1.0) * uColor;
};
