#include "../Resources/shaders/FluidRaymarch.hlsl"
RWStructuredBuffer<float4> faceResults : register(u0);
[numthreads(64,1,1)]
void TestPlayerFace(uint3 id : SV_DispatchThreadID) {
    if(id.x>=230)return;
    if(id.x==0)faceResults[id.x]=float4(SlimeEyeRadii(),1);
    else if(id.x==1||id.x==2)faceResults[id.x]=float4(SlimeEyeGuide(id.x-1),1);
    else if(id.x==3)faceResults[id.x]=float4(SlimeFaceForward(),1);
    else if(id.x<228){float radius,visibility;
        faceResults[id.x]=float4(SlimeBubblePosition(id.x-4,radius,visibility),radius);}
    else {float3 front=SlimeFaceForward(),center=SlimeEyeGuide(0);float distance;float3 normal;
        float3 origin=center+front*3;
        if(id.x==229)origin.y+=SlimeEyeRadii().y*1.1f;
        bool hit=SlimeEllipsoidHit(origin,-front,center,SlimeEyeRadii(),front,distance,normal);
        faceResults[id.x]=float4(distance,normal.y,0,hit?1:0);}
}
