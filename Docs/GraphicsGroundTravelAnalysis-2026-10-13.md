# 移動中の地面の白化・遠景の乱れ

対象動画：`スライム、跡で本気出す。 2026-10-13 11-29-52.mp4`。1920×992、約71.73秒、2087フレーム。60fps表記の可変フレームレートで、平均は約29.1fps。前回の角度依存の検査に追加して、今回はカメラの並進と遠距離の位置復元を調べた。

## 動画の確認

30fpsの時間基準で全区間をデコードし、エディタのGame画像部分を抽出。約51.13秒で地面が白っぽくなるフレームを特定し、その前後の画像を比較した。左側の地面領域のRGB平均は約102から148へ上がり、次のフレームで約102に戻る。プレイヤーや敵の形状が崩れる現象とは異なる。遠景には視線の先に向かう明暗の帯も残っている。

解析データは `tests/out/video-travel/stats.csv`、連続画像は `flash-01.png`～`flash-09.png`。36～38秒の大きな画面差には急なカメラ移動・被写体による遮蔽も含まれるため、地面の白化の測定値として使わない。

## 修正した箇所

1. **GPU処理中のテクスチャ参照表の上書き**。`Renderer::BeginFrame` が毎回同じ動的SRV領域をリセットしていた。WindowDXは2フレームを並行処理するため、前のGPU処理が残っている状態でPBR材質やRTの参照表を変更できてしまう。移動によるカリング・描画順の変化で、誤った材質を読む可能性がある。各フレームスロットに1048個ずつの領域を割り当て、対応するフェンス完了後にだけ再利用するよう変更した。静的1000個＋動的1048個×2で、ヒープ容量は3096個。フレーム当たりの容量は従来と同じ。

   この管理条件は [MicrosoftのDescriptor Tables仕様](https://learn.microsoft.com/en-us/windows/win32/direct3d12/advanced-use-of-descriptor-tables) にも記載されている。GPUが参照する可能性のある表は、処理完了前にCPUから変更できない。

2. **遠景での逆行列の精度低下**。平行移動を含むView×Projection行列をFP32で直接反転していた。near 0.1／far 2000、カメラ位置(235,2,-157)付近の実HLSL検査で、復元したレイの終点が実際の地面より最大2.7421m手前になるケースを確認した。`Inverse(Projection) × Inverse(View)` に分解して反転する共通ヘルパーへ変更。RT照明、RT影、照明履歴、DLSSの動き入力、ポスト処理で同じ方法を使用する。

3. **RTの探索区間と深度判定の不一致**。深度一致は距離に応じた誤差を許容していたが、受光面を探すレイの終点は常に復元位置＋0.3mだった。逆行列の改善後も、最大0.7676mの終点誤差が残り、旧30cm区間では195ケースで地面を取り逃がす。探索区間と深度一致の両方で `max(0.2m, viewDepth×0.002)` を使い、照明がフレームごとに抜ける条件を修正した。`RtLighting.cso` も再コンパイル済み。

## 再現検査と確認範囲

- `tests/ReceiverDepthTests.hlsl` は実際の投影・復元計算で作ったレイと、高さ−0.08mの地面の解析的交点を比較する。5方向×3角度×2位置×256距離の7680ケース。直接逆行列では距離許容区間に合わせても477件取り逃がし、改善した逆行列＋旧30cm区間では195件、両方の修正後は0件。
- `tests/Run-FluidTests.ps1 -Case --graphics-only`：上記と既存の材質Mip、影、傾斜面、履歴、動き生成、HDRの検査が通過。DLSS相当の1.5倍出力サイズでも傾斜面の影が均一になることを追加確認。
- `tests/Run-RtLightingSmoke.ps1 -Travel`：4方向×2角度×5設定（RTなし、影のみ、反射のみ、間接光のみ、全有効）の40条件。移動中の地面のRGB平均差と参照表の領域を確認する。6フレームごとに読み戻し、間のフレームでは通常のCPU/GPU並行処理を維持する。地面検査区間では流体の描画と古い流体深度を除外する。前半では通常のプレイヤー描画、設定画面復帰、RT物体移動の既存検査も行う。
- 修正前の共有割り当てに一時的に戻した同じ検査で、参照表分離チェックが失敗した。修正版では通過。画像の明るさは読み戻しによるGPU同期にも影響されるため、この比較の平均差を動画の白化の改善率として扱わない。

動画の敵配置・入力を完全に同じ時系列で再生した検査ではない。白化の映像、参照表の寿命違反、遠景の数値誤差をそれぞれ確認し、対応する処理を修正した。小さな単独画素の変化は地面領域の平均差だけでは検出できない。

最終Release／Development x64ビルドが成功。実GPUの最終エディタ検査（DLSS有効）は40条件が通過し、地面の最大RGB平均差は0.00802793、参照表分離チェックは通過、DLSS評価3033フレーム。D3D12 ERROR／CORRUPTIONなし。結果は `tests/out/rt-lighting-dlss-editor-travel.txt`。この値は6フレーム間隔の領域平均で、動画の単一フレームの測定とは条件が異なる。

再実行：エディタは `tests/Run-RtLightingSmoke.ps1 -Executable ../Generated/Outputs/Development/DirectXGameApp.exe -Travel -Editor`、DLSS無効のReleaseは `tests/Run-RtLightingSmoke.ps1 -Travel -Native`。数値検査の結果は `tests/out/travel-graphics-tests.txt`。

最終ReleaseのDLSS無効検査も40条件通過。最大RGB平均差0.00724294、参照表分離チェック通過、D3D12 ERROR／CORRUPTIONなし。結果は `tests/out/rt-lighting-native-travel.txt`。

負荷測定 `tests/Run-SceneryBenchmark.ps1 -Label ground-travel-final-dlss -Dlss -Validation` が通過。RTX 4060 Laptop・1920×1080・DLSS＋RT全有効で、固定状態75.12 FPS（13.31ms）、通常更新中75.01 FPS（13.33ms）。RTレイ計算約0.32ms、照明フィルター約0.18ms。GPUクロック・電源条件を固定していないため、前回測定との差を改善率とは扱わない。D3D12 ERROR／CORRUPTIONなし。

`dist/SlimeAssault-Release.zip` を最終Releaseで更新。300エントリー、新しい深度許容ヘルパー、修正RTバイトコード、DLSS DLLを同梱。配布EXEのSHA-256が最終Releaseと一致。配布フォルダーからの `Run-DlssPackageSmoke.ps1` もDLSS評価89フレームで通過し、D3D12 ERROR／CORRUPTIONなし。ゲーム／エディタのプロセスを再起動して使う。
