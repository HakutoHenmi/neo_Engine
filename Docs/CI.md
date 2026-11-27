# CI (GitHub Actions)

`.github/workflows/ci.yml` は、全ブランチへの push、プルリクエスト、マージキュー、手動実行で起動します。
Windows Server 2022 / Visual Studio 2022 の環境で `Development | x64` をビルドします。
GitHub にこの設定を含むコミットを push すると有効になります。

## 成否の条件

チェック名は **Windows build and tests** です。次のいずれかが失敗すると CI は失敗します。

- LLVM 19.1.7 による `Engine` / `Game` / `tests` の自作 C/C++ の整形チェック。
- Assimp の準備と、エンジン・ゲーム・DirectXTex を含むソリューションのビルド。
- 選定した CPU / ツール回帰テストのいずれか。

回帰テストは `Chrono`、`DataDriven`、`DesignPattern`、`Encapsulation`、`ControllerMenu`、
`SlimeTuning`、`WeaponShape`、`CodeQuality`、`ReadabilityTools`、`QualityLogging` の 10 スイートです。
失敗しても残りのスイートを実行してログを残し、最後に全体を失敗させます。
`continue-on-error` でこれらの失敗を成功扱いにする設定はありません。

GPU を必要とする実機・描画・性能テストは、この CI の対象外です。
`Run-SlimeLifeTests.ps1` も選定テストには含めていません。
このスクリプトは GPU テストまで実行するうえ、ルールテストにも既知の
`first stage rules are wrong` の失敗が残るためです。条件やアサーションは変更していません。
CI の成功は、全テスト・全描画機能の正常動作を保証するものではありません。

## 依存ライブラリと出力

Assimp の `.lib` は Git の管理対象外なので、`scripts/ci/bootstrap_dependencies.ps1` で準備します。
`dependencies.json` に固定した公式ソース (5.3.1) を SHA256 で確認し、v143・x64・Release・
静的 CRT で Assimp と zlib をビルドします。対応するヘッダーも同じソースから配置します。

`scripts/ci/build.props` は CI 専用の出力先と依存ライブラリの参照を設定します。
通常の Visual Studio のプロジェクト設定や `externals/assimp/lib` は書き換えません。
ビルド中のローカル品質監視は無効にし、解析は後続の専用ステップで実行します。

- 実行ファイル: `artifacts/ci/bin/Development/DirectXGameApp.exe`
- 中間ファイル: `artifacts/ci/obj/<プロジェクト>/Development/`
- ビルド・テストログ: `artifacts/ci/logs/`
- 各テストの終了コード: `artifacts/ci/logs/tests.json`

## 品質レポート

Engine / Game の自作 C++ を対象に、Lizard・PMD CPD・clang-tidy の改善候補を収集します。
既存の指摘件数は CI の失敗条件にしません。
整形チェックは別途、必須条件として実行します。品質ツールのインストールや収集スクリプト自体の失敗は CI を失敗させます。

clang-tidy は全体解析で異常終了・タイムアウトする場合があります。
解析結果の `error`・`timeout`・`skipped` は GitHub の警告とジョブの Summary に表示し、
解析成功や指摘ゼロとしては扱いません。しきい値やタイムアウトは既存の品質設定を使います。

成功時・失敗時とも、Actions の実行画面の Artifacts に `ci-logs-<実行ID>-<試行番号>` を保存します (14 日間)。
ビルドのテキスト / バイナリログ、各テストの出力、品質解析の JSON / 生ログ / CSV / CPD XML が含まれます。
実行ファイル・中間ファイル・ダウンロードしたツールはアップロードしません。
AI に確認を依頼する際は、品質レポートの `latest.json` と該当するテストログを使用してください。

## ローカルで同じビルド・テストを実行する

Visual Studio 2022 の C++ / CMake ツール、LLVM 19.1.7、Python が必要です。
PowerShell 7 で、リポジトリのルートから実行します。

```powershell
.\scripts\build\setup_code_quality_tools.ps1
.\scripts\build\format_code.ps1 -All -Check
.\scripts\ci\bootstrap_dependencies.ps1
.\scripts\ci\run_ci.ps1

# ビルド済みで、テストだけ再実行する場合
.\scripts\ci\run_ci.ps1 -Stage Tests

# CI と同じ保存先に品質レポートを収集
.\scripts\build\quality_log.ps1 -LogDirectory artifacts/ci/quality
```

## 導入時の確認 (2026-11-27)

ローカルで次の確認が通過しました。GitHub 上の初回実行は、変更の commit / push 後です。

- 公式ソースから Assimp / zlib を準備し、その参照でソリューションをビルド。
- 最終ソースでの Development ビルドと、10 スイートの再実行。
- 自作 C++ 288 ファイルの整形検査。残っていた 19 ファイルの書式差分を揃え、
  C++ のトークン・文字列・コメントが変わっていないことも確認。
- actionlint と PowerShell パーサーによるワークフロー / スクリプトの検証。
- 途中のテストだけを失敗させた隔離 fixture で、残りも実行し、CI 全体は失敗することを確認。
- 品質レポートを収集し、clang-tidy のタイムアウトが警告と Summary に表示されることを確認。

確認ログは `artifacts/ci/` にあります。解析の未完了状態は品質レポートに残しています。

## ビルド失敗時のマージを禁止する

このワークフローを配置するだけでは、push やマージの禁止までは設定されません。
初回の CI 実行後、GitHub の `master` 向けブランチ保護ルールで
**Windows build and tests** を必須ステータスチェックに設定します。
その設定で、CI が通らないプルリクエストのマージを制限できます。
この変更ではリポジトリ側の保護ルールは変更していません。

詳細は [GitHub のブランチ保護の説明](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches) を参照してください。
