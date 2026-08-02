# 評価課題 追加実装まとめ

このプロジェクトで追加・拡張した評価課題要素をまとめます。
主な確認シーンは `AssignmentScene` です。タイトルの Select Scene から `Assignment Scene` を選ぶと、スキニングモデル、アニメーション補間、骨デバッグ、武器追従、手から出るパーティクル、GPU Particle をまとめて確認できます。

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
| ComputeShaderによるSkinning | 未接続 | `SkinningCS.hlsl` と宣言はあるが、PSO作成・Dispatch・描画接続は未実装 |
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

現状は未完成です。
`Resources/shaders/SkinningCS.hlsl`、`Renderer::ComputeSkinning()` の宣言、`rootSigSkinningCS_`、`psoSkinningCS_` のメンバはありますが、`Renderer.cpp` に以下が接続されていません。

- SkinningCS 用 RootSignature 作成
- SkinningCS 用 PSO 作成
- 元頂点 buffer / bone palette / skinned vertex buffer の bind
- `Dispatch()` による skinning 実行
- `Model::GetSkinnedVBV()` を使った描画切り替え

そのため、現在の課題提出で主張できるのは Vertex Shader skinning です。

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

- `ComputeShaderによるSkinning` は未接続なので、提出時は未完成項目として扱います。
- `GPU Particle` は AssignmentScene で確認できます。
- `GPU流体 / メタボール表現` は PlayGame 側の player 表現として確認できます。
- `AssignmentScene` は課題要素の確認用、`GameScene` は実ゲーム用のシーンとして分けています。
