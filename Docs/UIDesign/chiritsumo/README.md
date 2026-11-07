# チリツモ UI 案

タイトル・セレクト・リザルトの 3 方向 × 3 画面の参考画像です。画面の数値は見本で、ゲーム内の描画ではありません。

| 方向 | タイトル | セレクト | リザルト |
| --- | --- | --- | --- |
| 物差し | `s1_title_scale.png` | `s2_select_scale.png` | `s3_result_scale.png` |
| 山 | `p1_title_pile.png` | `p2_select_pile.png` | `p3_result_pile.png` |
| 腹の中 | `g1_title_gut.png` | `g2_select_gut.png` | `g3_result_gut.png` |

## 作り直し方

1. リポジトリの根で `python -m http.server 8765 --bind 127.0.0.1` を動かします (モデルの読み込みに必要)。
2. このフォルダで `python build_pages.py` を実行し、HTML を書き出します。
3. `python render.py` で 1920x1080 の PNG にします (Edge を使用)。`python render.py s1 p3` のように絞り込めます。

`scene3d.js` が共通処理です。three.js でゲームの PolyHaven モデルを読み、cannon-es の物理で積み、ロゴの字形もここに持っています。three.js、cannon-es、フォントは CDN から読むので、書き出しにはネット接続が必要です。キーのアイコンは `../scenes/assets/` を使います。

## ゲームで使う画像

- `bake_sprites.py`: 小道具とロゴを `Resources/Textures/UI/Scale/` へ書き出します (`bake.html` を使用)。
- `matte_slime.py`: ゲームの GPU 流体を撮った `tests/out/sprite` から、透明度付きのスライムの連番を作ります。手順は `Docs/UITheme.md` にあります。
