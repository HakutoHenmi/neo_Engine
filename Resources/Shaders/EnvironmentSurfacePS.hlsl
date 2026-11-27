#include "ShadowFrustum.hlsli"
Texture2D gTex : register(t0);
Texture2D gShadowMap : register(t1);
Texture2D normalMap : register(t4);
Texture2D roughnessMap : register(t5);
Texture2D materialAO : register(t6);
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
    float bias = 0.0007f;
    [unroll] for(int x=-1;x<=1;++x) [unroll] for(int y=-1;y<=1;++y)
        coverage+=gShadowMap.SampleCmpLevelZero(gShadowSmp,p.xy+float2(x,y)/4096.0f,p.z - bias).r;
    return lerp(1,coverage/9.0f,ShadowFrustumWeight(p));
}
#ifdef CITY_SURFACE
// 結合した街のメッシュ全体に大きな風化を付けるための、なめらかな値ノイズ (継ぎ目なし)。
float CityHash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float CityNoise(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(CityHash(i),CityHash(i+float2(1,0)),f.x),lerp(CityHash(i+float2(0,1)),CityHash(i+float2(1,1)),f.x),f.y);}
// 空気遠近法: 遠くの街区は空のかすみに溶け込み、街に奥行きが出る。
float3 CityFog(float3 colour,float3 worldPos,float3 daylight){
    // かすみはプレイヤーより遠くから始まり、スライムが大きくなってカメラが引くと
    // 薄くなるので、巨大になっても周りの街が見える。
    float distance=length(worldPos-gCamPos),follow=length(gPlayerPos-gCamPos);
    float amount=(1-exp(-max(distance-110-follow,0)*.0009*220/(220+follow)))*lerp(1,.75,saturate(worldPos.y/(600+follow)));
    float3 haze=float3(.6,.65,.72)*saturate(length(daylight)*.6+.25);
    return lerp(colour,haze,saturate(amount)*.82);
}
#endif
float4 main(float4 svpos:SV_POSITION,float3 worldPos:TEXCOORD0,float3 normal:TEXCOORD1,float2 uv:TEXCOORD2,float4 color:COLOR0):SV_TARGET {
#ifdef CITY_ARMOR
    // 敵の装甲のメッシュには UV が無い: 主な世界の軸から使い古しの塗装のテクスチャを
    // 投影する。不透明度と被弾 / 構えの光はアルファに詰めて渡す (2*光+不透明度)。
    {float3 an=abs(normalize(normal));uv=(an.x>an.y&&an.x>an.z?worldPos.zy:an.y>an.z?worldPos.xz:worldPos.xy)*.22;}
    float armorLight=floor(color.a*.5),armorOpacity=saturate(color.a-armorLight*2);
    armorOpacity*=smoothstep(.8,2.8,distance(gCamPos,worldPos));clip(armorOpacity-.001);
    if(armorOpacity<.999){float dither=frac(52.9829189*frac(dot(floor(svpos.xy),float2(.06711056,.00583715))));clip(armorOpacity-dither);}
    color.a=1;
#else
    // アルファが負: カメラとプレイヤーの間の物は網戸状にフェードする (影は残す)。
    if(color.a<0){clip(-color.a-frac(52.9829189*frac(dot(floor(svpos.xy),float2(.06711056,.00583715)))));color.a=1;}
#endif
    float4 texel=gTex.Sample(gSmp,uv);
#ifdef VEGETATION_CUTOUT
    clip(texel.a-.42f);
#endif
    float3 albedo=texel.rgb*color.rgb;
#ifdef VEGETATION_CUTOUT
    // アルベドはすでに線形。暗い画面向けの古い補正はかけない。
    albedo*=float3(.95f,1.f,.9f);
#endif
    float3 n=normalize(normal);
#if defined(CITY_SURFACE)&&!defined(CITY_GLASS)&&!defined(CITY_ARMOR)&&!defined(LOWPOLY_SURFACE)
    // 風化: 街区ごとの広い色味 / 明るさのずれ、通りの高さ付近の汚れ、
    // 壁のかすかな雨だれ。世界の位置で決めるので、結合したメッシュでも変化が出る。
    albedo*=lerp(.84,1.12,CityNoise(worldPos.xz*.011))*lerp(float3(1.05,1,.95),float3(.95,1,1.06),CityNoise(worldPos.xz*.006+17));
    if(abs(n.y)<.5){float street=saturate(1-worldPos.y/34);
        float streak=CityNoise(float2(dot(worldPos.xz,float2(.7,.7))*.32,worldPos.y*.01));
        albedo*=1-.22*street-.14*saturate(streak*1.8-.8)*saturate(1-worldPos.y/520);}
#endif
    float3 px=ddx(worldPos),py=ddy(worldPos);float2 tx=ddx(uv),ty=ddy(uv);
    float determinant=tx.x*ty.y-tx.y*ty.x;
    if(abs(determinant)>.0000001){
        float3 tangent=normalize((px*ty.y-py*tx.y)/determinant);
        tangent=normalize(tangent-n*dot(n,tangent));
        float3 bitangent=normalize(cross(n,tangent))*sign(determinant);
        // DirectX 形式の法線マップ。Z は XY から作り直すので、2 チャンネルの BC5 も RGB と同じように使える。
        float3 map;map.xy=normalMap.Sample(gSmp,uv).xy*2-1;map.y=-map.y;map.z=sqrt(saturate(1-dot(map.xy,map.xy)));
        n=normalize(tangent*map.x+bitangent*map.y+n*map.z);
    }
    float3 light=gDir[0].enabled?normalize(-gDir[0].dir):normalize(float3(-.4f,.8f,-.3f));
    float3 daylight=gDir[0].enabled?gDir[0].color:float3(1,.9f,.8f);
    float3 view=normalize(gCamPos-worldPos);
#ifdef VEGETATION_CUTOUT
    // 草のカードは両面: 拡散光には見えている葉の面を使う。
    if(dot(n,view)<0)n=-n;
#endif
    float rough=clamp(roughnessMap.Sample(gSmp,uv).r,.12,1),ao=materialAO.Sample(gSmp,uv).r;
    float3 halfVector=normalize(light+view);float nl=saturate(dot(n,light)),nv=max(.001,saturate(dot(n,view))),nh=saturate(dot(n,halfVector)),vh=saturate(dot(view,halfVector));
#ifdef CITY_ARMOR
    // 塗装した鋼: 通りの面よりつやがある。
    rough=clamp(rough*.55,.18,.6);
#endif
#ifdef MEADOW_GROUND
    // 乾いた芝が、濡れて磨かれた微小面のようにふるまってはいけない。
    rough=max(rough,.78);
#endif
#ifdef VEGETATION_CUTOUT
    rough=max(rough,.85);
#endif
    // サンプル範囲内の法線のばらつきで、ピクセルより小さいハイライトを広げる。
    // ミップマップした法線で縮小時のエイリアスを消す。輪郭にも効く。
    float3 normalDx=ddx(n),normalDy=ddy(n);
    float variance=.5*(dot(normalDx,normalDx)+dot(normalDy,normalDy));
    float alpha=min(1,sqrt(pow(rough,4)+min(variance,.5))),a2=alpha*alpha;
    float distribution=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2));
    float k=pow(rough+1,2)/8;float geometry=nl/(nl*(1-k)+k)*nv/(nv*(1-k)+k);
    float3 fresnel=.04+.96*pow(1-vh,5);
    float3 specular=distribution*geometry*fresnel/max(4*nl*nv,.001);
    float3 diffuse=(1-fresnel)*albedo/3.14159265;
    float3 direct=(diffuse+specular)*daylight*nl*Shadow(worldPos)*3.14159265;
    float3 ambient=albedo*gAmbientColor*.32*ao;
