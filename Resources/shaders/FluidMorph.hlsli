// Point-cloud assignment is separate from spatial-neighbour sorting.
struct MorphTarget {float3 local;float blend;float3 origin;float density;float3 forward;float omega;float4 color;float4 material;float4 motion;float4 shape;float4 reserved;};
struct MorphEntry {float3 start;uint target;float3 previous;uint key;uint source;uint owner;float2 unused;};
StructuredBuffer<MorphTarget> MorphTargets : register(t1);
RWStructuredBuffer<MorphEntry> MorphData : register(u7);
float MorphSpeedLimit(uint original,float type){
    float limit=type>=4.f?80.f:30.f;
    if(type<.5f&&pad8>.5f){MorphTarget t=MorphTargets[MorphData[8192+original].target];
        if(t.shape.w<.5f&&t.reserved.w<-.5f)limit=max(limit,min(600.f,length(t.reserved.xyz)+60.f));}
    return limit;
}
uint MorphSpread(uint n){n&=1023;n=(n|(n<<16))&0x030000ff;n=(n|(n<<8))&0x0300f00f;n=(n|(n<<4))&0x030c30c3;n=(n|(n<<2))&0x09249249;return n;}
uint MorphKey(float3 p){uint3 q=(uint3)(clamp((p+12)/24,0,.99999)*1024);return MorphSpread(q.x)|(MorphSpread(q.y)<<1)|(MorphSpread(q.z)<<2);}
float3 MorphLocal(float3 p,MorphTarget t){float3 d=p-t.origin;return float3(dot(d,float3(t.forward.z,0,-t.forward.x)),d.y,dot(d,float3(t.forward.x,0,t.forward.z)));}
float3 MorphWorld(float3 p,MorphTarget t){return t.origin+float3(t.forward.z,0,-t.forward.x)*p.x+float3(0,p.y,0)+float3(t.forward.x,0,t.forward.z)*p.z;}
// A symmetric, volume-preserving horizontal deformation in world space.
// It relaxes the old extension and grows the new one; face yaw cannot swing
// a long containment field sideways or bend its rear into a hooked tail.
float3 MorphFlowMatrix(MorphTarget t){
    float2 q=t.reserved.xy;float magnitude=length(q);q*=min(1.f,.2f/max(magnitude,1e-6f));
    float c=t.forward.z,s=t.forward.x;
    float xx=q.x*(c*c-s*s)-2*q.y*c*s,xz=2*q.x*c*s+q.y*(c*c-s*s);
    return float3(1+xx,xz,1-xx)/sqrt(max(.9f,1-dot(q,q)));
}
float3 MorphField(float3 local,MorphTarget t){float3 m=MorphFlowMatrix(t);
    return float3(m.z*local.x-m.y*local.z,local.y,-m.y*local.x+m.x*local.z);}
float3 MorphFromField(float3 field,MorphTarget t){float3 m=MorphFlowMatrix(t);
    return float3(m.x*field.x+m.y*field.z,field.y,m.y*field.x+m.z*field.z);}
float3 MorphFieldNormal(float3 field,MorphTarget t){
    float3 m=MorphFlowMatrix(t),g=field/max(t.shape.xyz*t.shape.xyz,.01f);
    float3 gradient=float3(m.z*g.x-m.y*g.z,g.y,-m.y*g.x+m.x*g.z);
    float3 n=normalize(gradient+float3(0,1e-6f,0));return float3(t.forward.z,0,-t.forward.x)*n.x+float3(0,n.y,0)+float3(t.forward.x,0,t.forward.z)*n.z;
}
[numthreads(64,1,1)]
void MorphKeys(uint3 id:SV_DispatchThreadID){
    uint i=id.x;if(i>=8192)return;MorphEntry e=(MorphEntry)0;e.key=0xffffffff;e.owner=0xffffffff;e.source=i;
    if(i<(uint)pad6.z){MorphTarget t=MorphTargets[i];bool valid=Particles[i].color.a>.01f&&Particles[i].position.y>-500;
        e.start=valid?MorphLocal(Particles[i].position,t):t.local;e.owner=(uint)t.forward.y;
        e.key=MorphKey(e.start);e.previous=valid?Particles[i].position:MorphWorld(t.local,t);}
    MorphData[i]=e;
}
[numthreads(64,1,1)]
void MorphSort(uint3 id:SV_DispatchThreadID){
    uint i=id.x,j=i^emitCursor;if(i>=8192||j<=i)return;MorphEntry a=MorphData[i],b=MorphData[j];
    bool greater=a.owner>b.owner||(a.owner==b.owner&&(a.key>b.key||(a.key==b.key&&a.source>b.source)));bool ascending=(i&emitCount)==0;
    if(greater==ascending){MorphData[i]=b;MorphData[j]=a;}
}
[numthreads(64,1,1)]
void MorphAssign(uint3 id:SV_DispatchThreadID){uint i=id.x;if(i>=(uint)pad6.z)return;MorphEntry e=MorphData[i];if(((uint)emitType&(1u<<e.owner))==0)return;e.target=i;
    if(pad6.x>.5f){MorphTarget t=MorphTargets[i];Particle p=Particles[e.source];p.position=MorphWorld(t.local,t);p.velocity=0;p.color=t.color;p.type=0;Particles[e.source]=p;e.start=t.local;e.previous=p.position;}
    MorphData[8192+e.source]=e;}
