# C++ の整形と静的解析

`clang-format` でインデント・改行を統一し、`clang-tidy` で複雑な関数や冗長な処理などを検出します。
Lizard で関数の複雑度を測り、PMD CPD で重複したコードを探せます。
以下のコマンドはリポジトリ直下で実行します。

## 必要なもの

- Visual Studio 2022 の「C++ によるデスクトップ開発」、v143、Windows SDK
- LLVM 19.x の完全な配布版。基準バージョンは **19.1.7** です。

```powershell
winget install --id LLVM.LLVM --exact --source winget --version 19.1.7
```

標準のインストール先は `C:\Program Files\LLVM` です。
Visual Studio 同梱の clang-tidy だけでは、DirectXMath が必要とする `cpuid.h` などの
組み込みヘッダーが不足する場合があるため、完全な配布版を使います。

スクリプトは標準の LLVM、Visual Studio 2022 同梱版、ほかの Visual Studio 同梱版、
PATH 上のツールの順に探します。別の場所にある場合は各スクリプトの
`-ToolPath 'D:\LLVM\bin\clang-format.exe'` / `-ToolPath 'D:\LLVM\bin\clang-tidy.exe'` で指定できます。

## 整形

```powershell
# HEAD との差分があるファイルと未追跡ファイルを整形（ファイル全体が対象）
.\scripts\build\format_code.ps1

# 同じ対象を検査するだけ。違反があれば終了コード 1
.\scripts\build\format_code.ps1 -Check

# 指定したファイルだけを整形
.\scripts\build\format_code.ps1 -Files Engine/Core/JobSystem.cpp

# 自作の C/C++ ファイル全体を検査する
.\scripts\build\format_code.ps1 -All -Check
```

対象は `Engine/`・`Game/`・`tests/` 内の C/C++ ソース、ヘッダー、`.inl` です。
削除済みファイル、Git の除外対象、`Engine/ThirdParty/` は自動選択しません。
`-All` で全体を整形する場合は `-Check` を外します。

ルールはルートの `.clang-format` にあります。基本はタブ幅 4、120 文字での折り返し、
空でない関数を複数行で書く形式です。include の並び順は維持します。
日本語コメントと文字列は元の行を維持し、構造体の配列はインデントをそろえて表示します。
`.editorconfig` も同じインデント幅に合わせています。
外部コードには専用の `.clang-format` を置き、エディター経由の整形も無効にしています。

## 静的解析

```powershell
# Engine と Game を解析（コンパイル・リンク・実行は行わない）
.\scripts\build\analyze_code.ps1

# 一方のプロジェクトだけを解析
.\scripts\build\analyze_code.ps1 -Project Engine

# 指定したソースと、そのソースが読み込むヘッダーを解析
.\scripts\build\analyze_code.ps1 -File Engine/Core/JobSystem.cpp

# ビルド構成を変えて解析
.\scripts\build\analyze_code.ps1 -Project Game -Configuration Debug
```

既定の構成は `Development|x64` です。MSBuild から実際の include パス・マクロ・C++20 の
設定を取得するため、`compile_commands.json` を手書きする必要はありません。
解析結果とコンパイルデータベースは `artifacts/code-quality/` に出力します。
`externals/`・`Engine/ThirdParty/` は解析対象から外しています。
ヘッダーや独立した `tests/*.cpp` は `-File` に指定できません。

ルールは `.clang-tidy` に集約しています。関数の大きさ・複雑さ、括弧の省略、
重複した分岐、不要なコピー、`override` の付け忘れ、静的解析によるバグ検出が対象です。
可読性などの指摘は警告として表示し、自動修正は行いません。
解析自体に失敗した場合や構文エラーがある場合は終了コード 1 になります。

