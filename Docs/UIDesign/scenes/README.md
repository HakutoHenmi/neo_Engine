# タイトル・セレクト・リザルト デザイン案

3 方向 × 3 画面の参考画像です。画面の数値は見本で、ゲーム内の描画ではありません。

| 方向 | タイトル | セレクト | リザルト |
| --- | --- | --- | --- |
| A 路面ペイント | `a1_title_road.png` | `a2_select_road.png` | `a3_result_road.png` |
| B B級怪獣映画 | `b1_title_kaiju.png` | `b2_select_kaiju.png` | `b3_result_kaiju.png` |
| C 柱のキズ | `c1_title_pillar.png` | `c2_select_pillar.png` | `c3_result_pillar.png` |

## 作り直し方

1. `python prep_assets.py` でゲームのテクスチャとスクリーンショットから素材を切り出します。
2. `python render.py` で HTML を 1920x1080 の PNG にします (Edge を使用)。`python render.py a1 c3` のように絞り込めます。

`gel.js` はスライムのゲル描画 (高さ場・屈折・吸収・鏡面) と網点の処理です。フォントは Google Fonts から読み込むので、書き出しにはネット接続が必要です。
