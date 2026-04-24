// Resources/shaders/GaussianPost.hlsl
// ポストエフェクト: ガウシアンフィルタ（ガウスぼかし）
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

// 5x5 ガウシアンカーネル（σ≒1.0）
// 重み合計 = 256
static const float kernel[5][5] = {
    { 1,  4,  6,  4, 1 },
    { 4, 16, 24, 16, 4 },
    { 6, 24, 36, 24, 6 },
    { 4, 16, 24, 16, 4 },
    { 1,  4,  6,  4, 1 }
};
static const float kernelSum = 256.0;

float4 main(PSIn i) : SV_TARGET
{
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0);

    // gDistortion でぼかし強度を制御（テクセルの倍率）
    float scale = max(1.0, 1.0 + gDistortion * 2.0);

    float3 col = float3(0, 0, 0);

    // 5x5 ガウシアンカーネルを適用
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 offset = float2(x, y) * texelSize * scale;
            float weight = kernel[y + 2][x + 2];
            col += gScene.Sample(gSmp, i.uv + offset).rgb * weight;
        }
    }
    col /= kernelSum;

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette);

    return float4(Saturate3(col), 1.0);
}
