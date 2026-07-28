#include "Obj.hlsli"

// スキニング用定数バッファ
cbuffer CBBone : register(b3)
{
    matrix gBones[128];
};

static const uint kMaxSkinBones = 128;

matrix IdentityMatrix()
{
    return matrix(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

void AccumulateBone(inout matrix skinMat, inout float validWeight, uint index, float weight)
{
    if (weight > 0.0001f && index < kMaxSkinBones) {
        skinMat += gBones[index] * weight;
        validWeight += weight;
    }
}

// スキニング対応トゥーン頂点シェーダー
VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD,
              float4 weights : WEIGHTS, uint4 indices : BONES)
{
    VSOutput output;

    // スキニング行列の合成
    matrix skinMat = IdentityMatrix() * 0.0f;
    float weightSum = 0.0f;
    AccumulateBone(skinMat, weightSum, indices.x, weights.x);
    AccumulateBone(skinMat, weightSum, indices.y, weights.y);
    AccumulateBone(skinMat, weightSum, indices.z, weights.z);
    AccumulateBone(skinMat, weightSum, indices.w, weights.w);
    if (weightSum < 0.001f) {
        skinMat = IdentityMatrix();
    } else {
        skinMat *= rcp(weightSum);
    }

    float4 skinnedPos = mul(pos, skinMat);
    float3 skinnedNrm = mul(float4(normal, 0.0f), skinMat).xyz;

    // ワールド座標変換
    float4 worldPos = mul(skinnedPos, world);
    output.worldpos = worldPos;
    output.normal = normalize(mul(float4(skinnedNrm, 0), world).xyz);
    output.svpos = mul(worldPos, mul(view, projection));
    output.uv = uv;

    return output;
}
