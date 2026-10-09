// Single-bounce diffuse transport and glossy reflections of opaque scenery.
// Closest hits use real mesh UVs, albedo/normal/roughness maps and sun visibility.
RaytracingAccelerationStructure geometry : register(t0);
Texture2D<float> opaqueDepth : register(t1);
Texture2D<float> fluidDepth : register(t2);
struct Triangle { float4 p[3]; float4 n[3]; };
struct Material { uint offset,albedo,normal,roughness; float4 tint; uint ground; float3 pad; };
StructuredBuffer<Triangle> triangles : register(t3);
StructuredBuffer<Material> materials : register(t4);
TextureCube<float4> environmentMap : register(t5);
Texture2D<float4> maps[32] : register(t6);
RWTexture2D<float4> reflections : register(u0);
RWTexture2D<float4> indirectLight : register(u1);
RWStructuredBuffer<uint> diagnostics : register(u2);
SamplerState wrapSampler : register(s0);
SamplerState clampSampler : register(s1);
cbuffer Camera : register(b0) {
    row_major float4x4 inverseVP;
    float3 towardSun; float nearPlane;
    float3 cameraPosition; float farPlane;
    float2 dimensions; float includeFluid; float sunEnabled;
    float3 sunColor; float reflectionEnabled;
    float3 ambientColor; float indirectEnabled;
    uint recordDiagnostics; float3 diagnosticPad;
};
static const float PI=3.14159265;
struct Surface {float3 p,n,albedo; float roughness;};
float3 SafeNormal(float3 n,float3 fallback){return dot(n,n)>1e-10?normalize(n):fallback;}
float3 Sky(float3 direction,float roughness){
    uint w,h,levels;environmentMap.GetDimensions(0,w,h,levels);
    float3 env=environmentMap.SampleLevel(clampSampler,direction,roughness*max(0,(float)levels-1)).rgb;
    float3 fallback=lerp(float3(.035,.055,.075),float3(.3,.43,.55),saturate(direction.y*.5+.5));
    return max(env,fallback*.3);
}
bool TraceSurface(float3 origin,float3 direction,float maximum,out Surface surface,out float distance){
    surface=(Surface)0;distance=maximum;
    RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.025;ray.TMax=maximum;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;query.TraceRayInline(geometry,0,1,ray);while(query.Proceed()){}
    if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return false;
    distance=query.CommittedRayT();surface.p=origin+direction*distance;
    Material material=materials[query.CommittedInstanceID()];
    Triangle tri=triangles[material.offset+query.CommittedPrimitiveIndex()];
    float2 bary=query.CommittedTriangleBarycentrics();float3 b=float3(1-bary.x-bary.y,bary);
    float3 localNormal=tri.n[0].xyz*b.x+tri.n[1].xyz*b.y+tri.n[2].xyz*b.z;
    float3x3 normalTransform=(float3x3)query.CommittedWorldToObject3x4();
    float3 n=SafeNormal(mul(localNormal,normalTransform),float3(0,1,0));
    float2 uv=float2(dot(float3(tri.p[0].w,tri.p[1].w,tri.p[2].w),b),dot(float3(tri.n[0].w,tri.n[1].w,tri.n[2].w),b));
    float2 uvA=float2(tri.p[1].w-tri.p[0].w,tri.n[1].w-tri.n[0].w),uvB=float2(tri.p[2].w-tri.p[0].w,tri.n[2].w-tri.n[0].w);
    float determinant=uvA.x*uvB.y-uvA.y*uvB.x;
    float3x3 toWorld=(float3x3)query.CommittedObjectToWorld3x4();
    if(abs(determinant)>1e-7){
        float3 tangent=mul(toWorld,((tri.p[1].xyz-tri.p[0].xyz)*uvB.y-(tri.p[2].xyz-tri.p[0].xyz)*uvA.y)/determinant);
        tangent=SafeNormal(tangent-n*dot(n,tangent),float3(1,0,0));float3 bitangent=SafeNormal(cross(n,tangent),float3(0,0,1))*sign(determinant);
        float3 map=maps[NonUniformResourceIndex(material.normal)].SampleLevel(wrapSampler,uv,2).xyz*2-1;map.y=-map.y;
        map.xy*=material.ground!=0?.35:1;
        n=SafeNormal(tangent*map.x+bitangent*map.y+n*map.z,n);
    }
    if(dot(n,-direction)<0)n=-n;surface.n=n;
    surface.albedo=maps[NonUniformResourceIndex(material.albedo)].SampleLevel(wrapSampler,uv,2).rgb*material.tint.rgb;
    surface.roughness=clamp(maps[NonUniformResourceIndex(material.roughness)].SampleLevel(wrapSampler,uv,2).r,.12,1);
    if(material.ground!=0)surface.roughness=max(.78,surface.roughness);
    return true;
}
float SunVisibility(float3 p,float3 n){
    if(sunEnabled<.5)return 0;
    RayDesc ray;ray.Origin=p+n*.06+towardSun*.025;ray.Direction=towardSun;ray.TMin=.025;ray.TMax=1600;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_OPAQUE> query;query.TraceRayInline(geometry,0,1,ray);while(query.Proceed()){}
    return query.CommittedStatus()==COMMITTED_TRIANGLE_HIT?0:1;
}
float3 Incoming(float3 origin,float3 direction,float roughness,out bool hit){
    Surface surface;float distance;hit=TraceSurface(origin,direction,1600,surface,distance);
    if(!hit)return Sky(direction,roughness);
    // Radiance from one diffuse bounce; its direct sun term has its own shadow ray.
    float3 illumination=sunColor*saturate(dot(surface.n,towardSun))*SunVisibility(surface.p,surface.n);
    illumination+=Sky(surface.n,1)*.22;
    return max(0,surface.albedo*illumination);
}
float RawDepth(uint2 p,out bool fluid){
    uint w,h;opaqueDepth.GetDimensions(w,h);uint2 at=min(uint2((float2(p)+.5)/dimensions*float2(w,h)),uint2(w-1,h-1));
    float raw=opaqueDepth[at];fluid=false;
    if(includeFluid>.5){float z=fluidDepth[at];if(z>0){float f=farPlane/(farPlane-nearPlane)-nearPlane*farPlane/(farPlane-nearPlane)/z;if(f<raw){raw=f;fluid=true;}}}
    return raw;
}
float3 Position(uint2 p,float raw){
    uint w,h;opaqueDepth.GetDimensions(w,h);uint2 at=min(uint2((float2(p)+.5)/dimensions*float2(w,h)),uint2(w-1,h-1));
    float2 uv=(float2(at)+.5)/float2(w,h);float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),raw,1),inverseVP);return world.xyz/world.w;
}
float3 DepthNormal(uint2 p,float raw,float3 world){
    uint2 a=uint2(max(int(p.x)-1,0),p.y),b=uint2(min(p.x+1,uint(dimensions.x)-1),p.y);
    uint2 c=uint2(p.x,max(int(p.y)-1,0)),d=uint2(p.x,min(p.y+1,uint(dimensions.y)-1));bool unused;
    float da=RawDepth(a,unused),db=RawDepth(b,unused),dc=RawDepth(c,unused),dd=RawDepth(d,unused);
    float3 dx=abs(da-raw)<abs(db-raw)?world-Position(a,da):Position(b,db)-world;
    float3 dy=abs(dc-raw)<abs(dd-raw)?world-Position(c,dc):Position(d,dd)-world;
    float3 n=SafeNormal(cross(dx,dy),float3(0,1,0));return dot(n,cameraPosition-world)<0?-n:n;
}
float2 Sequence(uint sample,uint count,float rotation){
    return float2((sample+.5)/count,frac(sample*.61803398875+rotation));
}
float3 Hemisphere(float3 n,float2 u){
    float3 tangent=SafeNormal(cross(abs(n.y)<.95?float3(0,1,0):float3(1,0,0),n),float3(1,0,0));
    float radius=sqrt(u.x),phi=2*PI*u.y;
    return tangent*(radius*cos(phi))+cross(n,tangent)*(radius*sin(phi))+n*sqrt(1-u.x);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=uint2(dimensions)))return;uint2 p=id.xy;bool fluid;float raw=RawDepth(p,fluid);
    float z=nearPlane*farPlane/max(.0001,farPlane-raw*(farPlane-nearPlane));
    reflections[p]=0;indirectLight[p]=0;if(raw>=.99999)return;
    float3 world=Position(p,raw),view=SafeNormal(cameraPosition-world,float3(0,0,-1));Surface receiver;float distance;
    if(fluid){receiver.p=world;receiver.n=DepthNormal(p,raw,world);receiver.roughness=.16;receiver.albedo=0;}
    else if(!TraceSurface(cameraPosition,-view,length(world-cameraPosition)+.3,receiver,distance)||abs(distance-length(world-cameraPosition))>max(.2,z*.002))return;
    float3 origin=receiver.p+receiver.n*.06;
    // A camera-invariant low-discrepancy sequence avoids random rotations at
    // every half-meter boundary. The radiance filter supplies spatial smoothing.
    float rotation=.381966;
    if(reflectionEnabled>.5){
        float3 reflected=reflect(-view,receiver.n),radiance=0;float f0=fluid?.02:.04;
        float fresnel=f0+(1-f0)*pow(1-saturate(dot(receiver.n,view)),5);
        [unroll]for(uint i=0;i<2;++i){float3 micro=SafeNormal(lerp(receiver.n,Hemisphere(receiver.n,Sequence(i,2,rotation)),receiver.roughness*receiver.roughness),receiver.n);
            float3 direction=reflect(-view,micro);if(dot(direction,receiver.n)<=0)direction=reflected;
            bool hit;float3 light=Incoming(origin,direction,receiver.roughness,hit);
            if(recordDiagnostics!=0&&hit)InterlockedAdd(diagnostics[0],1);
            // Fluid already has cubemap reflection: replace only rays that hit scenery.
            if(fluid)light=hit?light-Sky(direction,receiver.roughness):0;
            radiance+=light;
        }
        float attenuation=fluid?1:pow(1-receiver.roughness*.8,2);
        reflections[p]=float4(clamp(radiance*.5*fresnel*attenuation,-2,4),fluid?-z:z);
    }
    if(indirectEnabled>.5&&!fluid){
        float3 irradiance=0;
        [unroll]for(uint i=0;i<8;++i){bool hit;irradiance+=Incoming(origin,Hemisphere(receiver.n,Sequence(i,8,rotation)),1,hit);if(recordDiagnostics!=0&&hit)InterlockedAdd(diagnostics[1],1);}
        // Replace the existing constant ambient term partially, preserving direct lighting.
        float3 bounce=receiver.albedo*(irradiance/8-ambientColor*.32)*.45;
        indirectLight[p]=float4(clamp(bounce,-.4,2),z);
    }
}
