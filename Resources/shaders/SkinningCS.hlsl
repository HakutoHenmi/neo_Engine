struct Vertex {
    float4 pos;
    float2 uv;
    float3 nrm;
    float4 weights;
    uint4 indices;
};

StructuredBuffer<Vertex> gInVertices : register(t0);
RWStructuredBuffer<Vertex> gOutVertices : register(u0);

cbuffer CBBone : register(b0) {
    row_major float4x4 gBones[128];
};

cbuffer CBCount : register(b1) {
    uint gNumVertices;
};

static const uint kMaxSkinBones = 128;

float4x4 IdentityMatrix()
{
    return float4x4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

void AccumulateBone(inout float4x4 skinMat, inout float validWeight, uint index, float weight)
{
    if (weight > 0.0001f && index < kMaxSkinBones) {
        skinMat += gBones[index] * weight;
        validWeight += weight;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint id = DTid.x;
    if (id >= gNumVertices) return;

    Vertex v = gInVertices[id];

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

    Vertex outV = v;
    outV.pos = skinnedPos;
    outV.nrm = normalize(skinnedNrm);
    gOutVertices[id] = outV;
}
