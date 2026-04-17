#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

// 距離減衰計算
float AttenDist(float3 atten, float d)
{
    return 1.0 / (atten.x + atten.y * d + atten.z * d * d);
}

float4 main(VSOutput input) : SV_TARGET
{
    // UV変換
    float2 uv = float2(
        input.uv.x * m_uv_scale.x + m_uv_offset.x,
        input.uv.y * m_uv_scale.y + m_uv_offset.y
    );
    float4 texcolor = tex.Sample(smp, uv);

    // 基本ベクトル
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    
    // ベースカラー
    float3 albedo = texcolor.rgb * color.rgb;
    // アンビエント成分
    float3 finalColor = albedo * ambientColor;

    // ----------------------------------------------------
    // Directional Lights
    // ----------------------------------------------------
    for (int i = 0; i < MAX_DIR_LIGHTS; ++i)
    {
        if (dirLights[i].enabled != 0)
        {
            float3 L = normalize(-dirLights[i].direction);
            float NdotL = saturate(dot(N, L));
            
            float3 H = normalize(L + V);
            float spec = pow(saturate(dot(N, H)), 32.0f); // Shininess=32

            float3 diffuse = albedo * NdotL;
            float3 specular = m_specular * spec;

            finalColor += (diffuse + specular) * dirLights[i].color;
        }
    }

    // ----------------------------------------------------
    // Point Lights
    // ----------------------------------------------------
    for (int j = 0; j < MAX_POINT_LIGHTS; ++j)
    {
        if (pointLights[j].enabled != 0)
        {
            float3 Lvec = pointLights[j].position - input.worldpos.xyz;
            float d = length(Lvec);
            if (d < pointLights[j].range)
            {
                float3 L = Lvec / max(d, 1e-5);
                float att = AttenDist(pointLights[j].atten, d);

                float NdotL = saturate(dot(N, L));
                float3 H = normalize(L + V);
                float spec = pow(saturate(dot(N, H)), 32.0f);

                float3 diffuse = albedo * NdotL;
                float3 specular = m_specular * spec;

                finalColor += (diffuse + specular) * pointLights[j].color * att;
            }
        }
    }

    // ----------------------------------------------------
    // Spot Lights
    // ----------------------------------------------------
    for (int k = 0; k < MAX_SPOT_LIGHTS; ++k)
    {
        if (spotLights[k].enabled != 0)
        {
            float3 Lvec = spotLights[k].position - input.worldpos.xyz;
            float d = length(Lvec);
            if (d < spotLights[k].range)
            {
                float3 L = Lvec / max(d, 1e-5);
                float cosAng = dot(L, normalize(spotLights[k].direction));
                float ang = smoothstep(spotLights[k].outerCos, spotLights[k].innerCos, cosAng);
                float att = AttenDist(spotLights[k].atten, d);

                float NdotL = saturate(dot(N, L));
                float3 H = normalize(L + V);
                float spec = pow(saturate(dot(N, H)), 32.0f);

                float3 diffuse = albedo * NdotL;
                float3 specular = m_specular * spec;

                finalColor += (diffuse + specular) * spotLights[k].color * att * ang;
            }
        }
    }

    return float4(finalColor, texcolor.a * color.a);
}