# Windows CI

GitHub Actions の `.github/workflows/ci.yml` で、push / pull request / merge queue / 手動実行時に
Visual Studio 2022・x64 の次の3構成を個別に検証します。

| 構成 | 確認内容 |
| --- | --- |
| Debug | Debug CRT・デバッグ設定でソリューションをビルドし、CPU / ツール回帰テストを実行 |
| Development | エディタを含む開発設定でビルドし、CPU / ツール回帰テストを実行 |
| Release | 最適化設定でビルド・テストし、素材・必須DLL・ライセンスの梱包を検査 |

各構成は独立したジョブです。1構成が失敗しても残りの構成は最後まで実行します。
従来と同名の **Windows build and tests** は3構成の集約チェックで、
すべて成功した場合だけ成功します。失敗・キャンセル・スキップを成功扱いにしません。

## 必須チェック

- LLVM 19.1.7 による自作 C/C++ の整形チェック。
- 固定した公式ソースからの Assimp / zlib の準備と、エンジン・ゲーム・DirectXTex のビルド。
- 各構成での10スイートの回帰テスト。
- Release の素材・DLL・ライセンス検査。

回帰テストは `Chrono`、`DataDriven`、`DesignPattern`、`Encapsulation`、`ControllerMenu`、
`SlimeTuning`、`WeaponShape`、`CodeQuality`、`ReadabilityTools`、`QualityLogging` です。
7つの C++ スイートは構成別に再コンパイルします。Debug は `/MTd /Od /D_DEBUG`、
Development は `/MT /Od`、Release は `/MT /O1 /DNDEBUG` を使用します。
`USE_IMGUI` も有効にし、ControllerMenu は実際の ImGui の実装にリンクします。
Release でもテストの判定が消えないよう、製品ヘッダーを読み込んだ後にテスト側の `assert` を有効にしています。
残り3つは構成に依存しない整形・解析ツールのテストです。
途中のスイートが失敗しても残りを実行し、最後に全体を失敗させます。

GPU を必要とする実機・描画・性能テストは対象外です。
`Run-SlimeLifeTests.ps1` も選定テストには含めていません。
このスクリプトは GPU テストまで実行するうえ、ルールテストにも既知の
`first stage rules are wrong` の失敗が残るためです。条件やアサーションは変更していません。
CI 成功は、すべての描画機能や全テストの正常動作を保証するものではありません。

## 依存ライブラリと梱包

`bootstrap_dependencies.ps1 -Configuration <構成>` は SHA256 で確認した Assimp 5.3.1 をビルドします。
Debug は Debug CRT の `assimp-vc143-mtd.lib` と `zlibstaticd.lib` を専用フォルダーに配置し、
Development / Release は Release CRT のライブラリを使用します。
Assimp 5.3.1 の Debug PDB インストールにはパスの不一致があるため、PDB のインストールは無効です。

`scripts/ci/build.props` は出力先と依存ライブラリの参照を CI 用に切り替えます。
通常の Visual Studio の出力先や `externals` の SDK は書き換えません。
ビルド中の品質監視は無効にし、解析は専用ステップで実行します。

Release ビルド前に `prepare_release.ps1` が公式 RTXNTC v0.10.0-beta SDK と DLL の SHA256 を検証し、
CI 専用フォルダーに DLL だけを復元します。SDK ツールは実行しません。
Release のビルド後イベントで実行ファイルの隣に素材・DLL を配置し、`verify_release.ps1` が次を検査します。

- 素材マニフェストと全 HLSL / HLSLI が揃い、梱包元と SHA256 が一致すること。モデル用 PNG は既存の DDS 変換を適用。
- 必須の `NTC/libntc.dll` と付属ライセンスの内容が一致すること。
- 任意の Streamline SDK がある場合は NVIDIA 署名と必要な DLL・ライセンスを検査。
  Streamline が使う VC ランタイム3ファイルも、Visual Studio 2022 の再頒布フォルダーから Microsoft 署名を確認して同梱。
  SDK がないクリーンな CI 環境では native rendering のパッケージを検証。
