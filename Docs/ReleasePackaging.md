# スライム、跡で本気出す。の配布

Release|x64をビルドすると、`scripts/SlimeResources.json`に列挙した素材だけを
`../Generated/Outputs/Release/Resources`に配置します。以前の別ゲームの素材など、
一覧にないファイルはこの出力先から削除します。ソース側のResourcesは削除しません。
共通エンジンが起動時にコンパイルするシェーダーとそのinclude、素材ライセンスも必要です。

ビルド後に `scripts/New-SlimeRelease.ps1` を実行すると、配布用の
`dist/SlimeAssault/SlimeAssault.exe` と Resources、Streamline、NTC、ZIPが作られます。
提出するのは `dist/SlimeAssault-Release.zip` です。受け取った側はZIPを展開してから
`SlimeAssault.exe`を実行します。EXEと同じ階層にResources・Streamline・NTCを置き、
中のフォルダー構成を変えないでください。StreamlineはDLSS用のDLLとライセンスを含みます。
SDKが配置されていない環境ではStreamlineを同梱せず、ネイティブ描画で動きます。
NTCは道路材質をロード時にGPUで復元する `libntc.dll` (NVIDIA RTX SDK) とライセンスを含みます。
`externals/RTXNTC/bin/libntc.dll` が無い・ハッシュが違う場合はパッケージ作成を止めます。
作成の途中で同梱EXEを一度起動し、起動からゲーム画面までに使うシェーダーを `Resources/ShaderCache` に書き出します
（数十秒かかります。詳しくは [LoadTimeOptimization.md](LoadTimeOptimization.md)）。
古いEXEだけを使い回さないでください。`.pdb`、`.lib`、ソース、ログは提出に不要です。
外部DLLの直接依存はWindows標準DLLのみで、DLSSのDLLはStreamlineから、NTCのDLLはNTCから動的に読み込みます。

## 文字が消えていた原因

旧WinMainはEXEの3階層上のneo_Engineが存在すると、そのディレクトリを
カレントディレクトリにしていました。デスクトップ上の配布先では、別の
`Desktop/neo_Engine`を選び、その場所にRajdhaniなどのフォントがありませんでした。
さらにPathUtilsも親階層のプロジェクトを優先していました。

Releaseでは両方ともEXE所在ディレクトリを基準に統一しました。
フォントもPathUtilsで解決したUnicode対応の絶対パスで読み込みます。
Developmentは従来通りプロジェクトの素材を使います。

## 配布確認

ReleaseのEXEを `--package-smoke` 付きで起動すると、明示的な診断として
タイトル、選択、敗北、クレジット4ページ、戦闘を描画し、英語・日本語フォントの
読み込みとGPUキャプチャを確認して終了します。結果はEXE隣のPackageCheckです。
通常起動はタイトルで待ち、Enterまたは開始ボタン以外では進みません。

2026-09-27: 日本語名フォルダに置いたRelease版を別の作業ディレクトリから起動し、
8画面のキャプチャと両フォントを確認。Resourcesは5,703.36 MiBから34.65 MiBへ削減。