コンパイルに直接組み込むネイティブのコード分析は明示実行のままです。
ビルド開始時には、後述の品質ログ収集を別プロセスで開始します。
Visual Studio ではプロジェクトを右クリックして「コード分析の実行」を選ぶと、
同じ `.clang-tidy` 設定で解析できます。完全な LLVM が標準の場所にあれば自動で利用します。
Visual Studio で別の LLVM を使う場合は、プロジェクトのプロパティの
「コード分析 → Clang-Tidy → Clang-Tidy ツール ディレクトリ」に `bin` の場所を指定します。

## Lizard と PMD CPD

初回は Python とインターネット接続が必要です (検証環境は Python 3.14)。
専用の Python 仮想環境、PMD、Java ランタイムを `artifacts/code-quality/tools/` に配置します。
システムの PATH や JAVA_HOME の変更、仮想環境の手動有効化は必要ありません。

```powershell
# 初回セットアップ / ツールを消した場合の再セットアップ
.\scripts\build\setup_code_quality_tools.ps1

# 関数の行数・分岐の複雑度・引数の数を計測
.\scripts\build\measure_complexity.ps1

# 100 トークン以上の重複ブロックを検索
.\scripts\build\find_duplicates.ps1

# プロジェクトを絞る / テストコードも含める
.\scripts\build\measure_complexity.ps1 -Project Engine
.\scripts\build\find_duplicates.ps1 -IncludeTests

# 指定ファイルだけを解析 (PowerShell からは配列を渡せる)
.\scripts\build\measure_complexity.ps1 -Files Game/Systems/WeaponCraft.cpp
.\scripts\build\find_duplicates.ps1 -Files Engine/Graphics/Renderer.cpp,Engine/Graphics/RendererNtc.cpp

# CI 用: 指摘があった場合に終了コード 1 にする
.\scripts\build\measure_complexity.ps1 -Check
.\scripts\build\find_duplicates.ps1 -Check
```

通常は `Engine/` と `Game/` を対象にし、`externals/`・`Engine/ThirdParty/`・生成物は除外します。
`-IncludeTests` で `tests/` も追加できます。ソースの自動修正は行いません。
保存・ビルドからの自動実行は、次の品質ログ収集が担当します。

| レポート | 内容 |
| --- | --- |
| `artifacts/code-quality/readability/complexity.csv` | 全関数の行数・循環的複雑度・引数数・位置。複雑度の高い順 |
| `artifacts/code-quality/readability/complexity.json` | 件数、しきい値、しきい値を超えた関数 |
| `artifacts/code-quality/readability/cpd.xml` | 重複したコード、元ファイルのパスと行番号 |
| `artifacts/code-quality/readability/duplicates.csv` | 重複グループごとの場所、トークン数、行数 |
| `artifacts/code-quality/readability/duplicates.json` | 解析したファイル数と重複グループの件数 |

どちらも `-OutputDirectory` で出力先を変えられます。
Lizard の既定のしきい値は複雑度 25・コメントを除く関数の行数 100・引数 8 個です。
`-MaxComplexity`・`-MaxFunctionLines`・`-MaxParameters` で変更できます。
CPD のしきい値は `-MinimumTokens` で変更できます。
通常実行では指摘があってもレポートを出して成功終了し、`-Check` のときだけ指摘を失敗扱いにします。
ツールの起動失敗や、CPD がファイルを読み取れなかった場合は通常実行でも失敗します。

Lizard の複雑度は循環的複雑度であり、clang-tidy の認知的複雑度とは別の指標です。
数値や重複の検出は、分割・共通化する箇所を判断する手がかりとして使います。
CPD は `.inl` と末尾改行のないヘッダーにも対応するため、出力先の `cpd-input/` に解析用コピーを作ります。
元のソースは変更せず、レポート内のパスを元のファイルに戻し、全選択ファイルの解析完了を確認します。

