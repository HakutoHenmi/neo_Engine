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

    // --- スライムの顔 (目と口) の描画 ---
    // color.a が 0.985 ～ 0.995 の範囲（弾や破片の 0.99 など）の場合は顔を描画しない
    bool hideFace = (color.a > 0.985f && color.a < 0.995f);
    if (!hideFace) {
        // モデル全体のスケールアップ (2.6) に合わせて座標をスケーリング
        float3 localPos = input.localpos / 2.6f;

        // 正面 (localPosNorm.z > 0.0) に描画
        float dx_l = (localPos.x + 0.18) / 0.04;
        float dy_l = (localPos.y - 0.15) / 0.10;
        float dist_left_eye = dx_l * dx_l + dy_l * dy_l;
        float eye_alpha = 1.0 - smoothstep(0.8, 1.0, dist_left_eye);
        
        float dx_r = (localPos.x - 0.18) / 0.04;
        float dy_r = (localPos.y - 0.15) / 0.10;
        float dist_right_eye = dx_r * dx_r + dy_r * dy_r;
        float eye_alpha_r = 1.0 - smoothstep(0.8, 1.0, dist_right_eye);
        
        float eyes = saturate(eye_alpha + eye_alpha_r);
        
        // 口 (笑顔)
        float mouth_y = 2.5 * localPos.x * localPos.x - 0.15;
        float dist_to_mouth = abs(localPos.y - mouth_y);
        float mouth_alpha = 0.0;
        if (abs(localPos.x) < 0.15) {
            mouth_alpha = 1.0 - smoothstep(0.015, 0.025, dist_to_mouth);
            float edge_fade = 1.0 - smoothstep(0.12, 0.15, abs(localPos.x));
            mouth_alpha *= edge_fade;
        }
        
        float face_alpha = saturate(eyes + mouth_alpha);
        float3 localPosNorm = normalize(input.localpos);
        float front_fade = smoothstep(0.45, 0.65, localPosNorm.z);
        face_alpha *= front_fade;
        
        float3 faceColor = float3(0.05, 0.05, 0.05);
        finalColor = lerp(finalColor, faceColor, face_alpha * 0.95);
        alpha = max(alpha, face_alpha * 0.95);
    }

    // 明るくなりすぎないよう抑える
    finalColor = min(finalColor, float3(1.5f, 1.5f, 1.5f));

    return float4(finalColor, alpha);
}
