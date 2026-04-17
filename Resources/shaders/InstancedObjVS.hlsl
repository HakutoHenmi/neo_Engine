#pragma pack_matrix(row_major)

struct InstanceData {
    matrix world;
    float4 color;
    float4 uvScaleOffset; // x:scaleU, y:scaleV, z:offsetU, w:offsetV
};

StructuredBuffer<InstanceData> gInstanceData : register(t2);

cbuffer ViewProjection : register(b0)
{
    matrix view;
    matrix projection;
    matrix viewProj;
    float3 cameraPos;
    float time;
};

struct VSIn
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float3 nrm : NORMAL;
};

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : COLOR0;
};

VSOut main(VSIn v, uint instanceID : SV_InstanceID)
{
    VSOut o;
    InstanceData data = gInstanceData[instanceID];
    
    float4 wp = mul(v.pos, data.world);
    o.worldPos = wp.xyz;
    o.normal = normalize(mul(float4(v.nrm, 0), data.world).xyz);
    o.svpos = mul(mul(wp, view), projection);
    o.uv = v.uv;
    o.color = data.color;
    
    return o;
}
