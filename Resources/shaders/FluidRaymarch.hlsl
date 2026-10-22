#define FLUID_VOLUME_CORE
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
StructuredBuffer<VolumeParticle> originalParticles : register(t8);
Texture3D<float4> fluidVelocity : register(t9);
SamplerState linearClamp : register(s0);

// Morphing uses a fitted domain: its physical extent changes with the shape.
// Share this exact border between raymarch and particle-LOD classification.
float NearVolumeBorder(){return 2*voxelSize;}
float3 NearVolumeMargin(float3 world){return min(world-volumeOrigin,volumeOrigin+voxelSize*volumeSize-world);}
float NearVolumeWeight(float3 world) {
    float3 margin=NearVolumeMargin(world);
    return smoothstep(0,NearVolumeBorder(),min(margin.x,min(margin.y,margin.z)));
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
// Direction sunlight travels. Offline tests leave the constant zero.
float3 SunTravel(){return sunDirection.w>0?sunDirection.xyz:normalize(float3(.45f,-.8f,.35f));}
float SunIntensity(){return sunDirection.w>0?sunDirection.w:1;}
float3 FlowVelocity(float3 p){
    float3 uv=(p-volumeOrigin)/(voxelSize*volumeSize);
    if(any(uv<0) || any(uv>1)) return 0;
    return fluidVelocity.SampleLevel(linearClamp,uv,0).xyz;
}
float WaterHash(float3 p){p=frac(p*float3(.1031f,.1030f,.0973f));p+=dot(p,p.yxz+33.33f);return frac((p.x+p.y)*p.z);}
float WaterNoise(float3 p){
    float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    float a=lerp(WaterHash(i),WaterHash(i+float3(1,0,0)),f.x),b=lerp(WaterHash(i+float3(0,1,0)),WaterHash(i+float3(1,1,0)),f.x);
    float c=lerp(WaterHash(i+float3(0,0,1)),WaterHash(i+float3(1,0,1)),f.x),d=lerp(WaterHash(i+float3(0,1,1)),WaterHash(i+1),f.x);
    return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
}
float WaterFbm(float3 p){return WaterNoise(p)*.55f+WaterNoise(p*2.03f+11.1f)*.3f+WaterNoise(p*4.11f+23.7f)*.15f;}
// Fast water draws in air as it travels. x: broken white skin on the surface,
// y: milky scattering inside. The streak field is advected by the resolved
// fluid velocity with two half-cycle phases (a flow map), so it rides the jet
// without stretching as the clock grows.
float2 WaterAeration(float3 world,float3 normal,float3 ray,float thickness){
    float3 velocity=FlowVelocity(world);
    float speed=length(velocity);
    float aerated=smoothstep(8,26,speed);
    if(aerated<=0.001f) return 0;
    // The jet leaves the nozzle glassy and whitens as it breaks up downstream.
    float amount=aerated*lerp(.2f,1,smoothstep(1.5f,14,length(world-playerBodyCenter)));
    // A sheet sliding over the street keeps only a little white at its front.
    amount*=lerp(.25f,1,smoothstep(.5f,1.3f,world.y));
    // Shear stretches entrained air into thin streaks along the flow.
    float3 along=velocity/speed;
    float3 across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));
    float3x3 frame=float3x3(across*5.f,cross(along,across)*5.f,along*.9f);
    const float cycle=.4f;
    float phase=frac(time/cycle),blend=1-abs(2*phase-1);
    float3 drift=velocity*cycle;
    float streak=WaterFbm(mul(frame,world-drift*phase))*blend+
                 WaterFbm(mul(frame,world-drift*frac(phase+.5f))+19.3f)*(1-blend);
    float grazing=pow(1-saturate(dot(normal,-ray)),1.5f);
    // Sparse streaks first; more air lowers the threshold into a broken skin.
    float low=.6f-.28f*amount-.12f*grazing;
    float skin=smoothstep(low,low+.2f,streak)*amount;
    float milk=amount*(1-exp(-thickness*1.1f));
    return float2(skin,milk);
}
float3 RainbowColor(float t){
    // t: 0 violet edge, 1 red edge of the band.
    float band=smoothstep(-.12f,.08f,t)*(1-smoothstep(.92f,1.12f,t));
    float hue=(1-saturate(t))*.78f;
    return band*saturate(abs(frac(hue+float3(1,2.f/3,1.f/3))*6-3)-1);
}
#include "SlimeDecoration.hlsli"
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
        bool softSlime=selected==0&&SlimeMorphActive();
        float roughness=selected==2?0.12f:softSlime?.14f:.20f;
        float3 tint=saturate(phaseColor[selected].rgb);
        float4 localDensity=fluidDensity.SampleLevel(linearClamp,(cameraPosition+ray*farthest-volumeOrigin)/(voxelSize*volumeSize),0);
        float inkFraction=selected==0?saturate(localDensity.w/max(localDensity.x,.001f)):0;
        tint=lerp(tint,float3(.045,.32,.17),inkFraction);
        float gelAbsorption=softSlime?.85f/max(SlimeDecorationScale(),1):2.2f;
        float3 absorption=(1-tint)*(selected==2?0.55f:lerp(gelAbsorption,7.2f,inkFraction))+0.035f;
        float3 transmission=exp(-absorption*thickness[selected]);
        float3 body=result*transmission+tint*(1-transmission)*(softSlime?.22f:.12f);
        float opacity=saturate(phaseColor[selected].a);
        float3 surface=lerp(body,Reflection(n,ray,roughness),fresnel);
        if(selected==2){
            // Entrained air scatters diffusely. Inside it only clouds the water;
            // the broken skin is lit white with no glassy glare.
            float2 air=WaterAeration(cameraPosition+ray*farthest,n,ray,thickness[2]);
            float3 sky=Reflection(float3(0,1,0),float3(0,-1,0),.9f);
            float lit=.5f+.5f*saturate(dot(n,-SunTravel()));
            float3 white=float3(.88f,.93f,.97f)*(sky*.55f+lit*SunIntensity()*.7f);
            surface=lerp(surface,lerp(body,white*.8f,.6f),air.y*.5f);
            surface=lerp(surface,white,air.x*.75f);
        }
        result=lerp(result,surface,opacity);
    }
    float3 nearestWorld=cameraPosition+ray*nearest;
    if(playerDecoration>0.001f && nearestPhase==0 &&
       length(nearestWorld-playerBodyCenter)<max(playerRadii.x,playerRadii.z)*(1+.55f*playerMotion)*1.6f)
        result=lerp(result,lerp(phaseColor[0].rgb,1.f,.12f),pow(1-saturate(dot(normal,-ray)),2)*.28f*saturate(playerDecoration));
    output.color=float4(lerp(background,result,NearVolumeWeight(nearestWorld)),1);
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
    float3 centerWorld:TEXCOORD6;
};
SprayOut SprayVS(uint vertex:SV_VertexID, uint instance:SV_InstanceID) {
    SprayOut o=(SprayOut)0;
    float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    o.local=corners[vertex];
    if(instance>=particleCount) {
        uint id=instance-particleCount;
        float3 world;float visibility=1;
        if(id<slimeInteriorBubbles){
            world=SlimeBubblePosition(id,o.radius,visibility);o.distantBulk=2;
            if(Density(world).x<isoValue*.55f)visibility=0;
        }else if(id<slimeInteriorBubbles+slimeEscapedBubbles){
            float3 random=SlimeHash(float(id));bool morph=SlimeMorphActive();
            float age=frac(time*(morph?.18f+.08f*random.y:.32f+.14f*random.y)+random.x);
            float angle=float(id)*2.39996323f;
            float3 field=float3(cos(angle)*playerRadii.x*.48f,0,sin(angle)*playerRadii.z*.48f);
            world=SlimeFieldPosition(field);o.radius=(.13f+.10f*random.y)*SlimeDecorationScale();
            if(morph)o.radius=min(.18f,(.065f+.045f*random.y)*sqrt(SlimeDecorationScale()));
            // Start above the reconstructed surface, including landing squash and sliding.
            // An assumed ellipsoid can keep the entire plume buried inside the SPH body.
            float extent=playerRadii.y*1.7f;float previous=extent;bool found=false;
            [loop] for(uint step=0;step<=24;++step){
                float height=extent*(1-float(step)/12);
                if(Density(world+float3(0,height,0)).x>=isoValue){
                    float outside=previous,inside=height;
                    [unroll] for(uint refine=0;refine<4;++refine){float middle=(outside+inside)*.5f;
                        if(Density(world+float3(0,middle,0)).x>=isoValue)inside=middle;else outside=middle;}
                    world.y+=(outside+inside)*.5f+o.radius+age*(morph?.8f*sqrt(SlimeDecorationScale()):2.8f*SlimeDecorationScale());found=true;break;
                }previous=height;
            }
            world.xz+=float2(sin(time*1.6f+angle),cos(time*1.3f+angle))*.13f*age*SlimeDecorationScale();
            visibility=found?smoothstep(0,.07f,age)*(1-smoothstep(.72f,1,age)):0;o.distantBulk=4;
            if(morph&&id>=slimeInteriorBubbles+12)visibility=0;
        }else{
            uint eye=id-slimeInteriorBubbles-slimeEscapedBubbles;
            float3 front=SlimeFaceForward(),guide=SlimeEyeGuide(eye);
            float extent=max(playerRadii.x,playerRadii.z)*(1+.55f*playerMotion)*1.45f;
            bool found=false;float previous=extent;
            world=guide;
            // Anchor each rigid eye to the actual reconstructed SPH boundary.
            // This follows lagging/flapping fluid instead of an assumed ellipse.
            [loop] for(uint step=0;step<40&&(!SlimeCoreActive()||SlimeMorphActive());++step){
                float distance=extent*(1-float(step)/20);
                if(Density(guide+front*distance).x>=isoValue){
                    float outside=previous,inside=distance;
                    [unroll] for(uint refine=0;refine<5;++refine){float middle=(outside+inside)*.5f;
                        if(Density(guide+front*middle).x>=isoValue)inside=middle;else outside=middle;}
                    world=guide+front*((outside+inside)*.5f+.045f*SlimeDecorationScale());found=true;break;
                }previous=distance;
            }
            if(SlimeCoreActive()&&!SlimeMorphActive()){world=guide;found=Density(SlimeCorePosition()).x>isoValue*.25f;}
            visibility=found?1:0;o.radius=SlimeEyeRadii().y*1.2f;o.distantBulk=3;
        }
        if(visibility<=0){o.position=float4(2,2,2,1);return o;}
        o.color.a=visibility;o.centerWorld=world;
        o.centerView=mul(float4(world,1),view).xyz;
        o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
        return o;
    }
    FluidShape s=particleShapes[instance];
    VolumeParticle p=renderParticles[instance];
    float3 margin=NearVolumeMargin(ShapeCenter(s));
    bool distant=s.info.x>0 && min(margin.x,min(margin.y,margin.z))<NearVolumeBorder()+s.info.y;
    if(s.info.w!=0 && s.info.z<0.005f && !distant && p.type>1.05f && p.type<1.25f) {
        // Hydrant mist: a seeded subset of resolved droplets also sheds a soft
        // puff. Airborne water shreds into a widening sheath as it travels;
        // water that has just hit something throws up a rising cloud.
        // A further seeded subset becomes loose glassy droplets: torn off the
        // jet's skin in flight, or thrown up ballistically from an impact.
        float seed=frac(p.color.r*1000);
        float age=p.pad.x;bool airborne=age<=0;
        float keep=airborne?.3f:.12f,drops=keep+(airborne?.06f:.06f);
        if(seed>=drops || age>=1.1f) { o.position=float4(2,2,2,1); return o; }
        bool droplet=seed>=keep;
        float random=droplet?(seed-keep)/(drops-keep):seed/keep;
        float speed=length(p.velocity);
        float3 along=speed>.01f?p.velocity/speed:float3(0,1,0);
        float3 across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));
        float spin=random*45.553f+time*(random-.5f)*1.3f;
        float3 radial=across*cos(spin)+cross(along,across)*sin(spin);
        float3 swirl=float3(sin(time*.9f+random*40),cos(time*.7f+random*23),sin(time*.8f+random*31))*.25f;
        float spread=saturate((length(p.position-playerBodyCenter)-2)/30);
        float reach=.5f+frac(random*37.1f);
        float3 world;
        if(droplet){
            if(airborne){
                world=p.position+radial*(.25f+1.5f*spread)*reach;
                o.color.a=smoothstep(.02f,.15f,spread);
            }else{
                if(age>.7f) { o.position=float4(2,2,2,1); return o; }
                float lift=(3.5f+3*frac(random*5.3f))*age-10*age*age;
                world=p.position+radial*(.3f+4*age)*reach+float3(0,max(0,lift),0);
                o.color.a=1-smoothstep(.4f,.7f,age);
            }
            o.radius=.04f+.05f*frac(random*13.7f);
            o.distantBulk=0;o.shapeIndex=instance;o.phase=2;
            o.centerView=mul(float4(world,1),view).xyz;
            o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
            return o;
        }
        if(airborne){
            world=p.position+radial*(.35f+1.7f*spread)*reach+swirl*spread;
            o.radius=.8f+2.f*spread;
            o.color.a=.05f*smoothstep(.05f,.4f,spread);
        }else{
            world=p.position+radial*(.2f+1.4f*age)*reach+float3(0,.3f+1.5f*age,0)+swirl;
            o.radius=1.f+2.5f*age;
            o.color.a=.06f*smoothstep(0,.12f,age)*(1-smoothstep(.4f,1.1f,age));
        }
        o.color.r=random;o.distantBulk=5;o.centerWorld=world;
        o.centerView=mul(float4(world,1),view).xyz;
        o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
        return o;
    }
    if(s.info.w==0 || (s.info.z<0.005f && !distant)) { o.position=float4(2,2,2,1); return o; }
    o.shapeIndex=instance; o.distantBulk=distant ? 1 : 0;
    o.phase=VolumePhase(p.type);
    o.centerView=mul(float4(distant ? ShapeCenter(s) : p.position,1),view).xyz;
    o.radius=distant ? s.info.y : s.info.z;
    o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
    o.color=p.color;
    return o;
}
float4 SprayPS(SprayOut input):SV_Target {
    if(input.distantBulk==5) {
        float r2=dot(input.local,input.local);
        if(r2>=1) discard;
        uint width,height; sceneDepth.GetDimensions(width,height);
        float2 uv=input.position.xy/float2(width,height);
        float z=input.centerView.z;
        // Soft particle: fade into geometry, the camera and the liquid surface.
        float soft=saturate((ViewDepth(uv)-z)/max(input.radius,.1f))*saturate((z-.3f)/1.5f);
        float bulkDepth=fluidDepth.Load(int3((int2)input.position.xy,0));
        if(bulkDepth>0) soft*=saturate((bulkDepth-z)/input.radius+1);
        // Wisps: the billboard is only a bound. Density comes from a rising,
        // drifting noise field at the pixel's world position on the puff plane.
        float3 right=float3(view[0][0],view[1][0],view[2][0]),up=float3(view[0][1],view[1][1],view[2][1]);
        float3 at=input.centerWorld+(right*input.local.x+up*input.local.y)*input.radius;
        float wisp=WaterFbm(at*.8f+float3(0,-time*.5f,0)+input.color.r*31.7f);
        float density=(1-r2)*(1-r2)*smoothstep(.3f,.75f,wisp)*1.8f*soft*input.color.a;
        if(density<=1e-4f) discard;
        // Per-pixel ray: a puff spans more than the 1.8-degree bow band.
        float3 viewRay=normalize(float3((uv*float2(2,-2)+float2(-1,1))/float2(projection[0][0],projection[1][1]),1));
        float3 pixelRay=normalize(mul(viewRay,transpose((float3x3)view)));
        float3 travel=SunTravel();float sun=SunIntensity();
        // Henyey-Greenstein forward scattering: mist glows against the sun.
        float g=.6f,mie=(1-g*g)/(12.566371f*pow(1+g*g+2*g*dot(pixelRay,travel),1.5f));
        float3 sky=Reflection(float3(0,1,0),float3(0,-1,0),.9f);
        float3 mist=sky*.7f+float3(1,.98f,.94f)*sun*(.25f+mie*1.6f);
        // Primary and fainter, reversed secondary bows around the antisolar
        // point; only with the sun behind the viewer. Fine hose droplets widen
        // and wash out the bands, so they stay soft and pastel.
        float angle=degrees(acos(clamp(dot(pixelRay,travel),-1,1)));
        float3 bow=RainbowColor((angle-39.8f)/2.8f)+RainbowColor((53.8f-angle)/3.6f)*.35f;
        bow=lerp(dot(bow,1.f/3),bow,.7f);
        // Dispersion redistributes the scattered light rather than adding a glow.
        float3 colour=bow*sun*1.1f*saturate(-travel.y*4);
        mist=mist*(1-.3f*saturate(dot(colour,1.f/3)))+colour;
        return float4(mist,density);
    }
    if(input.distantBulk>=2) {
        uint width,height; sceneDepth.GetDimensions(width,height);
        float2 uv=input.position.xy/float2(width,height);
        float3 viewRay=normalize(float3((uv*float2(2,-2)+float2(-1,1))/float2(projection[0][0],projection[1][1]),1));
        float3 ray=normalize(mul(viewRay,transpose((float3x3)view)));
        bool eye=input.distantBulk==3;
        float3 radii=eye?SlimeEyeRadii():float3(input.radius,input.radius,input.radius);
        float distance;float3 normal;
        bool halo=false;
        if(!SlimeEllipsoidHit(cameraPosition,ray,input.centerWorld,radii,SlimeFaceForward(),distance,normal)){
            if(!eye||!SlimeEllipsoidHit(cameraPosition,ray,input.centerWorld,radii*1.18f,SlimeFaceForward(),distance,normal))discard;
            halo=true;
        }
        float z=distance*viewRay.z;
        if(z>ViewDepth(uv)) discard;
        float bulkDepth=fluidDepth.Load(int3((int2)input.position.xy,0));
        float depthInGel=bulkDepth>0?max(0,z-bulkDepth):0;
        if(eye){
            if(SlimeMorphActive()&&depthInGel>.25f*SlimeDecorationScale())discard;
            if(!SlimeMorphActive()&&(bulkDepth<=0 || (!SlimeCoreActive()&&depthInGel>.2f*SlimeDecorationScale())))discard;
            float facing=saturate(dot(normal,-ray));
            float edge=smoothstep(0,.22f,facing);
            if(SlimeCoreActive()&&!SlimeMorphActive())edge*=exp(-depthInGel*.055f)*smoothstep(-.15f,.25f,dot(normalize(cameraPosition-SlimeCorePosition()),SlimeFaceForward()));
            if(halo)return float4(.68f,.90f,.26f,.14f*edge*input.color.a*saturate(playerDecoration));
            return float4(lerp(float3(.75f,.85f,.20f),float3(1,.97f,.64f),pow(facing,.35f)),
                edge*input.color.a*saturate(playerDecoration));
        }
        if(input.distantBulk==2 && bulkDepth<=0)discard;
        if(input.distantBulk==4 && depthInGel>0)discard;
        float facing=saturate(dot(normal,-ray));
        float fresnel=.02f+.98f*pow(1-facing,5);
        float3 light=normalize(float3(-.45f,.8f,-.35f));
        float highlight=pow(saturate(dot(reflect(-light,normal),-ray)),90);
        float3 viewNormal=mul(normal,(float3x3)view);
        float2 refracted=clamp(uv+viewNormal.xy*float2(1,-1)*input.radius*.15f/max(z,1),.001f,.999f);
        if(ViewDepth(refracted)<z)refracted=uv;
        float3 reflection=Reflection(normal,ray,.035f);
        float3 refractedColor=sceneColor.SampleLevel(linearClamp,refracted,0).rgb;
        float transmission=exp(-depthInGel*(SlimeMorphActive()?.24f/max(SlimeDecorationScale(),1):.48f));
        float3 tint=input.distantBulk==2?float3(.36f,.95f,.73f):float3(.9f,1,.96f);
        float3 color=(reflection*3.f+float3(.24f,.39f,.33f))*tint+highlight*float3(2.3f,2.4f,2.25f);
        color=lerp(refractedColor*tint*.55f,color,saturate(.18f+fresnel*.82f+highlight));
        float coverage=smoothstep(0,.12f,facing);
        float opacity=(input.distantBulk==4?.24f:.09f)+.7f*fresnel+.85f*highlight;
        opacity*=transmission;
        return float4(color,coverage*opacity*input.color.a*saturate(playerDecoration));
    }
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
        bool ink=renderParticles[input.shapeIndex].type>=8.0f;
        float3 tint=ink?float3(.045,.32,.17):saturate(phaseColor[input.phase].rgb);
        float3 transmission=exp(-((1-tint)*(input.phase==2 ? 0.55f : ink?7.2f:2.2f)+0.035f)*(endT-t));
        float3 viewNormal=mul(normal,(float3x3)view);
        float2 refracted=clamp(uv+viewNormal.xy*float2(1,-1)*min(endT-t,2.0f)*0.025f/max(t*viewRay.z,1),0.001f,0.999f);
        if(ViewDepth(refracted)<t*viewRay.z) refracted=uv;
        float3 background=sceneColor.SampleLevel(linearClamp,refracted,0).rgb;
        float3 color=lerp(background*transmission+tint*(1-transmission)*0.12f,Reflection(normal,ray,input.phase==2 ? 0.12f : 0.20f),fresnel);
        float edgeCoverage=saturate(discriminant/(a*0.15f));
        return float4(color,weight*edgeCoverage*saturate(input.color.a)*saturate(phaseColor[input.phase].a));
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
