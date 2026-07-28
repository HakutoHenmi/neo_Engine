// Resources/shaders/ChromaticAberrationPost.hlsl
// ポストエフェクト: 色収差 (Chromatic Aberration) + レンズ歪み
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
    float2 uv : TEXCOORD0;
};

// レンズ歪み（樽型歪曲）
float2 LensDistortion(float2 uv, float strength)
{
    float2 centerOffset = uv - 0.5;
    float r2 = dot(centerOffset, centerOffset);
    // 距離の2乗に比例してUVをずらす
    float f = 1.0 + r2 * strength;
    return centerOffset * f + 0.5;
}

float4 main(PSIn i) : SV_TARGET
{
    // 中心からの距離
    float2 centerOffset = i.uv - 0.5;
    float dist = length(centerOffset);
    
    // パラメータによる強度制御 (gChromaShift を基準に、外側ほど強く)
    float baseShift = 0.005 + gChromaShift * 0.02;
    float shiftAmount = baseShift * dist * dist; // 画面端ほどズレる

    // レンズ歪みも適用（gDistortionパラメータを流用）
    float distortionStrength = gDistortion * 0.2;
    float2 uvR = LensDistortion(i.uv, distortionStrength - shiftAmount);
    float2 uvG = LensDistortion(i.uv, distortionStrength);
    float2 uvB = LensDistortion(i.uv, distortionStrength + shiftAmount);

    // RGBのチャンネルを別々にサンプリング
    float r = gScene.Sample(gSmp, uvR).r;
    float g = gScene.Sample(gSmp, uvG).g;
    float b = gScene.Sample(gSmp, uvB).b;
    float3 col = float3(r, g, b);

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette);

    return float4(Saturate3(col), 1.0);
}
