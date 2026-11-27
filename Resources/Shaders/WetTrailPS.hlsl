// WetTrailPS.hlsl
// スライムが通った所のぬれ跡 (DevourGrowth.cpp)。床に置いた四角に、縁がでこぼこした丸いしみを描く。
// 下の床を暗くし (水を吸った色)、見る角度によって空の光をつやとして映す。
// color.r は跡ごとの模様の種、color.a は濃さ (古いほど薄い)
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
    // 縁のでこぼこ (跡ごとに模様を変える)
    float2 seed = float2(i.color.r, i.color.r * 1.618);
    float wobble = noise(p * 2.5 + seed * 13) * .55 + noise(p * 6 - seed * 7) * .25;
    float d = length(p) + (wobble - .4) * .55;
    float mask = 1 - smoothstep(.55, .9, d);
    clip(mask - .01);
    // 中ほど濃く、縁は乾きかけで薄い
    float wet = mask * lerp(.6, 1, saturate(1 - d));
    // つや: 床の上向きの面に映る空と太陽
    float3 n = float3(0, 1, 0);
    float3 v = normalize(cameraPos - i.worldPos);
    float fresnel = .02 + .98 * pow(1 - saturate(dot(n, v)), 5);
    float3 l = normalize(float3(.35, 1, -.25));
    float spec = pow(saturate(dot(n, normalize(l + v))), 180) * 3;
    float3 sheen = float3(.75, .82, .9) * fresnel * .9 + spec;
    // 半透明で重ねる: 下の床は (1 - alpha) 倍に暗くなり、つやの分だけ明るさが足される
    float alpha = saturate(wet * i.color.a * (.38 + .5 * fresnel) + spec * wet * i.color.a);
    return float4(float3(.015, .015, .02) + sheen, alpha);
}