導入バージョンは Lizard **1.26.1**、PMD CPD **7.29.0**、Temurin JRE **21.0.13.1+2** です。
Python の依存関係は `scripts/build/requirements-code-quality.txt`、PMD と Java の配布元・SHA256 は
`scripts/build/code_quality_tools.json` に固定しています。
セットアップでは PyPI と PMD / Eclipse Adoptium の公式配布ファイルを使い、PMD / Java の SHA256 を検証します。

```powershell
# 複雑度・重複の検出、終了コード、.inl、UTF-8 パス、ソースの保持を検証
.\tests\Run-ReadabilityToolsTests.ps1
```

## 保存時・ビルド時の品質ログと AI レビュー

`Engine/` と `Game/` の C/C++ ソース・ヘッダー・`.inl` を対象に、
保存時は整形チェック・Lizard・CPD、ビルド開始時は clang-tidy も含めて記録します。
ログは `artifacts/code-quality/logs/` に保存します。
指摘やツールの異常終了で通常のビルドを停止させる設定にはしていません。

```powershell
# 保存の監視をバックグラウンドで起動 (エディターの種類によらず利用可能)
.\scripts\build\quality_log.ps1 -Action Start

# 動作状態 / 停止要求
.\scripts\build\quality_log.ps1 -Action Status
.\scripts\build\quality_log.ps1 -Action Stop

# 今すぐ記録。clang-tidy は指定したソースだけに絞れる
.\scripts\build\quality_log.ps1 -File Engine/Core/JobSystem.cpp

# 整形・複雑度・重複を先に確認する
.\scripts\build\quality_log.ps1 -SkipClangTidy
```

両 C++ プロジェクトの `PrepareForBuild` の直前にイベントをキューへ入れ、
監視プロセスが解析します。Visual Studio・MSBuild・既存のビルドスクリプトから利用できます。
ビルドの開始イベントなので、ログの存在はコンパイルやリンクの成功を意味しません。
コンパイラーの診断とビルド結果は、Visual Studio / MSBuild の出力で確認してください。
ログ収集内の MSBuild 呼び出しには `EnableCodeQualityLogging=false` を渡して再帰実行を防ぎます。
ビルドによる起動を無効にする場合は `/p:EnableCodeQualityLogging=false` を指定し、
既に起動している監視も止める場合は上記の `Stop` を実行します。

VS Code には `Quality: Watch saves` タスクを追加しています。
フォルダーを開いた際の自動タスクは、VS Code 側で初回の許可が必要です。
Visual Studio では最初のビルドで監視が起動します。ビルド前から記録する場合は `Start` を実行します。
保存の監視では外部コード、テスト、HLSL、素材は対象外です。
監視は PC 再起動で終了し、次のビルド・タスク起動・`Start` で再開します。

| ファイル | 内容 |
| --- | --- |
| `latest.json` | 最新の指摘一覧。ツール、ルール、ファイル、行番号、計測値、ソースと設定の SHA256 |
| `latest.txt` | 最新結果の概要と先頭 30 件の改善候補 |
| `latest-build.json` / `latest-save.json` | ビルド起点 / 保存起点の直近の結果 (各イベントの完了後に作成) |
| `status.json` | 現在解析中のツール、または待機状態 |
| `runs/<実行ID>/results.json` | 実行ごとの結果を保持した履歴 |
| `runs/<実行ID>/*.log` | 各ツールの標準出力・標準エラー |
| `events.jsonl` | 実行の時刻・件数・履歴ファイルの場所を追記する一覧 |
| `AI_REVIEW.txt` | AI にログを渡すときの確認事項 |

ツールの状態は `ok`・`findings`・`error`・`timeout`・`skipped` を区別します。
たとえば保存起点の clang-tidy は `skipped` になり、解析済みとは扱いません。
clang-tidy の既知の異常終了やタイムアウトも、生ログと状態に残します。
解析中に編集されたファイルは `stale_files`、設定変更は `stale_configuration` に記録します。
古い行番号や指摘から修正を始めず、現在のハッシュと照合してから扱ってください。

