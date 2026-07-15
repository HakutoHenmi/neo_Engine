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

// --- 奥行きと水感を出すためのノイズ・パターン生成関数 ---
// 疑似乱数
float hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// 水面の波紋・コースティクス（光の屈折模様）
float waterCaustics(float2 uv, float time) {
    float2 p = uv * 6.0; // スケールを大きく（模様を粗く）して気持ち悪さを軽減
    float2 i = p;
    float c = 1.0;
    float inten = 0.05;

    for (int n = 0; n < 3; n++) {
        float t = time * (1.0 - (3.0 / float(n + 1)));
        i = p + float2(cos(t - i.x) + sin(t + i.y), sin(t - i.y) + cos(t + i.x));
        c += 1.0 / length(float2(p.x / (sin(i.x + t) / inten), p.y / (cos(i.y + t) / inten)));
    }
    c = 1.0 - clamp(c, 0.0, 1.0);
    return pow(c, 1.5); // コントラストを和らげ、自然な光の集まりに
}

// 疑似的な気泡（立体空間・ワールド座標ベース）
// 視点に依存せず、ワールドのXZ平面からY軸（上）に向かって気泡が昇る
float bubbles3D(float3 wPos, float time) {
    float3 p = wPos * 4.0; // スライムのサイズ感に合わせたスケール
    
    // XZ平面で空間をグリッド分割し、気泡の柱を作る
    float2 cell = floor(p.xz);
    float2 f = frac(p.xz);
    float res = 0.0;
    
    for(int j=-1; j<=1; j++) {
        for(int i=-1; i<=1; i++) {
            float2 b = float2(i, j);
            float h = hash(cell + b);
            
            // 間引き：ハッシュ値が0.15以下の場合のみ泡を発生させる
            if (h > 0.15) continue;
            
            // XZ平面上の気泡のローカル座標
            float2 r = float2(b) - f + h;
            
            // Y軸（高さ）の判定
            // 時間経過で上(Y軸プラス)へ昇る。ワールドY座標が低いほど早く出現。
            float yOffset = time * (0.5 + h * 0.8) + h * 10.0;
            
            // 縦方向に一定間隔で気泡がリピートするようにフラクタル化
            float localY = frac(p.y * 0.2 - yOffset) - 0.5; // -0.5 ~ 0.5 の範囲
            
            // 3D空間での気泡の中心からの距離
            float d = sqrt(dot(r, r) + localY * localY * 10.0);
            
            // 泡のサイズ
            float radius = 0.15 + h * 0.1; 
            
            // リング状の泡
            float ring = smoothstep(radius, radius - 0.05, d) - smoothstep(radius - 0.05, radius - 0.15, d);
            
            res += ring * (h / 0.15); // 明るさ
        }
    }
    return res;
}

// ---------------------------------------------------------
// ブラー関数（強力なガウシアン）
// ---------------------------------------------------------
float4 GetBlurredColor(Texture2D<float4> texObj, SamplerState smpObj, float2 uv, float2 texelSize, float spread) {
    float4 c = float4(0, 0, 0, 0);
    float wSum = 0.0;
    // 9x9 で極めて強力にブラーをかける（凸凹を完全に溶かして滑らかにする）
    for(int y = -4; y <= 4; ++y) {
        for(int x = -4; x <= 4; ++x) {
            float w = exp(-float(x * x + y * y) / 10.0);
            c += texObj.SampleLevel(smpObj, uv + float2(x, y) * texelSize * spread, 0) * w;
            wSum += w;
        }
    }
    return c / wSum;
}

