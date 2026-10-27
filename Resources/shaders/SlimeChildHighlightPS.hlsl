// 子分のスライムの縁取り。加算で輪郭を光らせ、中央はほとんど透かす (壁越しの表示にも使う)
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float4 main(PSIn i) : SV_TARGET {
    float3 direction = normalize(cameraPos - i.worldPos);
    float rim = pow(1 - saturate(abs(dot(normalize(i.normal), direction))), 2.5);
    float pulse = .8 + .2 * sin(time * 4 + i.worldPos.x * .3 + i.worldPos.z * .3);
    return float4(i.color.rgb * (.08 + rim * 2.2) * pulse, i.color.a);
}
