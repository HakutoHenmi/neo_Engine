#include "FluidVolumeCommon.hlsli"
cbuffer Camera : register(b0) {
    row_major float4x4 view;
    row_major float4x4 projection;
    row_major float4x4 viewProjection;
    float3 cameraPosition; float time;
};
Texture3D<float4> fluidDensity : register(t0);
Texture2D<float4> sceneColor : register(t1);
Texture2D<float> sceneDepth : register(t2);
TextureCube<float4> environmentMap : register(t3);
StructuredBuffer<FluidShape> particleShapes : register(t4);
StructuredBuffer<VolumeParticle> renderParticles : register(t5);
Texture2D<float> fluidDepth : register(t6);
Texture3D<float4> occupancy : register(t7);
SamplerState linearClamp : register(s0);

// The 98.56-unit horizontal volume reaches 49.28 units from its center.
// Keep the crossfade inside that boundary.
static const float nearVolumeFullRadius = 46.0f;
static const float nearVolumeFadeEndRadius = 48.5f;

float NearVolumeWeight(float3 world) {
    float3 margin=min(world-volumeOrigin,volumeOrigin+voxelSize*volumeSize-world);
    // Broad, rounded transition instead of a narrow square band around player.
    float3 centered=world-(volumeOrigin+voxelSize*volumeSize*0.5f);
    float radial=length(centered.xz);
    float horizontal=1-smoothstep(nearVolumeFullRadius,nearVolumeFadeEndRadius,radial);
    float vertical=smoothstep(0.0f,4.0f,margin.y);
    return horizontal*vertical;
}

