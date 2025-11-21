#version 410 core

in VS_OUT {
    vec3 fragPos;
    vec3 normal;
    vec2 uv;
    vec3 color;
    float light;
    float material;
    vec3 anim;
} fs_in;

out vec4 FragColor;

uniform sampler2D uAtlas;
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
uniform vec2 uAtlasSize;
uniform vec2 uAtlasInvSize;
uniform float uAtlasTileSize;

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
    vec3 color = vec3(0.0);
    float alpha = 1.0;

    if (fs_in.material > 1.5 && fs_in.material < 2.5) {
        vec2 uv = fs_in.uv * 4.0 + uCloudOffset * 0.25 + vec2(0.0, uCloudTime * 0.02);
        float d = fbm(uv) * 0.7 + fbm(uv * 1.7 + 23.1);
        d = smoothstep(0.35, 0.75, d);
        float pulse = 0.85 + 0.15 * sin(uCloudTime * 0.4);
        color = mix(vec3(0.58, 0.68, 0.82), vec3(1.0), d) * pulse;
        alpha = d * 0.65 * uCloudEnabled;
    } else if (fs_in.material >= 3.0) {
        color = fs_in.color;
    } else {
        vec4 texData = texture(uAtlas, atlasUV);
        if (texData.a < 0.5) {
            discard;
        }
        vec3 texColor = texData.rgb;
        color = texColor * fs_in.color;
        float diffuse = max(dot(normal, normalize(-uSunDir)), 0.0);
        vec3 lighting = uAmbient + uSunColor * diffuse;
        color *= lighting * fs_in.light;

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
    vec3 fogColor = mix(vec3(0.45, 0.55, 0.72), vec3(0.72, 0.82, 0.95), clamp(-uSunDir.y * 0.5 + 0.5, 0.0, 1.0));
    color = mix(color, fogColor, clamp(fogFactor, 0.0, 1.0));

    FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