保存は 3 秒待ってまとめ、解析完了から次の自動解析までは最低 15 秒空けます。
通常のツールは 60 秒、clang-tidy は 120 秒でタイムアウトさせます。
値は `scripts/build/quality_logging.json` にあり、監視設定の変更後は `Stop` → `Start` で再起動します。
停止要求は実行中の解析が終了してから処理されます。
履歴は自動削除しないため、不要になった履歴は `artifacts/` 内で整理できます。

AI に依頼するときは、次のようにログのパスを指定できます。

> `artifacts/code-quality/logs/latest.json` と `AI_REVIEW.txt` を読み、
> 現在のソースと一致する指摘から優先度の高い箇所を修正してください。
> 動作を保ち、関連テストと修正後の再解析まで行ってください。

ログ収集はローカルで行い、AI への送信やソースの自動修正は行いません。
修正する際にはログをこのチャットから読み、変更内容とテスト結果を確認できます。

```powershell
# 実際の保存イベント、ビルドのキュー投入、失敗・タイムアウト・古い結果の扱いを検証
.\tests\Run-QualityLoggingTests.ps1
```

## エディターと検証

Visual Studio は既存の `.clang-format` を利用できます。
「ツール → オプション → テキスト エディター → C/C++ → コード スタイル → 書式設定」で
ClangFormat のサポートが有効になっていることを確認してください。

VS Code は Microsoft の C/C++ 拡張 (`ms-vscode.cpptools`) を使用します。
`.vscode/settings.json` で C++ の保存時整形と標準の LLVM のパスを設定しています。
既存の Format ボタンも実際の整形スクリプトを実行します。

```powershell
# 整形の検出・書き換え・再実行時の安定性・外部コード除外・解析ルールを検証
.\tests\Run-CodeQualityTests.ps1

# 武器形状の評価・形状ルール・説明文の回帰テスト
.\tests\Run-WeaponShapeTests.ps1
```

作業時は「変更ファイルを整形 → 対象ソースを解析 → 差分をレビュー」の順で使います。
関数の分割や命名の判断は、解析の指摘を手がかりにレビューで確認してください。

## 可読性改善の検証記録 (2026-11-26)

自作 C/C++ の 278 ファイルを整形・検査しました。コードの空白とコメントを除いた比較では、
265 ファイルは整形だけの変更です。処理・宣言・テスト内容の変更は次の 5 ファイルです。

- `Game/Chrono/WeaponShapeMetrics.h`: 影絵生成の重複をなくし、輪郭・柱・部品の割合・採点を分割。
  `MeasureWeaponShape` の認知的複雑度は 141 から設定のしきい値 25 以下になりました。
- `Game/Chrono/WeaponCraftRules.h`: 武器の説明文を描画から独立した関数に分離。
- `Game/Systems/WeaponCraft.cpp`: 一覧・選択・材料枠・プレビュー・性質・強化を個別の描画関数に分割。
  描画順と座標は維持しています。分割した関数と `DrawWeaponCraft` に複雑度の警告はありません。
- `Game/Systems/ChronoSystem.h`: 描画関数の private 宣言を追加。
- `tests/WeaponShapeMetricsTests.cpp`: 非立方体グリッドの三方向の投影と説明文の順序・数値を検証。

Development のソリューション全体のビルド、全体の整形チェック、武器形状・Chrono・データ駆動・
デザインパターン・カプセル化・コントローラー・スライム調整・整形ツールのテストは通過しました。
形状評価は 300 種類のグリッドについて旧実装と全レポート項目の完全一致を確認し、
既存の 511,104 通りの形状ルールも通過しています。
ビルド出力と検証ログは `artifacts/code-quality/` にあります。

一方、次の検証は全件成功にはなっていません。

