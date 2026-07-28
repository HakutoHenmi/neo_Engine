// Resources/shaders/DepthOutlinePost.hlsl
// ポストエフェクト: デプス（深度）ベースのアウトライン検出
#include "PostProcessCommon.hlsli"

Texture2D gScene : register(t0);
Texture2D gDepth : register(t3); // Depthテクスチャは t3 にバインドされている
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
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0);
    float edgeThickness = max(1.0, 1.0 + gDistortion * 2.0); // Distortionで線の太さを調整

    // 十字方向のデプスを取得
    float dCenter = gDepth.Sample(gSmp, i.uv).r;
    float dLeft   = gDepth.Sample(gSmp, i.uv + float2(-1.0, 0.0) * texelSize * edgeThickness).r;
    float dRight  = gDepth.Sample(gSmp, i.uv + float2( 1.0, 0.0) * texelSize * edgeThickness).r;
    float dUp     = gDepth.Sample(gSmp, i.uv + float2( 0.0,-1.0) * texelSize * edgeThickness).r;
    float dDown   = gDepth.Sample(gSmp, i.uv + float2( 0.0, 1.0) * texelSize * edgeThickness).r;

    // デプスの差分を計算
    float diffX = abs(dLeft - dRight);
    float diffY = abs(dUp - dDown);
    float depthDiff = diffX + diffY;

    // 閾値を設定。デプスの差が大きい＝エッジ
    // デプス値は非線形なので、シーンスケールに合わせて閾値を調整する
    float edge = step(0.0005, depthDiff);

    // デプスが遠すぎる場所（スカイボックス等）はエッジを描かない
    if (dCenter >= 1.0) {
        edge = 0.0;
    }

    // 元の色
    float3 col = gScene.Sample(gSmp, i.uv).rgb;

    // アウトラインは黒で描画
    col = lerp(col, float3(0.0, 0.0, 0.0), edge);

    // Vignette
    float2 d = i.uv - 0.5;
    col *= saturate(1.0 - dot(d, d) * gVignette);

    return float4(Saturate3(col), 1.0);
}
