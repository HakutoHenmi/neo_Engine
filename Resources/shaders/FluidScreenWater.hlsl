// FluidScreenWater.hlsl
// 坂の町の放水の水を、画面の上でなめらかな水の面にして描く (Renderer::DrawScreenWater)。
// 体積の升は放水の届く広さ (数十 m) まで広げると 1 升が大きくなり、水の筋が粗いかたまりになる。
// ここでは粒を画面の上の小さな球として描き、近さ (奥行き) をなめらかにしてから面の向きを求めるので、
// 遠くへ撃っても近くで見ても、スライムと同じように映り込み・屈折・泡のある水の面になる。
//   WaterSpriteVS/PS : 粒を球として描く。RT0 に近さ (1/奥行き) の最大、RT1 に厚みと速さを足し合わせる
//   WaterSmoothPS    : 奥行きを縦と横に分けてなめらかにする (奥行きの差が大きい所はぼかさない)
//   WaterCompositePS : なめらかにした奥行きから面の向きを求め、水の色を付けて画面へ重ねる
#define FLUID_VOLUME_CORE
#include "FluidVolumeCommon.hlsli"
cbuffer Camera : register(b0) {
    row_major float4x4 view;
    row_major float4x4 projection;
    row_major float4x4 viewProjection;
    float3 cameraPosition; float time;
};
// 段ごとの値: なめらかにする向き (画素)・粒の大きさの倍率・最初の粒の番号・なめらかにする最初の段か
cbuffer ScreenWaterPass : register(b2) {
    float2 smoothAxis; float sizeScale; uint firstParticle;
    uint particleSlots; float firstSmooth; float outsideOnly; float unusedPass;
};
StructuredBuffer<VolumeParticle> particles : register(t0);
Texture2D<float4> sceneColor : register(t1);
Texture2D<float> sceneDepth : register(t2);
TextureCube<float4> environmentMap : register(t3);
Texture2D<float> fluidDepth : register(t4);
Texture2D<float> waterNear : register(t5);
Texture2D<float4> waterThick : register(t6);
Texture2D<float> waterDepth : register(t7);
SamplerState linearClamp : register(s0);

static const float kWaterSize=.7f;    // 空中の粒の球の半径 (ゲームの単位)
static const float kSheetSize=1.05f;  // 地面を流れる粒の球の半径 (平たくする)
static const float kSheetFlat=.32f;   // 地面を流れる粒の高さの割合

bool HoseWater(VolumeParticle p){return p.type>1.05f&&p.type<1.25f&&p.color.a>=.01f&&p.position.y>-500;}
bool WaterSheet(VolumeParticle p){return p.pad.z>0&&abs(p.position.y-p.pad.y)<.35f&&abs(p.velocity.y)<2.5f;}
float ViewDepth(int2 pixel){
    uint w,h;sceneDepth.GetDimensions(w,h);
    float z=sceneDepth.Load(int3(clamp(pixel,0,int2(w,h)-1),0));
    return projection[3][2]/(z-projection[2][2]);
}
// 画素を通る視線 (ビュー空間)。DLSS の揺らし (projection[2].xy) を外す (FluidRaymarch.hlsl と同じ)
float3 ViewRayFromUV(float2 uv){
    float2 ndc=uv*float2(2,-2)+float2(-1,1)-float2(projection[2][0],projection[2][1]);
    return normalize(float3(ndc/float2(projection[0][0],projection[1][1]),1));
}
float3 ToWorldDirection(float3 v){return mul(v,transpose((float3x3)view));}

