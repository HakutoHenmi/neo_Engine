# 提出ZIPの容量内訳

対象: `dist/SlimeAssault-Release.zip`。2026-10-27にZIPエントリーを直接集計。
ZIP全体: **505.43 MB（482.02 MiB）**、516ファイル。展開後: 528.04 MiB。

割合の分母はZIPファイル全体。各ファイルのZIP圧縮後サイズを使い、展開後容量と混同しない。1 MiB = 1,048,576 bytes。

| 種類 | ファイル数 | ZIP内容量 | 全体比 |
| --- | ---: | ---: | ---: |
| テクスチャ | 224 | 394.115 MiB | 81.76% |
| DLSS・Streamline | 6 | 30.529 MiB | 6.33% |
| 3Dモデル | 69 | 27.689 MiB | 5.74% |
| 音声・BGM | 17 | 25.223 MiB | 5.23% |
| ゲームEXE | 1 | 2.357 MiB | 0.49% |
| フォント | 4 | 1.785 MiB | 0.37% |
| シェーダー | 171 | 0.201 MiB | 0.04% |
| その他データ | 24 | 0.036 MiB | 0.01% |
| ZIP管理情報 | — | 0.084 MiB | 0.02% |

## テクスチャの詳細

| 種類 | ファイル数 | ZIP内容量 | ZIP全体比 |
| --- | ---: | ---: | ---: |
| 色JPEG | 46 | 113.978 MiB | 23.65% |
| 法線JPEG | 45 | 112.990 MiB | 23.44% |
| 粗さJPEG | 42 | 76.354 MiB | 15.84% |
| AO JPEG | 41 | 54.773 MiB | 11.36% |
| 空のDDS | 1 | 26.956 MiB | 5.59% |
| その他画像・付随データ | 49 | 9.064 MiB | 1.88% |

## 大きいファイル

| ファイル | ZIP内容量 | 全体比 |
| --- | ---: | ---: |
| `Streamline/nvngx_dlss.dll` | 29.682 MiB | 6.16% |
| `Resources/Textures/PolyHaven/kloppenheim_06_puresky_8k_cube.dds` | 26.956 MiB | 5.59% |
| `Resources/Textures/PolyHaven/leafy_grass_diff_4k.jpg` | 17.658 MiB | 3.66% |
| `Resources/Sound/BGM/Call to Adventure.mp3` | 8.638 MiB | 1.79% |
| `Resources/Sound/BGM/Carefree.mp3` | 6.136 MiB | 1.27% |
| `Resources/Models/PolyHaven/boulder_01_2k.fbx` | 6.014 MiB | 1.25% |
| `Resources/Textures/PolyHaven/leafy_grass_nor_dx_2k.jpg` | 5.740 MiB | 1.19% |
| `Resources/Sound/BGM/Volatile Reaction.mp3` | 4.977 MiB | 1.03% |
| `Resources/Textures/PolyHaven/asphalt_02_nor_dx_2k.jpg` | 4.815 MiB | 1.00% |
| `Resources/Textures/PolyHaven/boulder_01_nor_dx_2k.jpg` | 4.456 MiB | 0.92% |
| `Resources/Textures/PolyHaven/hand_truck_nor_dx_2k.jpg` | 4.013 MiB | 0.83% |
| `Resources/Textures/PolyHaven/water_manhole_cover_nor_dx_2k.jpg` | 3.996 MiB | 0.83% |
| `Resources/Textures/PolyHaven/castle_wall_slates_nor_dx_2k.jpg` | 3.948 MiB | 0.82% |
| `Resources/Textures/PolyHaven/namaqualand_boulder_02_nor_dx_2k.jpg` | 3.946 MiB | 0.82% |
| `Resources/Textures/PolyHaven/leafy_grass_AO_2k.jpg` | 3.942 MiB | 0.82% |

今回のNTC実験はこの提出ZIPに入っていない。道路の色・法線・粗さ・AOは元JPEGのままで、NTC実験用SDK・学習データも入っていない。

ZIP SHA256: `b6edd42594ce56719014556723bc5252ebdebbbbb64fa27324a3239220dd9951`
