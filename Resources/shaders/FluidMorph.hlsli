// 点群の割り当ては、空間の近傍の並べ替えとは別に行う。
struct MorphTarget {float3 local;float blend;float3 origin;float density;float3 forward;float omega;float4 color;float4 material;float4 motion;float4 shape;float4 reserved;};
struct MorphEntry {float3 start;uint target;float3 previous;uint key;uint source;uint owner;float2 unused;};
StructuredBuffer<MorphTarget> MorphTargets : register(t1);
RWStructuredBuffer<MorphEntry> MorphData : register(u7);
float MorphSpeedLimit(uint original,float type){
    float limit=type>=4.f?80.f:30.f;
    if(type>1.05f&&type<1.25f)limit=96.f;
    if(type<.5f&&pad8>.5f){MorphTarget t=MorphTargets[MorphData[8192+original].target];
        // 速さは体と一緒に大きくなり、粒子の間隔も大きくなる: 世界の単位での上限だと、
        // 大きくなった体が飛びかかりやダッシュで自分の中心に置いていかれた。
        float size=max(1.f,pow(275.f/max(t.density,1e-6f),1.f/3.f));
        if(t.shape.w<.5f&&t.reserved.w<-.5f)limit=max(limit,min(600.f*size,length(t.reserved.xyz)+60.f*size));
        // 決まった上限より速く動く液体の体 (大きくなったスライム) は置いていかれてしまう。
        if(t.shape.w>.5f)limit=max(limit,min(2000.f*size,length(t.motion.xyz)*1.4f+30.f*size));}
    return limit;
}
uint MorphSpread(uint n){n&=1023;n=(n|(n<<16))&0x030000ff;n=(n|(n<<8))&0x0300f00f;n=(n|(n<<4))&0x030c30c3;n=(n|(n<<2))&0x09249249;return n;}
uint MorphKey(float3 p){uint3 q=(uint3)(clamp((p+12)/24,0,.99999)*1024);return MorphSpread(q.x)|(MorphSpread(q.y)<<1)|(MorphSpread(q.z)<<2);}
// 液体の体の足元の傾き (x: 体の右へ 1 進むごとの高さ、y: 前へ 1 進むごとの高さ)。blend にまとめてある
// (Renderer::PackLiquidBlend。100 万以上がまとめた値で、それより小さいときは傾きなし)。
// 形の点を持つ体 (変形・飛びかかり) は点そのものを傾けるので 0
float2 MorphSlope(MorphTarget t){
    if(t.shape.w<=.5f||t.blend<1000000.f)return 0;
    float n=round(t.blend-1000000.f);float right=fmod(floor(n/101),101),forward=floor(n/10201);
    return float2(right,forward)/50-1;}
// 体の座標: 坂では高さを傾きの分だけずらし (せん断)、液体の体を包む楕円体と床を坂に沿わせる
float3 MorphLocal(float3 p,MorphTarget t){float3 d=p-t.origin;
    float3 local=float3(dot(d,float3(t.forward.z,0,-t.forward.x)),d.y,dot(d,float3(t.forward.x,0,t.forward.z)));
    float2 slope=MorphSlope(t);local.y-=slope.x*local.x+slope.y*local.z;return local;}
float3 MorphWorld(float3 p,MorphTarget t){float2 slope=MorphSlope(t);
    return t.origin+float3(t.forward.z,0,-t.forward.x)*p.x+float3(0,p.y+slope.x*p.x+slope.y*p.z,0)+float3(t.forward.x,0,t.forward.z)*p.z;}
// 体の下の地面の高さ (坂では傾いた面)
float MorphGround(float3 p,MorphTarget t){float3 d=p-t.origin;float2 slope=MorphSlope(t);
    return t.material.w+slope.x*dot(d,float3(t.forward.z,0,-t.forward.x))+slope.y*dot(d,float3(t.forward.x,0,t.forward.z));}
