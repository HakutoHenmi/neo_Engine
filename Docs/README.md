# ドキュメント目次

このフォルダーには、ゲームとエンジンの仕様・設計・作業記録をまとめています。
初めて読む場合は、上から順に読むのがおすすめです。

## まず読むもの

| 資料 | 内容 |
| --- | --- |
| [../README.md](../README.md) | プロジェクトの概要、ビルドと実行、フォルダー構成 |
| [GameDesign.md](GameDesign.md) | ゲームの概要 (1 ページ) |
| [SlimePlayerDemo.md](SlimePlayerDemo.md) | スライムの移動・変形試験場と停止時のカメラ |
| [SlimeLife.md](SlimeLife.md) | **現在のゲーム仕様**:操作、成長と変形、街ステージ、調合画面 |
| [GameData.md](GameData.md) | 調整値・表・画面の文字を置いている JSON の一覧と編集方法 |

## エンジン

| 資料 | 内容 |
| --- | --- |
| [TDEngine_Manual.md](TDEngine_Manual.md) | エンジンの基本 (ECS とスクリプト) とゲームの作り方 |
| [TDEngine_Technical_Manual.md](TDEngine_Technical_Manual.md) | エンジンの内部構造 (アーキテクチャ、描画、システム) |
| [FluidVolume.md](FluidVolume.md) | スライムの GPU 流体と、表面の 3D 再構成 |
| [FluidSandbox.md](FluidSandbox.md) | エディタのガラス水槽で流体を試す機能 |
| [GraphicsRenderingAudit.md](GraphicsRenderingAudit.md) | 描画の改善 (HDR ブルーム、GTAO、PBR、DLSS、RT の影) と実機での検証 |
| [PostEffect_ReadMe.md](PostEffect_ReadMe.md) | ポストエフェクトの一覧と使いどころ |

## ゲームの画面と設計の見直し

| 資料 | 内容 |
| --- | --- |
| [UITheme.md](UITheme.md) | UI の配色と共通部品 |
| [StatePatternReview.md](StatePatternReview.md) | State パターンでのリファクタリング |
| [Encapsulation.md](Encapsulation.md) | カプセル化のリファクタリング |

## 配布

| 資料 | 内容 |
| --- | --- |
| [ReleasePackaging.md](ReleasePackaging.md) | リリース版の作り方と、配布する素材の一覧 |
| [NVIDIA-RTX-SDK-LICENSE.txt](NVIDIA-RTX-SDK-LICENSE.txt) | DLSS (Streamline) のライセンス全文 |

## 作業記録

| 資料 | 内容 |
| --- | --- |
| [DevelopmentLog.md](DevelopmentLog.md) | 評価課題の追加実装と、エディタなどの改修の記録 (旧 README) |
| [archive/](archive/README.md) | 今のゲームには使われていない、過去のモード・計画・分析の記録 |
