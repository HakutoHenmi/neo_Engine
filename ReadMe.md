# 評価課題 追加実装まとめ

このプロジェクトで追加・拡張した評価課題要素をまとめます。
主な確認シーンは `AssignmentScene` です。タイトルの Select Scene から `Assignment Scene` を選ぶと、スキニングモデル、アニメーション補間、骨デバッグ、武器追従、手から出るパーティクル、GPU Particle をまとめて確認できます。

## 影の凸凹と移動中の視点の揺れを修正（2026-10-19）

影は粒子ごとの円板を重ねていたため、液体本体が滑らかでも輪郭に粒の丸みが出ていました。また、流体更新より前の粒子を描いており、移動中は本体と1フレームずれていました。

| 原因 | 修正内容 |
| --- | --- |
| 粒子の丸い輪郭がそのまま影になる | [液体の影](Resources/shaders/FluidShadow.hlsl)を追加。本体と同じ密度場・同じ表面の閾値を光の方向から探索し、表面の深度を書く |
| 影と本体の更新がずれる | [描画順序](Engine/Renderer.cpp)を流体更新→密度再構成→影→本体へ変更。再構成はフレームに1回だけ行う |
| 成長後のカメラ距離を毎フレーム設定し直して減衰させていた | [追従カメラ](Game/Systems/ChronoInk.inl)の目標距離に成長分を足し、距離を継続して補間する |
| CPUとGPUの時間が違い、33ms超のフレームで追従速度を増幅していた | [ゲームの時計](Game/Scenes/GameScene.cpp)をエンジンと共有。GPUへ実際の移動時間を渡し、同じ時間を1/100秒以下の刻みに分割して計算する。速度の倍率補償を撤去 |
| 目標点6600個で同じ地面判定・密度・物性を毎回再計算 | [目標点の生成](Game/Systems/SlimeLife.inl)で個体共通の値を1回だけ計算。地面のレイ判定は粒子ごとから個体ごとへ削減 |
| 大きい武器の描画グリッドがセル単位で切り替わる | [描画領域](Engine/FluidVolumeDomain.h)を本体へ連続追従させ、移動時のサンプリング位置の飛びを除去。光源のシャドウマップはテクセル単位で安定化 |

`tests/Run-SlimeLifeTests.ps1`で通常形状・3武器と最大サイズの表面／影を検証します。`tests/Run-SlimeLifeSmoke.ps1 -Executable build/growth/bin/DirectXGameApp.exe -MotionOnly`は成長3段階・ハンマー544／1200・剣1200・飛行機1200を移動させ、フレーム時間を変化させた状態のカメラ距離とGPUへ渡す時間を記録します。結果は`tests/out/slime-life-motion.txt`、移動中の影の画像は`tests/out/slime-life-shadow-moving.png`です。

Developmentビルド（警告0・エラー0）とCPU／GPU回帰テストが通過しました。8条件の影を密度場から独立に求めたCPU参照と比較し、輪郭差は0ピクセル。18〜50msの不規則なフレームでも、一定速度12の武器追従の速度標準偏差は約0.00001でした。

RTX 4060 Laptop GPU・Developmentの同じサイズ別シナリオで、前回の成長安定化版から次のように改善しました。値はフレーム時間の95パーセンタイルで、小さいほど重いフレームが少ないことを示します。

| 条件 | 前回版 | 今回版 |
| --- | ---: | ---: |
| 最大通常形状・質量1200 | 33.90ms | 26.22ms |
| 最大ハンマー・質量1200 | 37.82ms | 17.17ms |
| 本体960＋子分12体 | 36.08ms | 22.81ms |

サイズ別7条件に加え、全7形態の移動・通常プレイも通過し、粒子の発散・D3D12検証エラーはありませんでした。背景・CPU・解像度の影響を含む1回の測定で、全環境の一定FPSを保証する値ではありません。確認用の`build/growth/bin/DirectXGameApp.exe`は再ビルド済みです。通常の出力先もエンジン・ゲーム・シェーダーを揃えて再ビルドしてください。

## 成長時の表面の膨縮・処理負荷を安定化（2026-10-19）

大きくなったスライムがブクブクと膨縮する原因を、流体計算から修正しました。従来は粒子間の計算距離を0.4に固定して目標密度だけを下げており、質量600では目標18.71が粒子1個の最低密度24.48を下回っていました。表面の粒をぼかすだけでは、この不整合は解消しませんでした。

| 修正箇所 | 現在の処理 |
| --- | --- |
| [流体ソルバー](Resources/shaders/FluidSimCS.hlsl) | 個体の粒子間隔で距離を正規化し、密度・圧力・粘性を同じ基準で計算。大きくなっても近傍粒子を失わない |
| [個体ごとの近傍探索](Resources/shaders/FluidSimCS.hlsl) | 個体ごとのセル幅と所有者を含むハッシュを使い、大小の個体が混在しても他個体の粒を大量に探索しない。モーフ用テーブルは65536から8192バケットへ縮小 |
| [表面再構成](Resources/shaders/FluidVolumeCS.hlsl) | 近傍の入れ替わりで向きが変わるPCAをモーフ表面から外し、粒子間隔に比例した球状カーネルを使う |
| [描画領域](Engine/FluidVolume.cpp) | 粒の幅とセル幅を一緒に拡大。固定6600粒子のまま、1粒あたりの描画処理量を大きさに依存させない |

