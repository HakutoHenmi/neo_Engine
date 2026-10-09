RaytracingAccelerationStructure geometry : register(t0);
Texture2D<float> opaqueDepth : register(t1);
Texture2D<float> fluidDepth : register(t2);
RWTexture2D<float2> visibility : register(u0);
cbuffer Camera : register(b0){
    row_major float4x4 inverseVP;
    float3 towardSun; float nearPlane;
    float3 cameraPosition; float farPlane;
    float2 dimensions; float includeFluid; float sunEnabled;
};
float rawDepth(uint2 p){
    uint w,h;opaqueDepth.GetDimensions(w,h);
    uint2 at=min(uint2((float2(p)+.5)/dimensions*float2(w,h)),uint2(w-1,h-1));
    float raw=opaqueDepth[at];
    if(includeFluid>.5){float z=fluidDepth[at];if(z>0)raw=min(raw,farPlane/(farPlane-nearPlane)-nearPlane*farPlane/(farPlane-nearPlane)/z);}
    return raw;
}
float3 position(uint2 p,float d){
    uint w,h;opaqueDepth.GetDimensions(w,h);
    uint2 at=min(uint2((float2(p)+.5)/dimensions*float2(w,h)),uint2(w-1,h-1));
    // The inverse projection must use the same input texel as rawDepth().
    float2 uv=(float2(at)+.5)/float2(w,h);
    float4 v=mul(float4(uv*float2(2,-2)+float2(-1,1),d,1),inverseVP);return v.xyz/v.w;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=uint2(dimensions)))return;
    uint2 p=id.xy;float d=rawDepth(p);float z=nearPlane*farPlane/max(.0001,farPlane-d*(farPlane-nearPlane));
    if(d>=.99999||sunEnabled<.5){visibility[p]=float2(1,z);return;}
    float3 world=position(p,d);
    uint2 l=uint2(max(int(p.x)-1,0),p.y),r=uint2(min(p.x+1,uint(dimensions.x)-1),p.y);
    uint2 u=uint2(p.x,max(int(p.y)-1,0)),b=uint2(p.x,min(p.y+1,uint(dimensions.y)-1));
    float dl=rawDepth(l),dr=rawDepth(r),du=rawDepth(u),db=rawDepth(b);
    float3 dx=abs(dl-d)<abs(dr-d)?world-position(l,dl):position(r,dr)-world;
    float3 dy=abs(du-d)<abs(db-d)?world-position(u,du):position(b,db)-world;
    float3 n=cross(dx,dy);n=dot(n,n)>1e-10?normalize(n):float3(0,1,0);
    if(dot(n,cameraPosition-world)<0)n=-n;
    RayDesc ray;ray.Origin=world+n*.18+towardSun*.05;ray.Direction=towardSun;ray.TMin=.02;ray.TMax=1600;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(geometry,0,1,ray);while(query.Proceed()){}
    visibility[p]=float2(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT?0:1,z);
}
