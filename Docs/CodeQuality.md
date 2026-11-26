# C++ の整形と静的解析

`clang-format` でインデント・改行を統一し、`clang-tidy` で複雑な関数や冗長な処理などを検出します。
導入時には既存ソースの一括整形を行っていません。以下のコマンドはリポジトリ直下で実行します。

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
.\scripts\build\format_code.ps1 -Files Engine/JobSystem.cpp

# 自作の C/C++ ファイル全体を検査する
.\scripts\build\format_code.ps1 -All -Check
```

対象は `Engine/`・`Game/`・`tests/` 内の C/C++ ソース、ヘッダー、`.inl` です。
削除済みファイル、Git の除外対象、`Engine/ThirdParty/` は自動選択しません。
`-All` で全体を整形する場合は `-Check` を外します。

ルールはルートの `.clang-format` にあります。基本はタブ幅 4、120 文字での折り返し、
空でない関数を複数行で書く形式です。include の並び順は維持します。
`.editorconfig` も同じインデント幅に合わせています。
外部コードには専用の `.clang-format` を置き、エディター経由の整形も無効にしています。

## 静的解析

```powershell
# Engine と Game を解析（コンパイル・リンク・実行は行わない）
.\scripts\build\analyze_code.ps1

# 一方のプロジェクトだけを解析
.\scripts\build\analyze_code.ps1 -Project Engine

# 指定したソースと、そのソースが読み込むヘッダーを解析
.\scripts\build\analyze_code.ps1 -File Engine/JobSystem.cpp

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

通常のビルドでは解析を毎回実行しません。
Visual Studio ではプロジェクトを右クリックして「コード分析の実行」を選ぶと、
同じ `.clang-tidy` 設定で解析できます。完全な LLVM が標準の場所にあれば自動で利用します。
Visual Studio で別の LLVM を使う場合は、プロジェクトのプロパティの
「コード分析 → Clang-Tidy → Clang-Tidy ツール ディレクトリ」に `bin` の場所を指定します。

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
```

作業時は「変更ファイルを整形 → 対象ソースを解析 → 差分をレビュー」の順で使います。
関数の分割や命名の判断は、解析の指摘を手がかりにレビューで確認してください。
