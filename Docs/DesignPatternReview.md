# デザインパターンの適用 (State / Singleton 以外)

## 方針

確認課題「01_04 デザインパターン」に沿って、State Pattern と Singleton Pattern 以外のパターンを、**そのパターンで解決できる問題が実際にある箇所**に適用した。名前を付けるためだけの置き換えはせず、変更前のコードの問題点と、パターンを選んだ理由を各項目に書いている。

| パターン | 層 | 対象 | 主なファイル |
| --- | --- | --- | --- |
| Object Pool | エンジン | CPU パーティクル | `Engine/ObjectPool.h`, `Engine/Particle.*` |
| Strategy | エンジン | パーティクルの放出形状 | `Engine/EmissionShapes.h`, `Engine/ParticleEmitter.*` |
| Command | エンジン + アプリ | エディタの Undo / Redo | `Engine/CommandHistory.h`, `Game/Editor/EditorCommandTypes.h`, `Game/Editor/Editor*.cpp` |
| Strategy | アプリ | 吸収モードの部屋のギミック 6 種 | `Game/Systems/DevourGimmickBehaviors.h`, `Game/Systems/DevourGimmick.cpp`, `Game/Systems/RoomStage.cpp` |

ゲームの数値・演出・保存形式 (パーティクルの JSON の `shape` の値、シーンファイル) は変更していない。

## 1. Object Pool: CPU パーティクル

### 変更前の問題

`ParticleSystem` は最大数ぶんの `std::vector<Particle>` を確保し、各要素に `active` フラグを持たせていた。

- `Emit` のたびに配列の先頭から未使用の要素を線形探索していた。粒が多いほど 1 粒の放出が遅くなる (最大 1000 粒なら最悪 1000 回の比較)。
- `Update` と `Draw` は、生きている粒が数個でも全 1000 要素を走査していた。

### 採用した理由

パーティクルは「短い寿命で大量に生成・破棄される」「最大数が決まっている」という、Object Pool がもっとも効果を出す条件にあてはまる。毎回 new / delete しないのは元のコードと同じだが、**空きの管理**と**使用中だけの走査**をプールに任せることで、放出・返却を O(1) にし、更新・描画の走査を生きている粒の数に比例させた。

### 実装

`Engine::ObjectPool<T>` は汎用のテンプレートで、パーティクル以外 (弾・ヒットエフェクト) にもそのまま使える。

- 最初に容量ぶんを確保し、実行中は領域を確保し直さない。
- 空きは free list (`free_`)、使用中は密な番号の配列 (`active_`) で持つ。
- `Release` は使用中の配列の末尾と入れ替えて O(1)。二重返却は無視する。
- `ForEachActive(fn)` は `fn` が `false` を返した要素をその場で返却する。末尾から走査するため、返却による入れ替えで未走査の要素を飛ばさない。パーティクルの寿命切れはこの仕組みで返す。
- 空きが無いときの `Acquire` は `nullptr` を返す。元のコードと同じく、上限を超えた放出は捨てる。

## 2. Strategy: パーティクルの放出形状

### 変更前の問題

`ParticleEmitter::EmitBurst` に、`EmissionShape` の値ごとの `if / else if` で球と円錐の計算が直接書かれていた。形状を増やすたびに放出処理本体を書き換える必要があり、形状の計算だけを単体で確認することもできなかった。

### 採用した理由

「放出位置と初速の決め方」というアルゴリズムだけが形状ごとに入れ替わり、それを使う側 (寿命・大きさ・色などの決定) は共通である。アルゴリズムを交換可能な部品にする Strategy がそのまま当てはまる。

### 実装

- `IEmissionShape::Shape(params, random, position, velocity)` を `PointEmission` / `SphereEmission` / `ConeEmission` が実装する。
- 形状オブジェクトはデータを持たない不変の共有インスタンス。半径・角度は呼び出しごとに `EmissionShapeParams` で渡す。
- 乱数は関数ポインタで渡す。エミッターと同じ `rand()` を使い、乱数を引く順番も変更前と同じにしているため、同じシードなら同じ結果になる。テストでは決まった値を返す乱数を渡して形状の性質を確認している。
- `EmissionShapeFor(shape)` が保存値 (enum) から形状を選ぶ。壊れた JSON などの未知の値は点として扱う。形状を足すときは、クラスを 1 つ追加してここに登録するだけで `EmitBurst` は変えない。

