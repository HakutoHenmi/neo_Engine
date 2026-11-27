// RainbowPS.hlsl
// 消火栓の放水の霧にかかる虹 (DevourHose.cpp)。半円の帯のメッシュに描く。
// uv.x は弧に沿った位置 (両端は薄く消える)、uv.y は帯の内側 (0: 紫) から外側 (1: 赤)。color.a で濃さ
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float3 Hue(float h) { return saturate(abs(frac(h + float3(0, 2.0 / 3.0, 1.0 / 3.0)) * 6 - 3) - 1); }
float4 main(PSIn i) : SV_TARGET {
    float across = saturate(i.uv.y), along = saturate(i.uv.x);
    // 外側の赤から内側の紫まで。白っぽく淡くして、霧に溶けて見えるようにする
    float3 color = lerp(Hue((1 - across) * 0.78), float3(1, 1, 1), 0.18);
    // 帯の縁と弧の両端をやわらかく消し、霧のむらに合わせて少しゆらす
    float band = pow(sin(3.14159 * across), 0.8);
    float ends = smoothstep(0.0, 0.22, along) * smoothstep(0.0, 0.22, 1 - along);
    float shimmer = 0.85 + 0.15 * sin(along * 40 + time * 1.7);
    return float4(color * 1.1, saturate(band * ends * shimmer * i.color.a));
}
