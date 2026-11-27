// Resources/Shaders/PaperFramePost.hlsl
// 和紙の質感をシーン全体へ薄く重ね、周辺を紙の色へなじませる。

Texture2D gScene : register(t0); 
Texture2D gPaper : register(t1);   // 和紙テクスチャ
Texture2D gFrame : register(t2);   // フレームマスク（vignette.png: 中央が白1.0、端が黒0.0の墨ビネット）
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0) { 
    float gTime; 
    float gNoiseStrength; 
    float gDistortion; 
    float gChromaShift; 
    float gVignette;    
    float gScanline;    
    float2 pad; 
};

float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    // 1. 3Dシーンカラーの取得
    float3 sceneCol = gScene.Sample(gSmp, uv).rgb;

    // 2. ゆっくり流れる連続的なUVスクロール
    // 紙のテクスチャを斜め方向へ連続的に流す。
    float2 scrollOffset = float2(-0.030f, 0.020f) * gTime;

    // 3. アスペクト比補正とスクロールを加えた紙テクスチャのサンプリング
    float aspect = 1920.0 / 1080.0;
    float2 uvPaper = uv;
    uvPaper.x *= aspect;
    uvPaper *= 2.5; // 紙の繊維の細かさを設定する。
    uvPaper += scrollOffset;

    float3 paperCol = gPaper.Sample(gSmp, uvPaper).rgb;

    // 4. 3Dシーンへの「和紙質感オーバーレイ(Overlay)」処理
    float3 overlayCol;
    for(int i = 0; i < 3; ++i) {
        overlayCol[i] = (sceneCol[i] < 0.5f) 
            ? (2.0f * sceneCol[i] * paperCol[i]) 
            : (1.0f - 2.0f * (1.0f - sceneCol[i]) * (1.0f - paperCol[i]));
    }
    // 繊維の主張を抑え、3Dシーンをクリアに見せる
    float3 paperScene = lerp(sceneCol, overlayCol, saturate(gNoiseStrength * 0.45f));

    // 5. フレームの輝度を、紙の色へ切り替えるためのマスクとして使う。
    float3 frameCol = gFrame.Sample(gSmp, uv).rgb;
    float mask = frameCol.r;

    // 円形のビネットを加え、フレーム画像で覆いきれない四隅も紙の色へ切り替える。
    float2 centerDist = uv - 0.5f;
    float circleMask = saturate(dot(centerDist, centerDist) * (1.0f + gVignette * 4.0f));

    // フレームと円形ビネットの合計を、紙の色の混合率にする。
    float blendAlpha = saturate((1.0f - mask) + circleMask);

    // 紙の質感を重ねたシーンと、紙だけの色を混合する。
    float3 finalCol = lerp(paperScene, paperCol, blendAlpha);

    // ダメージを受けた瞬間の赤いダメージビネット
    if (gDistortion > 0.001) {
        float distSq = dot(centerDist, centerDist);
        float damageVignette = saturate(distSq * 3.0f * gDistortion);
        finalCol = lerp(finalCol, float3(1.0, 0.1, 0.1), damageVignette * 0.6f);
    }

    return float4(finalCol, 1.0f);
}
