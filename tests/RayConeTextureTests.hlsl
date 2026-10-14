#include "../Resources/shaders/RayConeTextureLod.hlsli"
Texture2D<float4> material : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Test : register(b0){float phase,cosine,coneWidth,pad;}
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{
    float2 density=RayConeUVDensity(float3(4,0,0),float3(0,4,0),float2(1,0),float2(0,1));
    float footprint=coneWidth/max(.05,abs(cosine));uint w,h,levels;material.GetDimensions(0,w,h,levels);
    float lod=min(float(levels-1),RayConeMipLevel(density,footprint,float2(w,h)));
    float2 at=uv*.8+.1+phase;
    return float4(material.SampleLevel(linearClamp,at,0+lod).r,material.SampleLevel(linearClamp,at,2).r,lod,1);
}
