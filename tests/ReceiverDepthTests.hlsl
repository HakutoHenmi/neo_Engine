#include "../Resources/shaders/ReceiverDepthTolerance.hlsli"
StructuredBuffer<float4> positions : register(t0);
RWStructuredBuffer<float4> results : register(u0);
cbuffer Test : register(b0){
    row_major float4x4 viewProjection,inverseVP;
    float3 camera;float count;
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=uint(count))return;
    float4 clip=mul(positions[id.x],viewProjection);
    float4 ndc=clip/clip.w;
    float4 world=mul(float4(ndc.xyz,1),inverseVP);world/=world.w;
    float3 delta=world.xyz-camera;float distance=length(delta),actual=(-.08-camera.y)/normalize(delta).y;
    // Analytic intersection of the reconstructed ray with the physical ground.
    // This is independent of the depth tolerance implementation under test.
    results[id.x]=float4(actual-distance,actual>ReceiverTraceMaximum(distance,clip.w),actual>distance+.3,clip.w);
}
