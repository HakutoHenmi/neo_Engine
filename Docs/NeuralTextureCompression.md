# neo_Engine: NTC / TSNC 導入検討

調査日: 2026-10-27。本書は設計調査。後続で道路1材質のBCn基準とNTC On Loadを実機測定した。結果は [道路材質GPU実験](RoadTextureGpuExperiment.md) を参照。NTC復元は動作したが、今回の設定では通常BCnの方がロードが速く、画質も高かった。ゲーム全体のFPS・起動時間への効果は未測定。

さらに [法線BC5固定のハイブリッド調整](RoadTextureHybridExperiment.md) を実施。法線はBC5と全mipのバイト一致を保ち、要求12 bppで通常BCnに対して材質単体約31%削減を得た。色・粗さ・AOは完全無劣化ではない。

## 判断

- NVIDIA NTC: 導入試作は可能。まず道路材質1組を対象に Inference on Load を検証する。
- NTC Inference on Sample: VRAM削減の研究候補。提出版の既定経路にはしない。
- Intel TSNC: 公開サンプルを確認できるが、公開された完成版SDKの提供条件は今回確認できなかった。サンプルによる別の検証対象とする。
- 提出版は通常のDDS/BCn読み込みを先行し、常に通常材質へ戻せる構造を作る。

## 実機と既存コード

nvidia-smi: NVIDIA GeForce RTX 4060 Laptop GPU / 8188 MiB / driver 610.78。
NTCの推奨GPUクラスには該当するが、現在公開SDKのDX12 LinAlg高速経路が要求する620.12以上のプレビュードライバー条件を満たしていない。

- Engine/Renderer.cpp: LoadTexture2DUncached_ はWICで画像を読み、RGBA8へ変換してCPUでミップ生成する。
- Engine/Renderer.cpp: 一般の描画シェーダーはD3DCompileFromFileでShader Model 5.0としてコンパイルする。On Sampleの統合にはDXC/Slang等の新しい経路が必要。On Load実験ではlibNTCのプリコンパイル済みComputeシェーダーを使用でき、DXCやCUDA Toolkitの追加インストールなしでD3D12復元できた。
- Resources/shaders/EnvironmentSurfacePS.hlsl: 色・法線・粗さ・AOを個別にサンプリングする。
- Game/Systems/CityStage.cpp: 材質単位の4マップがそろっており、ニューラルな「テクスチャセット」へ対応付けやすい。
- Engine/Renderer.cpp: LoadCubeMap はDDSを扱うが、通常の2DテクスチャをBCnのままロードする経路は別途必要。

## SDKの条件

[NVIDIA NTC README](https://github.com/NVIDIA-RTX/RTXNTC)で確認した版は v0.10.0 BETA。

- 現行DX12 LinAlg高速経路はAgility SDK 1.721.0-preview、SM 6.10、対応プレビュードライバー、Developer Modeを要求し、SDKはこの経路での製品出荷を禁止している。
- DP4aの非CoopVec経路もある。動作可能なGPUと実用的な速度が出るGPUを区別して検証する。
- [Inference on Sampleガイド](https://github.com/NVIDIA-RTX/RTXNTC/blob/main/docs/integration/InferenceOnSample.md)は、DP4aによるサンプル時復元は遅いため機能検証向けとし、STFとシェーディング後の再構成を説明している。
- SDKの使用・再配布は[NVIDIA RTX SDKs LICENSE](https://github.com/NVIDIA-RTX/RTXNTC/blob/main/LICENSE.txt)と採用版の配布条件を確認する。現在のStreamline導入だけではNTCは有効にならない。

Intelは[公式記事](https://www.intel.com/content/www/us/en/developer/articles/technical/cooperative-vectors-demo.html)と[公開サンプル](https://github.com/GameTechDev/TextureSetNeuralCompressionSample)を確認した。公開サンプルにはMITライセンスと --disable-coop オプションがある。ただし、その条件を未確認の別SDKへ一般化しない。フォールバックの存在だけではRTX 4060での速度は保証されない。

## 方式を混同しない

| 経路 | ディスク | 実行中のVRAM | 描画時の追加計算 | 主な改修 |
| --- | --- | --- | --- | --- |
| 通常のBCn | 内容・JPEGとの比較次第 | RGBA8より削減 | 通常のハードウェア展開 | DDSの2Dロード、ビルド時変換 |
| NTC On Load → BCn | NTCファイルで削減を狙う | 復元先BCn相当 | 復元後は通常の描画 | NTCファイル、Compute復元、BCn変換、ロード同期 |
| NTC On Sample | NTCファイルで削減を狙う | 潜在データを保持して削減を狙う | 毎回の推論・フィルタ処理 | 新マテリアル、SRV/重み、DXC/Slang、STF、時間方向の画質検証 |

[On Load統合ガイド](https://github.com/NVIDIA-RTX/RTXNTC/blob/main/docs/integration/InferenceOnLoad.md)に沿ってエンジン側がGPUリソース、Computeパイプライン、ディスパッチ、同期を実装する。ライブラリをリンクするだけでは完成しない。

6.5 GB→970 MBや最大18倍という数値を、提出ZIP全体の削減率には使わない。比較対象がRGBA・BCn・VRAM・ディスクのどれか、画質と復元方式が同じかをそろえる。どちらも一般的な可逆アーカイブ圧縮とは異なり、画質差を伴う。

## 先に作る比較基準

実ファイルのasphalt_02の色・法線・粗さ・AOはすべて2048×2048。
現在の4枚RGBA8＋全ミップは理論上約85.3 MiB。
色BC7、法線BC5、粗さBC4、AO BC4では約16.0 MiBとなる見積もり（約81%削減）。GPU計測値ではなく、フォーマットのビット数からの計算であり、その他のGPUバッファは含まない。

BC5法線ではZ成分の再構成が必要。sRGB/linear、ミップ、法線Yの向きも現行材質に合わせる。
[BCnの形式とブロックサイズ](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11)

この比較をせずにRGBA8とNTCだけを比べると、通常BCnでも得られる削減をニューラル圧縮だけの成果として数えてしまう。

## 最小の試作

1. 道路材質asphalt_02の4マップだけを対象にする。UI文字・透明な草・動的な流体データは対象外。
2. 元のRGBA8とBCnの切り替えを用意し、同じカメラ・解像度・設定で基準を記録する。
3. 素材変換時にNTCを生成する。実行開始時に学習させない。
4. 非CoopVecのOn Load→BCn経路を追加し、出力したBCnを通常のTextureHandleとして既存描画へ渡す。
5. 同じ材質をOn Sampleで試すのは、対応環境と安定したBCnフォールバックを用意した後とする。
6. 非対応・復元失敗時はBCn材質へ戻す。

比較するのは配布サイズ、タイトル/本編ロード時間、ロード中の一時VRAMを含むピーク、定常VRAM、GPUフレーム時間の中央値とp95、カメラ移動時のちらつき、法線・粗さの画質。流体/DLSS/RTを通常設定にした場面でも測る。

今回の起動約2.2秒はDLSS等の初期化が大きく、NTC導入だけで即起動になる根拠はない。主な狙いは本編の素材容量、VRAM、ストリーミングである。