#ifdef VEGETATION_CUTOUT
    // 薄い葉は両面で空の拡散光を受け、日光を透かす。
    // 視線向きの法線だけだと、逆光の草むらが黒い針金のように見えた。
    ambient=albedo*(gAmbientColor*.32+daylight*.16)*lerp(.6,1,ao);
    direct+=albedo*daylight*(saturate(dot(-n,light))*.22+pow(saturate(dot(-light,view)),3)*.08)*Shadow(worldPos);
#endif
#ifdef CITY_SURFACE
    // 高い街区が通りの大半を陰にする。それでも開けた空や、日の当たる正面から
    // 跳ね返った光は届く: 半球の補助光 (上向きほど明るく、空からは寒色) で
    // 陰の通りが環境光だけに落ちず、見やすいままにする。
    float skyward=saturate(n.y*.5+.5);
    float3 sky=daylight*lerp(float3(.17,.16,.15),float3(.3,.33,.39),skyward);
    ambient=albedo*(gAmbientColor*.32+sky)*lerp(.55,1,ao);
#endif
#ifdef MEADOW_GROUND
    float variation=.9+.07*sin(worldPos.x*.019+worldPos.z*.013)+.06*sin(worldPos.z*.041-worldPos.x*.023);
    ambient*=variation;direct*=variation;
