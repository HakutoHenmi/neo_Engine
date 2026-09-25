#include "FluidVolumeCommon.hlsli"
StructuredBuffer<VolumeParticle> particles : register(t0);
StructuredBuffer<uint> originalIndices : register(t1);
StructuredBuffer<uint> gridCount : register(t2);
StructuredBuffer<uint> gridOffset : register(t3);
Texture3D<float4> inputDensity : register(t4);
Texture3D<float4> historyDensity : register(t5);
Texture3D<float4> velocityField : register(t6);
RWStructuredBuffer<uint4> densityAccum : register(u0);
RWStructuredBuffer<int4> momentumAccum : register(u1);
RWStructuredBuffer<FluidShape> shapes : register(u2);
RWTexture3D<float4> outputDensity : register(u3);
RWTexture3D<float4> outputVelocity : register(u4);
SamplerState linearClamp : register(s0);

[numthreads(4,4,4)]
void ClearVolume(uint3 p : SV_DispatchThreadID) {
    if (any(p>=volumeSize)) return;
    uint index=VolumeIndex(p);
    densityAccum[index]=0;
    momentumAccum[index]=0;
}

void Jacobi(inout float3x3 a, inout float3x3 eigenvectors, uint p, uint q) {
    if (abs(a[p][q]) < 1e-7f) return;
    float angle=0.5f*atan2(2.0f*a[p][q], a[q][q]-a[p][p]);
    float c=cos(angle), s=sin(angle);
    float3x3 r=float3x3(1,0,0,0,1,0,0,0,1);
    r[p][p]=c; r[q][q]=c; r[p][q]=s; r[q][p]=-s;
    a=mul(transpose(r),mul(a,r));
    eigenvectors=mul(eigenvectors,r);
}

