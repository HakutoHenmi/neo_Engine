//======================================================
// StageCommonVS.hlsl
// ステージ用 共通VS（Obj.hlsli準拠）
//======================================================
#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    // 標準的な座標変換（ガイドの作法）
    float4 worldPos = mul(pos, world);
    float4 worldNormal = normalize(mul(float4(normal, 0), world));

    VSOutput output;
    // ※ svpos / worldpos / normal / uv は Obj.hlsli の VSOutput に合わせる
    output.svpos = mul(pos, mul(world, mul(view, projection)));
    output.worldpos = worldPos;
    output.normal = worldNormal.xyz;
    output.uv = uv;

    return output;
}
