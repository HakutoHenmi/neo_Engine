cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {float4 pos:SV_POSITION;float3 worldPos:TEXCOORD0;float3 normal:TEXCOORD1;float2 uv:TEXCOORD2;float4 color:COLOR0;};
float4 main(PSIn i):SV_TARGET {
    float2 p=i.worldPos.xz;
    float wave=.5+.5*sin(length(p)*.32-time*1.7);
    float glints=pow(saturate(sin(p.x*.65+time*.6)*sin(p.y*.71-time*.4)),16);
    // Low-opacity additive color leaves terrain and attack telegraphs legible.
    return float4(i.color.rgb*(.85+.15*wave)+glints*.12,i.color.a);
}
