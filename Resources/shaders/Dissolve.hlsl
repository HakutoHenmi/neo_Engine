// Resources/shaders/Dissolve.hlsl
// ポストエフェクト: ディゾルブ効果（ノイズによる溶解）
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
    float3 col = sceneColor;
    float intensity = saturate(gSan);

    // ノイズを生成（FBMを使用）
    float n = FBM(i.uv * 10.0 + gTime * 0.1);
    
    // gTimeを元にしたサイン波などで、ディゾルブのしきい値を0〜1に変化させる
    // 0 = 全て表示, 1 = 全て消滅
    float dissolveAmount = intensity * intensity * (3.0 - 2.0 * intensity);
    float slowWave = sin(gTime * 0.65) * 0.5 + 0.5;
    float threshold = dissolveAmount * lerp(0.45, 0.85, slowWave);

    // エッジ部分（溶解しかけの部分）を赤熱っぽくする
    float edgeWidth = 0.08;
    if (n < threshold)
    {
        // 完全に溶解
        col = float3(0.0, 0.0, 0.0);
    }
    else if (n < threshold + edgeWidth)
    {
        // エッジ部分を赤やオレンジに光らせる
        float t = (n - threshold) / edgeWidth;
        col = lerp(float3(1.0, 0.2, 0.0), col, t);
    }

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette * intensity);
    col = lerp(sceneColor, col, intensity);

    return float4(Saturate3(col), 1.0);
}
