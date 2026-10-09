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
        coverage+=gShadowMap.SampleCmpLevelZero(gShadowSmp,p.xy+float2(x,y)/2048.0f,p.z - bias).r;
    return lerp(1,coverage/9.0f,ShadowFrustumWeight(p));
}
float4 main(float4 svpos:SV_POSITION,float3 worldPos:TEXCOORD0,float3 normal:TEXCOORD1,float2 uv:TEXCOORD2,float4 color:COLOR0):SV_TARGET {
    float4 texel=gTex.Sample(gSmp,uv);
#ifdef VEGETATION_CUTOUT
    clip(texel.a-.42f);
#endif
    float3 albedo=texel.rgb*color.rgb;
#ifdef VEGETATION_CUTOUT
    // Albedo is already linear; do not apply the old compensation for a dark display.
    albedo*=float3(.95f,1.f,.9f);
#endif
    float3 n=normalize(normal);
    float3 px=ddx(worldPos),py=ddy(worldPos);float2 tx=ddx(uv),ty=ddy(uv);
    float determinant=tx.x*ty.y-tx.y*ty.x;
    if(abs(determinant)>.0000001){
        float3 tangent=normalize((px*ty.y-py*tx.y)/determinant);
        tangent=normalize(tangent-n*dot(n,tangent));
        float3 bitangent=normalize(cross(n,tangent))*sign(determinant);
        float3 map=normalMap.Sample(gSmp,uv).xyz*2-1;map.y=-map.y; // DirectX normal maps.
        n=normalize(tangent*map.x+bitangent*map.y+n*map.z);
    }
    float3 light=gDir[0].enabled?normalize(-gDir[0].dir):normalize(float3(-.4f,.8f,-.3f));
    float3 daylight=gDir[0].enabled?gDir[0].color:float3(1,.9f,.8f);
    float3 view=normalize(gCamPos-worldPos);
#ifdef VEGETATION_CUTOUT
    // Grass cards are two sided: use the visible leaf side for diffuse lighting.
    if(dot(n,view)<0)n=-n;
#endif
    float rough=clamp(roughnessMap.Sample(gSmp,uv).r,.12,1),ao=materialAO.Sample(gSmp,uv).r;
    float3 halfVector=normalize(light+view);float nl=saturate(dot(n,light)),nv=max(.001,saturate(dot(n,view))),nh=saturate(dot(n,halfVector)),vh=saturate(dot(view,halfVector));
#ifdef MEADOW_GROUND
    // Dry turf must not behave like wet, polished microfacets.
    rough=max(rough,.78);
#endif
#ifdef VEGETATION_CUTOUT
    rough=max(rough,.85);
#endif
    // Widen subpixel highlights using the footprint's normal variance.
    // Mipmapped normals remove minification aliasing; this also handles silhouettes.
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
    // Thin leaves receive diffuse sky light on both sides and transmit sunlight.
    // A view-facing normal alone made backlit tufts look like black wire shapes.
    ambient=albedo*(gAmbientColor*.32+daylight*.16)*lerp(.6,1,ao);
    direct+=albedo*daylight*(saturate(dot(-n,light))*.22+pow(saturate(dot(-light,view)),3)*.08)*Shadow(worldPos);
#endif
#ifdef MEADOW_GROUND
    float variation=.9+.07*sin(worldPos.x*.019+worldPos.z*.013)+.06*sin(worldPos.z*.041-worldPos.x*.023);
    ambient*=variation;direct*=variation;
#endif
    return float4(ambient+direct,abs(color.a));
}