## 3. Command: エディタの Undo / Redo

### 変更前の問題

`UndoCommand { description, std::function undo, std::function redo }` を `EditorInternal.h` のグローバルな `undoStack` / `redoStack` に積んでいた。

- 位置・回転・大きさ・名前・親子・生成・削除のそれぞれで、同じ形のラムダの組を各ウィンドウ (Inspector / Viewport / Hierarchy / メニュー) に書いていた。エンティティが消えていないかの確認や、親子変更後のワールド行列の再計算を、書く場所ごとに忘れずに入れる必要があった。
- 実際に、**ギズモで物を動かしてから取り消すと、回転が 0、大きさが 1 に戻ってしまう**不具合があった。ドラッグ開始時の回転・大きさとして実際の値ではなく `{0,0,0}` / `{1,1,1}` を記録していたためである。キーフレームを動かしたときも、取り消しがキーフレームではなく物の位置を書き換えていた。
- 生成・削除の取り消しは、`shared_ptr<entt::entity>` を2つのラムダで共有して、作り直したエンティティの ID を追跡していた。

### 採用した理由

「操作を、実行と取り消しを持つオブジェクトにして履歴に積む」のは Command Pattern の代表的な用途であり、授業資料でも開発用ツールのアンドゥ / リドゥが例に挙がっている。操作の種類ごとにクラスを分けることで、無効なエンティティの確認や再計算を 1 か所にまとめ、上記の不具合のような記録漏れを起こしにくくした。

### 実装

- エンジン層: `Engine::ICommand` (`Execute` / `Undo` / `Name`) と `Engine::CommandHistory`。履歴は操作の中身を知らず、最大件数 (100) を超えたら古いものから捨て、新しい操作を記録すると Redo の履歴を消す。ドラッグ編集のように操作中にすでに値が変わっている場合は `Record`、まだ実行していない操作は `Execute` を使う。
- アプリ層 (`Game/Editor/EditorCommandTypes.h`):
  - `TransformCommand`: 位置・回転・大きさ (どの値を対象にするかを指定)。Inspector、ギズモ、「Move To View」で使う。
  - `KeyframeMoveCommand`: モーションのキーフレームの位置。
  - `RenameCommand`、`ReparentCommand` (付け替え後にワールド行列を計算し直す)。
  - `EntityLifetimeCommand`: 生成・貼り付け・削除。作り直したエンティティの ID をコマンド自身が持つ。
- 履歴は `EditorUI` の private な静的メンバーにし、グローバル変数 `undoStack` / `redoStack` を削除した。
- 既存の `PushUndo(UndoCommand)` は、外部データを丸ごと差し替えるような一度きりの編集 (Fluid Sandbox、吸収アニメーションエディタ) 用に残し、内部で `Engine::FunctionCommand` に包んで同じ履歴に積む。
- Edit メニューに次に取り消す / やり直す操作の名前を表示し、履歴が無いときは押せないようにした。

## 4. Strategy: 吸収モードの部屋のギミック

### 変更前の問題

部屋のギミック 6 種 (だるま落とし・ドミノ・ボウリング・絵の具・液体・紙飛行機) の処理が、8 つの関数に `GimmickKind` の `switch` / `if` 列として分散していた。

| 処理 | 変更前の場所 |
| --- | --- |
| 乗り始めの形と位置 | `TickGimmickInput` の switch |
| 乗っている間の動き | `GimmickStep` の switch |
| 乗っている間の入力 (撃つ・やめる) | `TickGimmickInput` の if |
| 仕掛けの毎フレームの動き | `TickDevourGimmicks` の if 列 |
| 操作の札 | `GimmickRidePrompts` の if 列 |
| カメラの構図 / 照準 | `GimmickCameraSubject` / `DrawGimmickMarks` の if |
| 看板のモデル | `AddGimmick` の、enum の順に依存する配列 |
| 物を置かない範囲 | `RoomStage.cpp` の `GimmickClear` の条件式 |

