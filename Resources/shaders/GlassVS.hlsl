#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD) {
    VSOutput o;
    o.worldpos = mul(pos, world);
    o.svpos = mul(o.worldpos, viewProj);
    // Correct for the strongly nonuniform scales of thin cube panels.
    float3x3 basis = (float3x3)world;
    float3x3 cofactors = float3x3(cross(basis[1], basis[2]),
                                 cross(basis[2], basis[0]),
                                 cross(basis[0], basis[1]));
    o.normal = normalize(mul(normal, cofactors));
    o.uv = uv;
    return o;
}
