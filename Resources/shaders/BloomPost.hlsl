// Resources/shaders/BloomPost.hlsl
// ポストエフェクト: 疑似Bloom（高輝度抽出＋ブラー加算をワンパスで行う）
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

// 輝度閾値
static const float BLOOM_THRESHOLD = 0.8;
static const float BLOOM_INTENSITY = 1.2;

float4 main(PSIn i) : SV_TARGET
{
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0);
    
    // サンプリング範囲 (gDistortion でブラー半径を可変にする)
    float radius = max(1.0, 1.0 + gDistortion * 5.0);
    
    float3 bloomColor = float3(0, 0, 0);
    float weightSum = 0.0;

    // 簡易的な 5x5 ガウシアン的なサンプリングで高輝度部分だけを抽出
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 offset = float2(x, y) * texelSize * radius;
            float3 sampleCol = gScene.Sample(gSmp, i.uv + offset).rgb;
            
            // 輝度計算
            float lum = Luminance(sampleCol);
            
            // 閾値を超えた明るい部分だけを足し込む
            float contribution = max(0.0, lum - BLOOM_THRESHOLD);
            
            // 距離に応じた重み付け（簡易ガウス）
            float weight = 1.0 / (1.0 + x*x + y*y);
            
            bloomColor += sampleCol * contribution * weight;
            weightSum += weight;
        }
    }
    
    bloomColor = (bloomColor / weightSum) * BLOOM_INTENSITY;

    // 元の色
    float3 baseColor = gScene.Sample(gSmp, i.uv).rgb;
    
    // 加算合成（スクリーン合成）
    float3 finalColor = baseColor + bloomColor;
    
    // Vignette
    float2 d = i.uv - 0.5;
    finalColor *= saturate(1.0 - dot(d, d) * gVignette);

    return float4(Saturate3(finalColor), 1.0);
}
