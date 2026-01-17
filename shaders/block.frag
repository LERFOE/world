#version 410 core

in VS_OUT {
    vec3 fragPos;
    vec3 normal;
    vec2 uv;
    vec3 color;
    float light;
    float material;
    vec3 anim;
    vec4 fragPosLightSpace;
} fs_in;

out vec4 FragColor;

uniform sampler2D uAtlas;
uniform sampler2D uPigTex;
uniform sampler2D uCowTex;
uniform sampler2D uSheepTex;
uniform sampler2D uShadowMap;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uEyePos;
uniform vec3 uTargetBlock;
uniform float uTargetActive;
uniform float uBreakProgress;
uniform float uFogDensity;
uniform float uTime;
uniform vec2 uCloudOffset;
uniform float uCloudTime;
uniform float uCloudEnabled;
uniform float uShadowStrength;
uniform vec2 uAtlasSize;
uniform vec2 uAtlasInvSize;
uniform float uAtlasTileSize;
uniform int uAnimalKind; // 0=pig,1=cow,2=sheep

float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.45);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * noise(p);
        p *= 2.02;
        amp *= 0.55;
    }
    return sum;
}

const float kPi = 3.14159265;

float calcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) {
        return 0.0;
    }
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float ndotl = max(dot(normal, lightDir), 0.0);
    float bias = max(0.0006 * (1.0 - ndotl), 0.0008);

    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow * uShadowStrength;
}

vec3 applyLighting(vec3 albedo,
                   vec3 normal,
                   vec3 viewDir,
                   vec3 lightDir,
                   float ao,
                   vec3 F0,
                   float shininess,
                   float specStrength,
                   float shadow) {
    float ndotl = max(dot(normal, lightDir), 0.0);
    vec3 ambient = uAmbient * albedo * ao;
    vec3 diffuse = uSunColor * albedo * ndotl;

    vec3 specular = vec3(0.0);
    if (ndotl > 0.0) {
        vec3 halfDir = normalize(lightDir + viewDir);
        float ndoth = max(dot(normal, halfDir), 0.0);
        float specTerm = pow(ndoth, shininess);
        float norm = (shininess + 8.0) / (8.0 * kPi);
        float hdotv = max(dot(halfDir, viewDir), 0.0);
        vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - hdotv, 5.0);
        specular = uSunColor * fresnel * (specStrength * specTerm * norm);
    }

    vec3 direct = (diffuse + specular) * (1.0 - shadow);
    return ambient + direct;
}

