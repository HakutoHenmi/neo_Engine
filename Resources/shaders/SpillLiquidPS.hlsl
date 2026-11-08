// SpillLiquidPS.hlsl
// 部屋のこぼれた液体 (蜂蜜・のり・ジュース) の粒と糸。半透明で、縁ほど濃く (厚みが増して見える)、
// 上からの光の強いつやと、部屋の明るさの映り込みを足す。color.a で透けぐあいを決める
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
    float facing = saturate(dot(n, v));
    float rim = pow(1 - facing, 2.2);
    // 上の照明と窓の光のつや
    float3 light = normalize(float3(.35, 1, -.25));
    float3 h = normalize(light + v);
    float spec = pow(saturate(dot(n, h)), 70) * 1.6 + pow(saturate(dot(n, h)), 12) * .18;
    // 中心は透けて色が薄く、縁は厚く濃い色。上の面は部屋の明るさを映す
    float3 body = i.color.rgb * lerp(.78, 1.08, saturate(n.y * .5 + .5));
    float3 deep = i.color.rgb * i.color.rgb * .75;
    float3 color = lerp(body, deep, rim * .7) + spec + rim * .12;
    float alpha = saturate(i.color.a * (.72 + .28 * rim) + spec * .5);
    return float4(color, alpha);
}
