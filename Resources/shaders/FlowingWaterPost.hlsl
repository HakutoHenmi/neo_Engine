// Resources/shaders/FlowingWaterPost.hlsl
// Screen-space water-flow refraction and caustics for gameplay scenes.
#include "PostProcessCommon.hlsli"

Texture2D gScene : register(t0);
Texture2D gDepth : register(t3);
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0)
{
    float gTime;
    float gNoiseStrength;
    float gDistortion;
    float gChromaShift;
    float gVignette;
    float gScanline;
    float gSan;
    float pad0;
};

struct PSIn
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float FlowCaustic(float2 uv, float time)
{
    float a = sin((uv.x * 18.0 + uv.y * 7.0) - time * 1.8);
    float b = sin((uv.x * -9.0 + uv.y * 22.0) + time * 1.3);
    float c = sin((uv.x * 28.0 - uv.y * 16.0) + time * 0.9);
    float v = (a + b + c) * 0.333 + 0.5;
    return smoothstep(0.58, 0.92, v);
}

float4 main(PSIn i) : SV_TARGET
{
    float2 uv = i.uv;
    float3 sceneColor = gScene.Sample(gSmp, uv).rgb;

    float intensity = smoothstep(0.0, 1.0, saturate(gSan));
    float flowSpeed = 0.35 + gDistortion * 0.55;
    float2 flowDir = normalize(float2(0.82, 0.28));
    float2 flowUv = uv * float2(1.35, 0.85) + flowDir * (gTime * flowSpeed);

    float noiseA = FBM(flowUv * 5.5);
    float noiseB = FBM((flowUv.yx + float2(2.1, -1.7)) * 8.0 - gTime * 0.12);
    float2 wave = float2(noiseA - 0.5, noiseB - 0.5);
    wave += float2(
        sin(uv.y * 32.0 + gTime * 2.4),
        cos(uv.x * 24.0 - gTime * 1.9)
    ) * 0.18;

    float2 offset = wave * (0.004 + gDistortion * 0.010) * intensity;
    float2 refractUv = saturate(uv + offset);

    float3 refracted = gScene.Sample(gSmp, refractUv).rgb;
    float2 chroma = float2(gChromaShift * 0.35 * intensity, 0.0);
    refracted.r = gScene.Sample(gSmp, saturate(refractUv + chroma)).r;
    refracted.b = gScene.Sample(gSmp, saturate(refractUv - chroma)).b;

    float caustic = FlowCaustic(uv + wave * 0.02, gTime);
    float3 waterTint = float3(0.52, 0.84, 1.0);
    float3 col = lerp(refracted, refracted * waterTint + waterTint * 0.08, 0.18 * intensity);
    col += caustic * float3(0.08, 0.14, 0.18) * intensity;

    float depthC = gDepth.Sample(gSmp, uv).r;
    float depthR = gDepth.Sample(gSmp, saturate(uv + float2(1.0 / 1280.0, 0.0))).r;
    float depthU = gDepth.Sample(gSmp, saturate(uv + float2(0.0, 1.0 / 720.0))).r;
    float depthEdge = saturate((abs(depthC - depthR) + abs(depthC - depthU)) * 450.0);
    col += depthEdge * float3(0.03, 0.08, 0.10) * intensity;

    float2 d = uv - 0.5;
    col *= lerp(1.0, saturate(1.0 - dot(d, d) * gVignette), intensity);

    return float4(Saturate3(lerp(sceneColor, col, intensity)), 1.0);
}
