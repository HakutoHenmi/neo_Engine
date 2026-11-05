# Third-party credits / 素材・ライブラリのクレジット

タイトル右下の CREDITS ボタンから、音楽・素材・フォント・ソフトウェアを閲覧できます。

## Music
Kevin MacLeod (https://incompetech.com/), CC BY 4.0、タイトル曲 "Lamadio Tiado" は SackJo22, CC BY 3.0。
曲別の出典は Music.md および ../Sound/BGM/CREDITS.md、ライセンス原文は
../Sound/BGM/CC-BY-4.0.txt に同梱しています。音源は未加工、ゲーム内で音量と速さの調整・ループ再生をします。

## Sound effects
吸収モードの効果音: EZduzziteh, rubberduck (OpenGameArt), Kenney (Music Jingles), すべて CC0。
scripts/BuildDevourAudio.py で素材と合成音を組み合わせて作成。出典は ../Sound/Devour/CREDITS.md。

## Models / UI
- Poly Haven: scenery, grass and PBR terrain — CC0. See MeadowRefresh.md,
  MeadowRefreshSources.json, MeadowAssets.md and PBR-Materials.md for sources.
- Poly Haven: city stage props, iron gate, cannon, rat and street/facade/roof
  surfaces — CC0. See CityStage.md and CityStageSources.json.
- Kenney: UI Pack (2.0), Input Prompts (1.5A). https://www.kenney.nl/ — CC0 1.0.
  原文: ../Textures/UI/kenney_ui-pack/License.txt,
  ../Textures/UI/kenney_input-prompts_1.5/License.txt.
- Quaternius: Ultimate Platformer Pack. https://quaternius.com/ — CC0 1.0.
  原文: ../Models/Quaternius/License.txt.
- スライム用のビーム・飛沫メッシュ、slime-pressure.wav / slime-recall.wav /
  slime-acid.wav はこのプロジェクトの生成スクリプトで作成しています。

## Fonts
All fonts below are distributed under SIL Open Font License 1.1.
- Rajdhani: Copyright (c) 2014 Indian Type Foundry. ../Fonts/Rajdhani/OFL.txt.
- M PLUS Rounded 1c: Copyright 2016 The Rounded M+ Project Authors.
  ../Fonts/MPLUSRounded1c/OFL.txt.
- Huninn (開発用ソースにのみ保持、Releaseには同梱しない): Copyright 2025 The Huninn Project Authors.
  ../Textures/fonts/Huninn/OFL.txt.
- Kiwi Maru (開発用ソースにのみ保持、Releaseには同梱しない): Copyright 2020 The Kiwi Maru Project Authors.
  ../Textures/fonts/Kiwi_Maru/OFL.txt.

## Software
- Dear ImGui: Copyright (c) 2014-2023 Omar Cornut. MIT. See Dear-ImGui-LICENSE.txt.
- DirectXTex: Copyright (c) Microsoft Corporation. MIT (DirectXTex.h).
- JSON for Modern C++: Copyright (c) 2013-2022 Niels Lohmann. MIT (json.hpp).
- Assimp: Copyright (c) 2006-2023 assimp team. See Assimp-LICENSE.txt.
- stb_truetype: Copyright (c) 2017 Sean Barrett. See stb_truetype-LICENSE.txt.
- cpp-httplib: Copyright (c) 2026 Yuji Hirose. MIT (httplib.h).
- EnTT and ufbx: project contributors. Bundled source files do not contain a
  standalone license notice; confirm the matching upstream version's notice when packaging.

## 出典確認が必要な既存素材
今回の一覧は、リポジトリ内のライセンス原文・ソース・生成スクリプトで確認できた範囲です。
旧効果音 kettei.mp3 / suittikirikae.mp3 / idou.mp3 などと旧BGM1.mp3 / BGM2.mp3、
その他ライセンスファイルのない旧モデル・テクスチャについては、作者・入手元の記録が
見つからないため推測でクレジットを付けていません。kettei.mp3 / suittikirikae.mp3 は
2026-11-06 にゲームから外し、Release にも同梱していません。元の配布ページや購入・制作記録が確認できたら、表示と本書を追記してください。
