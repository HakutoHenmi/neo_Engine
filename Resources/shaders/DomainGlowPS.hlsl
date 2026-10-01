cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float4 main(PSIn i) : SV_TARGET {
    // Thin additive trails keep the boss's floor warnings visible through them.
    float nearPlayer = smoothstep(3, 9, distance(cameraPos, i.worldPos));
    return float4(i.color.rgb * (1.1 + .15 * sin(time * 8 + i.uv.y * 12)), i.color.a * nearPlayer);
}
