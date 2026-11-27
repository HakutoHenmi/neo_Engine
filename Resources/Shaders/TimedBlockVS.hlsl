#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;

    // 標準の変換（ガイドのサンプルと同じ形）
    float4 worldPos = mul(pos, world);
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = worldPos;
    output.normal = normalize(mul(float4(normal, 0), world)).xyz;
    output.uv = uv;

    return output;
}
