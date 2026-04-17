#pragma pack_matrix(row_major)

struct VSOut
{
    float4 svpos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : COLOR0;
};

Texture2D gTex : register(t0); 
Texture2D gShadowMap : register(t1);
SamplerState gSmp : register(s0);
SamplerComparisonState gShadowSmp : register(s1);

cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };

struct DirLight { float3 dir; float pad0; float3 color; float pad1; uint enabled; float3 pad2; };
struct PointLight { float3 pos; float pad0; float3 color; float range; float3 atten; float pad1; uint enabled; float3 pad2; };
struct SpotLight { float3 pos; float pad0; float3 dir; float range; float3 color; float inner; float3 atten; float outer; uint enabled; float3 pad2; };
struct AreaLight { float3 pos; float pad0; float3 color; float range; float3 right; float halfWidth; float3 up; float halfHeight; float3 dir; float pad1; float3 atten; float pad2; uint enabled; float3 pad3; };

#define MAX_DIR 1
#define MAX_POINT 4
#define MAX_SPOT 4
#define MAX_AREA 4
cbuffer CBLight : register(b2) { float3 gAmbientColor; float padA0; DirLight gDir[MAX_DIR]; PointLight gPoint[MAX_POINT]; SpotLight gSpot[MAX_SPOT]; AreaLight gArea[MAX_AREA]; row_major float4x4 gShadowMatrix; };

float GetAttenuation(float3 atten, float d) { return 1.0 / (atten.x + atten.y * d + atten.z * d * d); }
float3 BlinnPhong(float3 L, float3 V, float3 N, float3 C, float3 A) {
    float NdotL = max(dot(N, L), 0.0); float3 diff = A * C * NdotL;
    float3 H = normalize(L + V); float NdotH = max(dot(N, H), 0.0);
    float3 spec = C * pow(NdotH, 32.0) * 0.5; return diff + spec;
}

float CalcShadow(float3 worldPos) {
    float4 shadowPos = mul(float4(worldPos, 1.0f), gShadowMatrix);
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    projCoords.x = projCoords.x * 0.5f + 0.5f;
    projCoords.y = -projCoords.y * 0.5f + 0.5f;
    if (projCoords.x < 0.0f || projCoords.x > 1.0f || projCoords.y < 0.0f || projCoords.y > 1.0f || projCoords.z < 0.0f || projCoords.z > 1.0f)
        return 1.0f;
    float shadow = 0.0f;
    float texelSize = 1.0f / 2048.0f;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSmp, projCoords.xy + float2(x, y) * texelSize, projCoords.z).r;
        }
    }
    return shadow / 9.0f;
}

float4 main(VSOut input) : SV_TARGET {
    float4 tex = gTex.Sample(gSmp, input.uv); 
    // 頂点カラー(input.color)を反映
    float3 albedo = tex.rgb * input.color.rgb; 
    float3 N = normalize(input.normal); 
    float3 V = normalize(gCamPos - input.worldPos);
    float3 finalColor = albedo * gAmbientColor;

    float shadowFactor = CalcShadow(input.worldPos);

    for(int i=0; i<MAX_DIR; ++i) if(gDir[i].enabled) finalColor += BlinnPhong(normalize(-gDir[i].dir), V, N, gDir[i].color, albedo) * shadowFactor;
    for(int i=0; i<MAX_POINT; ++i) if(gPoint[i].enabled) { float3 Lv = gPoint[i].pos - input.worldPos; float d = length(Lv); if(d < gPoint[i].range) finalColor += BlinnPhong(normalize(Lv), V, N, gPoint[i].color, albedo) * GetAttenuation(gPoint[i].atten, d); }
    for(int i=0; i<MAX_SPOT; ++i) if(gSpot[i].enabled) { float3 Lv = gSpot[i].pos - input.worldPos; float d = length(Lv); if(d < gSpot[i].range) { float3 L = normalize(Lv); float c = dot(L, normalize(-gSpot[i].dir)); float s = smoothstep(gSpot[i].outer, gSpot[i].inner, c); finalColor += BlinnPhong(L, V, N, gSpot[i].color, albedo) * GetAttenuation(gSpot[i].atten, d) * s; } }
    
    return float4(finalColor, tex.a * input.color.a);
}
