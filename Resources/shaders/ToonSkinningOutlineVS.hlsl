#include "Obj.hlsli"

// スキニング用定数バッファ
cbuffer CBBone : register(b3)
{
    matrix gBones[128];
};

static const float outlineWidth = 0.008f;
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

// スキニング対応アウトライン頂点シェーダー
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
    float3 worldNormal = normalize(mul(float4(skinnedNrm, 0), world).xyz);

    // クリップ空間（画面空間）での位置を計算
    float4 clipPos = mul(worldPos, mul(view, projection));

    // 法線をクリップ空間へ変換
    float3 clipNormal = mul(worldNormal, (float3x3)mul(view, projection));
    float2 offset = normalize(clipNormal.xy);

    // w成分を掛けることで遠近に関わらず一定のアウトライン幅を保つ
    clipPos.xy += offset * outlineWidth * clipPos.w;

    output.svpos = clipPos;
    output.worldpos = worldPos;
    output.normal = worldNormal;
    output.uv = uv;

    return output;
}
