#version 330 core

out vec4 color;

in vec4 gl_FragCoord;
in vec3 vNormal;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
// uniform vec3 uViewDir;

/*
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
*/

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    // vec3 viewDir = normalize(uViewDir);

    vec3 ambient = uLightColor * 0.8;

    float diff = clamp(dot(-normal, lightDir), 0.0, 1.0);
    vec3 diffuse = uLightColor * 0.15 * diff;

    // vec3 reflectDir = reflect(lightDir, normal);
    // float spec = pow(max(dot(-viewDir, reflectDir), 0.0), 25);
    // vec3 specular = 0.1 * spec * uLightColor;
    // color = vec4(specular + diffuse + ambient, 1.0) * uColor;

    color = vec4((diffuse + ambient) * uColor, 1.0);
    // color = vec4(uColor, 1);
};
