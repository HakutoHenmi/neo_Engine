#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput output;
    output.worldpos = mul(pos, world);
    output.svpos = mul(output.worldpos, viewProj);
    // 薄い立方体の板の、大きく偏った拡大率を補正する。
    float3x3 basis = (float3x3)world;
    float3x3 cofactors = float3x3(cross(basis[1], basis[2]),
                                 cross(basis[2], basis[0]),
                                 cross(basis[0], basis[1]));
    output.normal = normalize(mul(normal, cofactors));
    output.uv = uv;
    return output;
}
