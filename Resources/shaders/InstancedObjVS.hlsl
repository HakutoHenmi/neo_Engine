struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; };
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0; };
VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut output;
    InstanceData data = gInstanceData[instanceID];
    float4 wp = mul(v.pos, data.world);
    output.worldPos = wp.xyz;
    output.normal = normalize(mul(float4(v.nrm, 0), data.world).xyz);
    output.svpos = mul(mul(wp, gView), gProj);
    output.uv = v.uv;
    output.color = data.color;
    return output;
}