ゲーム内の成長上限1200と粒子数6600は維持しています。GPU回帰テストはさらに長さ16倍まで密度・圧力補正・粘性の一致を確認し、異なる大きさの13個体を重ねても密度が混ざらないことを検証します。最大サイズの通常形状・3武器でも、表面描画の処理範囲は1軸10セル、代替粒の重ね描きは0でした。

`tests/Run-SlimeLifeTests.ps1`でルール・GPU物理・最大サイズの描画を確認できます。実機のサイズ別測定は`tests/Run-SlimeLifeSmoke.ps1 -Executable <exe> -StressOnly`。成長4段階・最大ハンマー／剣・子分12体の状態で、移動・停止後の表面、フレーム時間、GPU時間、粒子の有限性を記録します。詳しい結果は [検証記録](Docs/SlimeLife.md#検証と素材) を参照してください。

Developmentビルドと上記検証が通過し、7条件すべてで粒子の発散・D3D12検証エラーはありませんでした。最大通常形状の重いフレーム（95パーセンタイル）は36.68→33.90ms、最大ハンマーは45.05→37.82ms。最大サイズの修正前／後画像は`tests/out/slime-life-max-size-before.png`／`slime-life-max-size.png`です。全体のFPSは背景・CPU・解像度にも依存しますが、流体の計算結果と1粒あたりの再構成処理量は成長で破綻しない構成へ変更しました。確認用実行ファイルは`build/growth/bin/DirectXGameApp.exe`です。通常の出力先もエンジンとシェーダーを組で再ビルドしてください。

## 目の正面配置・変形中の移動・武器攻撃の調整（2026-10-19）

| 問題 | 今回の修正 |
| --- | --- |
| 目が進行方向の前へ来ず、飛行機の尾翼側に付く | カメラ側へ向ける設定を草原では解除し、体の正面の実表面へ配置。調合室ではカメラ向きを維持。剣の細い先端には目の幅と間隔を合わせる |
| 変形して移動すると小刻みにガクつく | 武器の方向転換を補間し、点の追従速度に回転も含める。GPUの各サブステップで、その時刻の目標位置を追うよう修正。描画グリッドも小さな変形では解像度を保つ |
| ハンマーが横向きのまま振られる | 頭部の長い軸を振り下ろす面に沿う向きへ90度回転。縦の振り上げ・叩きつけを共通の描画・命中軌道で計算 |
| 剣のコンボが不自然 | 斜め斬り・回転斬り・コンボ受付・追加命中区間を撤去。毎回0.64秒の横薙ぎだけを行う。次の横薙ぎの入力予約、変形維持、R／Bでの解除は継続 |

主な変更先は [武器ルール](Game/Chrono/SlimeWeaponRules.h)、[入力・GPU目標点・HUD](Game/Systems/SlimeLife.inl)、[GPUの目標位置](Resources/shaders/FluidMorph.hlsli)、[サブステップの時刻](Engine/Renderer.cpp)、[描画グリッド](Engine/FluidVolumeDomain.h)、[目の配置](Resources/shaders/SlimeDecoration.hlsli) です。

一定速度12の移動を再現したGPU検証では、30fpsでの速度の標準偏差が0.49220から表示精度で0.00000へ減少し、60／120fpsも同じ精度で安定しました。これは目標位置の段差だけを調べるテストで、液体らしい揺れや命中時のカメラ演出は残しています。`tests/Run-SlimeLifeTests.ps1`でCPU・GPUの軌道、移動、ハンマー・剣・飛行機・大型スライムの表面と目を確認できます。`--face-only`ではカメラを回しても草原の顔の向きが変わらないことも確認します。

Developmentのビルド（警告0・エラー0）、CPU/GPUテスト、実ゲームの移動・攻撃・手動解除が通過しました。実ゲームでは質量604・粒子6600を維持し、ハンマー5回・剣の同じ横薙ぎ5回・飛行機の突進4回を確認。D3D12の検証エラーもありません。`tests/out/slime-life-hammer-downswing.png`と`slime-life-sword-sweep.png`に攻撃画像を保存しています。確認用の実行ファイルは`build/growth/bin/DirectXGameApp.exe`。通常の出力先で実行する場合もエンジンとゲームを再ビルドしてください。

## 液体らしい武器・内部の泡・成長後の目を修正（2026-10-19）

武器を維持すると固い塊に見えること、泡が柄の下に偏ること、大きいスライムの表面が細かく動きすぎること、目が体に埋もれることを修正しました。

| 症状・原因 | 変更内容と主なファイル |
| --- | --- |
| 武器が静止した形へ強く固定され、厚い液体の光が吸収されすぎる | [武器の共通姿勢](Game/Chrono/SlimeWeaponRules.h)に体積を保つゆっくりした伸縮・せん断を追加。[追従処理](Game/Systems/SlimeLife.inl)は待機中のばねを弱め、攻撃時は従来の追従力を使う。[光学処理](Resources/shaders/FluidRaymarch.hlsl)の吸収を成長倍率に合わせて調整 |
| 泡が通常スライムの楕円形の範囲から生成され、ハンマーの頭部へ届かない | [泡の配置](Resources/shaders/SlimeDecoration.hlsli)を本体の実際のGPU粒子に追従させ、頭部と柄の内部へ分散。皮膚に接する前に薄くし、外へ抜ける泡は12個・小さめ・ゆっくりに変更 |
| 成長して粒が疎になっても表面の再構成幅が固定で、粒の密度や近傍の変化が凹凸になる | [表面再構成](Resources/shaders/FluidVolumeCS.hlsl)で成長に合わせて粒の幅を広げ、細長いカーネルを球に近づける。描画の体積は目標密度から求め、物理密度の細かな変動で表面を膨縮させない |
| 目と泡だけ旧来の小さい成長倍率を使い、目を液体内部のコアへ置いていた | [描画定数](Engine/FluidVolume.cpp)を実際の成長倍率に合わせ、目を実際の液体表面へ配置。続く正面配置の修正で、草原は体の正面、調合室はカメラ側を使う |

通常スライムの自由な流動、着地の揺れ、変形維持・連打・手動解除は継続します。通常スライムの粒を個別の目標点へ固定する処理は追加していません。

検証は`tests/Run-SlimeLifeTests.ps1`、`Run-FluidTests.ps1 -Case '--face-only'`／`'--near-volume-only'`、Developmentビルド、実ゲームの成長スモークが通過。青いハンマー（質量232）と大型スライム（600）を3視点で描画し、内部の泡と両目が実際に画像へ出ることを確認しました。実ゲームでも質量604・粒子6600・ハンマー連打・剣3段・手動解除が通り、D3D12の検証エラーはありません。詳しい数値と画像の場所は [検証記録](Docs/SlimeLife.md#検証と素材) にまとめています。

今回はC++側の描画定数とGPUバッファの接続も変更しています。**シェーダーだけを古い実行ファイルへコピーせず、エンジンとゲームを一緒に再ビルドしてください。** 検証済みの実行ファイルは`build/growth/bin/DirectXGameApp.exe`です。

## ハンマーの色の帯・粒の重なりを修正（2026-10-19）

ハンマーの頭部に粒の輪郭が大量に重なり、途中で色が分かれる現象を修正しました。変形に合わせて流体の描画領域を拡大・縮小している一方、描画方式を切り替える条件には固定の距離（縦4・横46〜48.5）を使っていたことが原因です。領域内の頭部まで粒ごとの代替描画へ切り替わり、厚みや色の異なる描画が混ざっていました。

- 表面描画と粒の代替描画が、現在の領域サイズとセル幅から同じ境界を計算するようにしました。
- 領域の余白に、粒の広がり・表面フィルター・境界のぼかし・グリッドへの丸めを含め、成長した武器の先端も一つの流体表面で描きます。
- 遠方の代替描画でも、現在の素材色・透明度を参照するよう統一しました。

変更先は [描画領域の余白](Engine/FluidVolumeDomain.h)、[領域の組み立て](Engine/FluidVolume.cpp)、[表面と代替描画の境界・色](Resources/shaders/FluidRaymarch.hlsl) です。質量232の青いハンマーを使ったGPU再現テストでは、誤って代替描画へ切り替わる粒子が6021／6600個から0個になり、正面・上・下の3視点で粒の重ね描きも0ピクセルになりました。実行は`tests/Run-FluidTests.ps1 -Case '--surface-only'`。この検証は`Run-SlimeLifeTests.ps1`にも追加しました。

Developmentビルド、CPU・GPUの武器／表面テスト、細かい解像度の水面・遠方表示・目と泡の描画、実ゲームの成長・変形維持・連打・手動解除が通過しました。実ゲームのハンマー画像でも頭部の粒の重なりが消え、粒子数6600を維持し、D3D12の検証エラーはありません。

別件として、既存の`--volume-only`のセル幅0.56の薄い水面テストは失敗します。変更前のテストと未変更の密度計算でも、水の密度ピーク0.203が表面の閾値0.25に届かず失敗することを確認しました。セル幅0.18／0.25／0.40の検証は通過しており、今回のハンマーの境界修正とは分けて記録しています。

## 変形の維持と武器ごとの連続攻撃（2026-10-19）

攻撃が終わると毎回スライムへ戻っていた処理を改め、変形状態と攻撃動作を分離しました。一度変形すると姿を維持し、**R／ゲームパッドのB**で好きなタイミングに解除できます。武器を構えたまま移動・ジャンプもできます。

| 武器 | 変形中の左クリック／RT |
| --- | --- |
| ハンマー | 縦に振り下ろす→頭が少し潰れて跳ね返る→構え直す。長押しで連打 |
| 剣 | 毎回同じ横薙ぎ。コンボ・回転斬りは撤去。入力を止めても構えを維持 |
| 飛行機 | 入力ごとに突進。突進終了後も飛行機の姿を維持 |

1／2／3・十字キーで武器を選び、左クリック／RTで変形します。変形後は同じ入力で攻撃し、別の武器を選んで入力すると切り替えます。取り込みはスライムに戻ってから行います。被弾で攻撃は中断しますが、必要な大きさを満たす武器の姿は維持します。小さくなって変形条件を下回ると通常形状へ戻ります。

実装は [武器の維持・軌道と命中判定](Game/Chrono/SlimeWeaponRules.h) と [入力・GPU目標点・戦闘・HUD](Game/Systems/SlimeLife.inl) に分けています。描画と攻撃判定が同じ軌道を使い、振りごとの命中IDで毎フレームの重複ダメージを防ぎます。剣は0.24秒の次の横薙ぎの入力予約だけを持ち、コンボ段数は持ちません。

速い振りでも形を保てるよう、武器の各点の移動速度をGPUへ渡し、シミュレーションの各ステップで追従に使います。武器の速度上限も軌道の速さに合わせ、通常のスライムの速度上限は維持しています。

当初の実装時にはDevelopmentビルド、CPU・GPUテスト、既存のChrono・カプセル化テストが通過し、ハンマー5回・剣の3段コンボ・飛行機4回と手動解除を確認しました。剣はその後、上記の単発の横薙ぎへ変更しています。過去の結果と現在の検証は [検証記録](Docs/SlimeLife.md#検証と素材) を参照してください。

## 吸収による成長と段階解放（2026-10-18）

通常プレイのSlimeLifeに、吸収して本体を大きくし、子分と武器を段階的に増やす進行を追加しました。

| 段階 | 戦い方・子分 | 本体の変形条件 |
| --- | --- | --- |
| 序盤 | 本体のみで開始し、被弾で生まれた子分が自動攻撃。プレイヤーはタックル・踏みつけで攻撃。子分上限2体 | 本体質量180未満では武器に変形できない |
| 中盤 | 吸収による成長値180で子分上限6体。本体の武器攻撃を併用 | 本体質量180でハンマー、300で剣 |
| 終盤 | 成長値480で子分上限12体。群れと複数の武器で攻撃 | 本体質量480で飛行機型 |

成長値は「開始質量100＋今回の出撃で確定した吸収量」です。武器の制限には**現在の本体質量**を使うため、被弾分裂で小さくなった場合は再び必要な大きさまで育てます。HUDで現在の大きさ・子分数と上限・武器ごとの必要質量を確認できます。

- 本体・子分のどちらが吸収しても、復元動作が完了した時点で本体の質量と最大HPが増えます。現在HPの自動回復は追加していません。実際の流体・当たり判定・カメラ距離も大きくなります。
- **分裂は被弾時のみ**です。出撃時の子分と吸収後の自動生成を撤去しました。被弾で生存し、子分枠が空いていると、本体の質量・HP・粒子を分けて子分が生まれます。吸収分はそのまま本体に蓄積します。
- 本体質量100で開始し、群れの最大質量を400から1200へ拡張。拾える小物を22個から40個へ増やし、必要な大きさと獲得量を段階別に調整しました。
- 敵は合計18体を6体ずつ配置します。開始時に6体、本体質量180で次の6体、480で最後の6体が出現し、終盤の武器と子分を使う前にクリアしてしまう進行を防ぎます。
- 左クリック／RTは序盤にタックル、成長後は選択中の武器で攻撃します。未解放の武器を選んで攻撃した場合は必要質量を表示します。1／2／3または十字キー左右で武器を選択します。
- GPUの個体管理を4枠から16枠に拡張し、本体＋最大12体の子分を扱います。個体番号を空間ソートキーの上位2bitへ詰めていた処理を改め、子分が増えても異なる個体の粒子割り当てが混ざらないようにしました。

主な変更先は [成長・解放ルール](Game/Chrono/SlimeLifeRules.h)、[ゲーム進行・HUD](Game/Systems/SlimeLife.inl)、[GPU個体管理](Resources/shaders/FluidMorph.hlsli) です。リトライ・新規出撃で成長・子分・武器条件を初期化し、調合室のプレビューは従来どおり開始サイズのままです。詳細と検証方法は [SlimeLifeの仕様](Docs/SlimeLife.md) を参照してください。

### 分裂・大きさ・前進時の形状の修正（2026-10-18）

| 対象 | 修正前の問題 | 修正内容 |
| --- | --- | --- |
| 子分生成 | 開始時に1体おり、吸収でも子分が増えていた | 初期の子分と`LifeBudChild`を削除。実際にダメージを受けた場合だけ`LifeTakeHit`が分裂を行う |
| 成長の見た目 | 立方根の倍率と自動分裂による質量消費で、大きさが分かりにくかった | 開始質量100を超えると`√(質量/100)`で拡大。質量480で約2.19倍、最大1200で約3.46倍。小さい子分の倍率は従来どおり |
| 流体密度 | 拡大率だけを変えると、密度補正が新しい体積と食い違う | 目標密度を粒子数と実際の倍率の3乗から計算し、拡大した体積に合わせる |
| 前進中の正面 | 世界座標に対する減衰で外側の液体が遅れ、さらに移動中の縦潰れが加わっていた | 減衰を本体の流速との差に変更し、走行だけで縦に潰す処理を削除。中心が先に動く慣性と、着地時の潰れ・揺れは維持 |

DevelopmentビルドとCPU・GPUテストが通過しました。実ゲームでも本体質量496・長さ約2.23倍・3種類の変形へ到達し、無被弾では子分0体、合計粒子数6600を維持しました。前進・方向転換・停止・中心部の先行も実ゲームで確認し、D3D12の検証エラーはありません。CPUテストでは被弾による子分生成・上限・質量と粒子の保存を、GPUテストでは2秒間前進したときの正面の高さと粒子量を確認しています。数値と実行方法は [SlimeLifeの検証記録](Docs/SlimeLife.md#検証と素材) を参照してください。

## State Patternによる状態管理の見直し（2026-10-16）

授業資料を基準に戦闘の状態管理をレビューし、プレイヤー・通常敵・ボス・群れ敵の4系統、合計32状態を状態クラスへ分割しました。詳しい指摘・設計・変更範囲は [State Patternレビュー記録](Docs/StatePatternReview.md) にまとめています。

### どこをどう変えたか

| 対象 | 変更前 | 変更後・主な実装先 |
| --- | --- | --- |
| 従来モードのプレイヤー（11状態） | `PlayerActionSystem::Update` の大きなswitchで待機・攻撃・回避・液状化などを処理 | [PlayerActionStates.inl](Game/States/PlayerActionStates.inl) の状態クラスへ分割。攻撃判定と回避へのキャンセル条件も状態側に移動 |
| 従来モードの通常敵（6状態） | `EnemyAISystem::Update` のswitchで追跡・攻撃予告・攻撃・スタンを処理 | [EnemyAIStates.inl](Game/States/EnemyAIStates.inl) へ分割。攻撃判定の開始・終了、警告色・スタン色をEnter / Exitへ整理 |
| 従来モードのボス（9状態） | `BossActionSystem::Update` のswitch内で行動・パターン選択・攻撃の後始末を処理 | [BossActionStates.inl](Game/States/BossActionStates.inl) へ分割。攻撃パターンの選択を共通化し、攻撃中断時も後始末を実行 |
| SlimeLifeなどで使用する群れ敵（6状態） | `TickSwarm` のif列で出現・追跡・予告・突進・回復・死亡を処理 | [SwarmStates.inl](Game/Chrono/SwarmStates.inl) へ分割。各状態クラスが行動と次状態を決定 |

共通の基底クラスは [Engine/IState.h](Engine/IState.h) です。Systemは現在の状態の仮想関数を呼び、通常の遷移先は状態クラスが決めます。プレイヤー・通常敵・ボスの `ChangeState` は「旧状態のExit → 状態とタイマーの更新 → 新状態のEnter」の順で処理します。被弾・死亡を扱う [CombatSystem](Game/Systems/CombatSystem.h) と [HealthSystem](Game/Systems/HealthSystem.h) もこの経路を使います。

状態クラスのインスタンスは共有し、タイマーや入力履歴は各エンティティのコンポーネントに保持します。既存の状態の列挙値、保存形式、Systemの更新順序、基本的なダメージ・行動時間は維持しています。

### レビューで見つけて修正した不具合

| 発生条件・以前の問題 | 修正内容 |
| --- | --- |
| ボスのジャンプ攻撃が未登録のときに `patterns[-1]` を参照する。選択済みインデックスの上限も未確認 | 有効な代替パターンを選択し、選択中のパターンが削除された場合もIdleに戻す |
| ボスが攻撃中にダウン・死亡すると、通常の攻撃終了処理を通らずビームが残る | 状態のExit / Enterで本体とビームの攻撃・描画・放出を停止。実体は戦闘の反復終了後にCleanupSystemで破棄 |
| ワープ中のプレイヤーが被弾すると、移動処理が無効のままになる | ワープのEnterで元の有効状態を保存し、被弾・着地・リセット時のExitで復元 |
| 短いフレームの直後に長いフレームが来ると、Shootが複数回発射してHPも重複消費する | 初回フレームの時間による推測をやめ、状態に入るたびに一度だけ発射 |
| 複数プレイヤーの入力履歴をSystem全体で共有し、押し始めの判定が干渉する | 前回の攻撃・ハンマー・回避入力を各PlayerActionComponentで管理 |
| HPが減った状態でのけぞると、HP倍率が毎フレーム累積して体が縮み続ける | StaggerのUpdateで基準姿勢を設定してからHP倍率を適用 |
| ボスが通常攻撃中に凍結・泡で止まると、解除後も本体の攻撃判定が戻らない | Attack継続中は本体とビームの判定を復帰。既に命中した対象の履歴は維持 |

### 検証方法と確認結果

リポジトリのルートで次を実行できます。

```powershell
./tests/Run-StatePatternTests.ps1
./tests/Run-ChronoTests.ps1
./tests/Run-SlimeLifeTests.ps1
./tests/Run-EncapsulationTests.ps1
```

`Run-StatePatternTests.ps1` は、生成物を `build/state-pattern` に置いてDebugアプリ全体をビルドし、実際のゲーム実装をリンクした回帰テストを実行します。リソースのコピーとゲーム画面の起動は行いません。同じソースで全体ビルドを確認済みの場合は `-SkipBuild` でテストのビルド・実行だけを再実行できます。

2026-10-16にDebug全体ビルドが **警告0件・エラー0件** で成功し、追加した7組の状態遷移テストと上記の既存テストが通過しました。SlimeLifeの検証には、既存のGPU変形・取り込み表現のテストも含まれます。ゲーム画面での手動操作による確認は未実施です。

今回の変更範囲は上記4系統です。Chronoの大型ボスの変形・演出やSlimeLife本体の取り込み・変形攻撃など、別の状態管理の扱いはレビュー記録に記載しています。

## 操作方法

- `W / A / S / D`: モデル移動。移動開始・停止で Idle と Walking を補間します。
- `Space` またはゲームパッド `A`: attack1 アニメーションへ遷移します。
- 右スティック、または矢印キー: カメラ操作。
- `0` - `9`: AssignmentScene 内のポストエフェクト手動確認。実際のゲーム中でも各状態に応じて自動発火します。
- `ESC`: タイトルへ戻る。
- ImGui 有効時: `GPU Particle Editor` で GPU Particle の emitter / field / type を調整できます。

## 加点要素の対応状況

| 項目 | 状態 | 内容 |
| --- | --- | --- |
| Skinningモデルの表示 | 実装済み | FBX の bone weight / bone index を読み込み、Vertex Shader でスキニングして表示 |
| ComputeShaderによるSkinning | 整理済み | 未接続だった Compute Shader skinning 用 shader は未使用リソースとして削除。現状は Vertex Shader skinning を使用 |
| MultiMesh & MultiMaterial対応 | 実装済み | FBX/ufbx の material part ごとに subset を作り、subset 単位で texture SRV を切り替えて描画 |
| Animation補間 | 実装済み | Idle / Walking / attack1 の切り替え時に translation / rotation / scale を補間 |
| 骨のデバッグ表示 | 実装済み | bone line、local axis、bone name を 3D/2D で表示 |
| 手からパーティクルを出す | 実装済み | LeftHand bone の現在位置から CPU particle を発生 |
| 武器を手に持たせる | 実装済み | RightHand bone の行列に sword mesh を追従 |
| GPU Particle | 実装済み | Compute Shader による初期化・発生・更新、複数 emitter、mesh emitter、field、particle type、editor 対応 |
| その他 | 実装済み | ポストエフェクト複数追加、GPU流体/メタボール系プレイヤー表現、GPU Validation 対応修正 |

## Skinningモデルの表示

FBX/ufbx から skinning 情報を読み込み、モデルの頂点に bone weight と bone index を保持するようにしています。
頂点構造 `VertexData` に最大 4 本分の bone weight / index を持たせ、`Model::LoadWithUFBX()` で ufbx の skin cluster から値を入れています。

描画時は `Renderer::DrawSkinnedMesh()` で bone palette を draw call に積み、`Renderer` が `CBBone` として GPU に渡します。
実際の skinning 計算は `Resources/shaders/SkinningVS.hlsl` と `Resources/shaders/ToonSkinningVS.hlsl` の Vertex Shader 側で行っています。

主な実装ファイル:
- `Engine/Model.h`
- `Engine/Model.cpp`
- `Engine/Renderer.cpp`
- `Resources/shaders/SkinningVS.hlsl`
- `Resources/shaders/ToonSkinningVS.hlsl`
- `Game/Scenes/AssignmentScene.cpp`

## ComputeShaderによるSkinning

未接続だった Compute Shader skinning 用 shader は未使用リソース整理で削除しました。
現在の skinning は `Resources/shaders/SkinningVS.hlsl` と `Resources/shaders/ToonSkinningVS.hlsl` の Vertex Shader 側で行っています。

## MultiMesh & MultiMaterial対応

`ModelData` に `materials` と `subsets` を追加し、ひとつのモデル内に複数 material / 複数描画範囲を持てるようにしています。
ufbx 読み込みでは `mesh->material_parts` を走査して `MeshSubset` を作成し、material ごとに `indexStart` / `indexCount` / `materialIndex` を保存します。

描画時は `Model::Draw()` / `Model::DrawInstanced()` で subset ごとに `DrawIndexedInstanced()` を呼び、`materialIndex` に対応する SRV がある場合は描画直前に texture を切り替えます。
`AssignmentScene` では Mutant model に対して `SetMaterialSrv()` で複数 material の SRV を差し替え、MultiMaterial の確認用にしています。

主な実装ファイル:
- `Engine/Model.h`
- `Engine/Model.cpp`
- `Engine/Renderer.cpp`
- `Game/Scenes/AssignmentScene.cpp`

## Animation補間

`AssignmentScene` では `Mutant Idle.fbx` を基本モデルとして読み込み、`Mutant Walking.fbx` と `attack1.fbx` を追加アニメーションとして読み込んでいます。
移動開始時は Idle から Walking、停止時は Walking から Idle、攻撃時は attack1 へ遷移します。

遷移時には現在のアニメーション名・時間を `prevAnim_` / `prevAnimTime_` に保存し、`blendFactor_` を使って約 0.25 秒で補間します。
`Model::UpdateSkeleton()` では node ごとに translation と scale を `XMVectorLerp()`、rotation を `XMQuaternionSlerp()` で補間し、滑らかに姿勢を切り替えます。

主な実装ファイル:
- `Engine/Model.cpp`
- `Game/Scenes/AssignmentScene.cpp`

## 骨のデバッグ表示

`Model::UpdateSkeleton()` に `debugBones` 出力を追加し、各 node の global matrix と親位置を取得できるようにしています。
`AssignmentScene::Draw()` ではその情報を使って、bone の親子線、local X/Y/Z axis、bone name を描画します。

骨の状態を見やすくするため、bone line は xray 指定で描画しています。
RightHand / LeftHand などの bone 名も画面に出るため、武器追従や手からのパーティクル発生位置を確認しやすくしています。

主な実装ファイル:
- `Engine/Model.h`
- `Engine/Model.cpp`
- `Game/Scenes/AssignmentScene.cpp`

## 武器を手に持たせる

`AssignmentScene::Draw()` で `RightHand` または `mixamorig:RightHand` の bone を探し、その bone の global matrix に sword mesh を追従させています。
剣のワールド座標 `swordPos_` と向き `swordDir_` も同時に更新し、GPU Particle の sword mesh emitter に利用しています。

剣は `Resources/Models/weapons/sword.obj` と `Resources/Models/weapons/bukiUV.png` を使用しています。

主な実装ファイル:
- `Game/Scenes/AssignmentScene.cpp`
- `Game/Scenes/AssignmentScene.h`

## 手からパーティクルを出す

`AssignmentScene::Draw()` で `LeftHand` または `mixamorig:LeftHand` の bone を探し、その現在位置から CPU particle を発生させています。
手の位置に追従する火のような表現として、上向き速度、軽い重力、色フェード、ランダムな広がりを設定しています。

主な実装ファイル:
- `Game/Scenes/AssignmentScene.cpp`
- `Engine/Particle.h`
- `Engine/Particle.cpp`

## GPU Particle

`Engine::GPUParticleSystem` を追加し、particle pool を GPU buffer として管理しています。
Compute Shader は `InitCS`、`EmitCS`、`UpdateCS` の 3 entry に分け、初期化、発生、更新を GPU 上で行います。
描画は `GPUParticleVS.hlsl` / `GPUParticlePS.hlsl` で、particle buffer を `StructuredBuffer` として読み、quad を instancing で描画します。

対応している機能:
- 複数 emitter
- emitter shape: Point / Sphere / Box / Mesh
- particle type: Default / Trail / Lit
- field type: None / Gravity / Wind / Tornado
- additive blend による発光系 particle
- billboard 表示
- velocity 方向へ伸びる Trail 表現
- light の影響を受ける Lit particle
- ImGui editor による emitter 調整

`AssignmentScene` では 2 つの emitter を使用しています。
Emitter 0 は Tornado field を持つ球状 emitter で、攻撃時の風圧で移動します。
Emitter 1 は sword mesh emitter で、剣の位置・向きに追従し、剣メッシュの頂点から particle を発生させます。

Mesh emitter については、`GPUParticleSystem::SetEmitterMesh()` で mesh の vertex buffer GPU address、vertex count、stride を渡し、`GPUParticleCS.hlsl` 側で `ByteAddressBuffer` として頂点位置を読みます。
これにより、単なる点や球ではなく、メッシュ形状に沿った発生ができます。

主な実装ファイル:
- `Engine/GPUParticle.h`
- `Engine/GPUParticle.cpp`
- `Resources/shaders/GPUParticleCS.hlsl`
- `Resources/shaders/GPUParticleVS.hlsl`
- `Resources/shaders/GPUParticlePS.hlsl`
- `Game/Scenes/AssignmentScene.cpp`

## ポストエフェクト

Renderer の post process pipeline を拡張し、複数の screen space effect を切り替えられるようにしています。
AssignmentScene では `0` - `9` キーで手動確認できますが、それだけでなくゲーム中のイベントにも組み込んでいます。
scene 開始直後に無理やり全エフェクトを再生するのではなく、player と cube enemy の状態に応じて意味のあるタイミングで発火するようにしています。

追加・確認できる主な effect:
- Default
- Grayscale
- GaussianFilter
- OutlinePost
- DepthBasedOutline
- RadialBlur
- Vignetting
- BoxFilter
- Random
- Dissolve

ゲームプレイへの割り当て:
- `Grayscale`: cube enemy が player を発見した瞬間。戦闘開始の緊張感を出す。
- `OutlinePost`: player が cube enemy のロックオン距離にいる時。敵を見失いにくくする。
- `DepthBasedOutline`: cube enemy の攻撃予兆中。攻撃範囲と奥行きを強調する。
- `RadialBlur`: player が攻撃を開始した瞬間、および攻撃中。踏み込みの速度感を出す。
- `GaussianFilter`: player の攻撃が cube enemy に当たった瞬間。ヒットストップのような一瞬の重さを出す。
- `BoxFilter`: `Shift` / gamepad `LB` の Guard Focus 中、またはガード成功時。防御姿勢の集中感を出す。
- `Random`: ガードせずに cube enemy の攻撃を受けた瞬間。ダメージ時のノイズ/グリッチ表現。
- `Vignetting`: player の HP が低い時、または HP が 0 になった瞬間。危険状態を伝える。
- `Dissolve`: cube enemy 撃破中。敵が消えていく演出に使う。

AssignmentScene に追加した簡易ゲーム要素:
- 敵の代わりとして stationary cube enemy を配置
- cube enemy は移動しない
- player が近づくと攻撃予兆を出し、一定時間後に範囲内なら player にダメージ
- player は `Space` / gamepad `A` で攻撃
- player は `Shift` / gamepad `LB` で Guard Focus ができ、被ダメージを軽減する
- player の向きと距離が合っていれば cube enemy にダメージ
- player HP / enemy HP / 現在の post effect / post effect の発火理由を UI に表示
- cube enemy の攻撃範囲を地面の線で表示

主な実装ファイル:
- `Engine/Renderer.cpp`
- `Engine/Renderer.h`
- `Game/Scenes/AssignmentScene.cpp`
- `Game/Scenes/AssignmentScene.h`
- `Resources/shaders/*Post*.hlsl`
- `Resources/shaders/DepthBasedOutline.hlsl`
- `Resources/shaders/BoxFilter.hlsl`
- `Resources/shaders/Vignetting.hlsl`
- `Resources/shaders/Dissolve.hlsl`

## GPU流体 / メタボール表現

PlayGame 側の player 表現として、GPU particle を使った流体シミュレーションとメタボール風描画を追加しています。
`Renderer` 内に GPU fluid 用 buffer、grid count / offset、sorted particle、original index buffer を持ち、`FluidSimCS.hlsl` の複数 pass で更新します。

主な compute pass:
- `InitParticles`
- `Emit`
- `ClearOriginalIndices`
- `ClearGridCount`
- `CountParticles`
- `PrefixSum`
- `SortParticles`
- `CalcDensity`
- `CalcForce`
- `WriteBack`

描画では `GPUFluidVS.hlsl` / `GPUFluidPS.hlsl` で particle を描画し、`MetaballPS.hlsl` で screen space に合成します。
player core への引力、decoy core、AABB collision、debug arrow 表示も用意しています。

主な実装ファイル:
- `Engine/Renderer.h`
- `Engine/Renderer.cpp`
- `Resources/shaders/FluidSimCS.hlsl`
- `Resources/shaders/GPUFluidVS.hlsl`
- `Resources/shaders/GPUFluidPS.hlsl`
- `Resources/shaders/GPUFluidDebugVS.hlsl`
- `Resources/shaders/GPUFluidDebugPS.hlsl`
- `Resources/shaders/MetaballPS.hlsl`
- `Game/Scenes/GameScene.cpp`
- `Game/Systems/PlayerActionSystem.h`

## GPU Validation / フリーズ対策で修正したこと

PlayGame で GPU fluid player を出した時に device removed / freeze が起きていたため、GPU fluid と collision 周りの安全性を修正しています。
主に、無効 particle の処理、active particle count、buffer state transition、UAV barrier、未初期化 particle の除外を見直しています。

関連ファイル:
- `Engine/Renderer.cpp`
- `Engine/Renderer.h`
- `Resources/shaders/FluidSimCS.hlsl`
- `Resources/shaders/CollisionCompute.hlsl`
- `Resources/shaders/GPUFluidVS.hlsl`
- `Resources/shaders/GPUFluidDebugVS.hlsl`
- `d3d12_validation_log.txt`

## 補足

- `ComputeShaderによるSkinning` 用の未接続 shader は削除済みです。提出時に主張する skinning は Vertex Shader skinning です。
- `GPU Particle` は AssignmentScene で確認できます。
- `GPU流体 / メタボール表現` は PlayGame 側の player 表現として確認できます。
- `AssignmentScene` は課題要素の確認用、`GameScene` は実ゲーム用のシーンとして分けています。
