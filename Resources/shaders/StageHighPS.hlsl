//======================================================
// StageHighPS.hlsl
// パステル水色 → コーラル
// ・残す：ステージ全体グラデ（t_main）
// ・消す：一ブロック内の補助グラデ（t_detail）
//======================================================
#include "Obj.hlsli"
#include "ProceduralPaper.hlsli"

float4 main(VSOutput i) : SV_TARGET {
    float3 n = normalize(i.normal);

    // 端の2色：パステル水色 → コーラル
    float3 colAqua = float3(0.72f, 0.90f, 0.94f);
    float3 colCoral = float3(0.878f, 0.478f, 0.447f);

    // ステージ全体の流れ（XZ斜め）
    float2 dir = normalize(float2(1.0f, 0.55f));
    float p = dot(i.worldpos.xz, dir);

    const float gMinP = -20.0f;
    const float gMaxP = 300.0f;

    float t_main = (p - gMinP) / max(0.0001f, (gMaxP - gMinP));
    t_main = saturate(t_main);
    t_main = smoothstep(0.10f, 0.90f, t_main);

    // ★ここだけ：ステージ全体グラデだけ残す
    float t = t_main;

    // 色：水色 → コーラル
    float3 base = lerp(colAqua, colCoral, t);

    // パステル化（魚との共通点）
    float fogAqua = 0.10f;
    float fogCoral = 0.16f;
    float fogMix = lerp(fogAqua, fogCoral, t);

    float3 col = lerp(base, 1.0.xxx, fogMix);

    // ★追加: プロシージャル和紙マテリアルを適用
    ApplyProceduralPaper(i.worldpos.xyz, col, n, 0.4, 0.8);

    // 立体感（控えめ）
    float up = saturate(n.y);
    up = smoothstep(0.2f, 0.9f, up);
    col *= lerp(0.97f, 1.03f, up);

    return float4(col, 1.0f);
}








