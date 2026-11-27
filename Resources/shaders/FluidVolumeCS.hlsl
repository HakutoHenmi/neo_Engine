#include "FluidVolumeCommon.hlsli"
#include "FluidFarWater.hlsli"
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

// この入口は t0 を BuildShapes / SplatDensity と共有する。各 PSO は 2 つの宣言の
// どちらか一方だけを、それぞれの構造化バッファの間隔で使う。
struct FluidTendril { float3 start;float radius;float3 end;float endRadius; };
StructuredBuffer<FluidTendril> tendrils : register(t0);
[numthreads(64,1,1)]
void SplatTendrils(uint3 id : SV_DispatchThreadID) {
    if(id.x>=particleCount)return;
    FluidTendril arm=tendrils[id.x];
    // 終わりの半径が負なら箱の外皮の印: start が中心、end が半分の
    // 大きさ、radius が向き、-endRadius が外皮の厚み。飲み込んだ物は
    // 自分の形で体の表面を押し出す。
    if(arm.endRadius<0){
        float pad=max(-arm.endRadius*1.5f,voxelSize*1.65f),reach=length(arm.end)+pad,c=cos(arm.radius),s=sin(arm.radius);
        int3 low=max(int3(0,0,0),(int3)floor((arm.start-reach-volumeOrigin)/voxelSize-.5f));
        int3 high=min((int3)volumeSize-1,(int3)ceil((arm.start+reach-volumeOrigin)/voxelSize-.5f));
        [loop]for(int bz=low.z;bz<=high.z;++bz)[loop]for(int by=low.y;by<=high.y;++by)[loop]for(int bx=low.x;bx<=high.x;++bx){
            uint3 cell=uint3(bx,by,bz);float3 d=volumeOrigin+(cell+.5f)*voxelSize-arm.start;
            float3 q=max(abs(float3(d.x*c-d.z*s,d.y,d.x*s+d.z*c))-arm.end,0);
            float w=saturate(1-dot(q,q)/(pad*pad));
            InterlockedMax(densityAccum[VolumeIndex(cell)].x,(uint)round(1.4f*w*w*4096.f));
        }
        return;
    }
    float support=max(max(arm.radius,arm.endRadius)*1.5f,voxelSize*1.65f);
    int3 lo=max(int3(0,0,0),(int3)floor((min(arm.start,arm.end)-support-volumeOrigin)/voxelSize-.5f));
    int3 hi=min((int3)volumeSize-1,(int3)ceil((max(arm.start,arm.end)+support-volumeOrigin)/voxelSize-.5f));
    float3 segment=arm.end-arm.start;
    float squared=max(dot(segment,segment),1e-8f);
    [loop]for(int z=lo.z;z<=hi.z;++z)[loop]for(int y=lo.y;y<=hi.y;++y)[loop]for(int x=lo.x;x<=hi.x;++x){
        uint3 cell=uint3(x,y,z);float3 world=volumeOrigin+(cell+.5f)*voxelSize;
        float along=saturate(dot(world-arm.start,segment)/squared);
        float radius=max(lerp(arm.radius,arm.endRadius,along)*1.5f,voxelSize*1.65f);
        float3 delta=world-lerp(arm.start,arm.end,along);
        float w=saturate(1-dot(delta,delta)/(radius*radius));
        // 合計ではなく和集合にすることで、関節や重なった腕が膨らまない。
        // 共通のフィルター・レイマーチ・影・屈折がこの後に続く。
        uint value=(uint)round(1.4f*w*w*4096.f);
        InterlockedMax(densityAccum[VolumeIndex(cell)].x,value);
    }
}

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
    // 坂の町の放水: 消火栓の水は画面の上で面を作って描く (FluidScreenWater.hlsl)。体積へは入れず (密度 0)、
    // 霧と水しぶきの粒だけが使う形を置く。近くの粒を探す計算もしない
    // 坂の町の放水の描き方 (filterAxis の端数): .5 = 画面の上の面 (FluidScreenWater)、.25 = 箱庭のデモと同じふつうの水。
    // ふつうの水では、消火栓の水だけの形 (速い筋・薄く広い膜) を使わず、たまる水と同じ形で描く
    float hoseMode=filterAxis<0?frac(-filterAxis):0;
    bool hydrant=p.type>1.05f&&p.type<1.25f,hosePlain=hoseMode>.15f&&hoseMode<.35f;
    // ふつうの水の描き方でも、細かい升の窓のふち (2 升) より外の粒は画面の上の面 (FluidScreenWater の outsideOnly) が描く
    float3 windowMargin=min(p.position-volumeOrigin,volumeOrigin+voxelSize*volumeSize-p.position);
    bool outsideWindow=hosePlain&&min(windowMargin.x,min(windowMargin.y,windowMargin.z))<=2*voxelSize;
    if(hydrant&&(hoseMode>.4f||outsideWindow)){
        shape.row0=float4(2,0,0,p.position.x);shape.row1=float4(0,2,0,p.position.y);shape.row2=float4(0,0,2,p.position.z);
        shape.info=float4(0,.5f,0,1);shapes[i]=shape;return;
    }
    if(p.type>=8.0f){
        // 重なった異方性のカーネルが 1 つの連続した液体の密度の面になる。
        // プレイヤーのレイマーチが同じ法線と光学的な性質を与える。
        float width=max(.35f,p.pad.x),height=max(.28f,p.pad.y);
        shape.row0=float4(1/width,0,0,p.position.x);shape.row1=float4(0,1/height,0,p.position.y);shape.row2=float4(0,0,1/width,p.position.z);
        shape.info=float4(3.2f*p.color.a,width,0,1);shapes[i]=shape;return;
    }
    if(p.type>=4.0f&&p.type<8.0f&&p.pad.y>0){
        // 大きさ指定の放物線を描くスライム: 体が大きくなるとボクセルが粗くなるので、
        // ボクセルより小さい決まった大きさのしずくは消えてしまう。カーネルは体の倍率に従う。
        // しずくは寿命の終わりにかけて薄くなる。
        float life=max(.02f,(p.type-4)*10),fade=1-.6f*smoothstep(.5f,1.f,p.pad.x/life);
        float radius=max(.6f*p.pad.y*fade,voxelSize*1.05f);
        float3 axes=radius,along=float3(0,0,1),across=float3(1,0,0),up=float3(0,1,0);
        if(p.pad.z>.5f)axes=float3(radius*1.7f,radius*.35f,radius*1.7f); // 着地済み: 平たい跡
        else{float speed=length(p.velocity);
            // 空中のしずくは本物のしずくのように動きの向きに伸びる。
            if(speed>.5f){along=p.velocity/speed;across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));up=cross(along,across);
                axes=float3(radius*.9f,radius*.9f,radius*(1+clamp(speed*.035f,0,.8f)));}}
        shape.row0=float4(across/axes.x,p.position.x);
        shape.row1=float4(up/axes.y,p.position.y);
        shape.row2=float4(along/axes.z,p.position.z);
        shape.info=float4(saturate(p.color.a)*lerp(.65f,1.f,fade),max(axes.x,max(axes.y,axes.z)),0,1);shapes[i]=shape;return;
    }
    bool water=VolumePhase(p.type)==2;
    bool morph=p.type<.5f&&p.pad.x>=1000&&p.pad.x<1016&&p.pad.y>0;
    if(morph){
        // どの大きさでも粒子あたりの作業量は一定。まとまった等方的なカーネルで、
        // 大きな体に隣が出入りしても PCA の稜線が回らないようにする。
        float spacing=pow(275.f/max(p.pad.y,1e-6f),1.f/3.f);
        float radius=max(.8f*spacing,voxelSize*1.25f);
        shape.row0=float4(1/radius,0,0,p.position.x);
        shape.row1=float4(0,1/radius,0,p.position.y);
        shape.row2=float4(0,0,1/radius,p.position.z);
        float amplitude=(315.f/(64.f*3.14159265f))/(p.pad.y*radius*radius*radius);
        shape.info=float4(amplitude*saturate(p.color.a),radius,0,1);shapes[i]=shape;return;
    }
    bool supported=water && p.pad.z>0 &&
        abs(p.position.y-p.pad.y)<0.35f && abs(p.velocity.y)<2.5f;
    // (速い筋は、ふつうの水の描き方でも筋の形を使う。まわりの粒から形を決めると、まばらな空中の水が霧だけになって消え、重くもなる)
    if(hydrant&&!supported&&p.pad.x<.12f&&dot(p.velocity,p.velocity)>16){
        // 速い噴流が、ボクセルより小さい PCA の面と小さな孤立したしぶきの間で
        // 切り替わってはいけない。速度に沿って重なったカーネルが、まばらな先端や
        // 近いボリュームの LOD を含め、実際にシミュレーションした流れを再構成する。
        float speed=length(p.velocity);float3 along=p.velocity/speed;
        float3 across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));
        // pad.y はまだ何にも当たっていない水だけがノズルの幅。当たった後は地面の高さ (坂の町では 100 を超える) なので使わない
        float nozzle=p.pad.x<=0?p.pad.y:0;
        float radius=max(max(.48f,nozzle*.65f),voxelSize*1.65f);
        float axial=max(radius,clamp(speed/60,.8f,1.5f));
        shape.row0=float4(across/radius,p.position.x);
        shape.row1=float4(cross(along,across)/radius,p.position.y);
        shape.row2=float4(along/axial,p.position.z);
        shape.info=float4(1.1f*saturate(p.color.a),max(radius,axial),0,1);shapes[i]=shape;return;
    }
    float sum=0;
    float3 mean=0;
    float3x3 moment=(float3x3)0;
    uint neighbours=0;
    const float gridCell=.4f; // ソルバーの SMOOTHING_RADIUS のセルに合わせる (filterAxis がバケット数を運ぶ)。
    int3 cell=(int3)floor(p.position/gridCell);
    const float searchRadius=1.1f;
    float nearestDistance=searchRadius;
    float3 localPosition=p.position-(float3)cell*gridCell;
    [loop] for(int z=-3;z<=3;++z) [loop] for(int y=-3;y<=3;++y) [loop] for(int x=-3;x<=3;++x) {
        // 一番近い点がカーネルの外にあるセルは寄与できない。
        // ハッシュの格子を読む前に捨てる (遠い LOD でも)。
        float3 relativeMin=float3(x,y,z)*gridCell-localPosition;
        float3 outside=max(max(relativeMin,-relativeMin-gridCell),0.0f);
        if(dot(outside,outside)>=searchRadius*searchRadius) continue;
        int3 query=cell+int3(x,y,z);
        uint h=VolumeHash(query), end=min(gridOffset[h]+gridCount[h],particleCount);
        [loop] for(uint j=gridOffset[h];j<end;++j) {
            VolumeParticle q=particles[j];
            if (VolumePhase(q.type)!=VolumePhase(p.type) || any((int3)floor(q.position/gridCell)!=query)) continue;
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
    // ボクセル解像度での制限の前に、異方性を 4:1 までに抑える。
    float3 axes=sqrt(max(variance,maxVariance*0.0625f));
    axes/=pow(axes.x*axes.y*axes.z,1.0f/3.0f);
    bool spray=water && !supported && neighbours==0;
    axes=clamp(axes*.65f,max(voxelSize*1.25f,0.28f),1.1f);
    float3 center=p.position+mean*0.2f;
    // 列の固有ベクトルが、逆の楕円体の変換の行になる。
    float3x3 transform=transpose(vectors);
    // 接触した水は膜であり、空中のしずくではない。体積を保つ。
    if (supported) {
        float radius=neighbours>0 ? clamp(nearestDistance*1.8f,0.65f,1.1f) : 0.5f;
        // 広がる消火栓の膜は薄く連続した膜: 広く平たいカーネルでつなげ、
        // まばらな外れは玉にならずに薄くなっていく。
        if(hydrant&&!hosePlain)radius=neighbours>0?clamp(nearestDistance*3.f,1.2f,2.f):.7f;
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
    // ボクセルより薄い膜はサンプリングで消えてしまう。支えのある膜には
    // ボクセル 1 個分の再構成の広がりを与える。シミュレーションの質量は変えない。
    // 消火栓の膜は実際の水の体積を保たなければならない。隣との距離から体積を
    // 推定すると、まばらなしぶきが広がるにつれて大きくなった。
    if (supported && neighbours>0 && !(hydrant&&!hosePlain))
        volume=max(volume,min(nearestDistance,0.65f)*min(nearestDistance,0.65f)*voxelSize);
    float amplitude=(315.0f/(64.0f*3.14159265f))*volume/(axes.x*axes.y*axes.z);
    // 離れた小さなスライムの群れも、ゲーム上は見える質量を表す。
    if (!water && neighbours<3) amplitude=max(amplitude,0.7f);
    // 排水された通りの水は、急に消えずに最後の数秒で薄くなっていく。
    if (supported && p.type>1.05f&&p.type<1.25f) amplitude*=1-smoothstep(1.5f,4.5f,p.pad.x);
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
    // 各行は直交する逆の軸。薄い膜のまわりの最大半径の立方体を調べる代わりに、
    // 正確な世界の AABB を求め直す。
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
        if (w<=0) continue; // 勢いを含めて、寄与を正確に 0 にする。
        float density=s.info.x*w*w*w;
        uint value=(uint)round(min(density,16.0f)*4096.0f);
        uint index=VolumeIndex(cell);
        // 成分を明示すれば SM5 でコンパイルでき、他のチャンネルと競合しない。
        if(phase==0) InterlockedAdd(densityAccum[index].x,value);
        else if(phase==1) InterlockedAdd(densityAccum[index].y,value);
        else InterlockedAdd(densityAccum[index].z,value);
        if(particle.type>=8.0f)InterlockedAdd(densityAccum[index].w,value);
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
    outputDensity[p]=float4(densityAccum[index])/4096.0f;
    int4 momentum=momentumAccum[index];
    outputVelocity[p]=float4(float3(momentum.xyz)/max(momentum.w,1),momentum.w>0?1.f:0.f);
}

float4 ReadDensity(int3 p) {
    // 端は 0 にする (端の値で延ばさない): 切り取った領域に液体の壁が育ってはいけない。
    if(any(p<0) || any(p>=(int3)volumeSize)) return 0;
    return inputDensity.Load(int4(p,0));
}
[numthreads(4,4,4)]
void SmoothDensity(uint3 p : SV_DispatchThreadID) {
    if(any(p>=volumeSize)) return;
    int3 step=filterAxis<0.5f ? int3(1,0,0) : (filterAxis<1.5f ? int3(0,1,0) : int3(0,0,1));
    // 質量を保つ小さな 3D フィルター。大まかな形はぼかしではなく PCA から得る。
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
        // 材質ごとに形のつながりの変化を捨て、履歴を新しい支えの範囲に抑える。
        // 消えた流体は常に 0: 履歴だけの幽霊や太い軌跡は残さない。
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
    // 周囲の余白で、ブロックのすべての面での 3 線形補間を覆う。
    [loop] for(int z=-1;z<=4;++z) [loop] for(int y=-1;y<=4;++y) [loop] for(int x=-1;x<=4;++x)
        maximum=max(maximum,ReadDensity((int3)block*4+int3(x,y,z)));
    outputDensity[block]=maximum;
}

// 水だけの粗い再構成。孤立したしぶきはしずくのパスに残す。
[numthreads(64,1,1)]
void SplatFarWater(uint3 id:SV_DispatchThreadID){
    uint i=id.x;if(i>=particleCount)return;
    VolumeParticle p=particles[i];FluidShape s=shapes[i];
    if(VolumePhase(p.type)!=2||s.info.w==0||s.info.x<=0)return;
    float3 spacing=FarWaterSpacing(voxelSize),center=ShapeCenter(s);
    bool sheet=p.pad.z>0&&abs(p.position.y-p.pad.y)<.35f&&abs(p.velocity.y)<2.5f;
    float3 lengths=float3(1/length(s.row0.xyz),1/length(s.row1.xyz),1/length(s.row2.xyz));
    float3 axes=max(lengths,sheet?float3(voxelSize*1.35f,spacing.y,voxelSize*1.35f):voxelSize*.9f);
    float amplitude;
    if(sheet){
        // 粗いセルは再構成の広がりが大きいだけで、水が増えるわけではない。
        // 粒子ごとに密度の下限を設けると、着地したしぶきの数サンプルが
        // 実際の流れから離れた 1 m 幅の水たまりになってしまう。
        float volume=FluidReconstructionVolume(p.type,p.density);
        amplitude=(315.f/(64.f*3.14159265f))*volume*saturate(p.color.a)/(axes.x*axes.y*axes.z);
    }else{
        // 速い空中の噴流には、サンプルの間をボクセル未満で覆う範囲がまだ要る。
        amplitude=max(s.info.x*(lengths.x*lengths.y*lengths.z)/(axes.x*axes.y*axes.z),.7f*saturate(p.color.a));
    }
    // 細かいボリュームと同じ排水の薄れ方に従う。
    if(sheet&&p.type>1.05f&&p.type<1.25f)amplitude*=1-smoothstep(1.5f,4.5f,p.pad.x);
    s.row0.xyz=normalize(s.row0.xyz)/axes.x;s.row1.xyz=normalize(s.row1.xyz)/axes.y;s.row2.xyz=normalize(s.row2.xyz)/axes.z;
    float3 a0=s.row0.xyz*axes.x*axes.x,a1=s.row1.xyz*axes.y*axes.y,a2=s.row2.xyz*axes.z*axes.z;
    float3 extent=sqrt(a0*a0+a1*a1+a2*a2);
    int3 lo=max(0,(int3)floor((center-extent-volumeOrigin)/spacing-.5f));
    int3 hi=min((int3)volumeSize-1,(int3)ceil((center+extent-volumeOrigin)/spacing-.5f));
    [loop]for(int z=lo.z;z<=hi.z;++z)[loop]for(int y=lo.y;y<=hi.y;++y)[loop]for(int x=lo.x;x<=hi.x;++x){
        uint3 cell=uint3(x,y,z);float3 d=ShapePoint(s,volumeOrigin+(cell+.5f)*spacing-center);
        float w=saturate(1-dot(d,d));if(w<=0)continue;
        float density=amplitude*w*w*w;uint index=VolumeIndex(cell);
        InterlockedAdd(densityAccum[index].z,(uint)round(min(density,16.f)*4096.f));
        int weight=(int)round(min(density,1.f)*128.f);if(weight==0)continue;
        int3 momentum=(int3)round(clamp(p.velocity,-30,30)*weight);
        InterlockedAdd(momentumAccum[index].x,momentum.x);InterlockedAdd(momentumAccum[index].y,momentum.y);
        InterlockedAdd(momentumAccum[index].z,momentum.z);InterlockedAdd(momentumAccum[index].w,weight);
    }
}
