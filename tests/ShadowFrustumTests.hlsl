#include "../Resources/shaders/ShadowFrustum.hlsli"
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    return ShadowFrustumWeight(float3(uv,.5));
}
