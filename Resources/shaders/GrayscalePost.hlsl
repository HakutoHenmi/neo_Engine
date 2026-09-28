// Resources/shaders/GrayscalePost.hlsl
// ポストエフェクト: グレースケール変換
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
    float3 sceneColor = gScene.Sample(gSmp, i.uv).rgb;
    // In this effect gChromaShift controls the perfect-dodge radial blur.
    // Keep the aim point sharp, and leave other grayscale effects unchanged at zero.
    float2 radial=i.uv-.5;
    float blur=saturate(gChromaShift)*smoothstep(.12,.55,length(radial));
    if(blur>0){
        float3 samples=0;
        [unroll]for(int sampleIndex=0;sampleIndex<8;++sampleIndex)
            samples+=gScene.Sample(gSmp,saturate(i.uv-radial*(sampleIndex/7.0)*.075*blur)).rgb;
        sceneColor=lerp(sceneColor,samples/8,blur*.85);
    }

    // ITU-R BT.709 輝度係数によるグレースケール変換
    float gray = Luminance(sceneColor);

    // gSan を変換強度として使用 (0=カラー, 1=完全グレー)
    float strength = saturate(gSan);
    float3 col = lerp(sceneColor, gray.xxx, strength);

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette * strength);

    return float4(Saturate3(col), 1.0);
}