- EXE と梱包した DLL のインポートを `dumpbin` で確認し、Debug ランタイムへの依存や未梱包の非システム DLL がないこと。
  遅延ロードの DLL も記録・検査します。D3D12 で使用しない Streamline の任意の Vulkan レイテンシードライバー `NvLowLatencyVk.dll` だけは同梱対象外です。

GPU を必要とする起動・シェーダーキャッシュ作成は CI では実行しません。
配布前には別途、対象 GPU 上で起動・描画を確認してください。

## 出力とログ

- 実行ファイル: `artifacts/ci/bin/<構成>/DirectXGameApp.exe`
- 中間ファイル: `artifacts/ci/obj/<プロジェクト>/<構成>/`
- ビルド・テストログ: `artifacts/ci/logs/<構成>/`
- 各テストの終了コード: `artifacts/ci/logs/<構成>/tests.json`
- Release 検査結果・梱包ファイルの SHA256: `artifacts/ci/logs/Release/package.json`
- C++ テストの実行ファイル: `tests/out/<構成>/`

成功・失敗時とも Actions の Artifacts に `ci-logs-<構成>-<実行ID>-<試行番号>` を14日間保存します。
Release の全チェック成功時には、検証済みの EXE・Resources・NTC・任意の Streamline を
`release-package-<実行ID>-<試行番号>` として保存します。中間ファイルや開発用 `.lib` は含みません。

品質レポートの収集は Development で1回実行し、Lizard・PMD CPD・clang-tidy の改善候補を保存します。
既存の指摘件数は失敗条件にしません。ツール準備や収集スクリプト自体の失敗は CI を失敗させます。
`error`・`timeout`・`skipped` は警告と Summary に表示し、解析成功や指摘ゼロとして扱いません。

## ローカル実行

Visual Studio 2022 の C++ / CMake ツール、LLVM 19.1.7、Python が必要です。
PowerShell 7 でリポジトリのルートから実行します。

```powershell
.\scripts\build\setup_code_quality_tools.ps1
.\scripts\build\format_code.ps1 -All -Check

foreach ($configuration in 'Debug', 'Development', 'Release') {
    .\scripts\ci\bootstrap_dependencies.ps1 -Configuration $configuration
    if ($configuration -eq 'Release') { .\scripts\ci\prepare_release.ps1 }
    .\scripts\ci\run_ci.ps1 -Configuration $configuration
}

# 構成を指定してテストだけ再実行
.\scripts\ci\run_ci.ps1 -Configuration Release -Stage Tests

# 梱包済みの Release を再検査（欠落したファイルは補修せず失敗させる）
.\scripts\ci\run_ci.ps1 -Configuration Release -Stage Package

.\scripts\build\quality_log.ps1 -LogDirectory artifacts/ci/quality
```

`run_ci.ps1` は既定で Development、`-Stage All` です。
Release の `All` はビルド・テストに加えて梱包検査も実行します。
ビルドだけなら `-Stage Build` を指定します。

## 導入時の確認 (2026-11-29)

ローカルで Debug / Development / Release のソリューションビルドと、各10スイート（計30スイート）が成功しました。
Release は素材864ファイル、必須の NTC、手元にある DLSS SDK と VC ランタイムを含む877ファイルを検査しました。
SDK のない native rendering 分岐も隔離したフォルダーで確認しています。

素材の欠落・改変、NTC DLL の欠落、ライセンス改変が失敗になることと、失敗した検査が素材を勝手に補修しないことを確認しました。
途中のテストを失敗させる隔離 fixture では、全10スイートを実行し、失敗の終了コードを保存して全体を失敗させることも確認しています。
自作 C++ 全287ファイルの整形検査、actionlint、PowerShell / XML / JSON の構文検証も通過しました。
既存の整形違反は、コードの字句・文字列・コメントを保ったまま書式を揃えています。
確認ログは `artifacts/ci/` にあります。GitHub 上での実行は、この変更の commit / push 後です。

## マージ条件

ワークフローを配置するだけではマージ制限は設定されません。
GitHub の `master` 向けブランチ保護で **Windows build and tests** を必須チェックに設定すると、
3構成の成功をマージ条件にできます。従来のチェック名を維持しているため、同名の既存設定も利用できます。
この変更ではリポジトリ側の保護ルールを変更していません。
