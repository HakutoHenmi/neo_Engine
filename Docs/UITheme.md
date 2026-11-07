# チリツモ UI

タイトル、ステージ選択、戦闘HUD、ポーズ、勝利・敗北リザルト、Assignmentの練習HUDで、深緑・ライム・オフホワイトのテーマを使用。

- 共通部品: `Game/UI/GameUI.h`。1280×720を基準にスケーリングし、描画とクリック判定に同じ矩形を使用。
- パネルとボタン: Kenney UI PackのGrey素材を着色、9-sliceで描画。主要ボタンは深さ付き、ホバー時に明度と下線を変更。
- 操作案内: Kenney Input Prompts 1.5のKeyboard & Mouse。セレクトにはクリック・Enter（出撃）、Esc（タイトル）のみ表示。
- フォント: Rajdhani SemiBold（Indian Type Foundry）。英語のゲームUIに使用。エディタや日本語の独自UITextフォント設定は保持。
- 日本語タイトル・共通ヘッダー: M PLUS Rounded 1c ExtraBold。タイトルは「スライム、跡で本気出す。」。
- スプライト・テキストは`Draw()`でキューへ登録する。`DrawUI()`はRenderer::EndFrameの後なので、ここからスプライトを登録しない。
- 本編リザルト: R / Enter / クリックで再挑戦、Tab / クリックで選択へ。ポーズ: Esc / Enter / クリックで再開、Tab / クリックでタイトルへ。

## 物差しの UI (タイトル・セレクト・リザルト)

3 画面は巻尺の黄色を地に、下に対数の巻尺を通す。5cm から 126m までが同じ目盛りに乗り、大きさをいつでも比べられる。案と作り方は `Docs/UIDesign/chiritsumo/`。

- 共通部品: `Game/UI/ScaleUI.h`。巻尺・物・スライム・ロゴ・操作の案内・結果画面と、保存した記録の読み書きがある。
- スライム: ゲームの GPU 流体を撮影した連番 (`slime_idle_*`、`slime_roll_*`)。同じ瞬間を黄と青の背景で撮り、差から透明度を求めるので、後ろの物が体の中に透ける。撮り直しは `--slime-life-smoke --slime-devour --devour-stage=0 --slime-sprite-capture` で起動してから `Docs/UIDesign/chiritsumo/matte_slime.py` を実行し、出力の縦横比を `ScaleUI.h` の `IdleAspect` と `RollAspect` へ写す。実時間の流体ではないのは、タイトルを軽いまま保つためと、流体は UI より下に描かれて文字を飲む演出ができないため。
- 決定の演出: はじめるを選ぶと、体に入った物・ロゴ・メニューの文字を体の中へ吸い込み (`InsideSlime`、`Swallow`)、最後に緑へ溶けてセレクトへつなげる。
- 画像: `Resources/Textures/UI/Scale/`。ゲームのモデルから `bake_sprites.py` で書き出す。`_sil` は同じ形の白い影で、色を掛けて影や未解放の形に使う。色をそのまま見せる画像は `Canvas::Photo` で描く (`Image` は sRGB として読むので暗くなる)。縦横比は `ScaleUI.h` の表に合わせる。
- フォント: 数字は Anton、日本語は Zen Kaku Gothic New Black。大きな文字は `Canvas::Big` で描く。パスの `#256` のような指定で、同じフォントを高い解像度で読む。
- 流れ: タイトル → セレクト → 本編 → リザルト。選んだステージは `slime-lab-v1.json` の `devourStage` に保存し、本編が開始時に読む。
- 解放: 前のステージで最初の目標に届くと次が開く。
- 演出: 登場は決定の操作で飛ばせる。タイトルの 2 回目からは途中から始まる。`--ui-smoke` は飛ばして完成の画面を撮る。
- ロード: タイトルと同じ物を、読み込みに合わせてスライムが左から飲み込む (`LoadingUI.h`)。ロード用のスレッドは新しい画像を読めないので、使う画像と文字は `Loading::Warmup` で先に描く。説明は `loading.json`。
- 起動画面: 巻尺が伸びてロゴが押し出される。読み込みは巻尺の上の赤い線。
- クレジット: 章を巻尺の上の帯にして、スライムが今の章に座る (`CreditsUI.h`)。
- ポーズ: 吸収モードは止まったゲームの左に黄色の面を出し、巻尺に今の大きさとランクの目標を出す (`Scale::Pause`)。戦闘モードの強化パレットは以前のまま。
- 映像設定: 品質の 3 段を巻尺の上の帯にして、スライムが今の設定に座る (`GraphicsUI.h`)。
- リザルト: 画面を切り替えず、ゲームシーンの本物のスライムの上で行う。「そこまで！」と巻尺が流れ込み、カメラが横から全身を映す構図へ移り (`InkCamera`)、寸法線が体の端まで伸びて数字が止まる。カメラの位置は GPU 流体の粒子と飲んだ物を包む皮の外形から決め (`MeasureDevourBody`)、寸法線は同じ体を画面へ投影した端に合わせる。測り始めたら巻尺の目盛りは体の左端を 0 とした読みに変わる。カメラは体を囲む箱が記録の面の右に収まる位置へ引き、顔をカメラへ向ける。測り始めたら流体を止め、カメラが止まったら寸法線も固定する。カメラと体の間の物は隠し、やり直しで戻す。ゲームからタイトルへは `RequestCoveredChange` で覆ったまま切り替え、タイトルは最初の登場から始まる。
- 確認: `--slime-life-smoke --slime-devour --devour-stage=0 --slime-result-capture` で物を飲ませてから時間切れにし、`tests/out/result-seq-*.png` へ終了の流れを撮る。`--result-goal=N` は N 番目の目標を超えた大きさで撮り、`--slime-result-to-title` は撮影の後にタイトルへ切り替える。
- ゲーム中 (`Scale::Hud`): 下に流れる巻尺を置き、左下の大きさの札の右端が読み取りの線になる。今の大きさが線に来るよう巻尺が左へ流れ、目標は赤い旗、壁と最高記録は黒い旗で先に見える。読む文字は黄色か黒の面に載せ、最小でも 20 にする。
- 文字の解像度: `Canvas` が画面での実際の大きさから 32〜256 の段を選ぶ (`FontTier`)。ロード画面で新しい大きさの文字を使うときは `Loading::Warmup` にも足す。
- 画面の切り替え: 黄色の面が右から入って覆い、右へ抜ける。タイトルとセレクトの間はつなげて見せるので `RequestInstantChange` を使う。
- 確認: `tests/Run-UISmoke.ps1` で完成の画面を撮る。`tests/Run-ScaleFlow.ps1` は実際のキー入力でタイトル、クレジット、セレクト、ロード、ポーズ、映像設定、リザルトまで動かす (窓を前面に出すので、動いている間は操作しない)。Development ビルドの `--devour-time=秒` で制限時間を短くできる。

