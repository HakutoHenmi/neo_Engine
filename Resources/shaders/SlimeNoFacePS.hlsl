#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

struct SlimeVSOutput {
    float4 svpos : SV_POSITION;
    float4 worldpos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 localpos : TEXCOORD1;
};

float4 main(SlimeVSOutput input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    
    // フレネルの計算
    float ndotv = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - ndotv, 3.0);

    // 材質の基本
    float3 baseColor = color.rgb;
    
    // 環境光
    float3 diffuseLight = ambientColor * 0.7f;
    float3 specularLight = float3(0, 0, 0);
    float3 rimLight = float3(0, 0, 0);

    if (dirLights[0].enabled) {
        float3 L = normalize(-dirLights[0].direction);
        float ndotl = max(dot(N, L), 0.0);
        
        // 拡散光
        diffuseLight += dirLights[0].color * ndotl * 0.3f;

        // 表面下散乱の近似
        float sss = pow(saturate(dot(V, -L)), 2.5) * 1.2f;
        diffuseLight += baseColor * sss;

        // 鏡面反射のハイライト (鋭い、ガラスのような)
        float3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 200.0); 
        specularLight += dirLights[0].color * spec * 3.0f;
    }

    // 縁の光 (柔らかい縁の輝き)
    rimLight = baseColor * fresnel * 0.5f;

    // ライティングを合わせる
    float3 finalColor = baseColor * diffuseLight + specularLight + rimLight;

    // 基本の透明度: 中央 (0.1) は縁 (0.4) より透明
    float baseAlpha = lerp(0.1f, 0.4f, fresnel) * color.a;
    
    // 強い鏡面ハイライトだけが不透明度を大きく上げる
    float specIntensity = saturate(length(specularLight));
    float alpha = saturate(baseAlpha + specIntensity * 0.8f);

    // 明るくなりすぎないよう抑える
    finalColor = min(finalColor, float3(1.5f, 1.5f, 1.5f));

    return float4(finalColor, alpha);
}
