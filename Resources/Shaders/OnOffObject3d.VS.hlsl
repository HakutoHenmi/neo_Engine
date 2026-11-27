#include "Obj.hlsli"

// Obj.hlsli の行列と頂点出力形式を使用する。
VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;

    // ローカル座標をクリップ座標へ変換する。
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = mul(pos, world);
    output.normal = normalize(mul(float4(normal, 0), world)).xyz;
    output.uv = uv;

    return output;
}
