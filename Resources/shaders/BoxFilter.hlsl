// Resources/shaders/BoxFilter.hlsl
// ポストエフェクト: ボックスフィルタ（単純平均ブラー）
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

float4 main(PSIn i) : SV_TARGET
{
    // ブラーの広がり
    float spread = 3.0 + gDistortion * 5.0; 
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0) * spread;

    float3 col = float3(0, 0, 0);

    // 3x3 のサンプリング
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            col += gScene.Sample(gSmp, i.uv + float2(x, y) * texelSize).rgb;
        }
    }
    col /= 9.0;

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette);

    return float4(Saturate3(col), 1.0);
}
