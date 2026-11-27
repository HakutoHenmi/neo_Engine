// スライム陣取りの床の体液 (インクマップ方式)。
// gTex (t0) は床全体を覆う低い解像度のインクマップ (R がインクの量 0..1、G がぬれたての度合い 0..1)。CPU がしぶきの形を押して作り、変わったフレームだけ送る。
//  - 縁: 量を線形補間で読み、しきい値 (少し揺らす) で切る。低い解像度でも縁はなめらかで、液体らしく丸い
//  - 厚み: 量からしずくの高さを作り、高さの傾きを法線にする (縁が盛り上がって見える)
//  - つや: 低い粗さの鏡面反射と、空の映り込み (フレネル)。部屋の明かり (点光源) も足す
//  - ぬれたて: しばらくは縁が揺れ、少しふくらみ、強く光る (約 1 秒で落ち着く)
// 色は instance の color (線形)。インクの無い所は描かない (下のラグが見える)
#include "ShadowFrustum.hlsli"
Texture2D gTex : register(t0);
Texture2D gShadowMap : register(t1);
SamplerState gSmp : register(s0);
SamplerComparisonState gShadowSmp : register(s1);
cbuffer CBFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float3 gCamPos; float gTime;
    float4 gWindParams; float3 gPlayerPos; float gFramePad;
};
struct DirLight { float3 dir; float pad0; float3 color; float pad1; uint enabled; float3 pad2; };
struct PointLight { float3 pos; float pad0; float3 color; float range; float3 atten; float pad1; uint enabled; float3 pad2; };
struct SpotLight { float3 pos; float pad0; float3 dir; float range; float3 color; float inner; float3 atten; float outer; uint enabled; float3 pad2; };
struct AreaLight { float3 pos; float pad0; float3 color; float range; float3 right; float halfWidth; float3 up; float halfHeight; float3 dir; float pad1; float3 atten; float pad2; uint enabled; float3 pad3; };
cbuffer CBLight : register(b2) {
    float3 gAmbientColor; float padA0;
    DirLight gDir[1]; PointLight gPoint[4]; SpotLight gSpot[4]; AreaLight gArea[4];
    row_major float4x4 gShadowMatrix;
};
float Shadow(float3 worldPos) {
    float4 shadowPos=mul(float4(worldPos,1),gShadowMatrix);
    float3 p=shadowPos.xyz/shadowPos.w;
    p.xy=p.xy*float2(.5f,-.5f)+.5f;
    if(any(p<0)||any(p>1))return 1;
    float coverage=0;
    [unroll] for(int x=-1;x<=1;++x) [unroll] for(int y=-1;y<=1;++y)
        coverage+=gShadowMap.SampleCmpLevelZero(gShadowSmp,p.xy+float2(x,y)/4096.0f,p.z-.0007f).r;
    return lerp(1,coverage/9.0f,ShadowFrustumWeight(p));
}
float InkHash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float InkNoise(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(InkHash(i),InkHash(i+float2(1,0)),f.x),lerp(InkHash(i+float2(0,1)),InkHash(i+float2(1,1)),f.x),f.y);}
// インクの量 (高い周波数の揺らぎを少し足して、縁を液体のように波打たせる。ぬれたての間は揺らぎが動く)
float Ink(float2 uv,float2 texel){
    float2 amount=gTex.SampleLevel(gSmp,uv,0).rg;
    float flow=gTime*1.8*amount.g;
    float wobble=InkNoise(uv/texel*.55+flow)*.6+InkNoise(uv/texel*1.7-flow*.7)*.4;
    return amount.r+(wobble-.5)*(.16+.1*amount.g);
}
float4 main(float4 svpos:SV_POSITION,float3 worldPos:TEXCOORD0,float3 normal:TEXCOORD1,float2 uv:TEXCOORD2,float4 color:COLOR0):SV_TARGET {
    uint w,h;gTex.GetDimensions(w,h);const float2 texel=1/float2(w,h);
    const float threshold=.5;
    float ink=Ink(uv,texel);
    clip(ink-threshold);
    float wet=gTex.SampleLevel(gSmp,uv,0).g;
    // 厚み: しきい値から少し上で平らになる (縁だけが盛り上がった液だまり)
    const float bump=.14;
    float height=smoothstep(threshold,threshold+bump,ink);
    float hl=smoothstep(threshold,threshold+bump,Ink(uv-float2(texel.x,0),texel)),hr=smoothstep(threshold,threshold+bump,Ink(uv+float2(texel.x,0),texel));
    float hb=smoothstep(threshold,threshold+bump,Ink(uv-float2(0,texel.y),texel)),hf=smoothstep(threshold,threshold+bump,Ink(uv+float2(0,texel.y),texel));
    // 面の向き: 床は上向き。uv の u が +x、v が +z (メッシュの作り方と同じ)。表面の細かいさざ波も少し
    float ripple=InkNoise(worldPos.xz*2.3+gTime*.25)-.5;
    const float slope=1.6+1.2*wet;
    float3 n=normalize(float3((hl-hr)*slope+ripple*.05,1,(hb-hf)*slope+ripple*.05));
    float3 light=gDir[0].enabled?normalize(-gDir[0].dir):normalize(float3(-.4f,.8f,-.3f));
    float3 daylight=gDir[0].enabled?gDir[0].color:float3(1,.9f,.8f);
    float3 view=normalize(gCamPos-worldPos);
    // 色: 薄い縁は明るく、厚い所は少し深い色 (液体の吸収)
    float3 albedo=color.rgb*lerp(1.15,.82,height);
    float rough=lerp(.16,.07,wet);
    float3 halfVector=normalize(light+view);float nl=saturate(dot(n,light)),nv=max(.001,saturate(dot(n,view))),nh=saturate(dot(n,halfVector)),vh=saturate(dot(view,halfVector));
    float a2=pow(rough,4);
    float distribution=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2));
    float k=pow(rough+1,2)/8;float geometry=nl/(nl*(1-k)+k)*nv/(nv*(1-k)+k);
    float fresnel=.04+.96*pow(1-vh,5);
    float specular=distribution*geometry*fresnel/max(4*nl*nv,.001);
    float shadow=Shadow(worldPos);
    float3 direct=((1-fresnel)*albedo/3.14159265+specular)*daylight*nl*shadow*3.14159265;
    float skyward=saturate(n.y*.5+.5);
    float3 ambient=albedo*(gAmbientColor*.32+daylight*lerp(float3(.17,.16,.15),float3(.3,.33,.39),skyward));
    // 空の映り込み: 斜めから見るほど強い
    float3 r=reflect(-view,n);
    float3 sky=lerp(float3(.55,.58,.62),float3(.62,.72,.86),saturate(r.y))*saturate(length(daylight)*.6+.25);
    float rim=.04+.96*pow(1-nv,5);
    float3 colour=lerp(ambient+direct,sky,rim*(.55+.25*wet));
    // 部屋の明かり (点光源) と、そのつや
    [unroll] for(int lamp=0;lamp<4;++lamp)if(gPoint[lamp].enabled){
        float3 toLamp=gPoint[lamp].pos-worldPos;float d=length(toLamp),fall=saturate(1-d/gPoint[lamp].range);float3 l=toLamp/max(d,.001);
        colour+=(albedo*(saturate(dot(n,l))*.8+.2)+pow(saturate(dot(n,normalize(l+view))),120)*.6)*gPoint[lamp].color*fall*fall;}
    return float4(colour,1);
}
