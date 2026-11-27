#pragma pack_matrix(row_major)

struct InstanceData {
    matrix world;
    float4 color;
    float4 uvScaleOffset; // x:U の倍率, y:V の倍率, z:U のずらし量, w:V のずらし量
};

StructuredBuffer<InstanceData> gInstanceData : register(t2);

cbuffer ViewProjection : register(b0) {
    matrix view;
    matrix projection;
    matrix viewProj;
    float3 cameraPos;
    float time;
};

struct VSOutput {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD, uint instanceID : SV_InstanceID) {
    VSOutput output;
    InstanceData data = gInstanceData[instanceID];
    
    output.svpos = mul(pos, mul(data.world, mul(view, projection)));
    
    // インスタンスのデータから UV の倍率とずらし量をかける
    output.uv.x = uv.x * data.uvScaleOffset.x + data.uvScaleOffset.z;
    output.uv.y = uv.y * data.uvScaleOffset.y + data.uvScaleOffset.w;
    
    output.color = data.color;
    return output;
}
