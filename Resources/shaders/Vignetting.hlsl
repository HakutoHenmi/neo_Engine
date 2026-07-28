// Resources/shaders/Vignetting.hlsl
// ポストエフェクト: ビネット効果（四隅を暗くする）
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
    float3 col = gScene.Sample(gSmp, i.uv).rgb;

    // 中心からの距離に基づいて暗くする
    float2 d = i.uv - 0.5;
    // gVignette を無視して固定の強さでかけるか、あるいは gVignette が0の時も強めにかける
    float strength = 1.5 + gVignette; 
    float vignette = saturate(1.0 - dot(d, d) * strength);

    col *= vignette;

    return float4(Saturate3(col), 1.0);
}