float4 main(PSIn input) : SV_TARGET
{
    float w, h;
    depthTex.GetDimensions(w, h);
    float2 texelSize = float2(1.0 / w, 1.0 / h);

    // アウトラインと表面全体のギザギザを完全になくすため、中心色もブラーをかけたものを使う
    // ★サンプリングの飛ばしすぎによるドット化（ブロック状のジャギー）を防ぐため、spread は 1.5 に抑えます
    float4 color = GetBlurredColor(tex, smp, input.uv, texelSize, 1.5);
    
    // 完全に透明なら破棄
    if (color.a < 0.001) {
        discard;
    }

    // ★重要: パーティクルとアウトラインの間の「透明な空間」を無くすため、
    // 閾値を引き上げることで、枠がパーティクルの中心（矢印）にさらに密着します。
    // スプラッシュ(0.1)がギリギリ消えない 0.08 に設定します。
    float threshold = 0.08;

    // 滑らかさを保ちつつ、透明な枠を極限まで薄く（細く）するためのシャープなグラデーション幅
    float alphaEdge = smoothstep(threshold, threshold + 0.03, color.a);
    
    // 周辺のアルファ（密度）をサンプリングして、滑らかな勾配を計算する
    // ★斜めから見たときの法線のギザギザ（ジャギー）を完全に解消するため、
    // 法線計算用のサンプリングも「ブラーがかかった滑らかなアルファ」を使用する！
    float2 offX_a = float2(texelSize.x * 4.0, 0.0);
    float2 offY_a = float2(0.0, texelSize.y * 4.0);
    
    // サンプリング間隔を広げすぎると斜めから見たときに破綻するため、適度な距離(4.0)に戻す
    float a1 = GetBlurredColor(tex, smp, input.uv + offX_a, texelSize, 1.5).a; // Right
    float a2 = GetBlurredColor(tex, smp, input.uv - offX_a, texelSize, 1.5).a; // Left
    float a3 = GetBlurredColor(tex, smp, input.uv + offY_a, texelSize, 1.5).a; // Bottom
    float a4 = GetBlurredColor(tex, smp, input.uv - offY_a, texelSize, 1.5).a; // Top
    
    // 勾配ベクトルの計算
    float dx = (a2 - a1);
    float dy = (a3 - a4);
    
    // 【球面法線の正確な復元】
    // スライムの表面を「完璧なドーム状（半球）」にするため、勾配からXY成分を作り、
    // 球面の方程式 (x^2 + y^2 + z^2 = 1) に基づいてZ成分を逆算します。
    // 液状化したときのギザギザを抑えるため、法線強度を控えめにします。
    float normalStrength = 1.2; // 2.5から下げて平滑化
    float2 normalXY = float2(dx, dy) * normalStrength;
    
    // XYの長さが1を超えないように制限（1を超えるとZが計算できなくなるため）
    float xySq = saturate(dot(normalXY, normalXY));
    
    // フチに行くほどZ成分が0に近づき、側面までしっかり丸まる
    float dz = -sqrt(1.0 - xySq);
    
    float3 alphaNormal = float3(normalXY.x, normalXY.y, dz);
    
    // --- 中心部分の「真っ平ら」を解決するための大局的勾配ベース法線 ---
    // 深度バッファは背景の箱や床の段差を拾って破綻するため使用しません。
    // 代わりに、中心の平坦な領域でも「はるか遠くのフチ」をサンプリングすることで、
    // 自分がスライムのどの位置にいるか（大局的な丸み）を確実に推定します。
    // ★斜めから見たときに距離が遠すぎると背景を拾ってギザギザになるため、20.0 -> 12.0 に縮小します
    float2 offX_far = float2(texelSize.x * 12.0, 0.0);
    float2 offY_far = float2(0.0, texelSize.y * 12.0);
    float aRight = tex.SampleLevel(smp, input.uv + offX_far, 0).a;
    float aLeft  = tex.SampleLevel(smp, input.uv - offX_far, 0).a;
    float aBottom = tex.SampleLevel(smp, input.uv + offY_far, 0).a;
    float aTop    = tex.SampleLevel(smp, input.uv - offY_far, 0).a;
    
    // 大局的な勾配（左が濃ければ自分は右側にいるため、法線は右を向く）
    float dx_far = (aLeft - aRight);
    // UV座標は下が＋なので、ライティング（上が＋）と合わせるために引き算の順序を逆にします
    float dy_far = (aBottom - aTop);
    
    // 大局的な丸みを持つ球面法線の生成
    // シャープなハイライトが出るように、丸みのサンプリング距離と強さを調整します
    float2 farNormalXY = float2(dx_far, dy_far) * 0.3; 
    float farXySq = saturate(dot(farNormalXY, farNormalXY));
    float3 farNormal = float3(farNormalXY.x, farNormalXY.y, -sqrt(1.0 - farXySq));
    
    // 平坦な中心部(アルファが濃い部分)では大きく丸い farNormal を使い、
    // フチでは細かなディテールを持つ alphaNormal をブレンドして完璧なドームを作る
    float blendFactor = smoothstep(0.4, 0.9, color.a);
    float3 normal = normalize(lerp(alphaNormal, farNormal, blendFactor));
    
    // ビュー空間の3D座標（深度バッファに依存せず、安全な疑似視線ベクトルを作る）
    // 画面中央から放射状に伸びる視線ベクトル（パースペクティブ）。Zを深めにして極端な歪みを防ぐ。
    float3 viewDir = normalize(float3(input.uv.x * 2.0 - 1.0, -(input.uv.y * 2.0 - 1.0), -2.5)); 
    
    // --- 表面の波のディテール（揺らぎ） ---
    // 時間とUV座標を利用して法線を少し歪ませる
    float time = gTime * 2.0;
    // 波の強さを抑える（強すぎると光の反射が潰れてしまうため）
    float waveX = sin(input.uv.x * 30.0 + time) * cos(input.uv.y * 20.0 - time) * 0.015;
    float waveY = cos(input.uv.x * 25.0 - time) * sin(input.uv.y * 35.0 + time) * 0.015;
    normal = normalize(normal + float3(waveX, waveY, 0.0));

    // --- ライティングと反射の計算 ---
    // 光源の方向（ハイライトがカメラから見えやすくなるよう、少し手前に倒す）
    float3 lightDir = normalize(float3(-0.3, 0.6, -0.8));
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // フレネル (縁の光の回り込み)
    float3 F0 = float3(0.04, 0.1, 0.04); 
    float rim = 1.0 - NdotV;
    // 参考画像のようなゼリー感・水滴感を出すため、フチの光の回り込みを鋭く（薄く）する
    float3 fresnel = F0 + (1.0 - F0) * pow(rim, 4.0) * 1.5;
    
    // スペキュラ（ハイライト） - 太陽や強い光源の反射
    float3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    // 内側が白飛びしないよう、ハイライトをさらにシャープ（鋭く）にして水滴感を出します
    float specularSoft = pow(NdotH, 64.0) * 0.8;
    float specularHard = pow(NdotH, 256.0) * 2.0; 
    float3 specColor = float3(1.0, 1.0, 1.0) * (specularSoft + specularHard);
    
    // 環境反射 (疑似Skybox) 
    // 入力カラー(avgColor)ベースで動的に環境光を生成し、水とスライムで別々の反射色にする
    float3 avgColor = color.rgb / max(color.a, 0.0001);
    float skyFactor = smoothstep(0.0, 1.0, normal.y); // 上を向いている部分だけ空を反射
    float3 skyColor = saturate(avgColor * 1.2 + float3(0.02, 0.02, 0.02));     // 空色
    float3 groundColor = saturate(avgColor * 0.5);  // 地面色
    // 環境光の主張を抑えて、ベースの色をしっかり残す
    float3 envColor = lerp(groundColor, skyColor, skyFactor) * 0.6;
    
    // リアルな水滴・ガラスの質感を保つため、不自然な自己発光（ネオンのような縁のGlow）は削除し、
    // 環境反射とフレネル、スペキュラのみでフチを表現します。
    float3 surfaceReflection = envColor * fresnel + specColor;
    
    // --- 吸収（Absorption）と水の色 ---
    // 見た目の厚み（中央が厚く、縁が薄い）
    float apparentThickness = NdotV;
    
    // avgColor から動的に縁と中央の色を作る
    float3 shallowColor = saturate(avgColor * 1.25); // 縁の明るい色
    float3 deepColor = saturate(avgColor * 0.625);   // 中央の濃い色
    float3 waterBaseColor = lerp(shallowColor, deepColor, apparentThickness);
    
    // --- 奥行きと水感を出すパターン（コースティクスと気泡） ---
    // コースティクス（光の屈折・集光模様）はスクリーンUVベースでゆらめかせる
    float2 distortedUV = input.uv + normal.xy * 0.03 * (1.0 - apparentThickness);
    float caustics = waterCaustics(distortedUV, time * 0.5);
    // コースティクスの強度を抑えて白飛びを防ぐ
    float3 causticColor = saturate(avgColor * 1.5) * caustics * 0.3;
    
    // --- 立体的な気泡（3D空間ベース） ---
    // 深度バッファ(d0)を用いて、ビュー空間のピクセル座標(p0)を復元
    float d0 = depthTex.Sample(smp, input.uv).r;
    if (d0 > 100.0) d0 = 5.0; // 背景を抜いてしまった場合のフェールセーフ
    float3 p0 = GetViewPos(input.uv, d0);
    
    // ビュー行列の逆行列（回転部分の転置）を使ってワールド空間座標に戻す
    float3x3 invView = transpose((float3x3)gView);
    // 表面の法線による屈折（内部の気泡が表面の丸みで歪んで見える効果）
    float3 p0_distorted = p0 - normal * 0.3 * (1.0 - apparentThickness);
    float3 worldPos = gCamPos + mul(p0_distorted, invView);
    
    // ワールド空間の座標を渡して、3D空間で昇る気泡を生成
    float bubblePattern = bubbles3D(worldPos, time * 0.8);
    float3 bubbleColor = saturate(avgColor * 2.0) * bubblePattern * 1.5;
    
    // ベースカラーに水感のディテールを合成
    waterBaseColor += causticColor * apparentThickness;
    waterBaseColor += bubbleColor * apparentThickness;
    
    // 内部発光 (Subsurface Scattering っぽさ)
    // 光が液体の中を透過して内側から光る表現
    float backLight = pow(max(dot(viewDir, -lightDir), 0.0), 2.0);
    float3 sssColor = shallowColor * backLight * (1.0 - apparentThickness) * 1.0;
    
    float3 scatterColor = waterBaseColor * (NdotL * 0.4 + 0.6) + sssColor;
    
    // --- 通常のアルファブレンド合成 ---
    // カメラに向いている（中央・深い）ほど透明で、フチに向かうほど不透明になる表現
    float minAlpha = 0.55; 
    float maxAlpha = 1.0; // フチは完全に不透明(1.0)にして、背景が透ける透明な枠をなくす
    
    // NdotV(apparentThickness) が 1.0 (中央) の時 minAlpha, 0.0 (フチ) の時 maxAlpha
    float baseAlpha = lerp(maxAlpha, minAlpha, apparentThickness);
    
    // 最終的な色 = (水中の色 * 不透明度) + 表面の光の反射
    // （※C++側のBlendStateが通常アルファブレンド (SrcBlend=SRC_ALPHA) のため、シェーダー内でのアルファ乗算は行わない）
    float3 finalColor = scatterColor + surfaceReflection;
    
    // ハイライトやフチの光が当たっている部分は不透明度を上げる
    float dynamicAlpha = saturate(baseAlpha + (specularSoft + specularHard) * 0.4 + fresnel.x * 0.5);
    float finalAlpha = dynamicAlpha * alphaEdge;
    
    // 以前存在した `finalColor *= alphaEdge;` は削除しました。
    // （これがあると、GPUのBlendStateと二重にアルファが掛かってしまい、フチが黒く濁る原因になります）
    
    return float4(finalColor, finalAlpha);
}
