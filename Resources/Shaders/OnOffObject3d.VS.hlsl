#include "Obj.hlsli"

// 入出力は VSOutput を使う（マニュアル準拠）:contentReference[oaicite:3]{index=3}
VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;

    // 標準的な変換（サンプルと同じ形）:contentReference[oaicite:4]{index=4}
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = mul(pos, world);
    output.normal = normalize(mul(float4(normal, 0), world)).xyz;
    output.uv = uv;

    return output;
}
