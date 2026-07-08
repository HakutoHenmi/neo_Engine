// FluidSpherePS.hlsl
#pragma pack_matrix(row_major)

cbuffer ViewProjection : register(b0) {
    matrix view;
    matrix projection;
    matrix viewProj;
    matrix invProjection;
    float3 cameraPos;
    float time;
    float3 corePosition;
    float isLiquidated;
};

struct VSOutput {
    float4 pos : SV_POSITION; // Screen pos
    float2 uv : TEXCOORD;     // -1 to 1
    float3 viewPos : POSITION0;
    float3 worldPos : POSITION1;
    float3 centerWorldPos : POSITION2;
    float radius : BLENDWEIGHT0;
    float3 color : COLOR;
};

struct PSOutput {
    float4 colorOut : SV_TARGET0;
    float depth : SV_Depth;
};

PSOutput main(VSOutput input) {
    PSOutput output;
    
    // UVからの距離（中心0、エッジ1）
    float distFromCenter = length(input.uv);
    if (distFromCenter > 1.0f) discard;
    
    float z = sqrt(1.0f - dot(input.uv, input.uv));
    
    // ピクセルのビュー空間座標を計算
    float3 pixelViewPos = input.viewPos;
    pixelViewPos.z -= z * input.radius; 
    
    // クリップ空間へ変換して Z バッファ用の深度を計算
    float4 clipPos = mul(float4(pixelViewPos, 1.0f), projection);
    output.depth = clipPos.z / clipPos.w;
    
    // 表面の揺らぎ（近似）
    float waveX = sin(input.worldPos.x * 5.0f + time * 3.0f) * cos(input.worldPos.y * 5.0f - time * 2.0f) * 0.05f;
    float waveY = cos(input.worldPos.x * 4.0f - time * 3.0f) * sin(input.worldPos.y * 6.0f + time * 2.0f) * 0.05f;
    
    float3 normal = normalize(float3(input.uv.x, input.uv.y, -z) + float3(waveX, waveY, 0.0f));
    float3 lightDir = normalize(float3(0.5f, 1.0f, -0.5f));
    
    float3 viewDir = float3(0, 0, -1);
    float NdotV = max(dot(normal, viewDir), 0.0f);
    
    // フレネル (Schlickの近似)
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    float3 fresnel = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
    
    // スペキュラ（ハイライト）
    float3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float spec = pow(NdotH, 256.0f);
    float3 specColor = float3(1.0f, 1.0f, 1.0f) * spec * 5.0f;
    
    // 環境反射 (近似)
    float3 reflectDir = reflect(-viewDir, normal);
    float skyFactor = smoothstep(-0.5f, 1.0f, reflectDir.y);
    float3 envColor = lerp(float3(0.0f, 0.1f, 0.15f), float3(0.7f, 0.85f, 1.0f), skyFactor);
    float3 surfaceReflection = envColor * fresnel * 2.0f + specColor;
    
    // エッジのソフトフェードとAbsorption近似
    float edgeFade = smoothstep(1.0f, 0.3f, distFromCenter);
    float apparentThickness = NdotV;
    float3 shallowColor = float3(0.3f, 0.95f, 0.7f);
    float3 deepColor = float3(0.0f, 0.35f, 0.45f);
    float3 waterBaseColor = lerp(shallowColor, deepColor, apparentThickness) * input.color;
    
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 scatterColor = waterBaseColor * (NdotL * 0.4f + 0.6f);
    
    // 最終的な合成
    float3 finalColor = scatterColor * 0.8f + surfaceReflection;
    float alpha = edgeFade * 0.5f + fresnel.x * 1.5f + spec * 2.0f;
    
    output.colorOut = float4(finalColor, saturate(alpha));
    
    return output;
}