// ---- 粒を球として描く ----
struct SpriteOut {
    float4 position:SV_POSITION;
    float2 local:TEXCOORD0;
    float3 centerView:TEXCOORD1;
    float radius:TEXCOORD2;
    float flat:TEXCOORD3;
    float amount:TEXCOORD4;
    float3 velocity:TEXCOORD5;
};
SpriteOut WaterSpriteVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
    SpriteOut o=(SpriteOut)0;
    float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    o.local=corners[vertex];
    o.position=float4(2,2,2,1);
    if(instance>=particleSlots)return o;
    VolumeParticle p=particles[firstParticle+instance];
    if(!HoseWater(p)||!all(isfinite(p.position)))return o;
    // 体積 (細かい升の窓) で描いている水は描かない。窓のふち (2 升) からはこちらへ受け渡す
    if(outsideOnly>.5f){float3 margin=min(p.position-volumeOrigin,volumeOrigin+voxelSize*volumeSize-p.position);
        if(min(margin.x,min(margin.y,margin.z))>2*voxelSize)return o;}
    bool sheet=WaterSheet(p);
    // 地面に落ちた水は、流れきる最後の数秒で薄く小さくなって消える (体積の時と同じ時間)
    float fade=sheet?1-smoothstep(1.5f,4.5f,p.pad.x):1;
    if(fade<=.01f)return o;
    o.radius=(sheet?kSheetSize*lerp(.45f,1,fade):kWaterSize)*sizeScale;
    o.flat=sheet?kSheetFlat:1;
    o.amount=fade*saturate(p.color.a);
    o.velocity=p.velocity;
    o.centerView=mul(float4(p.position,1),view).xyz;
    if(o.centerView.z<.1f)return o;
    o.position=mul(float4(o.centerView+float3(o.local*o.radius,0),1),projection);
    return o;
}
struct SpriteTargets {float near:SV_Target0;float4 thick:SV_Target1;};
SpriteTargets WaterSpritePS(SpriteOut input){
    float r2=dot(input.local,input.local);
    if(r2>=1)discard;
    float bulge=sqrt(1-r2);
    // 平たい粒は奥行きの膨らみも小さい (上から見た地面の水が板になる)
    float z=input.centerView.z-bulge*input.radius*input.flat;
    if(z<.05f||z>ViewDepth(int2(input.position.xy))+.05f)discard;
    SpriteTargets o;
    o.near=1/z;
    // 厚み: 球を通る長さ。ふちほど薄い (ガウスに近い山)
    float thickness=2*bulge*input.radius*input.flat*input.amount;
    o.thick=float4(input.velocity*thickness,thickness);
    return o;
}