| 検証 | 結果と切り分け |
| --- | --- |
| スライム生活ルール | `first stage rules are wrong`。変更前の HEAD のソースでも再現 |
| GPU 変形 (`--morph-only`) | `upper-owner shape readiness missing`。変更前の HEAD のソースでも再現 |
| 起動スモーク | `titleVisible=0`。修正前からある実行ファイルでも同じ判定失敗を再現 |
| Game 全体の clang-tidy | clang-tidy 19.1.7 が `GameSceneEntities.cpp` の解析中に異常終了。変更箇所の可読性チェックは個別に実施 |

引き続き分割候補になる大きな関数は、`Renderer::FlushDrawCalls`、`Model::LoadWithUFBX`、
`App::Run` です。描画や読み込みの動作を確かめる回帰テストと併せて、役割ごとに分割する対象です。

## 品質ログからの改善 (2026-11-26)

保存ログ `20261126T093259.347492Z-614fa8f2` と現在のソース・設定の SHA256 が一致することを確認し、
Lizard の最上位だった `ChronoSystem::UpdateSlimeLife` を優先しました。
入力、移動、接触吸収、各体の吸収・武器・回復、解放通知、フレーム終了を個別の処理に分割しています。
停止中の早期終了、入力の押下判定、移動前の位置・接地状態、解放前の状態、更新順序を維持しました。
吸収と武器処理の外側の条件は早期リターンにして、深いインデントを減らしています。
変更範囲は `SlimeLife.cpp` の更新処理と `ChronoSystem.h` の private 宣言・フレーム内の受け渡し型です。

| Lizard の計測 | 修正前 | 修正後 |
| --- | --- | --- |
| `UpdateSlimeLife` の循環的複雑度 | 353 | 16 |
| 同関数のコード行数 (NLOC) | 724 | 56 |
| 分割対象の関数群の最大複雑度 | 353 | 50 |

分割した関数にはまだ複雑度・行数の指摘が 6 件残ります。関数単位の集計なので、
全体の Lizard 指摘関数数は 136 から 141 になりました。しきい値は変更していません。
次の候補は移動の制約・接地処理と変形入力です。CPD の重複グループ数は 39 のままです。

再解析 `20261126T094959.608129Z-02d1e6c1` では、Engine/Game 全体の整形・Lizard・CPD と、
`SlimeLife.cpp` の clang-tidy がすべて完走しました。ソースと設定のハッシュも再照合済みです。
clang-tidy は対象翻訳単位と読み込まれるヘッダーの 955 件を記録しています。
元の保存ログでは clang-tidy が省略されているため、総指摘数の単純な増減比較はできません。
全プロジェクトの clang-tidy 完走を意味するものではありません。

全体の Development ビルド、全 278 ファイルの整形検査、Chrono、スライム調整、
スライム更新に関係する既存ルール 11 群、実機のスライムデモのスモークテストが通過しました。
実機デモの全変形・攻撃・移動・ジャンプ・分裂・リセットの判定は修正前と一致しています。
全体の `Run-SlimeLifeTests.ps1` は既存の `first stage rules are wrong` で停止しました。
元のテストの条件やアサーションは変更していません。

坂の町の実機スモークは、修正後の初回に GPU 追従遅れの基準未達で失敗しました。
`lag8000` の最大値は 1.050414 (基準は 1 未満) です。保存した修正前のバイナリでは
0.803107 で通過し、同じ修正後バイナリの再実行も 0.620786 で通過しました。
初回失敗の原因は未確定で、GPU 追従検査の再現性に課題が残ります。
失敗ログを成功ログで上書きせず、`devour-after.log`・`devour-before.log`・
`devour-after-repeat.log` と個別の結果ファイルに保存しています。

修正前後のソース、ビルド・テストログ、既存ルールを個別実行したハーネスは
`artifacts/code-quality/life-refactor/` に保存しています。
最新の解析は `artifacts/code-quality/logs/latest.json`、実行履歴は `logs/runs/<実行ID>/` にあります。
