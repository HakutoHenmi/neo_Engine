Texture2D gTex : register(t0);
Texture2D gShadowMap : register(t1);
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
    float bias = 0.002f; // Z-fighting/shadow acne対策の手動バイアス
    [unroll] for(int x=-1;x<=1;++x) [unroll] for(int y=-1;y<=1;++y)
        coverage+=gShadowMap.SampleCmpLevelZero(gShadowSmp,p.xy+float2(x,y)/2048.0f,p.z - bias).r;
    return coverage/9.0f;
}
float4 main(float4 svpos:SV_POSITION,float3 worldPos:TEXCOORD0,float3 normal:TEXCOORD1,float2 uv:TEXCOORD2,float4 color:COLOR0):SV_TARGET {
    float4 texel=gTex.Sample(gSmp,uv);
#ifdef VEGETATION_CUTOUT
    clip(texel.a-.42f);
#endif
    float3 albedo=texel.rgb*color.rgb;
#ifdef VEGETATION_CUTOUT
    albedo=pow(max(albedo,float3(.001f,.001f,.001f)),float3(.58f,.58f,.58f))*float3(.92f,1.13f,.83f);
#endif
    float3 n=normalize(normal);
    float3 light=gDir[0].enabled?normalize(-gDir[0].dir):normalize(float3(-.4f,.8f,-.3f));
    float3 daylight=gDir[0].enabled?gDir[0].color:float3(1,.9f,.8f);
    float diffuse=saturate(dot(n,light));
    float3 illumination=float3(.12f,.095f,.075f)+gAmbientColor*.94f+daylight*diffuse*max(Shadow(worldPos),.42f)*.62f;
    float3 view=normalize(gCamPos-worldPos);
    float spec=pow(saturate(dot(n,normalize(light+view))),42)*.035f;
    return float4(albedo*illumination+spec,abs(color.a));
}
