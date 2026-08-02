# PostEffect ReadMe

このドキュメントは、追加・整備したポストエフェクトの場所と使いどころをまとめたものです。
主な確認場所は `AssignmentScene` と `GameScene` です。

## 実装場所

| 役割 | ファイル |
| --- | --- |
| ポストエフェクト用レンダーターゲット、Depth SRV、PSO 登録 | `Engine/Renderer.cpp` |
| ポストエフェクト切り替え API とパラメータ | `Engine/Renderer.h` |
| PlayGame 側の状態連動ポストエフェクト | `Game/Systems/PostProcessSystem.h` |
| 評価課題 scene 側の手動確認・戦闘イベント連動 | `Game/Scenes/AssignmentScene.cpp` |
| 共通関数、ノイズ、輝度計算 | `Resources/shaders/PostProcessCommon.hlsli` |
| 各ポストエフェクトの Pixel Shader | `Resources/shaders/*Post*.hlsl` など |

`Renderer::InitPostProcess_()` でシーンカラーと深度をポストエフェクト用 SRV として作成し、各 Pixel Shader を PSO として `pipelines_` に登録しています。
登録名は `renderer->SetPostEffect("EffectName")` で指定します。

## 追加したポストエフェクト

| 登録名 | シェーダ | 内容 |
| --- | --- | --- |
| `Grayscale` | `Resources/shaders/GrayscalePost.hlsl` | 画面をグレースケールへ補間します。敵発見や緊張感の演出用です。 |
| `GaussianFilter` | `Resources/shaders/GaussianPost.hlsl` | 5x5 ガウシアンで画面をぼかします。ヒットストップや衝撃表現に使います。 |
| `OutlinePost` | `Resources/shaders/OutlinePost.hlsl` | 画面色の輝度差からエッジを検出し、輪郭を強調します。ロックオン範囲や注目状態用です。 |
| `DepthBasedOutline` | `Resources/shaders/DepthBasedOutline.hlsl` | 深度差から輪郭を検出します。敵攻撃の予兆など、奥行きの境界を見せたい場面用です。 |
| `RadialBlur` | `Resources/shaders/RadialBlurPost.hlsl` | 画面中央方向に放射状ブラーをかけます。攻撃、回避、高速移動の勢いを出します。 |
| `Vignetting` | `Resources/shaders/Vignetting.hlsl` | 画面端を暗くします。低 HP、死亡、危険状態の演出用です。 |
| `BoxFilter` | `Resources/shaders/BoxFilter.hlsl` | 3x3 平均化フィルタで軽くぼかします。ガード中の集中状態に使います。 |
| `Random` | `Resources/shaders/RandomPost.hlsl` | ラインずれ、ノイズ、色ずれを足したダメージグリッチです。被弾時に使います。 |
| `Dissolve` | `Resources/shaders/Dissolve.hlsl` | FBM ノイズを使って画面を溶けるように変化させます。敵撃破演出で使います。 |
| `FlowingWaterPost` | `Resources/shaders/FlowingWaterPost.hlsl` | 水流の屈折、色味、コースティクス、深度エッジを重ねます。水関連アクションや川のある場面で使います。 |

既存の `Default` は CRT 系の標準ポストエフェクトとして残しています。
`Rich`、`Anime`、`Smoothing`、`PaperFrame` も `Renderer::InitPostProcess_()` で登録されていますが、この README では評価課題向けに追加・使用したものを中心にまとめています。

## 共通パラメータ

`Engine::Renderer::PostProcessParams` で以下の値を渡します。

| パラメータ | 用途 |
| --- | --- |
| `time` | ノイズ、揺らぎ、水流などの時間変化 |
| `noiseStrength` | ノイズやグリッチの強さ |
| `distortion` | ブラー、屈折、エッジ強調などの補助強度 |
| `chromaShift` | 色収差、色ずれ |
| `vignette` | 画面端の暗さ |
| `scanline` | 走査線系の表現 |
| `san` | エフェクト全体の強度。多くの追加エフェクトで 0 から 1 の補間値として使用 |

## AssignmentScene での使い方

`Game/Scenes/AssignmentScene.cpp` に、評価課題用の手動確認とゲームイベント連動を追加しています。

手動確認:

| キー | エフェクト |
| --- | --- |
| `1` | `Default` |
| `2` | `Grayscale` |
| `3` | `GaussianFilter` |
| `4` | `OutlinePost` |
| `5` | `DepthBasedOutline` |
| `6` | `RadialBlur` |
| `7` | `Vignetting` |
| `8` | `BoxFilter` |
| `9` | `Random` |
| `0` | `Dissolve` |

イベント連動:

| 条件 | エフェクト |
| --- | --- |
| 敵 cube が player を発見 | `Grayscale` |
| 敵 cube がロックオン範囲にいる | `OutlinePost` |
| 敵 cube が攻撃予兆中 | `DepthBasedOutline` |
| player が攻撃中 | `RadialBlur` |
| player の攻撃が敵 cube にヒット | `GaussianFilter` |
| player がガード中、またはガード成功 | `BoxFilter` |
| player がガードせず被弾 | `Random` |
| player の HP が低い、または HP が 0 になった | `Vignetting` |
| 敵 cube を撃破 | `Dissolve` |

`QueuePostEffect()` で短いイベント演出をキューに積み、`UpdatePostEffects()` でフェードイン・フェードアウトしながら `SetPostEffect()` と `SetPostProcessParams()` を呼びます。

## GameScene での使い方

PlayGame 側では `Game/Systems/PostProcessSystem.h` を `GameScene` の system 一覧に登録しています。
この system が player の状態や水場の有無を見て、自動でポストエフェクトを切り替えます。

| 条件 | エフェクト |
| --- | --- |
| player 死亡 | `Vignetting` |
| player 被弾 | `Random` |
| 川、水 emitter、水の缶、液状化中 | `FlowingWaterPost` |
| player の HP が 30% 未満 | `Vignetting` |
| 回避、強攻撃、高速移動、ヒットストップ | `RadialBlur` |
| ロックオン対象あり | `OutlinePost` |

`GameScene::DrawUI()` にはデバッグ用の手動切り替えもあります。
`1` で `Default`、`2` で `FlowingWaterPost`、`3` で `RadialBlur`、`4` で `Vignetting`、`5` で `OutlinePost`、`6` で `GaussianFilter`、`7` で `Grayscale`、`8` で `BoxFilter`、`9` で `Random` を確認できます。

## 追加時の手順

新しいポストエフェクトを増やす場合は、基本的に次の流れです。

1. `Resources/shaders/PostProcessCommon.hlsli` の共通関数を使って Pixel Shader を追加する。
2. `Engine/Renderer.cpp` の `Renderer::InitPostProcess_()` で shader を compile し、`pipelines_["登録名"]` に PSO を登録する。
3. scene または system から `renderer->SetPostEffect("登録名")` を呼ぶ。
4. 必要に応じて `Renderer::PostProcessParams` を設定する。
5. `AssignmentScene` や `GameScene::DrawUI()` にデバッグ切り替えを足す。

