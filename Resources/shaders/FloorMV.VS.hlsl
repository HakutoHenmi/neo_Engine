#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    // 標準変換（資料サンプルと同系統）
    float4 worldPos = mul(pos, world);
    float4 worldNormal = normalize(mul(float4(normal, 0), world));

    VSOutput output;
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = worldPos;
    output.normal = worldNormal.xyz;
    output.uv = uv;
    return output;
}
