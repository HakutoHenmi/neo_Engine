#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    
    // Fresnel calculation
    float ndotv = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - ndotv, 3.0);

    // Material base
    float3 baseColor = color.rgb;
    
    // Ambient light
    float3 diffuseLight = ambientColor * 0.7f;
    float3 specularLight = float3(0, 0, 0);
    float3 rimLight = float3(0, 0, 0);

    if (dirLights[0].enabled) {
        float3 L = normalize(-dirLights[0].direction);
        float ndotl = max(dot(N, L), 0.0);
        
        // Diffuse
        diffuseLight += dirLights[0].color * ndotl * 0.3f;

        // Subsurface scattering approximation
        float sss = pow(saturate(dot(V, -L)), 2.5) * 1.2f;
        diffuseLight += baseColor * sss;

        // Specular highlight (sharp, glass-like)
        float3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 200.0); 
        specularLight += dirLights[0].color * spec * 3.0f;
    }

    // Rim light (soft edge glow)
    rimLight = baseColor * fresnel * 0.5f;

    // Combine lighting
    float3 finalColor = baseColor * diffuseLight + specularLight + rimLight;

    // Base transparency: center is more transparent (0.1) than edges (0.4)
    float baseAlpha = lerp(0.1f, 0.4f, fresnel) * color.a;
    
    // Only strong specular highlights increase opacity significantly
    float specIntensity = saturate(length(specularLight));
    float alpha = saturate(baseAlpha + specIntensity * 0.8f);
    
    // Limit overexposure
    finalColor = min(finalColor, float3(1.5f, 1.5f, 1.5f));

    return float4(finalColor, alpha);
}
