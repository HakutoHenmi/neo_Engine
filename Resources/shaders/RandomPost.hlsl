// Resources/shaders/RandomPost.hlsl
// Damage glitch post effect. The original scene remains visible.
#include "PostProcessCommon.hlsli"

Texture2D gScene : register(t0);
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
    float2 texcoord : TEXCOORD0;
};

float rand2dTo1d(float2 value)
{
    float2 smallValue = sin(value);
    float random = dot(smallValue, float2(12.9898, 78.233));
    random = frac(sin(random) * 143758.5453);
    return random;
}

float4 main(PSIn input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 sceneColor = gScene.Sample(gSmp, uv).rgb;

    float intensity = saturate(gSan) * saturate(gNoiseStrength);
    float lineSeed = rand2dTo1d(float2(floor(uv.y * 160.0), floor(gTime * 18.0)));
    float jitter = (lineSeed - 0.5) * 0.006 * intensity;
    float2 shiftedUv = saturate(uv + float2(jitter, 0.0));

    float3 shifted = gScene.Sample(gSmp, shiftedUv).rgb;
    float noise = rand2dTo1d(uv * float2(1280.0, 720.0) + floor(gTime * 30.0));
    float speckle = (noise - 0.5) * 0.18 * intensity;
    float scan = sin(uv.y * 720.0) * 0.5 + 0.5;

    float3 glitch = shifted + speckle;
    glitch += float3(0.12, -0.03, -0.04) * intensity * lineSeed;
    glitch *= lerp(1.0, 0.92 + scan * 0.08, intensity);

    float3 col = lerp(sceneColor, saturate(glitch), intensity);
    return float4(Saturate3(col), 1.0f);
}
