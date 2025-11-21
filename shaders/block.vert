#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;
layout(location = 4) in float aLight;
layout(location = 5) in float aMaterial;
layout(location = 6) in vec3 aAnim;

out VS_OUT {
    vec3 fragPos;
    vec3 normal;
    vec2 uv;
    vec3 color;
    float light;
    float material;
    vec3 anim;
} vs_out;

uniform mat4 uViewProj;
uniform mat4 uModel = mat4(1.0);

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vs_out.fragPos = vec3(worldPos);
    vs_out.normal = mat3(uModel) * aNormal; // Simplified normal matrix for uniform scaling
    vs_out.uv = aUV;
    vs_out.color = aColor;
    vs_out.light = aLight;
    vs_out.material = aMaterial;
    vs_out.anim = aAnim;
    gl_Position = uViewProj * worldPos;
}
