#include "../Resources/shaders/FluidRaymarch.hlsl"
StructuredBuffer<float4> queries:register(t13);
RWStructuredBuffer<float4> queryResults:register(u0);
[numthreads(64,1,1)]
void ProbeFarWater(uint3 id:SV_DispatchThreadID){
    uint count,stride;queries.GetDimensions(count,stride);if(id.x>=count)return;
    float3 p=queries[id.x].xyz;
    if(queries[id.x].w>0){
        VolumeParticle water=(VolumeParticle)0;water.position=p;water.type=1.1f;water.density=125;water.pad=float3(.2f,p.y,1);
        FluidShape original=(FluidShape)0;original.row0=float4(1/1.2f,0,0,p.x);original.row1=float4(0,4,0,p.y);original.row2=float4(0,0,1/1.2f,p.z);original.info=float4(1,1.2f,0,1);
        FluidShape fallback=WaterFallbackShape(original,water);
        float3 radii=1/float3(length(fallback.row0.xyz),length(fallback.row1.xyz),length(fallback.row2.xyz));
        queryResults[id.x]=float4(radii,4.f*3.14159265f/3.f*radii.x*radii.y*radii.z);return;
    }
    queryResults[id.x]=float4(Density(p).z,NearVolumeWeight(p),FarWaterWeight(p),FlowVelocity(p).x);
}
