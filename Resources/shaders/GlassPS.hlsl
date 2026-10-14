#include "Obj.hlsli"
TextureCube environmentMap : register(t3);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    float facing = abs(dot(N, V));
    if (dot(N, V) < 0) N = -N;
    // Dielectric Fresnel for an approximate glass IOR of 1.5.
    float fresnel = .04 + .96 * pow(1 - saturate(facing), 5);
    float3 R = reflect(-V, N);
    float3 reflection = lerp(float3(.12, .18, .24), float3(.7, .85, 1), saturate(R.y * .5 + .5));
    if (useCubemap != 0) reflection = environmentMap.Sample(smp, R).rgb;
    float highlight = 0;
    if (dirLights[0].enabled != 0) {
        float3 H = normalize(V - normalize(dirLights[0].direction));
        highlight = pow(saturate(dot(N, H)), 180);
    }
    float strength = saturate(reflectivity);
    float3 tint = lerp(color.rgb * .35, reflection, saturate(.35 + fresnel * strength));
    float alpha = saturate(color.a + fresnel * strength * .55 + highlight * .18);
    return float4(tint + highlight * .8, alpha);
}
