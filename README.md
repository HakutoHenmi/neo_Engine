# 逸見 珀斗 — Game Development Portfolio

地上から地下へスクロールするHD2D風のポートフォリオです。公開先は [HakutoHenmi/neo_Engine](https://github.com/HakutoHenmi/neo_Engine)。サイト用の `gh-pages` ブランチに配置し、ゲームエンジンの `master` ブランチは変更しません。

## 掲載内容

| 分類 | 作品 | 紹介動画 |
| --- | --- | --- |
| ゲーム | スライムアクション | YouTube埋め込み |
| ゲーム | Defectory | YouTube埋め込み |
| ゲーム | 逃灯 | YouTube埋め込み |
| ゲーム | 削られ島でぶっ飛ばせ！ | YouTube埋め込み |
| ゲーム | リビルド | YouTube埋め込み |
| 技術デモ | Nigi-Nigi（にぎにぎ） | YouTube埋め込み・BOOTHリンク |

名前・作品名・担当範囲はPDFの作品ページをもとに整理しました。説明は編集用の短い初稿です。PDFの目次と本文に異なる作品名があるため、作品ページの名称を採用しています。制作期間・開発環境に食い違いがある箇所は断定せず、後から編集する欄にしています。

プロフィールからPDFを開けます。掲載内容とページ構成を保ち、公開用のPDFと背景画像を圧縮しました。紹介動画はすべてYouTubeで配信し、詳細を開いたときだけ読み込み、閉じると停止します。各動画にはYouTubeで直接開くリンクもあります。

Nigi-Nigiの紹介は [BOOTHの商品ページ](https://yurufuwa-lab.booth.pm/items/8531474) と [紹介動画](https://youtu.be/BFgeKeUAiSA) に基づいています。どちらも同じ作品なので1件にまとめています。

## 内容を編集する

`content.js` の各作品を編集します。`category` は `game` / `tech` の2種類です。

| 項目 | 用途 |
| --- | --- |
| `name` / `role` / `bio` | 名前・肩書き・自己紹介 |
| `contacts` | GitHub・PDFなどのリンク |
| `id` / `category` | 作品番号・分類 |
| `title` / `subtitle` / `tags` | 作品名・短い説明・タグ |
| `year` | 制作年。空欄なら表示しません |
| `description` | 詳細説明。改行は `\n` |
| `responsibility` | 担当範囲 |
| `highlights` | 実装の工夫・検証結果 |
| `tools` | 使用言語・エンジン・ツール |
| `image` | 作品画像。例：`./assets/rebuild.jpg` |
| `video` | YouTubeのURL |
| `url` / `urlLabel` | 外部作品URL・リンクの表示名 |

紹介動画を変更するときは各作品の `video` にYouTubeのURLを指定してください。説明は同じ作品の `description` / `responsibility` / `highlights` / `tools` で編集できます。

画面構成・技術領域の文章は `index.html`、配色・余白・文字サイズは `style.css` で編集できます。実際の説明を入れたら「説明は編集中」の注記も更新してください。インストールやビルドは不要です。Google Fontsに接続できない場合は端末の標準フォントを使います。

## GitHub Pagesの初回設定

このリポジトリでは既存のエンジンを保護するため `gh-pages` を公開元にします。

1. [Settings → Pages](https://github.com/HakutoHenmi/neo_Engine/settings/pages) を開きます。
2. Sourceで **Deploy from a branch** を選択します。
3. Branchに **gh-pages**、フォルダに **/(root)** を選んで **Save** を押します。
4. デプロイが完了すると `https://hakutohenmi.github.io/neo_Engine/` で公開されます。

初回設定後は `gh-pages` の更新で再公開されます。`master` を公開元にしないでください。

[GitHub公式の公開元設定手順](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)

## UIの演出

HPは装飾として100 / 100を表示します。EXPはスクロール進捗、レベルはLv.01〜03で変化します。深度も閲覧位置に応じた演出です。動きを減らす端末設定ではパララックス・光のアニメーション・スムーズスクロールを抑えます。

## 背景画像

内蔵画像生成ツールで制作したオリジナル背景です。

- `assets/surface.webp`：HD2D風のピクセルアート、森林と遺跡、青い山々、ミントの空、暖かい日差し、立体的な奥行き。
- `assets/underground.webp`：HD2D風のピクセルアート、藍色の洞窟、発光鉱石、琥珀色のランタン、地上から差す光。

