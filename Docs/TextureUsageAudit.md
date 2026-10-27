# 提出ZIPのテクスチャ使用状況監査

2026-10-27。対象は `dist/SlimeAssault-Release.zip`（482.019 MiB、505.434 MB）。**通常の街プレイに対して、未使用候補16枚・17.839 MiBと、読み込むが街では描画しない草原用20枚・57.831 MiBが含まれていた。** 合計36枚・約75.7 MiB、ZIP全体の約15.7%に当たる。

ただし後者は旧モードや一部の診断実行で使う。**現行EXEのまま36枚を単純に削除して、すべての起動モードの動作を保てるという意味ではない。** 本監査では元素材・配布リスト・提出ZIPを削除・変更していない。

## 内訳

ZIP内の画像222枚を集計した。Textures配下のライセンス2ファイルは画像数に含めない。容量は展開後ではなく、ZIPエントリーの圧縮後サイズ。

| 分類 | 枚数 | ZIP内容量 | 判断 |
| --- | ---: | ---: | --- |
| 街ステージの材質として動的に参照 | 151 | 291.430 MiB | 維持 |
| 空・UI・共通処理などにソース参照あり | 35 | 27.015 MiB | 保守的に維持。選択式・旧システムUIも含む |
| 草原用を先読みするが街では描画しない | 20 | **57.831 MiB** | 読込分岐・対応モードの整理対象 |
| ランタイム・シーン・モデル照合で参照を確認できない | 12 | **12.513 MiB** | 除外候補 |
| 開発用テストだけに明示参照 | 3 | **4.936 MiB** | 通常Release配布からの除外候補 |
| FBXに同名の参照があるが通常街では使わない | 1 | **0.389 MiB** | 実際の参照先・旧モードを確認して除外候補 |

参照ありの分類は「画面の全フレームで必ず使う」という意味ではない。UIの入力デバイス切替やグラフィックス設定などの任意経路を残すため、明確な参照があるものは保守的に維持した。

## 57.8 MiBの先読みについて

`Game/Systems/ChronoInk.cpp:40-54` は草原用の色・法線・粗さ・AOを読み、関数ローカルの `pbr` に保存している。しかし `:76` で `slimeLife_` のときは `BuildCityStage()` に進み、`goto sceneryDone` で草原のエンティティ生成を飛ばす。

これらのローカルハンドルと `pbr` は草原の `scenery()` / `natural()` でだけ描画材質に設定される。街の `CityStage.cpp` は別の材質を読み込むため、通常の街ではこの20枚は読み込み・キャッシュ保持だけになっている。「ファイルを読んだ」ことを「ゲーム画面に使用した」こととは扱わなかった。

| 素材群 | 枚数 | ZIP内容量 |
| --- | ---: | ---: |
| leafy_grass（色4k、追加マップ2k） | 4 | 29.501 MiB |
| namaqualand_boulder_02 | 4 | 11.913 MiB |
| boulder_01 | 4 | 11.127 MiB |
| rock_09（色＋PBRの3枚） | 4 | 3.405 MiB |
| grass_medium_02（RGBA＋追加マップ3枚） | 4 | 1.886 MiB |
| **合計** | **20** | **57.831 MiB** |

`Game/Systems/ChronoSystem.cpp:38-41` では通常プレイは街になる。一方、`--legacy-domain` や多くの `*-smoke` 引数では旧草原側へ進む。特に現在の `--package-smoke` / `--dlss-package-smoke` は `-smoke` を含むので、この20枚を削除すると診断対象の景観も変わる。除外するなら、先に草原の読み込みを草原側の分岐へ移し、提出版で残すモードとスモークテストもそろえる必要がある。

## 17.8 MiBの未使用候補

主なものは旧草原用の `rock_face_02`、複数形の `namaqualand_boulders_01`、`Grass004`、旧草の画像。現在読み込んでいる `boulder_01` / 単数形の `namaqualand_boulder_02` とは別のファイルである。

