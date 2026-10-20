// Light and camera views read the same, current reconstructed liquid surface.
// No particle discs: a particle's support radius is not an opaque silhouette.
#include "FluidVolumeCommon.hlsli"
cbuffer LightCamera : register(b0) {
    row_major float4x4 lightViewProjection;
    row_major float4x4 inverseLightViewProjection;
    float4 clipBounds; // min.xy, max.xy
};
Texture3D<float4> shadowDensity : register(t0);
Texture3D<float4> shadowOccupancy : register(t1);
SamplerState linearClamp : register(s0);
struct ShadowVertex {float4 position:SV_POSITION;float2 ndc:TEXCOORD0;};
ShadowVertex ShadowVS(uint vertex:SV_VertexID) {
    const float2 corners[6]={float2(0,0),float2(0,1),float2(1,0),float2(1,0),float2(0,1),float2(1,1)};
    ShadowVertex o;o.ndc=lerp(clipBounds.xy,clipBounds.zw,corners[vertex]);o.position=float4(o.ndc,0,1);return o;
}
float ShadowDensity(float3 world) {
    float3 uv=(world-volumeOrigin)/(voxelSize*volumeSize);
    if(any(uv<0)||any(uv>1))return 0;
    float2 d=shadowDensity.SampleLevel(linearClamp,uv,0).xy;
    return max(d.x,d.y); // Preserve the slime/decoy shadow policy; water is transparent.
}
float ShadowPS(ShadowVertex input):SV_DEPTH {
    float4 start=mul(float4(input.ndc,0,1),inverseLightViewProjection);
    float4 end=mul(float4(input.ndc,1,1),inverseLightViewProjection);
    float3 origin=start.xyz/start.w,ray=normalize(end.xyz/end.w-origin);
    float3 safeRay=float3(abs(ray.x)>1e-7f?ray.x:1e-7f,abs(ray.y)>1e-7f?ray.y:1e-7f,abs(ray.z)>1e-7f?ray.z:1e-7f);
    float3 a=(volumeOrigin-origin)/safeRay,b=(volumeOrigin+voxelSize*volumeSize-origin)/safeRay;
    float3 lo=min(a,b),hi=max(a,b);
    float t=max(0,max(lo.x,max(lo.y,lo.z))),farT=min(length(end.xyz/end.w-origin),min(hi.x,min(hi.y,hi.z)));
    [loop] for(uint i=0;i<1280&&t<farT;++i) {
        float3 world=origin+ray*t;
        int3 block=clamp((int3)floor((world-volumeOrigin)/(voxelSize*4)),0,(int3)((volumeSize+3)/4)-1);
        float2 maximum=shadowOccupancy.Load(int4(block,0)).xy;
        if(max(maximum.x,maximum.y)<isoValue) {
            float3 boundary=volumeOrigin+(float3(block)+step(0,ray))*voxelSize*4;
            float3 travel=(boundary-world)/safeRay;
            t+=max(min(travel.x,min(travel.y,travel.z)),voxelSize*.01f)+voxelSize*.001f;
            continue;
        }
        if(ShadowDensity(world)>=isoValue) {
            float left=max(0,t-voxelSize*.25f),right=t;
            [unroll] for(uint refine=0;refine<5;++refine){float middle=(left+right)*.5f;if(ShadowDensity(origin+ray*middle)>=isoValue)right=middle;else left=middle;}
            float4 clip=mul(float4(origin+ray*right,1),lightViewProjection);
            return saturate(clip.z/clip.w);
        }
        t+=voxelSize*.25f;
    }
    discard;return 1;
}
