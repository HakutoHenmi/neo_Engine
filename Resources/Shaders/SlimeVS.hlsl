#include "Obj.hlsli"

struct SlimeVSOutput {
    float4 svpos : SV_POSITION;
    float4 worldpos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 localpos : TEXCOORD1;
};

SlimeVSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    SlimeVSOutput output;
    
    // ワールド座標を計算
    output.worldpos = mul(pos, world);
    
    // 画面上の座標を計算
    output.svpos = mul(mul(output.worldpos, view), projection);
    
    // 法線の変換
    output.normal = normalize(mul(normal, (float3x3)world));
    
    output.uv = uv;
    output.localpos = pos.xyz;

    return output;
}