## ライセンスと入手元

- Kenney: 両パックの`License.txt`にCC0と商用利用可の記載。元ファイルをそのまま使用。
- Rajdhani: https://github.com/google/fonts/tree/main/ofl/rajdhani
- M PLUS Rounded 1c: https://github.com/google/fonts/tree/main/ofl/mplusrounded1c 。`Resources/Fonts/MPLUSRounded1c/`にフォントとOFLを同梱。ライセンス原本: https://github.com/google/fonts/blob/main/ofl/roundedmplus1c/OFL.txt
- 同梱フォント: `Resources/Fonts/Rajdhani/Rajdhani-SemiBold.ttf`
- 同梱ライセンス: `Resources/Fonts/Rajdhani/OFL.txt`（SIL Open Font License 1.1、改変なし）
- フォントの取得日: 2026-09-27。OSへのインストールは不要。既存のResourcesコピー処理で配布先にも同梱。

## 検証

Developmentビルド後に`tests/Run-UISmoke.ps1`でタイトル・選択・敗北の実シーン、および本編と同じ勝利・ポーズ部品をGPU描画し、`tests/out/ui-*.png`へ保存。フォント読込、タイトル幅、画像保存を検査する。OSへの疑似入力は行わないため、実入力の遷移はこのテストの対象外。

`tests/Run-ChronoSmoke.ps1 -Ink`で本編HUDを表示した状態の移動、回収、3段階ビーム、質量保存などを検証できる。

## 操作改善

- タイトル開始操作はムービーを挟まず次フレームにSelectへ遷移。
- スライム本編も共通のリザルト入力処理を通り、クリック・Tab・R・Enterを処理。ゲーム中のカーソル非表示はリザルトを除外。
- 下段バーとSTORED %は地面に残る回収可能量 / 現在の最大蓄積量。移動で増え、チャージ回収で減る。発射強度はLEVEL表示で区別。
- Developmentの --ui-result は本編を低体力の敗北確認用状態で起動。描画モックではなく実際のGameSceneの入力・カーソル・遷移を手動確認できる。

実操作確認（2026-09-27）: 本編の敗北リザルトでカーソル表示、STAGE SELECTクリック、Tab帰還、セレクトのEsc帰還、タイトルのEnter即時遷移を確認。
