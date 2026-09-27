# スライム、跡で本気出す。 UI

タイトル、ステージ選択、戦闘HUD、ポーズ、勝利・敗北リザルト、Assignmentの練習HUDで、深緑・ライム・オフホワイトのテーマを使用。

- 共通部品: `Game/UI/GameUI.h`。1280×720を基準にスケーリングし、描画とクリック判定に同じ矩形を使用。
- パネルとボタン: Kenney UI PackのGrey素材を着色、9-sliceで描画。主要ボタンは深さ付き、ホバー時に明度と下線を変更。
- 操作案内: Kenney Input Prompts 1.5のKeyboard & Mouse。セレクトにはクリック・Enter（出撃）、Esc（タイトル）のみ表示。
- フォント: Rajdhani SemiBold（Indian Type Foundry）。英語のゲームUIに使用。エディタや日本語の独自UITextフォント設定は保持。
- 日本語タイトル・共通ヘッダー: M PLUS Rounded 1c ExtraBold。タイトルは「スライム、跡で本気出す。」。
- スプライト・テキストは`Draw()`でキューへ登録する。`DrawUI()`はRenderer::EndFrameの後なので、ここからスプライトを登録しない。
- 本編リザルト: R / Enter / クリックで再挑戦、Tab / クリックで選択へ。ポーズ: Esc / Enter / クリックで再開、Tab / クリックでタイトルへ。

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