void main() {
    float frameIndex = fs_in.anim.x;
    float frameCount = max(fs_in.anim.y, 1.0);
    if (frameCount > 1.0) {
        float speed = fs_in.anim.z > 0.0 ? fs_in.anim.z : 1.0;
        float animFrame = floor(uTime * speed);
        frameIndex = fs_in.anim.x + mod(animFrame, frameCount);
    }
    float cols = uAtlasSize.x / uAtlasTileSize;
    float row = floor(frameIndex / cols);
    float col = frameIndex - row * cols;
    vec2 base = vec2(col * uAtlasTileSize + 0.5, row * uAtlasTileSize + 0.5) * uAtlasInvSize;
    vec2 extent = vec2(uAtlasTileSize - 1.0) * uAtlasInvSize;
    vec2 atlasUV = base + fs_in.uv * extent;

    vec3 normal = normalize(fs_in.normal);
    vec3 viewDir = normalize(uEyePos - fs_in.fragPos);
    vec3 lightDir = normalize(uSunDir);
    float ao = clamp(fs_in.light, 0.0, 1.0);
    vec3 color = vec3(0.0);
    float alpha = 1.0;

    if (fs_in.material > 1.5 && fs_in.material < 2.5) {
        vec2 uv = fs_in.uv * 4.0 + uCloudOffset * 0.25 + vec2(0.0, uCloudTime * 0.02);
        float d = fbm(uv) * 0.7 + fbm(uv * 1.7 + 23.1);
        d = smoothstep(0.35, 0.75, d);
        float pulse = 0.85 + 0.15 * sin(uCloudTime * 0.4);
        color = mix(vec3(0.58, 0.68, 0.82), vec3(1.0), d) * pulse;
        alpha = d * 0.65 * uCloudEnabled;
    } else if (fs_in.material >= 2.5 && fs_in.material < 3.5) {
        color = fs_in.color;
    } else if (fs_in.material >= 3.5 && fs_in.material < 4.5) {
        // 动物：使用独立纹理（不走图集），fs_in.uv 直接作为 0..1 UV
        vec4 texData;
        if (uAnimalKind == 0) {
            texData = texture(uPigTex, fs_in.uv);
        } else if (uAnimalKind == 1) {
            texData = texture(uCowTex, fs_in.uv);
        } else {
            texData = texture(uSheepTex, fs_in.uv);
        }
        if (texData.a < 0.5) {
            discard;
        }
        vec3 albedo = texData.rgb * fs_in.color;
        vec3 F0 = vec3(0.04);
        float shininess = 32.0;
        float specStrength = 0.35;
        float shadow = calcShadow(fs_in.fragPosLightSpace, normal, lightDir);
        color = applyLighting(albedo, normal, viewDir, lightDir, ao, F0, shininess, specStrength, shadow);
    } else if (fs_in.material >= 4.5 && fs_in.material < 5.5) {
        // 太阳 billboard：使用顶点颜色和太阳颜色，不采样动物贴图
        vec3 sunDir = normalize(uSunDir);
        float facing = max(dot(normal, -sunDir), 0.0);
        float bloom = 0.6 + 0.4 * facing;
        color = fs_in.color * uSunColor * bloom;
        alpha = 1.0;
    } else {
        vec4 texData = texture(uAtlas, atlasUV);
        if (texData.a < 0.5) {
            discard;
        }
        vec3 albedo = texData.rgb * fs_in.color;
        vec3 F0 = vec3(0.04);
        float shininess = 32.0;
        float specStrength = 0.35;
        float shadowScale = 1.0;

        if (abs(fs_in.material - 1.0) < 0.1) {
            F0 = vec3(0.02);
            shininess = 64.0;
            specStrength = 0.55;
            shadowScale = 0.5;
        } else if (fs_in.material > 1.05 && fs_in.material < 1.2) {
            F0 = vec3(0.08);
            shininess = 96.0;
            specStrength = 0.45;
            shadowScale = 0.6;
        }

        float shadow = calcShadow(fs_in.fragPosLightSpace, normal, lightDir) * shadowScale;
        color = applyLighting(albedo, normal, viewDir, lightDir, ao, F0, shininess, specStrength, shadow);

        if (abs(fs_in.material - 1.0) < 0.1) {
            color = mix(color, vec3(0.1, 0.32, 0.65), 0.45);
            color += 0.08 * sin(uTime * 2.0 + fs_in.fragPos.x * 0.6 + fs_in.fragPos.z * 0.6);
            alpha = 0.7;
        } else if (fs_in.material > 1.05 && fs_in.material < 1.2) {
            color = mix(color, vec3(0.92, 0.97, 1.0), 0.6);
            alpha = 0.35;
        }
    }

    if (uTargetActive > 0.5 && fs_in.material < 2.5) {
        vec3 blockPos = floor(fs_in.fragPos + vec3(0.001));
        vec3 target = floor(uTargetBlock + vec3(0.001));
        float match = 1.0 - min(length(blockPos - target), 1.0);
        float glow = clamp(match * (0.35 + 0.55 * uBreakProgress), 0.0, 1.0);
        color = mix(color, vec3(1.0, 0.82, 0.45), glow);
    }

    float dist = length(uEyePos - fs_in.fragPos);
    float fogFactor = 1.0 - exp(-dist * uFogDensity);

    // 雾颜色随太阳高度渐变：夜晚偏深蓝，白天偏浅蓝
    float sunHeight = clamp(uSunDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 nightFog = vec3(0.03, 0.04, 0.08);
    vec3 dayFog   = vec3(0.72, 0.82, 0.95);
    vec3 fogColor = mix(nightFog, dayFog, sunHeight);

    color = mix(color, fogColor, clamp(fogFactor, 0.0, 1.0));

    FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
