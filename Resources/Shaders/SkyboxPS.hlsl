#pragma pack_matrix(row_major)

#include "Space.hlsli"

TextureCube gCubeMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer CBFrame : register(b0) {
    matrix gView;
    matrix gProj;
    matrix gViewProj;
    float3 gCameraPos;
    float  gTime;
    float4 gWindParams;
    float3 gPlayerPos;
    uint   gUseCubemapBackground;
};

struct PSIn {
    float4 svpos : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

float4 main(PSIn p) : SV_TARGET {
    float3 dir = normalize(p.texCoord);
    
    float3 color;
    if (gUseCubemapBackground != 0) {
        // Cubemapから景色をサンプリング
        color = gCubeMap.Sample(gSampler, dir).rgb;
        // 実写HDRI等の高輝度テクスチャが白飛び・青飛びするのを防ぐためトーンマッピングを適用
        color = color / (color + 1.0);
        color = pow(color, 1.0 / 2.2);
        // 2 以上は、星空 (宇宙) へ変える割合 ((値 - 2) / 1000。Renderer::SetSkySpaceBlend)
        if (gUseCubemapBackground >= 2)
            // (星雲は少し暗くして、宇宙の暗さを出す)
            color = lerp(color, GetProceduralSpaceColor(dir, gTime) * float3(.55, .6, .7), saturate((gUseCubemapBackground - 2) / 1000.0));
    } else {
        // キューブマップ未使用時は、視線方向と時刻から星空を生成する。
        color = GetProceduralSpaceColor(dir, gTime);
    }

    return float4(color, 1.0);
}