// 世界空間での、左右対称で体積を保つ水平方向の変形。
// 古い伸びを緩め、新しい伸びを大きくする。顔の向きの回転で、長い閉じ込め場が
// 横へ振られたり、後ろが鉤状のしっぽに曲がったりしない。
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
// Renderer::kFluidMorphBodyCount と一致させる。持ち主は 30 ビットの空間キーとは別に持つ。
// 持ち主ごとに 1 グループ: 256 スレッドで粒子を分担し、グループ共有メモリで集計する
groupshared float3 morphMeasureShared[256];
[numthreads(256,1,1)]
void MorphMeasure(uint3 group:SV_GroupID,uint index:SV_GroupIndex){
    uint owner=group.x;float sum=0,maximum=0,count=0;
    for(uint i=index;i<(uint)pad6.z;i+=256){MorphEntry e=MorphData[8192+i];MorphTarget t=MorphTargets[e.target];if((uint)t.forward.y!=owner)continue;
        float error=length(Particles[i].position-MorphWorld(t.local,t));sum+=error*error;maximum=max(maximum,error);count+=1;}
    morphMeasureShared[index]=float3(sum,maximum,count);GroupMemoryBarrierWithGroupSync();
    for(uint width=128;width>0;width>>=1){
        if(index<width){float3 a=morphMeasureShared[index],b=morphMeasureShared[index+width];morphMeasureShared[index]=float3(a.x+b.x,max(a.y,b.y),a.z+b.z);}
        GroupMemoryBarrierWithGroupSync();}
    if(index==0){float3 total=morphMeasureShared[0];MorphEntry result=(MorphEntry)0;
        result.start=float3(total.z>0?sqrt(total.x/total.z):1000,total.y,total.z);MorphData[16000+owner]=result;}
}
float3 MorphIntegrate(uint original,float3 position,float3 velocity,float3 acceleration,float step){
    MorphEntry entry=MorphData[8192+original];MorphTarget t=MorphTargets[entry.target];
    if(t.shape.w>.5f){
        // 通常の液体の体には、粒子ごとの行き先は無い。柔らかい境界が体積を支え、
        // PBF と粘性がそれを配り直す。
        float3 local=MorphLocal(position,t),field=MorphField(local,t);
        float3 radii=max(t.shape.xyz,.1f);float extent=length(field/radii);
        float3 normal=MorphFieldNormal(field,t);
        // 境界の引き寄せは世界の単位での加速度: 体の大きさに合わせて大きくしないと、
        // 大きくなった体 (10〜100 倍) が自分の中心に遅れる。
        float size=max(1.f,pow(275.f/max(t.density,1e-6f),1.f/3.f));
        float3 confinement=-normal*min(100.f*size,t.omega*size*max(extent-.86f,0));
        confinement.y+=lerp(18.f,20.f,t.motion.w);
        float3 core=t.origin+float3(t.reserved.z,t.shape.y*.43f,t.reserved.w);
        float3 coreDelta=position-core;
        float3 coreRange=max(float3(t.shape.x*.5f,t.shape.y*.9f,t.shape.z*.5f),.2f);
        float influence=exp(-2*dot(coreDelta/coreRange,coreDelta/coreRange));
        float drive=28*influence;
        // 中心が先に反応し、外側の粒子は慣性を保つ。
        // 減衰は止まった世界ではなく、全体の流れに対して行う: 世界に対する抵抗だと、
        // 中心が進むにつれて先頭の面が遅れて薄い膜になった。
        // 変えるのは力だけで、粒子を剛体のように平行移動させることはない。
        float3 desired=float3(t.motion.x,t.motion.y*(1-lerp(.9f,.65f,t.motion.w)),t.motion.z);
        float damping=max(.1f,t.material.x)*lerp(1.f,.65f,t.motion.w);
        return (velocity+step*(acceleration+confinement+(damping+drive)*desired))/(1+(damping+drive)*step);
    }
    float3 goal=MorphWorld(lerp(entry.start,t.local,saturate(t.blend)),t);
    // 武器はフレーム全体の点の速度を送ってくる。フレーム中一定の目標から計算し直すと、
    // GPU の最初の段以外で動きが失われる。
    float3 targetVelocity=t.reserved.w<-.5f?t.reserved.xyz:clamp((goal-entry.previous)/max(step,1e-5),-30,30);
    entry.previous=goal;MorphData[8192+original]=entry;
    if(t.reserved.w<-.5f)goal-=targetVelocity*max(emitColor.x,0);
    // 吸い込みの姿勢 (形 0 で点の速度を送る) は柔らかいばねを使い、通常の液体と
    // 同じように動く。液体の体と同じように重さを支えないと、重力で薄い口の壁が
    // たるんで穴が開く。
    if(abs(t.shape.w)<.5f&&t.reserved.w<-.5f)acceleration.y+=18.f;
    float w=t.omega,k=w*w,c=2*w;
    // 陰的なばねと相対的な減衰。外からの加速度はまとめて 1 回だけ積分する。
    return (velocity+step*(acceleration+k*(goal-position)+c*targetVelocity))/(1+c*step+k*step*step);
}