[numthreads(64,1,1)]
void BuildShapes(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=particleCount) return;
    FluidShape shape=(FluidShape)0;
    if (originalIndices[i]==0xffffffffU) { shapes[i]=shape; return; }
    VolumeParticle p=particles[i];
    if (p.color.a<0.01f || p.position.y<-500 || !all(isfinite(p.position))) { shapes[i]=shape; return; }
    bool water=VolumePhase(p.type)==2;
    bool supported=water && (p.position.y<=0.4f ||
        (p.pad.z>0 && abs(p.position.y-p.pad.y)<0.35f && abs(p.velocity.y)<2.5f));
    float sum=0;
    float3 mean=0;
    float3x3 moment=(float3x3)0;
    uint neighbours=0;
    int3 cell=(int3)floor(p.position/0.4f);
    const float searchRadius=1.1f;
    float nearestDistance=searchRadius;
    float3 localPosition=p.position-(float3)cell*0.4f;
    [loop] for(int z=-3;z<=3;++z) [loop] for(int y=-3;y<=3;++y) [loop] for(int x=-3;x<=3;++x) {
        // A cell whose nearest point is outside the kernel cannot contribute.
        // Reject it before the hashed-grid reads, including for distant LODs.
        float3 relativeMin=float3(x,y,z)*0.4f-localPosition;
        float3 outside=max(max(relativeMin,-relativeMin-0.4f),0.0f);
        if(dot(outside,outside)>=searchRadius*searchRadius) continue;
        int3 query=cell+int3(x,y,z);
        uint h=VolumeHash(query), end=min(gridOffset[h]+gridCount[h],particleCount);
        [loop] for(uint j=gridOffset[h];j<end;++j) {
            VolumeParticle q=particles[j];
            if (VolumePhase(q.type)!=VolumePhase(p.type) || any((int3)floor(q.position/0.4f)!=query)) continue;
            float3 d=q.position-p.position;
            float r2=dot(d,d)/(searchRadius*searchRadius);
            if (r2>=1) continue;
            float w=(1-r2)*(1-r2)*(1-r2);
            sum+=w; mean+=w*d;
            if (!supported) moment+=w*float3x3(d*d.x,d*d.y,d*d.z);
            if (j!=i) { ++neighbours; nearestDistance=min(nearestDistance,length(d)); }
        }
    }
    mean/=max(sum,1e-6f);
    float3x3 covariance=moment/max(sum,1e-6f)-float3x3(mean*mean.x,mean*mean.y,mean*mean.z);
    float3x3 vectors=float3x3(1,0,0,0,1,0,0,0,1);
    [unroll] for(uint sweep=0;sweep<5 && !supported;++sweep) {
        Jacobi(covariance,vectors,0,1); Jacobi(covariance,vectors,0,2); Jacobi(covariance,vectors,1,2);
    }
    float3 variance=max(float3(covariance[0][0],covariance[1][1],covariance[2][2]),1e-5f);
    float maxVariance=max(variance.x,max(variance.y,variance.z));
    // Bound anisotropy to 4:1 before voxel-resolution clamping.
    float3 axes=sqrt(max(variance,maxVariance*0.0625f));
    axes/=pow(axes.x*axes.y*axes.z,1.0f/3.0f);
    bool spray=water && !supported && neighbours==0;
    axes=clamp(axes*0.65f,max(voxelSize*1.25f,0.28f),1.1f);
    float3 center=p.position+mean*0.2f;
    // Column eigenvectors become rows of the inverse ellipsoid transform.
    float3x3 transform=transpose(vectors);
    // Contact water is a sheet, not an airborne droplet. Preserve its volume.
    if (supported) {
        float radius=neighbours>0 ? clamp(nearestDistance*1.8f,0.65f,1.1f) : 0.5f;
        axes=float3(radius,max(voxelSize,0.25f),radius);
        transform=float3x3(1,0,0,0,1,0,0,0,1);
        center.y=p.position.y;
    } else if (water && !spray && neighbours<3 && dot(p.velocity,p.velocity)>0.01f) {
        float3 along=normalize(p.velocity);
        float3 across=normalize(cross(along,abs(along.y)<0.9f ? float3(0,1,0) : float3(1,0,0)));
        transform=float3x3(across,cross(along,across),along);
        axes=float3(0.32f,0.32f,clamp(nearestDistance*1.25f,0.65f,1.1f));
    }
    shape.row0=float4(transform[0]/axes.x,center.x);
    shape.row1=float4(transform[1]/axes.y,center.y);
    shape.row2=float4(transform[2]/axes.z,center.z);
    float volume=FluidReconstructionVolume(p.type,p.density);
    // Sub-voxel films otherwise vanish during sampling. Give supported sheets
    // a one-voxel reconstruction footprint; this does not change simulation mass.
    if (supported && neighbours>0)
        volume=max(volume,min(nearestDistance,0.65f)*min(nearestDistance,0.65f)*voxelSize);
    float amplitude=(315.0f/(64.0f*3.14159265f))*volume/(axes.x*axes.y*axes.z);
    // A tiny detached slime group still represents visible gameplay mass.
    if (!water && neighbours<3) amplitude=max(amplitude,0.7f);
    shape.info=float4(spray ? 0 : amplitude*saturate(p.color.a), max(axes.x,max(axes.y,axes.z)),
                      spray ? 0.045f : 0.0f, 1);
    shapes[i]=shape;
}

[numthreads(64,1,1)]
void SplatDensity(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=particleCount) return;
    FluidShape s=shapes[i];
    if (s.info.w==0 || s.info.x<=0) return;
    VolumeParticle particle=particles[i];
    float3 center=ShapeCenter(s);
    // Rows are orthogonal inverse axes. Recover the exact world AABB instead
    // of visiting a maximum-radius cube around a thin film.
    float3 axis0=s.row0.xyz/max(dot(s.row0.xyz,s.row0.xyz),1e-8f);
    float3 axis1=s.row1.xyz/max(dot(s.row1.xyz,s.row1.xyz),1e-8f);
    float3 axis2=s.row2.xyz/max(dot(s.row2.xyz,s.row2.xyz),1e-8f);
    float3 extent=sqrt(axis0*axis0+axis1*axis1+axis2*axis2)+1e-4f;
    int3 lo=max(int3(0,0,0),(int3)floor((center-extent-volumeOrigin)/voxelSize-0.5f));
    int3 hi=min((int3)volumeSize-1,(int3)ceil((center+extent-volumeOrigin)/voxelSize-0.5f));
    uint phase=VolumePhase(particle.type);
    [loop] for(int z=lo.z;z<=hi.z;++z) [loop] for(int y=lo.y;y<=hi.y;++y) [loop] for(int x=lo.x;x<=hi.x;++x) {
        uint3 cell=uint3(x,y,z);
        float3 d=ShapePoint(s, volumeOrigin+(cell+0.5f)*voxelSize-center);
        float w=saturate(1-dot(d,d));
        if (w<=0) continue; // Exact zero contribution, including momentum.
        float density=s.info.x*w*w*w;
        uint value=(uint)round(min(density,16.0f)*4096.0f);
        uint index=VolumeIndex(cell);
        // Explicit components compile on SM5 and never race on other channels.
        if(phase==0) InterlockedAdd(densityAccum[index].x,value);
        else if(phase==1) InterlockedAdd(densityAccum[index].y,value);
        else InterlockedAdd(densityAccum[index].z,value);
        int weight=(int)round(min(density,1.0f)*128.0f);
        if (weight==0) continue;
        int3 momentum=(int3)round(clamp(particle.velocity,-30,30)*weight);
        InterlockedAdd(momentumAccum[index].x,momentum.x);
        InterlockedAdd(momentumAccum[index].y,momentum.y);
        InterlockedAdd(momentumAccum[index].z,momentum.z);
        InterlockedAdd(momentumAccum[index].w,weight);
    }
}

