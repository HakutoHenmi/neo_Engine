// Resources/shaders/DepthBasedOutline.hlsl
// ポストエフェクト: 深度バッファを用いたエッジ検出アウトライン
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

float4 main(PSIn i) : SV_TARGET
{
    float intensity = saturate(gSan);
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0);
    
    // 周囲4近傍の深度を取得
    float dU = gDepth.Sample(gSmp, i.uv + float2(0, -1) * texelSize).r;
    float dD = gDepth.Sample(gSmp, i.uv + float2(0,  1) * texelSize).r;
    float dL = gDepth.Sample(gSmp, i.uv + float2(-1, 0) * texelSize).r;
    float dR = gDepth.Sample(gSmp, i.uv + float2( 1, 0) * texelSize).r;
    float dC = gDepth.Sample(gSmp, i.uv).r;

    // 差分
    float edgeH = abs(dL - dR);
    float edgeV = abs(dU - dD);
    float edge = max(edgeH, edgeV);

    // 深度の差が大きい部分をアウトラインとする
    edge = smoothstep(0.0001, 0.001, edge);
    
    // 線の太さや強さを強調
    float strength = max(1.0, 1.0 + gDistortion * 5.0);
    edge = saturate(edge * strength * 2.0);

    float3 sceneColor = gScene.Sample(gSmp, i.uv).rgb;

    // 深度エッジがある部分を黒にする
    float3 col = sceneColor * (1.0 - edge);

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette * intensity);
    col = lerp(sceneColor, col, intensity);

    return float4(Saturate3(col), 1.0);
}
