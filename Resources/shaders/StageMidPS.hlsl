//======================================================
// StageMidPS.hlsl
// 明るいパステル・ウォームグレージュ（Mid）
// 同じ構造：全体の流れ + 弱い揺らぎ
//======================================================
#include "Obj.hlsli"
#include "ProceduralPaper.hlsli"

float4 main(VSOutput i) : SV_TARGET {
    float3 n = normalize(i.normal);

    // ----------------------------------
    // ① グラデ2色（どちらも明るめのグレージュ）
    // ----------------------------------
    // 暗側（でも暗すぎない）
    float3 colDark = float3(0.70f, 0.68f, 0.62f); // くすみ明るめ

    // 明側（パステル寄り）
    float3 colLight = float3(0.86f, 0.84f, 0.78f); // さらに明るい

    // ----------------------------------
    // ② ステージ全体の流れ（XZ斜め）
    // ----------------------------------
    float2 dir = normalize(float2(1.0f, 0.55f));
    float p = dot(i.worldpos.xz, dir);

    const float gMinP = -20.0f;
    const float gMaxP = 300.0f;

    float t_main = (p - gMinP) / max(0.0001f, (gMaxP - gMinP));
    t_main = saturate(t_main);
    t_main = smoothstep(0.10f, 0.90f, t_main);

    // ----------------------------------
    // ③ 弱い揺らぎ（Midは最弱寄り）
    // ----------------------------------
    const float detailSpan = 24.0f; // ゆっくり
    float t_detail = 0.5f + 0.5f * sin((p / detailSpan) * 6.2831853f);

    const float detailAmp = 0.06f; // 0.04〜0.08
    float t = t_main + (t_detail - 0.5f) * detailAmp;
    t = saturate(t);

    // ----------------------------------
    // ④ 色グラデ（←ここが“明るいグラデ”の本体）
    // ----------------------------------
    float3 base = lerp(colDark, colLight, t);

    // ----------------------------------
    // ⑤ 仕上げの白混ぜ（魚との共通点：パステル感）
    // ----------------------------------
    float fogMix = 0.10f; // 明るいので少量
    float3 col = lerp(base, 1.0.xxx, fogMix);

    // ★追加: プロシージャル和紙マテリアルを適用
    ApplyProceduralPaper(i.worldpos.xyz, col, n, 0.4, 0.8);

    // ----------------------------------
    // ⑥ 上面／側面の差（主張しない）
    // ----------------------------------
    float up = saturate(n.y);
    up = smoothstep(0.2f, 0.9f, up);

    float3 top = col * 1.04f;
    float3 side = col * 0.96f;

    col = lerp(side, top, up);
    return float4(col, 1.0f);
}