ギミックを 1 種類追加・調整するたびに、2 ファイル 8 か所を探して整合させる必要があった。だるま落としの処理は 5 か所に分かれていた。

### 採用した理由

このゲームの中心は「体が道具の形になって決まった動きをする」ギミックで、今後も種類を増やす前提である。種類ごとに処理の中身が異なり、呼び出す側の流れ (近くの輪を探す → 乗る → 毎フレーム動かす → 終わったら戻る) は共通なので、種類ごとの振る舞いを交換可能なオブジェクトにする Strategy が適している。

State Pattern との違い: ギミックの種類は乗っている間に変わらず、オブジェクト自身が次の種類を決めることもない。状態遷移ではなく、**振る舞いの選択**の問題であるため State ではなく Strategy とした。

### 実装

- `ChronoSystem::GimmickBehavior` が上の 8 つの処理を仮想関数として持つ。既定の実装は「何もしない」なので、各ギミックは必要な処理だけを上書きする。
- `ShootGimmick` / `DominoGimmick` / `BowlingGimmick` / `PaintGimmick` / `PathGimmick` (液体と紙飛行機。動きの大部分が同じなので、紙飛行機かどうかを持つ 1 つのクラスで扱う)。
- 動きのオブジェクトはデータを持たない不変の共有インスタンスで、状態 (時間・狙い・倒れたピン) は今まで通り `DevourGimmick` / `GimmickRide` に置く。State Pattern の実装 (`Engine/IState.h`) と同じ考え方である。
- `ChronoSystem` の入れ子クラスにしているのは、システムの内部データ (小物・効果音・描き文字) を公開せずに使うため。宣言だけを `ChronoSystem.h` に、定義を `DevourGimmickBehaviors.h` と `DevourGimmick.cpp` に置く。
- 乗っている間の 1 フレームの共通の情報は `GimmickRideStep` にまとめ、形を作りながら寄せる処理 (`Settle`) も共通化した。
- `ChronoSystem` 側の 8 つの関数は、共通の処理と `GimmickBehaviorFor(kind)` の呼び出しだけになった。`GimmickKind` で分岐するのは `GimmickBehaviorFor` の 1 か所だけである。

## 採用しなかったもの・既存の箇所

- Factory Method: シーン (`SceneManager::Register`) とスクリプト (`ScriptEngine` の `REGISTER_SCRIPT`) は、すでに名前から生成関数を引く形になっているため変更していない。
- Observer: スクリプト間の通信は既存の `EventSystem` (名前付きのイベントバス) が担っている。衝突応答は ECS のシステムが直接処理しており、現時点で購読者が 1 つしかない箇所に Observer を入れても利点が少ないため見送った。
- Dirty Flag: 階層のワールド行列は、すでに `GameScene::InvalidateWorldMatrices` による再計算の要求で管理されている。

## 検証

- Debug ソリューション全体: 警告 0 件・エラー 0 件でビルド成功。
- `tests/Run-DesignPatternTests.ps1` (新規): Object Pool の容量・再利用・二重返却・走査中の返却、CommandHistory の実行・取り消し・やり直し・記録・最大件数、放出形状の範囲・速さの保持・乱数の回数・未知の値の扱いを確認する。
- `tests/Run-EncapsulationTests.ps1`、`tests/Run-ChronoTests.ps1`: 既存の回帰テストが通過。
- `tests/Run-SlimeLifeSmoke.ps1 -RoomOnly`: 部屋のステージ (ギミックの配置・看板・物を置かない範囲・毎フレームの仕掛けの更新) を含むスモークテスト。

ギミックに乗って遊ぶ操作と、エディタの Undo / Redo の画面上での手動確認は、別途プレイして行うこと。
