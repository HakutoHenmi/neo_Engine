#include "../Resources/shaders/FluidRaymarch.hlsl"
RWStructuredBuffer<float4> surfaceResults : register(u0);
SprayOut BubbleVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){return SprayVS(vertex,particleCount+instance);}
SprayOut EyeVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){return SprayVS(vertex,particleCount+248+instance);}
[numthreads(64,1,1)]
void SurfaceCoverage(uint3 id:SV_DispatchThreadID){
    if(id.x>=particleCount)return;
    FluidShape shape=particleShapes[id.x];SprayOut vertex=SprayVS(0,id.x);
    surfaceResults[id.x]=float4(NearVolumeWeight(ShapeCenter(shape)),vertex.distantBulk,vertex.position.x==2&&vertex.position.y==2?0:1,shape.info.w);
}
[numthreads(64,1,1)]
void MorphDecorations(uint3 id:SV_DispatchThreadID){
    if(id.x>=slimeDecorationCount)return;
    SprayOut vertex=SprayVS(0,particleCount+id.x);
    surfaceResults[id.x]=float4(vertex.centerWorld,vertex.color.a);
}
