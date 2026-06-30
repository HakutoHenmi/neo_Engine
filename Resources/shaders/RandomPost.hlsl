// Resources/shaders/RandomPost.hlsl
// ポストエフェクト: ランダムノイズ
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
    float2 texcoord : TEXCOORD0; // スライドの input.texcoord に合わせる
};

// スライドで紹介されている、Seed値のみで乱数を決定するアルゴリズム
float rand2dTo1d(float2 value)
{
    // 一般的な乱数生成アルゴリズムの実装例
    float2 smallValue = sin(value);
    float random = dot(smallValue, float2(12.9898, 78.233));
    random = frac(sin(random) * 143758.5453);
    return random;
}

// スライドの float32_t 等の型に合わせるためのエイリアス
#define float32_t float
#define float32_t4 float4

float4 main(PSIn input) : SV_TARGET
{
    // --- 乱数を白黒で出力 ---

    // 乱数生成。引数にtexcoordと時間（gTime）を渡して時間経過で動かす
    float32_t random = rand2dTo1d(input.texcoord + gTime);
    
    // 色にする
    float4 output_color = float32_t4(random, random, random, 1.0f);
    
    return output_color;
}
