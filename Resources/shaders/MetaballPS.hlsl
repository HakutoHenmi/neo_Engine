Texture2D<float4> tex : register(t0);
Texture2D<float> depthTex : register(t1);
SamplerState smp : register(s0);

cbuffer CBFrame : register(b0) { 
    row_major float4x4 gView; 
    row_major float4x4 gProj; 
    row_major float4x4 gViewProj; 
    float3 gCamPos; 
    float gTime; 
};

struct PSIn {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// UVと深度からビュー空間の座標を復元する
float3 GetViewPos(float2 uv, float z) {
    float2 clipXY = uv * 2.0f - 1.0f;
    clipXY.y = -clipXY.y;
    // 逆プロジェクション行列の計算 (P_00とP_11を使用)
    float viewX = clipXY.x / gProj[0][0];
    float viewY = clipXY.y / gProj[1][1];
    return float3(viewX * z, viewY * z, z);
}

float4 main(PSIn input) : SV_TARGET
{
    float4 color = tex.Sample(smp, input.uv);
    
    // 完全に透明なら破棄
    if (color.a < 0.01) {
        discard;
    }

    // アルファ値の閾値でカットアウト
    float threshold = 0.5;
    if (color.a < threshold) {
        discard;
    }

    // 滑らかなエッジを作る（エイリアシング対策）
    float alphaEdge = smoothstep(threshold, threshold + 0.05, color.a);
    float w, h;
    depthTex.GetDimensions(w, h);
    float2 texelSize = float2(1.0 / w, 1.0 / h);
    
    // --- 立体感を出すための深度ベース法線計算（パースペクティブ補正版） ---
    
    // オフセットサイズ（交差時のトゲトゲノイズを吸収するため、広めにサンプリング）
    float2 offX = float2(texelSize.x * 4.0, 0.0);
    float2 offY = float2(0.0, texelSize.y * 4.0);
    
    // 周辺の深度をサンプリング
    float d1 = depthTex.Sample(smp, input.uv + offX).r; // Right
    float d2 = depthTex.Sample(smp, input.uv - offX).r; // Left
    float d3 = depthTex.Sample(smp, input.uv + offY).r; // Bottom
    float d4 = depthTex.Sample(smp, input.uv - offY).r; // Top
    float d0 = depthTex.Sample(smp, input.uv).r;        // Center
    
    // 背景(10000.0) をサンプリングした場合は、球の縁として奥へ緩やかに曲がっているとみなす
    float edgeDepth = 0.5;
    if (d1 > 9999.0) d1 = d0 + edgeDepth;
    if (d2 > 9999.0) d2 = d0 + edgeDepth;
    if (d3 > 9999.0) d3 = d0 + edgeDepth;
    if (d4 > 9999.0) d4 = d0 + edgeDepth;

    // ビュー空間の3D座標を復元
    float3 p1 = GetViewPos(input.uv + offX, d1);
    float3 p2 = GetViewPos(input.uv - offX, d2);
    float3 p3 = GetViewPos(input.uv + offY, d3);
    float3 p4 = GetViewPos(input.uv - offY, d4);
    float3 p0 = GetViewPos(input.uv, d0);
    
    // X方向とY方向の接ベクトルを計算
    float3 vX = p1 - p2; // 右方向のベクトル (+X)
    float3 vY = p3 - p4; // 下方向のベクトル (-Y)
    
    // 外積で法線を計算（左手座標系: cross(Right, Down) => Backward (-Z), つまりカメラ方向）
    float3 normal = normalize(cross(vX, vY));
    
    // --- ライティング計算 ---
    
    // ビューベクトルはカメラ（原点）からピクセルへの逆ベクトル
    float3 viewDir = normalize(-p0);
    
    // 光源ベクトル（やや斜め上から）
    float3 lightDir = normalize(float3(-0.4, 0.6, -0.7));
    
    // フレネル（リムライト） - エッジのガラスのような透き通る反射
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    
    // スペキュラ（ハイライト） - 鋭く強い反射
    float3 halfVector = normalize(lightDir + viewDir);
    float specular = pow(max(dot(normal, halfVector), 0.0), 128.0);
    
    // --- 色の計算（清涼感のあるエメラルドグリーン液体） ---
    
    // 影色（濃い青緑）とベース色（鮮やかなエメラルド）
    float3 shadowColor = float3(0.0, 0.3, 0.4);
    float3 baseColor = float3(0.05, 0.85, 0.5); 
    
    // 陰影 (半球ライティング風に柔らかく)
    float nDotL = max(dot(normal, lightDir), 0.0);
    float3 diffuseColor = lerp(shadowColor, baseColor, nDotL * 0.5 + 0.5);
    
    // ハイライト（強い白の反射）
    float3 specColor = float3(1.0, 1.0, 1.0) * specular * 8.0;
    
    // リムライト（エッジの透き通るような明るい発光）
    float3 rimColor = fresnel * float3(0.4, 1.0, 0.8) * 2.0;
    
    // 空の反射（上を向いている面を少し明るく、青みを足す）
    float skyReflection = max(normal.y, 0.0) * 0.5;
    rimColor += float3(0.1, 0.4, 0.7) * skyReflection;
    
    // --- プレマルチプライド・アルファ（Premultiplied Alpha）合成 ---
    // Renderer.cppで DestBlend = INV_SRC_ALPHA, SrcBlend = ONE と設定されているため、
    // ベース色はアルファ値を乗算し、ハイライト・リムライトは加算（発光）としてそのまま足す。
    
    float baseAlpha = 0.55; // 液体の基本の不透明度
    
    // 動的アルファ（ハイライトが強い部分は不透明度を上げて奥を透けさせない）
    float dynamicAlpha = saturate(baseAlpha + fresnel * 0.6 + specular * 2.5);
    float finalAlpha = dynamicAlpha * alphaEdge; 
    
    // プレマルチプライカラーの計算
    float3 finalColor = (diffuseColor * baseAlpha) + specColor + rimColor;
    
    // カットアウトエッジのフェードを適用
    finalColor *= alphaEdge;
    
    return float4(finalColor, finalAlpha);
}