// ---- 奥行きをなめらかにする (縦・横に分けた両側フィルタ) ----
struct FullscreenIn {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
float ReadWaterDepth(int2 pixel,int2 size){
    if(any(pixel<0)||any(pixel>=size))return 0;
    if(firstSmooth>.5f){float near=waterNear.Load(int3(pixel,0));return near>0?1/near:0;}
    return waterDepth.Load(int3(pixel,0));
}
float WaterSmoothPS(FullscreenIn input):SV_Target{
    uint w,h;waterNear.GetDimensions(w,h);int2 size=int2(w,h);
    int2 pixel=int2(input.position.xy);
    float z0=ReadWaterDepth(pixel,size);
    if(z0<=0)return 0;
    // ぼかす幅: 世界で粒 2 つ分ほど (遠いほど画素は少なく)。重さを抑えるため最大 32 画素、取る点は 29 個まで
    float pixels=kWaterSize*2.2f*sizeScale*projection[1][1]*.5f*float(h)/z0;
    float radius=clamp(pixels,1,32);
    int taps=int(ceil(radius));int stride=max(1,taps/14);
    float sigma=radius*.5f+.5f;
    // 奥行きの差がこれより大きい点は別の面 (手前の筋と奥の地面など) として混ぜない
    float range=kWaterSize*1.2f*sizeScale;
    float sum=0,weights=0;
    [loop]for(int i=-taps;i<=taps;i+=stride){
        float z=ReadWaterDepth(pixel+int2(smoothAxis*i),size);
        if(z<=0)continue;
        float d=(z-z0)/range;
        float weight=exp(-.5f*i*i/(sigma*sigma))*exp(-d*d);
        sum+=z*weight;weights+=weight;
    }
    return weights>0?sum/weights:z0;
}

// ---- 水の面に色を付けて重ねる ----
float3 Reflection(float3 normal,float3 ray,float roughness){
    float3 r=reflect(ray,normal);
    uint w,h,levels;environmentMap.GetDimensions(0,w,h,levels);
    float3 env=environmentMap.SampleLevel(linearClamp,r,roughness*max((float)levels-1,0)).rgb;
    float3 sky=lerp(float3(.035f,.055f,.075f),float3(.3f,.43f,.55f),saturate(r.y*.5f+.5f));
    return max(env,sky*.3f);
}
float3 SunTravel(){return sunDirection.w>0?sunDirection.xyz:normalize(float3(.45f,-.8f,.35f));}
float SunIntensity(){return sunDirection.w>0?sunDirection.w:1;}
float WaterHash(float3 p){p=frac(p*float3(.1031f,.1030f,.0973f));p+=dot(p,p.yxz+33.33f);return frac((p.x+p.y)*p.z);}
float WaterNoise(float3 p){
    float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    float a=lerp(WaterHash(i),WaterHash(i+float3(1,0,0)),f.x),b=lerp(WaterHash(i+float3(0,1,0)),WaterHash(i+float3(1,1,0)),f.x);
    float c=lerp(WaterHash(i+float3(0,0,1)),WaterHash(i+float3(1,0,1)),f.x),d=lerp(WaterHash(i+float3(0,1,1)),WaterHash(i+1),f.x);
    return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
}
float WaterFbm(float3 p){return WaterNoise(p)*.55f+WaterNoise(p*2.03f+11.1f)*.3f+WaterNoise(p*4.11f+23.7f)*.15f;}
// 速い水は空気を巻き込んで白くなる。x: 表面の白い筋、y: 中の白いにごり (FluidRaymarch.hlsl の WaterAeration と同じ考え)
float2 WaterAeration(float3 world,float3 velocity,float3 normal,float3 ray,float thickness){
    float speed=length(velocity);
    float aerated=smoothstep(6,22,speed);
    if(aerated<=.001f)return 0;
    // ノズルを出た所は透明で、遠くへ飛ぶほど砕けて白くなる
    float amount=aerated*lerp(.2f,1,smoothstep(1.5f,14,length(world-playerBodyCenter)));
    // 地面を横へ流れる水は、先の方に少し白が残るだけ
    float flowing=1-smoothstep(.2f,.6f,abs(velocity.y)/max(speed,1e-3f));
    amount*=lerp(1,.3f,flowing);
    // 体積と一緒に描くとき (坂の町の放水) は、箱庭のデモと同じ澄んだ水に合わせる
    if(outsideOnly>.5f)amount*=.15f;
    float3 along=velocity/speed;
    float3 across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));
    // (筋の細かさ: 上空から見ると細かすぎる筋はざらつきに見えるので、体積の水より少し太くする)
    float3x3 frame=float3x3(across*2.8f,cross(along,across)*2.8f,along*.6f);
    const float cycle=.4f;
    float phase=frac(time/cycle),blend=1-abs(2*phase-1);
    float3 drift=velocity*cycle;
    float streak=WaterFbm(mul(frame,world-drift*phase))*blend+WaterFbm(mul(frame,world-drift*frac(phase+.5f))+19.3f)*(1-blend);
    float grazing=pow(1-saturate(dot(normal,-ray)),1.5f);
    float low=.58f-.28f*amount-.12f*grazing;
    float skin=smoothstep(low,low+.2f,streak)*amount;
    float milk=amount*(1-exp(-thickness*1.4f));
    return float2(skin,milk);
}
float3 ViewPosition(float2 uv,float z){float3 ray=ViewRayFromUV(uv);return ray*(z/max(ray.z,1e-5f));}
float4 WaterCompositePS(FullscreenIn input):SV_Target{
    uint w,h;waterDepth.GetDimensions(w,h);int2 size=int2(w,h);
    int2 pixel=int2(input.position.xy);float2 uv=(float2(pixel)+.5f)/float2(size);
    float z=waterDepth.Load(int3(pixel,0));
    if(z<=0)discard;
    // スライムの体 (体積の面) が手前にあれば、そちらが見える
    float body=fluidDepth.Load(int3(pixel,0));
    if(body>0&&body<z-.3f)discard;
    float4 thick=waterThick.Load(int3(pixel,0));
    float thickness=thick.a;
    float3 velocity=thick.a>1e-4f?thick.rgb/thick.a:0;
    // 面の向き: となりの画素の奥行きとの差から。差の小さい側を使い、ふちで向きが崩れないようにする
    float3 center=ViewPosition(uv,z);
    float2 texel=1/float2(size);
    float zl=waterDepth.Load(int3(pixel-int2(1,0),0)),zr=waterDepth.Load(int3(pixel+int2(1,0),0));
    float zu=waterDepth.Load(int3(pixel-int2(0,1),0)),zd=waterDepth.Load(int3(pixel+int2(0,1),0));
    float3 dx=0,dy=0;
    bool left=zl>0&&abs(zl-z)<kWaterSize*2,right=zr>0&&abs(zr-z)<kWaterSize*2;
    bool up=zu>0&&abs(zu-z)<kWaterSize*2,down=zd>0&&abs(zd-z)<kWaterSize*2;
    if(right&&(!left||abs(zr-z)<abs(z-zl)))dx=ViewPosition(uv+float2(texel.x,0),zr)-center;else if(left)dx=center-ViewPosition(uv-float2(texel.x,0),zl);
    if(down&&(!up||abs(zd-z)<abs(z-zu)))dy=ViewPosition(uv+float2(0,texel.y),zd)-center;else if(up)dy=center-ViewPosition(uv-float2(0,texel.y),zu);
    float3 viewRay=ViewRayFromUV(uv);
    float3 normalView=-viewRay;
    if(dot(dx,dx)>0&&dot(dy,dy)>0){float3 n=normalize(cross(dy,dx));normalView=dot(n,viewRay)>0?-n:n;}
    float3 ray=normalize(ToWorldDirection(viewRay));
    float3 normal=normalize(ToWorldDirection(normalView));
    float3 world=cameraPosition+ray*(z/max(viewRay.z,1e-5f));
    // ふち: 厚みの薄い所 (飛び散った細い水) は薄く見せる
    float coverage=smoothstep(.04f,.45f,thickness);
    if(coverage<=.002f)discard;
    float3 tint=saturate(phaseColor[2].rgb);
    float fresnel=.0204f+(1-.0204f)*pow(1-saturate(dot(normal,-ray)),5);
    float3 transmission=exp(-((1-tint)*.55f+.035f)*thickness);
    // 屈折: 厚い所ほど後ろの景色がずれて見える (手前に物があればずらさない)
    float2 shift=normalView.xy*float2(1,-1)*min(thickness,2.5f)*.35f*projection[0][0]*.5f/max(z,1);
    float2 refracted=clamp(uv+shift,.001f,.999f);
    if(ViewDepth(int2(refracted*size))<z)refracted=uv;
    bool behindBody=body>0;
    float3 background=sceneColor.SampleLevel(linearClamp,refracted,0).rgb;
    float3 sky=Reflection(float3(0,1,0),float3(0,-1,0),.9f);
    float3 scatter=tint*(sky*.5f+.12f);
    float3 reflection=Reflection(normal,ray,.05f);
    // 日の光のきらめき (鋭い点と、少し広い照り)
    float3 toSun=-SunTravel();float3 mirror=reflect(ray,normal);
    float glint=pow(saturate(dot(mirror,toSun)),300)*5+pow(saturate(dot(mirror,toSun)),40)*.25f;
    float3 sunColor=float3(1,.97f,.9f)*SunIntensity();
    // 空気を巻き込んだ白
    float2 air=WaterAeration(world,velocity,normal,ray,thickness);
    float lit=.5f+.5f*saturate(dot(normal,toSun));
    float3 white=float3(.88f,.93f,.97f)*(sky*.55f+lit*SunIntensity()*.7f);
    float3 add=scatter*(1-transmission)*(1-fresnel)+reflection*fresnel+glint*sunColor*(1-air.x);
    if(!behindBody){
        // 後ろが景色だけなら、屈折した景色ごと塗る
        float3 color=background*transmission*(1-fresnel)+add;
        color=lerp(color,lerp(color,white*.8f,.6f),air.y*.5f);
        color=lerp(color,white,air.x*.75f);
        return float4(color*coverage,coverage);
    }
    // 後ろにスライムの体がある所は、すでに描いた体の色を透かして見せる (屈折なし)
    float through=dot(transmission,1.f/3)*(1-fresnel);
    float3 color=lerp(add,white,air.x*.75f)+white*.8f*air.y*.3f;
    float alpha=saturate(1-through*(1-air.x*.75f)*(1-air.y*.3f));
    return float4(color*coverage,alpha*coverage);
}
