# ゲームデータ (JSON) の一覧

ゲームの調整値・表・画面の文字は `Resources/Data/` の JSON に置いています。
JSON を書き換えて起動し直すだけで反映され、コードを直す必要はありません。

読み込みは [`Game/Data/GameData.h`](../Game/Data/GameData.h) の `Data::Load` に一本化しています。
同じファイルは最初の 1 回だけ読み、ファイルが無い・項目が足りないときは
「どのファイルのどの項目か」を書いたエラーで止まります。

| ファイル | 中身 | 読むコード |
| --- | --- | --- |
| `game.json` | 最初のシーン、本編のシーンファイル、予備のシーン、ログのファイル名、コマンドライン引数とシーンの対応 | [`Game/Data/GameConfig.h`](../Game/Data/GameConfig.h), [`Game/main.cpp`](../Game/main.cpp) |
| `city_stage.json` | 街の床の配置、門の体力、敵 8 種の能力と名前、8 段階の題名・目標・出現位置 | [`Game/Chrono/CityStageRules.h`](../Game/Chrono/CityStageRules.h) |
| `roguelite.json` | 強化カード 18 枚、系統と相乗効果、経験値の曲線、希少度ごとの出やすさ | [`Game/Chrono/RogueliteRules.h`](../Game/Chrono/RogueliteRules.h) |
| `ui_text.json` | タイトル・セレクト・結果・ポーズ・グラフィック設定・強化画面の文字 | [`Game/UI/GameUI.h`](../Game/UI/GameUI.h) の `UI::Str("区切り.項目")` |
| `loading.json` | ロード画面のチュートリアル 6 ページ、読み込み中の文、起動画面の説明文 | [`Game/UI/LoadingUI.h`](../Game/UI/LoadingUI.h) |
| `credits.json` | クレジット画面のページと行 | [`Game/UI/CreditsUI.h`](../Game/UI/CreditsUI.h) |
| `music.json` | シーンごとの BGM のファイルと音量 | [`Game/UI/SceneMusic.h`](../Game/UI/SceneMusic.h) |
| `graphics.json` | 映像効果の既定の強さ、LOW / BALANCED / HIGH のプリセット、露出の範囲 | [`Game/UI/GraphicsUI.h`](../Game/UI/GraphicsUI.h) |
| `editor.json` | エディタのシーンランチャー、エディタで包むシーン、Slime Dev Tools のワープ先 | [`Game/Editor/EditorUI.cpp`](../Game/Editor/EditorUI.cpp), [`Game/Editor/SlimeDevTools.cpp`](../Game/Editor/SlimeDevTools.cpp) |
| `loadout.json` | 旧アクションゲームの缶の初期装備と表示名 | [`Game/CanLoadout.h`](../Game/CanLoadout.h) |
| `assignment.json` | 評価課題デモの素材・パーティクルの発生源・戦闘の値 | [`Game/Scenes/AssignmentScene.cpp`](../Game/Scenes/AssignmentScene.cpp) |

本編のシーンそのもの (配置するオブジェクトとコンポーネント) は `Resources/Scenes/chrono.json`、
本編が読めないときに使う最低限のシーンは `Resources/Scenes/fallback.json` です。どちらもエディタで編集できます。

## 編集するときの注意

- **並びと数が決まっているもの**:強化カードは 18 枚、街の段階と敵の種類は 8 つ、ロード画面のページは 6 ページです。
  効果や絵をコードが番号で扱っているため、並びを変えたり数を増減したりすると起動時にエラーになります。
- **`_comment` で始まる項目**:説明用なので、コードは読みません。
- **文字の `{n}`**:`ui_text.json` の `{n}` は表示するときに数字に置き換わります。
- **リリース版**:新しいデータファイルを足したら、[`scripts/SlimeResources.json`](../scripts/SlimeResources.json) にも追加してください。
  この一覧にあるファイルだけがリリースにコピーされます。

## まだコードにあるもの

次の値は今もコードの中にあります。

- **スライムのルールの数値**:質量の段階や、武器・群れの調整値など。単体テストやコンパイル時の計算で使っているためです。
- **ゲーム中のメッセージ**:スライムの上に出る「バリケード 残り…」など。
- **UI の見た目**:ボタンの位置・色・フォント。