// Must match Renderer::kFluidMorphBodyCount. Owner stays separate from the 30-bit spatial key.
[numthreads(16,1,1)]
void MorphMeasure(uint3 id:SV_DispatchThreadID){
    uint owner=id.x;float sum=0,maximum=0,count=0;
    for(uint i=0;i<(uint)pad6.z;++i){MorphEntry e=MorphData[8192+i];MorphTarget t=MorphTargets[e.target];if((uint)t.forward.y!=owner)continue;
        float error=length(Particles[i].position-MorphWorld(t.local,t));sum+=error*error;maximum=max(maximum,error);count+=1;}
    MorphEntry result=(MorphEntry)0;result.start=float3(count>0?sqrt(sum/count):1000,maximum,count);MorphData[16000+owner]=result;
}
float3 MorphIntegrate(uint original,float3 position,float3 velocity,float3 acceleration,float step){
    MorphEntry entry=MorphData[8192+original];MorphTarget t=MorphTargets[entry.target];
    if(t.shape.w>.5f){
        // No per-particle destination in the ordinary liquid body. A compliant
        // boundary supports the volume while PBF and viscosity redistribute it.
        float3 local=MorphLocal(position,t),field=MorphField(local,t);
        float3 radii=max(t.shape.xyz,.1f);float extent=length(field/radii);
        float3 normal=MorphFieldNormal(field,t);
        float3 confinement=-normal*min(100.f,t.omega*max(extent-.86f,0));
        confinement.y+=lerp(18.f,20.f,t.motion.w);
        float3 core=t.origin+float3(t.reserved.z,t.shape.y*.43f,t.reserved.w);
        float3 coreDelta=position-core;
        float3 coreRange=max(float3(t.shape.x*.5f,t.shape.y*.9f,t.shape.z*.5f),.2f);
        float influence=exp(-2*dot(coreDelta/coreRange,coreDelta/coreRange));
        float drive=28*influence;
        // The core responds first; exterior particles retain their inertia.
        // Damping is relative to the bulk flow, not the stationary world: world
        // drag slowed the leading surface into a thin sheet as the core advanced.
        // This changes forces only; particles never receive rigid translation.
        float3 desired=float3(t.motion.x,t.motion.y*(1-lerp(.9f,.65f,t.motion.w)),t.motion.z);
        float damping=max(.1f,t.material.x)*lerp(1.f,.65f,t.motion.w);
        return (velocity+step*(acceleration+confinement+(damping+drive)*desired))/(1+(damping+drive)*step);
    }
    float3 goal=MorphWorld(lerp(entry.start,t.local,saturate(t.blend)),t);
    // A weapon uploads its point velocity for the whole frame. Recomputing it
    // from a frame-constant goal loses motion on all but the first GPU substep.
    float3 targetVelocity=t.reserved.w<-.5f?t.reserved.xyz:clamp((goal-entry.previous)/max(step,1e-5),-30,30);
    entry.previous=goal;MorphData[8192+original]=entry;
    if(t.reserved.w<-.5f)goal-=targetVelocity*max(emitColor.x,0);
    float w=t.omega,k=w*w,c=2*w;
    // Implicit spring, relative damping; integrate all external acceleration once.
    return (velocity+step*(acceleration+k*(goal-position)+c*targetVelocity))/(1+c*step+k*step*step);
}
