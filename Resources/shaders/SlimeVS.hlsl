#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;
    
    // ワールド座標を計算
    output.worldpos = mul(pos, world);
    
    // 画面上の座標を計算
    output.svpos = mul(mul(output.worldpos, view), projection);
    
    // 法線の変換
    output.normal = normalize(mul(normal, (float3x3)world));
    
    output.uv = uv;

    return output;
}
