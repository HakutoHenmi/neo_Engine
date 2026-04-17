// Resources/Shaders/Distortion.hlsl
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };

struct InstanceData {
    row_major float4x4 world;
    float4 color;
    float4 uvScaleOffset;
};
StructuredBuffer<InstanceData> gInstanceData : register(t2);

struct VSIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float3 nrm : NORMAL;
    float4 weights : WEIGHTS;
    uint4 indices : BONES;
};

struct VSOut {
    float4 svpos : SV_POSITION;
    float4 clipPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : COLOR0;
};

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    
    // インスタンスデータまたは定数バッファからワールド行列と色を取得
    float4x4 world = gWorld;
    float4 color = gColor;
    
    // インスタンスIDが有効な範囲ならインスタンスデータを使用 (t2のバインド有無を判定することはできないが、
    // ここではインスタンス描画時に SV_InstanceID が 0 以外になること、あるいは
    // Renderer側で適切にバインドされることを前提とする)
    // 実際には Renderer 側でインスタンス描画時のみこのシェーダーを適切にセットアップする。
    [branch]
    if (instanceID < 1024) { // 安全策としての最大数
        world = gInstanceData[instanceID].world;
        color = gInstanceData[instanceID].color;
    }

    float4 wp = mul(v.pos, world);
    float4 vp = mul(wp, gView);
    o.svpos = mul(vp, gProj);
    o.clipPos = o.svpos;
    o.uv = v.uv;
    o.color = color;
    return o;
}

Texture2D gBackdropTex : register(t0); // Current scene color
Texture2D gDistortionMap : register(t1); // Normal map for distortion
SamplerState gSmp : register(s0);

float4 ps_main(VSOut i) : SV_TARGET {
    // Convert clip space to screen UV
    float2 screenUV = i.clipPos.xy / i.clipPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;

    // --- Premium Distortion Adjustments ---
    
    // --- Single Clean Ripple Logic ---
    
    // 中心(0.5, 0.5)からの距離と方向を計算
    float2 distDir = i.uv - 0.5f;
    float dist = length(distDir);
    float2 dir = normalize(distDir);
    
    // 境界チェック: ゼロ除算を避ける
    if (dist < 0.0001f) dir = float2(0, 0);

    // ★単一の波紋(リング)を作る
    // 外側に行くほど歪むのではなく、特定の「輪」の部分だけが歪むように設計
    float ringWidth = 0.15f;
    float ringPos = 0.3f; // リングの位置
    float ringMask = smoothstep(ringWidth, 0.0f, abs(dist - ringPos));
    
    // 放射状グラデーション(全体的なぼかし)
    float radialFalloff = smoothstep(0.5f, 0.2f, dist);
    
    // 歪み強度
    float strength = i.color.a * 0.15f * ringMask * radialFalloff;
    
    // 法線マップを1層だけ使用（ディテール用、スクロールなし）
    float3 n = gDistortionMap.Sample(gSmp, i.uv).rgb * 2.0f - 1.0f;
    
    // 歪み方向: 中心から外側へのベクトルをベースに、法線の微細な凹凸を混ぜる
    float2 offset = dir * strength + n.xy * strength * 0.3f;

    // Clamp to screen edges
    float2 distortedUV = clamp(screenUV + offset, 0.001f, 0.999f);
    float3 sceneColor = gBackdropTex.Sample(gSmp, distortedUV).rgb;
    
    // Chromatic aberration (屈折が強い場所ほど強く、弱い場所はスキップ)
    float aberration = 0.25f * strength; 
    [branch]
    if (aberration > 0.001f) {
        float3 colorR = gBackdropTex.Sample(gSmp, distortedUV + float2(offset.x * aberration, 0)).rgb;
        float3 colorB = gBackdropTex.Sample(gSmp, distortedUV - float2(offset.x * aberration, 0)).rgb;
        sceneColor.r = colorR.r;
        sceneColor.b = colorB.b;
    }

    // Tint and Final Color
    return float4(sceneColor * i.color.rgb, 1.0f);
}
