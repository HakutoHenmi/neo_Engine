# ガラス水槽の流体プレビュー

デバッグ／Developmentエディターで **Tools → Fluid Sandbox** を選ぶと、右側のツールタブが開きます。

1. **Open standalone sandbox scene** でゲーム用プレイヤーのいない水槽シーンを開きます。現在の編集内容はUndoで復元でき、ファイルには自動保存しません。
2. **Select emitter** で Water Emitter を選び、**Emit water** をオンにします。エディターのPLAYを押さなくても放出・シミュレーションできます。
3. **Particles / second** で流量、**Total particle limit** で累積放出量、**Velocity (units/s)** で初速、**Nozzle position** で位置を調整します。上限に達すると放出を止めます。
4. **Stop all emitters** で放出を止めると、水は箱の中に残って沈静化します。**Reset fluid and emission counts** で空に戻せます。放出がオンならリセット後すぐに再開します。

既存のシーンには **Create tank + emitter** で追加できます。作成前に内寸（幅／高さ／奥行き）、壁厚、位置を指定します。新しいエミッタは停止状態です。水槽または子エミッタを選ぶと内寸、壁厚、ガラスの色・透明度を変更できます。**Focus tank** は水槽を見渡す位置へカメラを移します。作成操作もUndoできます。

箱は底と4枚の壁をCubeから作り、上を開けています。Cubeの頂点が−1〜1にあるため、描画スケールは各パネルの寸法の半分、BoxCollider.sizeは2です。親の移動・スケールを反映したワールドAABBをGPUへ渡します。衝突はAABB近似のため、箱を傾けたり回転させたりせず使用してください。水がある状態で寸法を変更したらリセットしてください。

Glassシェーダーは低い不透明度、Fresnel反射、鋭いハイライトで透明なガラスを表現します。非等方スケールに対応した法線を使用し、水の描画後にパネルを遠い順で合成します。深度を書かず、不透明な影も落としません。背景を曲げるガラスの屈折は行いません。

**Keep settled water** は新しく放出する水の5秒寿命を無効にします。水の支持面情報は引き続き更新します。既存シーンの水源は従来の寿命と連続放出を維持し、古い保存データも読み込めます。設定はシーン・Prefab保存に含め、放出済み個数と端数はランタイム状態として保存しません。PLAY開始時にもカウントをリセットします。

水はゲームのエフェクト用25,000粒子プールを共有します。初期値は1,200粒子／秒、合計16,000粒子です。複数の水源や他の流体エフェクトでプールを使い切ると循環上書きされるため、保持を試す際は独立したsandboxシーンを使ってください。GPUの障害物上限は128個です。大量の水はGPU負荷が増えます。

## Rigidbodyの落下テスト

オブジェクトにRigidbodyを追加し、EnabledとUse Gravityをオン、Is KinematicをオフにしてPLAYすると落下します。Rigidbodyだけでも重力は働きます。床や水槽の壁で止めるには、落下するオブジェクトにもBoxColliderを追加してください。床・壁側はBoxColliderだけで固定障害物として扱い、Rigidbodyは不要です。Is Triggerのコライダーは落下を止めません。

通常ゲームのChronoモードでも汎用物理を登録し、停止中に別シーンを読み込んでからPLAYした場合にも動作します。有効なCharacterMovementは自身が上下移動を管理し、無効なCharacterMovementはRigidbodyの重力を妨げません。Pauseでは物理を止め、Stopでは編集時の位置を復元します。

`tests/Run-RigidbodySmoke.ps1` はChrono／旧シーンの両方で、Rigidbody単体の落下、固定床への接地、トリガー、無効化、描画行列、Play/Pause/Stopを検証します。2026-10-13のDevelopment検証ビルドで両方通過しています。

## 検証

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/Run-FluidTests.ps1 -Case --sandbox-only
powershell -NoProfile -ExecutionPolicy Bypass -File tests/Run-FluidSmoke.ps1 -Sandbox -Executable bld/FluidSandbox/DirectXGameApp.exe
```

数値テストは製品のHLSLをD3D11 WARPで実行し、30/60/120 FPSでの総量上限、放出時の保持フラグ、疎な水粒子を使った各壁面・底面の6.11秒の封じ込め、通常の水の寿命を検証します。統合テストはD3D12でプレイヤーのいないシーン、Glassパイプライン、保存・復元、親からの位置継承、放出停止、リセットを確認し、`tests/out/fluid-sandbox-*.png` に描画結果を保存します。

2026-10-13: Development構成の検証用ビルド、`--sandbox-only`、`--reversal-only`、`--liquefy-only`、sandbox統合テストが通過しました。RTX 4060 Laptop GPUの実描画でガラス越しの水とリセット後の空の箱を確認しました。Development実行ではD3D12のERROR/CORRUPTION報告はありませんでした（Debug構成のGPU-based validationは無効です）。検証用ビルドは環境のPDB生成エラーを避けるため、`bld/FluidSandbox/Validation.props` でデバッグシンボルの生成のみを変更しています。

親IDが0の正しい親子関係も復元します。従来の読み込み処理は0を一律に無効扱いしており、先頭の水槽を保存・再読み込みすると壁と水源が外れていました。存在しないIDと自分自身を親にする関係は無効扱いにします。
