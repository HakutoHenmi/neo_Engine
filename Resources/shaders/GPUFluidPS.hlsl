Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

struct PSOut {
    float4 color : SV_TARGET0;
    float depth : SV_TARGET1;
};

PSOut main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0, float viewZ:TEXCOORD1, float4 color:COLOR0, float type:TEXCOORD2) {
    // 距離を計算 (UV: 0.0 ~ 1.0) -> 中心(0.5, 0.5) からの距離
    float2 centerOffset = uv - float2(0.5f, 0.5f);
    float distSq = dot(centerOffset, centerOffset);
    
    // 半径0.5 (二乗で0.25) の外側は破棄
    if (distSq > 0.25f) { discard; }
    
    // 中心はアルファ1.0、端は0.0になるようなソフトな減衰
    float dist = sqrt(distSq);
    // パーティクル同士が滑らかに溶け合い、アウトラインの「凸凹」をなくすため、
    // powによる極端な減衰をなくし、ふっくらとした広いグラデーション（ガウス分布に近い形）にします
    float alpha = saturate(1.0f - (dist / 0.5f));
    alpha = alpha * alpha * (3.0f - 2.0f * alpha); // Smoothstep曲線で滑らかに繋げる
    
    // メタボール合成用に、アルファ（密度）を蓄積する
    float4 outColor = color;
    bool isPlayerSlime = (type < 0.5f);
    bool isDecoySlime = (type > 1.5f && type < 2.5f);
    bool isLostPlayerSlime = (type > 2.5f && type < 3.5f);
    bool isSlime = (isPlayerSlime || isDecoySlime || isLostPlayerSlime);
    
    if (isSlime) {
        if (isPlayerSlime || isLostPlayerSlime) {
            // プレイヤー：白飛びを防ぎつつ鮮やかなエメラルドグリーンにする
            outColor.r = 0.05f;
            outColor.g = 0.8f;
            outColor.b = 0.2f;
        } else {
            // デコイ：はっきりとした鮮やかな黄色
            outColor.r = 1.0f;
            outColor.g = 0.9f;
            outColor.b = 0.05f;
        }
        // ★重要: 密度が1.0に張り付いて巨大化する（透明な隙間ができる）のを防ぐため、
        // 1粒あたりの密度を下げて、複数重なった中心部分だけが濃くなるようにします。
        outColor.a = alpha * (isLostPlayerSlime ? 0.55f : 0.2f) * color.a;
    } else {
        // カエルの卵のように黒く濁らないよう、元の明るい色をそのまま使う
        outColor.rgb = color.rgb;
        // ★はぐれた水滴を完全に消すため、1粒のアルファをさらに下げます（0.07）。
        // メタボールの閾値（0.08）を下回るため、2粒以上重ならないと描画されず、
        // 完全に1枚の水たまりだけが残るようになります。
        outColor.a = alpha * 0.07f * color.a; 
    }
    
    // 加算ブレンド(ONE)で正しく色を乗せるための事前乗算アルファ (Premultiplied Alpha)
    outColor.rgb *= outColor.a;
    
    if (outColor.a <= 0.0f) { discard; }
    
    PSOut o;
    o.color = outColor;
    
    // スライムの立体感を出すため、球体としての丸み（深度のオフセット）を計算する
    // サイズはVSと合わせて0.7fとする。
    float sphereRadius = 0.7f;
    float normalizedDistSq = distSq * 4.0f; // 0.0 ~ 1.0
    float z_offset = sqrt(max(0.0f, 1.0f - normalizedDistSq)) * sphereRadius;
    
    // 丸みを持たせた深度を出力する（これがMetaballPSで法線計算に使われる）
    // 丸みによる重なりのノイズはMetaballPS側の平滑化処理で吸収する
    o.depth = viewZ - z_offset; 
    
    return o;
}