[numthreads(4,4,4)]
void ResolveDensity(uint3 p : SV_DispatchThreadID) {
    if(any(p>=volumeSize)) return;
    uint index=VolumeIndex(p);
    outputDensity[p]=float4(float3(densityAccum[index].xyz)/4096.0f,0);
    int4 momentum=momentumAccum[index];
    outputVelocity[p]=float4(float3(momentum.xyz)/max(momentum.w,1),0);
}

float4 ReadDensity(int3 p) {
    // Zero border, NOT clamp: a clipped domain must not grow a wall of liquid.
    if(any(p<0) || any(p>=(int3)volumeSize)) return 0;
    return inputDensity.Load(int4(p,0));
}
[numthreads(4,4,4)]
void SmoothDensity(uint3 p : SV_DispatchThreadID) {
    if(any(p>=volumeSize)) return;
    int3 step=filterAxis<0.5f ? int3(1,0,0) : (filterAxis<1.5f ? int3(0,1,0) : int3(0,0,1));
    // Small mass-preserving 3D filter; broad shape comes from PCA, not blur.
    outputDensity[p]=ReadDensity((int3)p)*0.75f+(ReadDensity((int3)p-step)+ReadDensity((int3)p+step))*0.125f;
}
[numthreads(4,4,4)]
void TemporalDensity(uint3 p : SV_DispatchThreadID) {
    if(any(p>=volumeSize)) return;
    float4 current=ReadDensity((int3)p);
    float3 velocity=velocityField.Load(int4(p,0)).xyz;
    float3 previousWorld=volumeOrigin+(p+0.5f)*voxelSize-velocity*frameDt;
    float3 uv=(previousWorld-previousOrigin)/(voxelSize*volumeSize);
    if(historyValid>0.5f && all(uv>0) && all(uv<1)) {
        float4 history=historyDensity.SampleLevel(linearClamp,uv,0);
        // Reject topology changes per material, clamp history to fresh support.
        // Vanished fluid is always zero; no history-only ghosts or thick trails.
        float4 lo=current, hi=current;
        [unroll] for(int axis=0;axis<3;++axis) {
            int3 d=axis==0 ? int3(1,0,0) : (axis==1 ? int3(0,1,0) : int3(0,0,1));
            lo=min(lo,min(ReadDensity((int3)p-d),ReadDensity((int3)p+d)));
            hi=max(hi,max(ReadDensity((int3)p-d),ReadDensity((int3)p+d)));
        }
        float4 confidence=saturate(1-abs(history-current)/max(current*0.5f,0.03f));
        confidence *= step(0.01f,current);
        current=lerp(current,clamp(history,lo,hi),confidence*0.2f);
    }
    outputDensity[p]=current;
}

[numthreads(4,4,4)]
void BuildOccupancy(uint3 block : SV_DispatchThreadID) {
    if(any(block>=(volumeSize+3)/4)) return;
    float4 maximum=0;
    // Halo covers trilinear filtering at all block faces.
    [loop] for(int z=-1;z<=4;++z) [loop] for(int y=-1;y<=4;++y) [loop] for(int x=-1;x<=4;++x)
        maximum=max(maximum,ReadDensity((int3)block*4+int3(x,y,z)));
    outputDensity[block]=maximum;
}
