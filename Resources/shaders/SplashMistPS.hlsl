// SplashMistPS.hlsl
// 飛沫の細かい霧 (DevourGrowth.cpp)。カメラを向いた四角に、ふちのやわらかい丸を描く。
// 中に細かな粒のむらを入れて、のっぺりした円に見えないようにする。color.b は模様をずらす種、color.a で濃さ
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float noise(float2 p) {
    float2 c = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    return lerp(lerp(hash(c), hash(c + float2(1, 0)), f.x), lerp(hash(c + float2(0, 1)), hash(c + 1), f.x), f.y);
}
float4 main(PSIn i) : SV_TARGET {
    float2 p = i.uv * 2 - 1;
    float d = length(p);
    float soft = pow(saturate(1 - d), 1.8);
    // 粒のむら (粒ごとに模様をずらす)
    float2 seed = float2(i.color.b * 37, i.color.b * 91);
    float grain = noise(p * 4 + seed) * .6 + noise(p * 11 - seed) * .4;
    float alpha = soft * (.45 + .9 * grain) * i.color.a;
    return float4(float3(.86, .91, .97) * 1.1, saturate(alpha));
}