- **参照を確認できない12枚・12.513 MiB**: 旧 `rock_face_02` の色＋PBR3枚、旧 `namaqualand_boulders_01` の色＋PBR3枚、`Grass004_AmbientOcclusion.jpg`、`grass_bermuda_01_rgba_2k.png`、`grass_medium_02_alpha_2k.png`、`button_rectangle_depth_flat.png`。
- **開発テストのみ3枚・4.936 MiB**: `grass004_color_1k.jpg`、`Grass004_NormalDX.jpg`、`Grass004_Roughness.jpg`。参照は `tests/UIValidationScene.h:82-83`。提出通常プレイの参照ではない。
- **FBX同名候補1枚・0.389 MiB**: `grass_medium_02_diff_2k.jpg`。草モデルのbase-color参照に同名がある。ただしモデル内の相対パスは配布側の `Textures/PolyHaven/` を指しておらず、通常街でその草モデルも生成されない。草原の明示材質は `grass_medium_02_rgba_2k.png` を使う。元diffのRGBとRGBA画像のRGBは画素一致を確認した。

全パスと個別容量は [候補一覧](TextureAudit/candidates.md)、全222枚の分類と根拠は [inventory.csv](TextureAudit/inventory.csv) / [inventory.json](TextureAudit/inventory.json)。

## 維持すべき大きいファイル

- `kloppenheim_06_puresky_8k_cube.dds`（ZIP内26.956 MiB）は `Engine/Renderer.cpp:313` と `Game/Scenes/GameScene.cpp:185` の空・環境用参照があり、未使用候補から外した。
- 街の色・法線・粗さ・AOは `CityStage.cpp:93-105` で材質IDからパスを組み立てる。完全なファイル名の文字列検索だけでは未使用と誤判定するため、surfaceIds・小物配列・place/modelのIDも展開して照合した。
- `PBR/NeutralNormal.png` は `Engine/HdrBloom.inl:3,12` の共通フォールバック用。PBRフォルダー全体を旧素材として除外してはいけない。

## 照合方法と限界

1. ZIPを直接読み、圧縮後サイズと画像パスを取得した。配布リストにあるだけでは使用の証拠にしない。
2. Game / Engineのソース、提出ZIP内のScenes / Data JSONとMTLを照合。Creditsの素材カタログや梱包マニフェストはランタイム参照と区別した。
3. 通常のタイトル→セレクト→mainStage→ChronoSystem→街分岐を追い、動的パスと草原のハンドルの行き先を確認した。
4. エンジンと同じufbxのbase-color / diffuse取得方法で配布側FBXを解析した。29件の画像参照があり、対象base-colorに埋込画像データはなかった。FBXの別解像度の名前だけを根拠に、同じ素材名の2k画像を必要とは判断しなかった。
5. 解析した配布フォルダーのモデル・MTL計68ファイルは、提出ZIPとCRCが一致することを確認した。OBJ/MTL参照とFBX参照の記録も候補判定に含めた。
6. ZIP画像はサイズ・CRCで重複候補を抽出し、候補があればSHA256で照合する方法で確認。**同一ファイルの重複はなかった。** 同一画像の別形式などは別問題で、grass_mediumのRGB重複は画素比較で確認した。

これは静的なコード・データフロー監査であり、全モードの実行時ファイルI/Oや全フレームのGPUサンプリングを記録したものではない。削除前には、対応モードを決めたうえで候補を除いた配布物の実機検証が必要。

この通常街プレイ向けの整理をすべて行えた場合、内容データだけの差引きではZIPが約482.0→**406.3 MiB**になる見込み（ZIP管理情報・再圧縮による差は別）。NTCや画質変更をせずに減らせる候補だが、すべてを現行の診断モードと両立させた削除としては未検証。

## 再集計

`python -X utf8 tests/AuditSubmittedTextures.py` で、現在のZIPとソースを再集計できる。監査補助スクリプトであり、自動削除や一般的な到達性証明は行わない。FBXの参照情報は `tests/InspectFbxTextures.cpp` をufbx.cとともにビルドし、配布側Modelsを引数にして `tests/out/texture-fbx-dependencies.tsv` に保存したものを併用する。今回の記録は [fbx-basecolor-dependencies.tsv](TextureAudit/fbx-basecolor-dependencies.tsv)、重複照合は [duplicate-check.json](TextureAudit/duplicate-check.json)。
