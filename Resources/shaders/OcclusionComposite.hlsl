// Pre-DLSS occlusion: multiplied into the jittered HDR scene (blend: dest*src)
// so DLSS antialiases and stabilizes AO / RT shadow contact darkening.
#define RT_LIGHTING_COMPOSITE
#include "ChronoFocusPost.hlsl"
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float factor=occlusionFactor(uv,viewDepth(uv));
    return float4(factor,factor,factor,1);
}
