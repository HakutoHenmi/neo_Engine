# ゲームシーンへの遷移を約42秒から約6.5秒へ短縮

2026-10-27。i7-12650H / RTX 4060 Laptop、Release提出パッケージで計測。**Select → Game（街）の遷移は、初回起動相当で約42秒から約6.5秒になった。**

## 計測方法

`--load-profile-auto` を付けて起動すると、キー入力を送らずに Title → Select → Game（本編の `chrono.json`）と進み、Gameを30フレーム表示して終了する。各処理の時間はEXEと同じフォルダの `load-profile.txt` に、プロセス開始からの秒・区分・所要ミリ秒・名前の順で残る。`--load-profile` だけなら記録のみ行う。どちらも付けなければ何もしない（[LoadProfile.h](../Engine/LoadProfile.h)）。

計測した項目: シェーダーのコンパイル、テクスチャ、メッシュ、パイプライン、音、NTC、シーン切り替え、`Renderer::InitializeDeferred`、`ChronoSystem` の構築段階、LOD、まとめ読みの各バッチ。

このノートPCは同じ条件でも±2秒ほど揺れる。下の値は代表値。

## 変更前の内訳（Select → Game 約42秒）

| 区間 | 時間 | 主な中身 |
| --- | ---: | --- |
| `Renderer::InitializeDeferred`（3D初期化） | 16.9 s | **HLSLの実行時コンパイル146回で16.1 s**（`FluidRaymarch` 4.0 s、`Gtao` 2.5 s など） |
| `ChronoSystem::BuildCityStage` | 21.8 s | **2K JPEG 147枚を1枚ずつ読込で18.3 s**（展開とCPUのミップ生成、1枚ごとにGPU待ち）、小物のLOD生成 1.9 s |
| その他 | 約3 s | 画面切り替え演出、音、シーン読込など |

## 実施した対策

1. **シェーダーのバイトコードキャッシュ**（[ShaderCache.cpp](../Engine/ShaderCache.cpp)）
   - `Renderer::CompileShader` と `CompileShaderFromFile` のコンパイル結果（DXBC）を保存し、次回から読むだけにする。
   - キーはターゲット・エントリ・フラグと、本体と `#include` で辿った全ファイルの中身。シェーダーを書き換えると別のキーになり、古い結果は使われない。
   - 読む順は、提出物の `Resources/ShaderCache` →ユーザーごとの `%LOCALAPPDATA%\LiquidTime\ShaderCache`。DXBCのヘッダーと大きさを確かめ、壊れたファイルは捨てて再コンパイルする。
   - `scripts/New-SlimeRelease.ps1` がパッケージを作るとき、同梱EXEを `--load-profile-auto --write-shader-cache` で一度動かし、起動からGameまでに使うシェーダー（157個、約1.3 MiB）を `Resources/ShaderCache` に書き出してからZIPにする。**このため初めて起動するPCでもコンパイルが起きない。**
2. **街の素材のまとめ読み**（`Renderer::BeginAssetBatch` / `EndAssetBatch`）
   - `BuildCityStage` の間は、`LoadTexture2D` がハンドルだけを確保し、`PrepareDistanceLods` は予約だけする。関数を抜けるときに次の順で処理する。
   - LOD: 22モデルの簡略化をJobSystemで並列に行い、頂点のGPU転送を1回の待ちにまとめる。
   - テクスチャ: JPEGの展開とアップロード用バッファへの書き込みをワーカーで並列に行う。メインスレッドはコピー命令を積むだけにし、12枚ずつ、次の組の展開と今の組の転送を重ねる。
   - **ミップはGPUで作る**（[GenerateMipsCS.hlsl](../Resources/shaders/GenerateMipsCS.hlsl)、2×2の平均。色のマップはリニア空間で平均してsRGBに戻す）。CPUのミップ生成は1枚あたり平均173 msで、並列でも詰まっていた。
   - まとめ読みは街の組み立てだけで使う。UIなど、それ以外の `LoadTexture2D` は従来どおりCPUでミップを作る。

## 変更後（Release提出パッケージ）

| 条件 | Select → Game | 3D初期化 | うちシェーダー | 街の組み立て | うちテクスチャ |
| --- | ---: | ---: | ---: | ---: | ---: |
| 変更前 | 約42 s | 16.9 s | 16.1 s | 21.8 s | 18.3 s |
| **初回起動（同梱キャッシュのみ）** | **6.5 s** | 0.47 s | 0.19 s | 3.1 s | 1.4 s |
| 2回目以降 | 6.0〜6.3 s | 0.33 s | 0.09 s | 2.9〜3.0 s | 1.3〜1.45 s |

残りの主な時間は、画面切り替えの演出（遷移要求からシーン切り替え開始まで約0.6 s）、Game表示後の30フレーム（計測の都合）、LODのまとめ処理（約0.7〜1.0 s、最大の1モデルで約0.3 s）、メッシュ読込（約0.6 s）。

## 画質への影響

- GPUで作ったミップは、従来のCPU（DirectXTex）の結果と完全には一致しない。`--package-smoke` が街の床材の色（sRGB）と粗さ（リニア）の全12段を比べ、結果を `PackageCheck/gpu-mips.txt` に残す。
  - 最上段: 完全一致。
  - 色: 1段目50.0 dB → 最小の1×1で37.3 dB。
  - 粗さ: 49.4 dB → 43.4 dB。
- 差は、前の段の8ビット値から次の段を作るために丸め誤差が積み重なるもので、小さい段ほど大きい（1×1で8ビット値の数値程度）。街のスクリーンショットでは見た目の差は確認できなかった。

## 検証

- Release提出パッケージ: `--package-smoke` PASS（NTC道路の一致確認を含む）、`--dlss-package-smoke` PASS、クラッシュダンプなし。
- Developmentビルド: 警告なしでビルド成功。
