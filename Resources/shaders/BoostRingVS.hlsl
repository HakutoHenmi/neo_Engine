#include "Obj.hlsli"

// 最小: 標準の座標変換 + 必要な情報をVSOutputへ詰める
VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;

    float4 worldPos = mul(pos, world);
    float4 worldN = normalize(mul(float4(normal, 0), world));

    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = worldPos;
    output.normal = worldN.xyz;
    output.uv = uv;

    return output;
}
