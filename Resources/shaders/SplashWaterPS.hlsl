// SplashWaterPS.hlsl
// 飛び散る水のしずく (DevourGrowth.cpp)。中は透けてほとんど見えず、縁 (フレネル) に空の映り込み、
// 光の側に鋭いつや (ブルームで光る)、反対側に屈折で集まった光の点を描く。color.rgb は水のわずかな色、color.a は濃さ
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float4 main(PSIn i) : SV_TARGET {
    float3 n = normalize(i.normal);
    float3 v = normalize(cameraPos - i.worldPos);
    // 透明の描き方は両面を描くので、奥側の面は捨てる (残すと縁の映り込みが手前に重なって白い玉に見える)
    clip(dot(n, v));
    float facing = saturate(dot(n, v));
    float fresnel = .04 + .96 * pow(1 - facing, 4);
    // 空の映り込み: 上を向いた所は明るい空、下は暗い地面
    float3 r = reflect(-v, n);
    float3 sky = lerp(float3(.30, .32, .34), float3(.85, .92, 1.05), smoothstep(-.3, .6, r.y));
    // 太陽のつや (鋭い点と、少し広いにじみ)
    float3 l = normalize(float3(.35, 1, -.25));
    float3 h = normalize(l + v);
    float nh = saturate(dot(n, h));
    float spec = pow(nh, 260) * 6 + pow(nh, 40) * .35;
    // 屈折で光が集まる点: 光と反対側の、こちらを向いた面
    float caustic = pow(saturate(dot(n, normalize(v - l))), 8) * .9;
    float3 tint = i.color.rgb;
    float3 color = tint * (.10 + caustic) + sky * fresnel + spec;
    float alpha = saturate(i.color.a * (.10 + .75 * fresnel + caustic * .5) + spec);
    return float4(color, alpha);
}