#endif
#ifdef CITY_GLASS
    {   // ガラス: 暗い室内 (明かりやブラインドのある窓もある) と、地平線より上は空 / 下は周りの街区の
        // 映り込みをフレネルで混ぜ、太陽のきらめきを足す。
        float3 flat=normalize(normal),side=abs(flat.y)>.9?float3(1,0,0):normalize(cross(float3(0,1,0),flat));
        float2 cell=floor(float2(dot(worldPos,side)/7.,worldPos.y/19.));float h=CityHash(cell+floor(worldPos.xz/400.)*13.);
        float3 interior=h>.84?float3(1,.78,.52)*(.32+.25*CityHash(cell+3.1)):h>.6?float3(.1,.1,.095):float3(.025,.03,.035);
        // ガラス板ごとに少しずつ角度が違うので、映り込みが板ごとに途切れる。
        float3 r=reflect(-view,n);r=normalize(r+side*(CityHash(cell+7.3)-.5)*.12+float3(0,(CityHash(cell+1.7)-.5)*.1,0));float up=r.y;
        float3 skyColour=lerp(float3(.78,.82,.88),float3(.36,.5,.74),pow(saturate(up),.55));
        float3 below=lerp(float3(.42,.43,.45),float3(.11,.11,.12),saturate(-up*2.5));
        float3 reflected=(up>0?skyColour:below)*saturate(length(daylight)*.65+.2)*lerp(.62,.85,CityHash(cell+5.9));
        float glint=pow(saturate(dot(r,light)),400)*6*Shadow(worldPos);
        float f=.1+.9*pow(1-nv,5);
        float3 glass=lerp(interior,reflected,saturate(f+.1))+daylight*glint;
        return float4(CityFog(glass*color.rgb*3.2,worldPos,daylight),abs(color.a));
    }
#endif
#ifdef CITY_ARMOR
    // 役割の色で色付けした金属のつや、縁の光、被弾 / 振りかぶりの光。
    float3 armorRim=pow(1-nv,3)*float3(.12,.16,.2)*saturate(length(daylight));
    direct+=specular*albedo*daylight*nl*Shadow(worldPos)*2;
    float3 armorColour=ambient+direct+armorRim;
    armorColour=lerp(armorColour,float3(1,.32,.055),saturate(armorLight/16)*.45);
    return float4(CityFog(armorColour,worldPos,daylight),1);
#endif
#ifdef LOWPOLY_SURFACE
    // 部屋の照明。部屋は内側から照らすので、太陽より柔らかい減衰の方が大事。
    [unroll] for(int lamp=0;lamp<4;++lamp)if(gPoint[lamp].enabled){
        float3 toLamp=gPoint[lamp].pos-worldPos;float d=length(toLamp),fall=saturate(1-d/gPoint[lamp].range);
        direct+=albedo*gPoint[lamp].color*(saturate(dot(n,toLamp/max(d,.001)))*.8+.2)*fall*fall;}
#endif
#ifdef CITY_SURFACE
    return float4(CityFog(ambient+direct,worldPos,daylight),abs(color.a));
#else
    return float4(ambient+direct,abs(color.a));
#endif
}
