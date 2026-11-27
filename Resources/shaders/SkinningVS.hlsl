cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };
cbuffer CBBone : register(b3) { row_major float4x4 gBones[128]; };

static const uint kMaxSkinBones = 128;

float4x4 IdentityMatrix() {
    return float4x4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

void AccumulateBone(inout float4x4 skinMat, inout float validWeight, uint index, float weight) {
    if (weight > 0.0001f && index < kMaxSkinBones) {
        skinMat += gBones[index] * weight;
        validWeight += weight;
    }
}

struct VSIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float3 nrm : NORMAL;
    float4 weights : WEIGHTS;
    uint4 indices : BONES;
};
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; };

VSOut main(VSIn v) {
    VSOut output;

    // スキニング行列の合成
    float4x4 skinMat = IdentityMatrix() * 0.0f;
    float weightSum = 0.0f;
    AccumulateBone(skinMat, weightSum, v.indices.x, v.weights.x);
    AccumulateBone(skinMat, weightSum, v.indices.y, v.weights.y);
    AccumulateBone(skinMat, weightSum, v.indices.z, v.weights.z);
    AccumulateBone(skinMat, weightSum, v.indices.w, v.weights.w);
    if (weightSum < 0.001f) {
        skinMat = IdentityMatrix();
    } else {
        skinMat *= rcp(weightSum);
    }

    float4 localPos = v.pos;
    float4 skinnedPos = mul(localPos, skinMat);
    float4 localNrm = float4(v.nrm, 0.0f);
    float3 skinnedNrm = mul(localNrm, skinMat).xyz;

    float4 wp = mul(skinnedPos, gWorld);
    output.worldPos = wp.xyz;
    float3 wn = mul(float4(skinnedNrm, 0), gWorld).xyz;
    output.normal = normalize(wn);

    float4 vp = mul(wp, gView);
    output.svpos = mul(vp, gProj);
    output.uv = v.uv;
    return output;
}
