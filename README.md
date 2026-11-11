# 逸見 珀斗 ポートフォリオ (GitHub Pages)

スクロールするほど、ページに置いた物をスライムが飲みこんで大きくなるポートフォリオです。
内容はスライド「ヘンミ_ハクト_ポートフォリオ」と同じで、見た目もスライドとゲーム「チリツモ」に合わせています。

公開先は `gh-pages` ブランチ (`https://hakutohenmi.github.io/neo_Engine/`)。ゲームエンジンの `master` は変更しません。

## ファイル

| ファイル | 内容 |
| --- | --- |
| `index.html` | 文章・作品・リンクのすべて。作品はカードで概要を並べ、詳細は下の `<template id="work-…">` に書きます |
| `style.css` | 配色・レイアウト。色は先頭の `:root` で変えられます |
| `main.js` | スライム・飲みこむ物・下の巻尺・動画の読み込み |
| `assets/works/` | 作品の画像 (スライドで使っている画像と同じ) |
| `assets/tools/` | 自己紹介のツールのロゴ |
| `assets/portfolio.pdf` | スライドを PDF にしたもの |

## スライムと飲みこむ物

- ページの中の `<span class="pickup" data-item="…">` が飲みこまれる物です。画面の上 45% より上へ来ると飲みこみ、戻ると吐き出します。
- 置く場所は `style="left:…;top:…"` (セクション内の位置) で変えられます。
- 物の絵と実際の大きさは `main.js` の `ITEMS`、飲んだ数ごとのスライムの大きさは `SIZES` (5cm から 22m)、「〜くらい」の文は `LIKE` にあります。
- 動きを減らす設定の端末では、物は飛ばずにその場で消えます。

## 作品の詳細

カードのリンク `#work-…` と同じ id の `<template>` の中身が、押したときにダイアログで開きます。`data-color` で背景色 (lime / blue / night / yellow / red / paper) を選べます。URL に `#work-toto` などを付けると、その作品を開いた状態で表示できます。

## 動画

作品の `data-video` に YouTube の ID を書きます。押すまで YouTube は読み込みません (youtube-nocookie を使用)。

## 確認

ビルドは不要です。ローカルで見るときは、このフォルダーで `python -m http.server` を実行して `http://localhost:8000/` を開きます。

## 更新するときの注意

GitHub Pages はファイルを 10 分ほどブラウザに覚えさせます。`style.css` や `main.js` を変えたら、`index.html` の `?v=…` の値も変えてください (古いファイルが残って見た目が崩れるのを防ぎます)。
