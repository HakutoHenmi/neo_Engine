#pragma pack_matrix(row_major)

// b0: フレームで共有するデータ
cbuffer ViewProjection : register(b0) {
    matrix view;
    matrix projection;
    matrix viewProj;
    float3 cameraPos;
    float time;
};

// b1: パーティクルのインスタンスのデータ
cbuffer ParticleTransform : register(b1) {
    matrix world;
    float4 color;
    float4 uvScaleOffset; // x:U の倍率, y:V の倍率, z:U のずらし量, w:V のずらし量
};

struct VSOutput {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    
    // UV の倍率とずらし量をかける
    output.uv.x = uv.x * uvScaleOffset.x + uvScaleOffset.z;
    output.uv.y = uv.y * uvScaleOffset.y + uvScaleOffset.w;
    
    output.color = color;
    return output;
}
