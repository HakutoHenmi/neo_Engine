# ゲームデータ (JSON) の一覧

ゲームの調整値・表・画面の文字は `Resources/Data/` の JSON に置いています。
JSON を書き換えて起動し直すだけで反映され、コードを直す必要はありません。
スライムの主要な調整値 (`slime_tuning.json`) は実行中の変更・保存にも対応しています。

固定データの読み込みは [`Game/Data/GameData.h`](../Game/Data/GameData.h) の `Data::Load` に一本化しています。
同じファイルは最初の 1 回だけ読み、ファイルが無い・項目が足りないときは
「どのファイルのどの項目か」を書いたエラーで止まります。
実行中に編集する `slime_tuning.json` は専用の `SlimeTuningStore` が読み込み、検証済みのスナップショットを共有します。

| ファイル | 中身 | 読むコード |
| --- | --- | --- |
| `game.json` | 最初のシーン、本編のシーンファイル、予備のシーン、ログのファイル名、コマンドライン引数とシーンの対応 | [`Game/Data/GameConfig.h`](../Game/Data/GameConfig.h), [`Game/main.cpp`](../Game/main.cpp) |
| `city_stage.json` | 街の床の配置、門の体力、敵 8 種の能力と名前、8 段階の題名・目標・出現位置 | [`Game/Chrono/CityStageRules.h`](../Game/Chrono/CityStageRules.h) |
| `roguelite.json` | 強化カード 18 枚、系統と相乗効果、経験値の曲線、希少度ごとの出やすさ | [`Game/Chrono/RogueliteRules.h`](../Game/Chrono/RogueliteRules.h) |
| `ui_text.json` | タイトル・セレクト・結果・ポーズ・グラフィック設定・強化画面の文字 | [`Game/UI/GameUI.h`](../Game/UI/GameUI.h) の `UI::Str("区切り.項目")` |
| `loading.json` | ロード画面のチュートリアル 6 ページ、読み込み中の文、起動画面の説明文 | [`Game/UI/LoadingUI.h`](../Game/UI/LoadingUI.h) |
| `credits.json` | クレジット画面のページと行 | [`Game/UI/CreditsUI.h`](../Game/UI/CreditsUI.h) |
| `music.json` | シーンごとの BGM のファイルと音量（`City` は街のステージ） | [`Game/UI/SceneMusic.h`](../Game/UI/SceneMusic.h) |
| `graphics.json` | 映像効果の既定の強さ、LOW / BALANCED / HIGH のプリセット、露出の範囲 | [`Game/UI/GraphicsUI.h`](../Game/UI/GraphicsUI.h) |
| `editor.json` | エディタのシーンランチャー、エディタで包むシーン、Slime Dev Tools のワープ先 | [`Game/Editor/EditorShell.cpp`](../Game/Editor/EditorShell.cpp), [`Game/Editor/SlimeDevTools.cpp`](../Game/Editor/SlimeDevTools.cpp) |
| `loadout.json` | 旧アクションゲームの缶の初期装備と表示名 | [`Game/CanLoadout.h`](../Game/CanLoadout.h) |
| `assignment.json` | 評価課題デモの素材・パーティクルの発生源・戦闘の値 | [`Game/Scenes/AssignmentScene.cpp`](../Game/Scenes/AssignmentScene.cpp) |
| `slime_tuning.json` | 調合による移動・ジャンプ・吸収・分裂・子の攻撃・液体の見た目、武器の攻撃時間・ダメージ・当たりの時間、タイヤ、カメラ | [`Game/Data/SlimeTuning.h`](../Game/Data/SlimeTuning.h), [`Game/Chrono/SlimeLifeRules.h`](../Game/Chrono/SlimeLifeRules.h), [`Game/Chrono/SlimeWeaponRules.h`](../Game/Chrono/SlimeWeaponRules.h) |

本編のシーンそのもの (配置するオブジェクトとコンポーネント) は `Resources/Scenes/chrono.json`、
本編が読めないときに使う最低限のシーンは `Resources/Scenes/fallback.json` です。どちらもエディタで編集できます。

## 編集するときの注意

- **並びと数が決まっているもの**:強化カードは 18 枚、街の段階と敵の種類は 8 つ、ロード画面のページは 6 ページです。
  効果や絵をコードが番号で扱っているため、並びを変えたり数を増減したりすると起動時にエラーになります。
- **`_comment` で始まる項目**:説明用なので、コードは読みません。
- **文字の `{n}`**:`ui_text.json` の `{n}` は表示するときに数字に置き換わります。
- **リリース版**:新しいデータファイルを足したら、[`scripts/SlimeResources.json`](../scripts/SlimeResources.json) にも追加してください。
  この一覧にあるファイルだけがリリースにコピーされます。

## 実行中に調整する (資料の解消方法3)

エディタの **Slime Dev Tools → ゲームの調整値 (JSON)** を開きます。
EDIT中にも編集でき、PLAY中は既にあるレシピや武器の処理へ即時に反映されます。

- **JSONへ保存**:現在の有効な値を `Resources/Data/slime_tuning.json` へ保存します。次の起動でも使われます。
- **JSONを再読み込み (編集を破棄)**:未保存の編集を捨て、ファイルの値を採用します。
- **外部エディタで変更**:GameSceneの更新中は0.5秒ごとに変更を確認します。ポーズ中にも反映し、ゲームを再起動する必要はありません。
  エディタの一時停止中・EDIT中は、Slime Dev Toolsを表示している間も確認します。
- **未保存の編集があるとき**:外部ファイルを自動で上書き反映せず、警告を表示します。保存で現在の編集を優先、再読み込みで外部ファイルを優先できます。
- **不正な値・壊れたJSON**:直前の有効な値を保ち、理由を表示します。起動時にファイルが壊れている場合は起動エラーになります。
  攻撃の当たり時間は `0 ≤ hitBegin ≤ hitEnd ≤ duration` が必要です。
- **保存に失敗**:一時ファイルへ最後まで書けてから元のファイルを置き換えるため、書き込み途中で元のJSONを壊しません。

`recipe` の計算は `base + Σ(materials[i] × 材料の割合)` です。
`materials` は **水・ホウ砂水・洗濯のり・泡** の順です。負の係数も使えますが、各材料100%での結果も許容範囲内である必要があります。
移動速度はさらに `scalars.speedScale` を掛け、子の攻撃間隔は `scalars.childCooldown / ChildCooldownDivisor` です。
既定の数値は従来の式と同じ結果になるよう移しています (浮動小数点の丸め差を除く)。

項目を増やす際は `Game/Data/SlimeTuning.h` の項目定義とJSONに追加し、ゲームの処理から参照します。
項目定義が読み込み・保存・範囲検証・編集画面で共有されます。
スナップショットは変更されないため、ロードスレッドが古い値を参照していても無効になりません。
変更できるのはメインスレッドのみです。その他のJSONのキャッシュは従来どおりです。

検証: `tests/Run-SlimeTuningTests.ps1` (従来の計算との比較、即時反映、外部変更、保存、再読み込み、不正値、保存失敗)。

## コードに残しているもの

次の値は今もコードの中にあります。

- **固定値**:配列の要素数・形態の識別子・数式や形状の定義など。意味のある固定値は名前付きの `constexpr` を使います。
- **未移行の調整値**:質量の段階、群れ・吸収モード固有の成長や演出、武器のメッシュ形状など。今回の即時編集の対象は上表の `slime_tuning.json` に記載した範囲です。
- **ゲーム中のメッセージ**:スライムの上に出る「バリケード 残り…」など。
- **UI の見た目**:ボタンの位置・色・フォント。
