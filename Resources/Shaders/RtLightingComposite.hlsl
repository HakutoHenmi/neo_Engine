#define RT_LIGHTING_COMPOSITE
#include "ChronoFocusPost.hlsl"
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 color=scene.SampleLevel(linearClamp,uv,0).rgb;float z=viewDepth(uv);
    if(z<dofFar*.95){
        if(rtReflection>.5)color+=rayLighting(rtReflections,uv,z);
        if(rtIndirect>.5)color+=rayLighting(rtIndirectLight,uv,z);
    }
    return float4(max(0,color),1);
}