float ViewDepth(float2 uv) {
    uint w,h; sceneDepth.GetDimensions(w,h);
    float z=sceneDepth.Load(int3(clamp(int2(uv*float2(w,h)),0,int2(w,h)-1),0));
    return projection[3][2]/(z-projection[2][2]);
}
float3 Density(float3 p) {
    float3 uv=(p-volumeOrigin)/(voxelSize*volumeSize);
    if(any(uv<0) || any(uv>1)) return 0;
    return fluidDensity.SampleLevel(linearClamp,uv,0).xyz;
}
float3 Normal(float3 p, uint phase, float3 fallback) {
    float h=voxelSize*0.75f;
    float3 g=float3(
        Density(p+float3(h,0,0))[phase]-Density(p-float3(h,0,0))[phase],
        Density(p+float3(0,h,0))[phase]-Density(p-float3(0,h,0))[phase],
        Density(p+float3(0,0,h))[phase]-Density(p-float3(0,0,h))[phase]);
    return dot(g,g)>1e-10f ? -normalize(g) : fallback;
}
bool RayBox(float3 origin, float3 ray, out float nearT, out float farT) {
    float3 safeRay=float3(abs(ray.x)<1e-7f ? 1e-7f : ray.x,abs(ray.y)<1e-7f ? 1e-7f : ray.y,abs(ray.z)<1e-7f ? 1e-7f : ray.z);
    float3 a=(volumeOrigin-origin)/safeRay;
    float3 b=(volumeOrigin+voxelSize*volumeSize-origin)/safeRay;
    float3 lo=min(a,b), hi=max(a,b);
    nearT=max(0,max(lo.x,max(lo.y,lo.z)));
    farT=min(hi.x,min(hi.y,hi.z));
    return farT>nearT;
}
float3 Reflection(float3 normal,float3 ray,float roughness) {
    // Cubemap can be a typed null descriptor; retain a restrained sky fallback.
    float3 r=reflect(ray,normal);
    uint w,h,levels; environmentMap.GetDimensions(0,w,h,levels);
    float3 env=environmentMap.SampleLevel(linearClamp,r,roughness*max((float)levels-1,0)).rgb;
    float3 sky=lerp(float3(0.035f,0.055f,0.075f),float3(0.3f,0.43f,0.55f),saturate(r.y*0.5f+0.5f));
    return max(env,sky*0.3f);
}
struct FullscreenIn { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
struct RayResult { float4 color:SV_Target0; float depth:SV_Target1; };

RayResult RaymarchPS(FullscreenIn input) {
    RayResult output;
    float2 uv=input.uv;
    float3 background=sceneColor.SampleLevel(linearClamp,uv,0).rgb;
    output.color=float4(background,1); output.depth=0;
    // Interpolate UV, then normalize in the pixel shader. Perspective rays
    // must NOT be normalized at fullscreen-triangle vertices.
    float3 viewRay=normalize(float3((uv*float2(2,-2)+float2(-1,1))/float2(projection[0][0],projection[1][1]),1));
    float3 ray=normalize(mul(viewRay,transpose((float3x3)view)));
    float nearT,farT;
    if(!RayBox(cameraPosition,ray,nearT,farT)) return output;
    farT=min(farT,ViewDepth(uv)/max(viewRay.z,1e-5f));
    if(farT<=nearT) return output;
    float3 hits=float3(-1,-1,-1), thickness=0;
    float t=nearT;
    float previousT=t;
    float3 previousDensity=Density(cameraPosition+ray*t);
    [unroll] for(uint startPhase=0;startPhase<3;++startPhase)
        if(previousDensity[startPhase]>=isoValue) hits[startPhase]=t;
    uint steps=0;
    [loop] for(;steps<640 && t<farT;++steps) {
        float3 world=cameraPosition+ray*t;
        int3 block=clamp((int3)floor((world-volumeOrigin)/(voxelSize*4)),0,(int3)((volumeSize+3)/4)-1);
        float3 maxD=occupancy.Load(int4(block,0)).xyz;
        if(max(maxD.x,max(maxD.y,maxD.z))<isoValue) {
            float3 boundary=volumeOrigin+(float3(block)+step(0,ray))*voxelSize*4;
            float3 travel=(boundary-world)/float3(abs(ray.x)>1e-7f?ray.x:1e-7f,abs(ray.y)>1e-7f?ray.y:1e-7f,abs(ray.z)>1e-7f?ray.z:1e-7f);
            float advance=min(travel.x,min(travel.y,travel.z));
            t=min(farT,t+max(advance,voxelSize*0.01f)+voxelSize*0.001f);
            previousT=t; previousDensity=Density(cameraPosition+ray*t);
            continue;
        }
        float nextT=min(t+voxelSize*0.5f,farT);
        float3 nextDensity=Density(cameraPosition+ray*nextT);
        [unroll] for(uint phase=0;phase<3;++phase) {
            float d0=previousDensity[phase], d1=nextDensity[phase];
            bool inside0=d0>=isoValue, inside1=d1>=isoValue;
            float span=nextT-previousT;
            float crossing=saturate((isoValue-d0)/(abs(d1-d0)>1e-6f ? d1-d0 : 1e-6f));
            if(inside0 && inside1) thickness[phase]+=span;
            else if(inside0!=inside1) thickness[phase]+=span*(inside0?crossing:1-crossing);
            if(hits[phase]<0 && (inside0 || inside1)) {
                float a=previousT,b=nextT;
                if(!inside0) {
                    [unroll] for(uint refine=0;refine<5;++refine) {
                        float middle=(a+b)*0.5f;
                        if(Density(cameraPosition+ray*middle)[phase]>=isoValue) b=middle; else a=middle;
                    }
                }
                hits[phase]=inside0?previousT:(a+b)*0.5f;
            }
        }
        t=nextT; previousT=t; previousDensity=nextDensity;
    }
    float nearest=1e20f;
    uint nearestPhase=0;
    [unroll] for(uint h=0;h<3;++h) if(hits[h]>=0 && hits[h]<nearest) { nearest=hits[h]; nearestPhase=h; }
    if(nearest==1e20f) {
        if(debugMode>3.5f) output.color=float4(0.1f,0.05f,0.15f,1);
        return output;
    }
    output.depth=nearest*viewRay.z;
    float3 normal=Normal(cameraPosition+ray*nearest,nearestPhase,-ray);
    if(dot(normal,ray)>0) normal=-normal;
    if(debugMode>0.5f && debugMode<1.5f) { output.color=float4(nearestPhase==0?float3(0,1,0):nearestPhase==1?float3(1,1,0):float3(0,0.4f,1),1); return output; }
    if(debugMode>1.5f && debugMode<2.5f) { output.color=float4(normal*0.5f+0.5f,1); return output; }
    if(debugMode>2.5f && debugMode<3.5f) { output.color=float4(saturate(thickness/3),1); return output; }
    if(debugMode>3.5f) { output.color=steps>=640?float4(1,0,1,1):float4(float3(steps/640.0f,0.3f,0),1); return output; }
    // Screen-space refraction is deliberately bounded and depth-tested to
    // prevent foreground rocks leaking into the water. It is not ray tracing.
    float3 viewNormal=mul(normal,(float3x3)view);
    float2 offset=viewNormal.xy*float2(1,-1)*min(thickness[nearestPhase],2.0f)*0.025f/max(output.depth,1.0f);
    float2 refractedUV=clamp(uv+offset,0.001f,0.999f);
    if(ViewDepth(refractedUV)<output.depth) refractedUV=uv;
    float3 result=sceneColor.SampleLevel(linearClamp,refractedUV,0).rgb;
    // Back-to-front sort by actual surface distance; no phase always wins.
    bool used[3]={false,false,false};
    [unroll] for(uint layer=0;layer<3;++layer) {
        int selected=-1; float farthest=-1;
        [unroll] for(uint p=0;p<3;++p) if(!used[p] && hits[p]>farthest) { selected=(int)p; farthest=hits[p]; }
        if(selected<0) break;
        used[selected]=true;
        float3 n=Normal(cameraPosition+ray*farthest,(uint)selected,-ray);
        n=dot(n,ray)>0?-n:n;
        float f0=selected==2?0.0204f:0.035f;
        float fresnel=f0+(1-f0)*pow(1-saturate(dot(n,-ray)),5);
        float roughness=selected==2?0.12f:0.20f;
        float3 tint=saturate(phaseColor[selected].rgb);
        float3 absorption=(1-tint)*(selected==2?0.55f:2.2f)+0.035f;
        float3 transmission=exp(-absorption*thickness[selected]);
        float3 body=result*transmission+tint*(1-transmission)*0.12f;
        result=lerp(body,Reflection(n,ray,roughness),fresnel);
    }
    output.color=float4(lerp(background,result,NearVolumeWeight(cameraPosition+ray*nearest)),1);
    return output;
}

struct SprayOut {
    float4 position:SV_POSITION;
    float2 local:TEXCOORD0;
    float3 centerView:TEXCOORD1;
    float radius:TEXCOORD2;
    float4 color:COLOR0;
    nointerpolation uint shapeIndex:TEXCOORD3;
    nointerpolation uint distantBulk:TEXCOORD4;
    nointerpolation uint phase:TEXCOORD5;
};
SprayOut SprayVS(uint vertex:SV_VertexID, uint instance:SV_InstanceID) {
    SprayOut o=(SprayOut)0;
    FluidShape s=particleShapes[instance];
    VolumeParticle p=renderParticles[instance];
    float3 margin=min(ShapeCenter(s)-volumeOrigin,volumeOrigin+voxelSize*volumeSize-ShapeCenter(s));
    float2 centered=ShapeCenter(s).xz-(volumeOrigin+voxelSize*volumeSize*0.5f).xz;
    bool distant=s.info.x>0 && (length(centered)+s.info.y>nearVolumeFullRadius || margin.y<4+s.info.y);
    if(s.info.w==0 || (s.info.z<0.005f && !distant)) { o.position=float4(2,2,2,1); return o; }
    o.shapeIndex=instance; o.distantBulk=distant ? 1 : 0;
    o.phase=VolumePhase(p.type);
    float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    o.local=corners[vertex];
    o.centerView=mul(float4(distant ? ShapeCenter(s) : p.position,1),view).xyz;
    o.radius=distant ? s.info.y : s.info.z;
    o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
    o.color=p.color;
    return o;
}
float4 SprayPS(SprayOut input):SV_Target {
    if(input.distantBulk!=0) {
        uint width,height; sceneDepth.GetDimensions(width,height);
        float2 uv=input.position.xy/float2(width,height);
        float3 viewRay=normalize(float3((uv*float2(2,-2)+float2(-1,1))/float2(projection[0][0],projection[1][1]),1));
        float3 ray=normalize(mul(viewRay,transpose((float3x3)view)));
        FluidShape shape=particleShapes[input.shapeIndex];
        float3 origin=ShapePoint(shape,cameraPosition-ShapeCenter(shape));
        float3 direction=ShapePoint(shape,ray);
        float a=dot(direction,direction), b=dot(origin,direction), c=dot(origin,origin)-1;
        float discriminant=b*b-a*c;
        if(discriminant<=0) discard;
        float root=sqrt(discriminant);
        float t=max(0,(-b-root)/a), endT=(-b+root)/a;
        if(endT<=t) discard;
        float3 world=cameraPosition+ray*t;
        float weight=1-NearVolumeWeight(world);
        if(weight<=0 || t*viewRay.z>ViewDepth(uv)) discard;
        float nearDepth=fluidDepth.Load(int3((int2)input.position.xy,0));
        if(nearDepth>0 && t*viewRay.z>nearDepth &&
           NearVolumeWeight(cameraPosition+ray*(nearDepth/max(viewRay.z,1e-5f)))>=0.999f) discard;
        float3 local=origin+direction*t;
        float3 normal=normalize(shape.row0.xyz*local.x+shape.row1.xyz*local.y+shape.row2.xyz*local.z);
        float f0=input.phase==2 ? 0.0204f : 0.035f;
        float fresnel=f0+(1-f0)*pow(1-saturate(dot(normal,-ray)),5);
        float3 transmission=exp(-((1-saturate(input.color.rgb))*(input.phase==2 ? 0.55f : 2.2f)+0.035f)*(endT-t));
        float3 viewNormal=mul(normal,(float3x3)view);
        float2 refracted=clamp(uv+viewNormal.xy*float2(1,-1)*min(endT-t,2.0f)*0.025f/max(t*viewRay.z,1),0.001f,0.999f);
        if(ViewDepth(refracted)<t*viewRay.z) refracted=uv;
        float3 background=sceneColor.SampleLevel(linearClamp,refracted,0).rgb;
        float3 color=lerp(background*transmission+input.color.rgb*(1-transmission)*0.12f,Reflection(normal,ray,input.phase==2 ? 0.12f : 0.20f),fresnel);
        float edgeCoverage=saturate(discriminant/(a*0.15f));
        return float4(color,weight*edgeCoverage*saturate(input.color.a));
    }
    float r2=dot(input.local,input.local);
    if(r2>=1) discard;
    float3 normalView=float3(input.local,-sqrt(1-r2));
    float z=input.centerView.z+normalView.z*input.radius;
    uint w,h; sceneDepth.GetDimensions(w,h);
    float2 uv=input.position.xy/float2(w,h);
    if(z>ViewDepth(uv)) discard;
    float bulkDepth=fluidDepth.Load(int3((int2)input.position.xy,0));
    if(bulkDepth>0 && z>bulkDepth) discard;
    float3 n=normalize(mul(normalView,transpose((float3x3)view)));
    float3 ray=normalize(mul(normalize(input.centerView),transpose((float3x3)view)));
    float fresnel=0.02f+0.98f*pow(1-saturate(dot(n,-ray)),5);
    float3 col=lerp(sceneColor.SampleLevel(linearClamp,uv,0).rgb*0.98f,Reflection(n,ray,0.16f),fresnel);
    float coverage=saturate((1-r2)/max(fwidth(r2),0.001f));
    return float4(col,coverage*saturate(input.color.a));
}
