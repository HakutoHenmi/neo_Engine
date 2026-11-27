struct VSOut {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 worldPos : WORLDPOS;
    float3 normal : NORMAL;
    uint particleType : TYPE;
};

struct DirectionalLight {
    float3 direction;
    float pad0;
    float3 color;
    float pad1;
    uint enabled;
    float pad2[3];
};

cbuffer LightCB : register(b1) {
    float3 ambientColor;
    float _pad0;
    DirectionalLight dirLights[1];
};

float4 main(VSOut i) : SV_TARGET {
    float4 baseColor = i.color;
    
    float2 uv = i.uv * 2.0 - 1.0;
    float dist = dot(uv, uv);
    if(dist > 1.0) discard;
    
    float alpha = 1.0 - smoothstep(0.8, 1.0, sqrt(dist));
    baseColor.a *= alpha;
    
    // ライティング (簡単な環境光+ディレクショナル)
    if (i.particleType == 2) {
        float3 lightDir = -normalize(dirLights[0].direction);
        float nDotL = saturate(dot(i.normal, lightDir));
        float3 diffuse = dirLights[0].color * nDotL + ambientColor;
        
        // 最終カラー (パーティクル自体の色を強く出すため、ライティングは半分程度の影響に)
        baseColor.rgb = baseColor.rgb * lerp(float3(1, 1, 1), diffuse, 0.5);
    }
    
    return baseColor;
}
