// PlayerActionStates.inl
// 旧アクションゲームのプレイヤー (缶とスライム) の行動状態。
// 待機・トゲ攻撃・ハンマー・回避・液状化・よろけ・溜め・発射・トゲ爆発・火炎・分身ワープ。
// PlayerActionSystem の中に読み込まれる。各状態は振る舞いだけを共有し、個体のデータは持たない
// 状態の更新に渡す情報 (このフレームの入力と、カメラを引く量)
struct StateContext {
	PlayerActionSystem& system;
	entt::registry& registry;
	entt::entity entity;
	GameContext& ctx;
	bool attackInput, attackPressed, hammerPressed, dashPressed, liquefyInput;
	float& targetCamOffset;
};
// プレイヤーの状態の基底: 回避で中断できるかと、攻撃判定の更新 (既定は判定なし)
class State : public Engine::IState<StateContext, EntityStateContext> {
public:
	// この状態は回避で中断できるか (既定はできない)
	virtual bool CanCancelToDodge(const PlayerActionComponent&) const { return false; }
	virtual void UpdateHitbox(EntityStateContext& frame) const {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			hitbox->isActive = false;
			hitbox->center.z = 0;
		}
	}
};

// 待機: 空中・移動中・停止中で体の形を変え、入力に応じて液状化・回避・攻撃・ハンマーへ
class IdleState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値と、このフレームの入力
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& pi = frame.registry.get<PlayerInputComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const bool attackPressed = frame.attackPressed;
		const bool hammerPressed = frame.hammerPressed;
		const bool dashPressed = frame.dashPressed;
		const bool liquefyInput = frame.liquefyInput;
		{
			// 移動しているか・接地しているか
			bool isMoving = (std::abs(pi.moveDir.x) > 0.01f || std::abs(pi.moveDir.y) > 0.01f);
			bool isGrounded = true;
			if (registry.all_of<CharacterMovementComponent>(entity)) {
				isGrounded = registry.get<CharacterMovementComponent>(entity).isGrounded;
			}

			if (!isGrounded) {
				// 空中：縦に伸びる
				tc.scale.x = 1.18f;
				tc.scale.y = 0.96f;
				tc.scale.z = 1.18f;
			} else if (isMoving) {
				// 移動中：地面を這うように平べったく進む（跳ねない）
				// 高さは一定にしつつ、XとZを少し伸縮させて這っている感を出します
				float slither = std::sin(pa.stateTimer * 12.0f);
				tc.scale.x = 1.2f - slither * 0.015f;
				tc.scale.y = 0.90f;
				tc.scale.z = 1.2f + slither * 0.015f;
			} else {
				// 待機時：ゆっくり呼吸しておまんじゅう型
				float breathe = std::sin(pa.stateTimer * 3.0f);
				tc.scale.x = 1.2f + breathe * 0.015f;
				tc.scale.y = 0.92f - breathe * 0.015f;
				tc.scale.z = 1.2f + breathe * 0.015f;
			}

			// 液状化の入力 → 液状化、回避 → 回避、攻撃 → トゲか火炎、ハンマー → ハンマー
			if (liquefyInput) {
				frame.system.BeginLiquefy(registry, entity, pa, pi, tc, ctx);
				ChangeState(registry, entity, PlayerActionState::Liquefy, 0.0f); // 長押し状態
			} else if (dashPressed && pa.dodgeCooldown <= 0.0f) {
				frame.system.StartDodge(registry, entity, pa, pi, tc, ctx);
			} else if (attackPressed) {
				pa.chargeTimer = 0.0f;
				ChangeState(registry, entity, pa.currentCan == CanType::Fire ? PlayerActionState::FireBreath : PlayerActionState::SlimeSpike, pa.currentCan == CanType::Fire ? 0.6f : 0.26f);
			} else if (hammerPressed) {
				ChangeState(registry, entity, PlayerActionState::SlimeHammer, 0.58f);
			}
			return;
		}
	}
};

// トゲ攻撃: 前方へ細く鋭く伸びて戻る。押し続けていれば溜めへ
class SlimeSpikeState final : public State {
public:
	// 0.08 秒以降なら回避で中断できる
	bool CanCancelToDodge(const PlayerActionComponent& action) const override { return action.stateTimer >= .08f; }
	// 伸びている間だけ攻撃判定を出す (伸びた長さに合わせる)
	void UpdateHitbox(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			const auto& action = frame.registry.get<PlayerActionComponent>(frame.entity);
			const auto& transform = frame.registry.get<TransformComponent>(frame.entity);
			// 判定が有効になった瞬間に、当たった相手の記録を消す
			const bool wasActive = hitbox->isActive;
			hitbox->isActive = action.stateTimer >= .025f && action.stateTimer <= .19f;
			if (!wasActive && hitbox->isActive)
				hitbox->hitTargets.clear();
			// ダメージと、伸びた長さに合わせた大きさと位置
			hitbox->damage = 30;
			hitbox->size = {1.5f, 1.5f, transform.scale.z * 1.5f};
			hitbox->center.z = transform.scale.z * .75f;
		}
	}
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const bool attackInput = frame.attackInput;
		{
			// 素早く前方に伸びるトゲ攻撃
			float progress = pa.stateTimer / pa.stateDuration;
			float spike = 0.0f;
			if (progress < 0.2f) {
				spike = progress / 0.2f; // 急に伸びる
			} else {
				spike = 1.0f - ((progress - 0.2f) / 0.8f); // 戻る
			}
			// 極限まで細く鋭く
			tc.scale.x = std::max(0.05f, 1.2f - (spike * 1.15f)); // 非常に細く
			tc.scale.y = std::max(0.05f, 0.8f - (spike * 0.75f)); // 非常に平たく
			tc.scale.z = 1.2f + (spike * 12.0f);                  // 針のように長く伸びる

			// 見た目に合わせて当たり判定を「体の根元から先端まで」にする
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				// 物理コライダー: 伸びた長さの半分だけ前方にズラす
				bc->center.z = tc.scale.z * 0.75f;
			}

			// 終わったとき、押し続けていれば溜めへ、離していれば待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				if (attackInput) {
					pa.chargeTimer = pa.stateDuration;
					ChangeState(registry, entity, PlayerActionState::Charging, 0.0f);
				} else {
					ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
				}
			}
		}
	}
};

// ハンマー: 膨らんでから前へ叩きつける
class SlimeHammerState final : public State {
public:
	// 0.22 秒以降なら回避で中断できる
	bool CanCancelToDodge(const PlayerActionComponent& action) const override { return action.stateTimer >= .22f; }
	void UpdateHitbox(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			const auto& action = frame.registry.get<PlayerActionComponent>(frame.entity);
			const auto& transform = frame.registry.get<TransformComponent>(frame.entity);
			const bool wasActive = hitbox->isActive;
			// 0.14〜0.42 秒の間だけ攻撃判定を出す
			hitbox->isActive = action.stateTimer >= .14f && action.stateTimer <= .42f;
			if (!wasActive && hitbox->isActive)
				hitbox->hitTargets.clear();
			hitbox->damage = 40;
			hitbox->size = {5.0f, 4.0f, transform.scale.z * 1.5f};
			hitbox->center.z = transform.scale.z * .75f;
		}
	}
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			// 膨らんでから前方に叩きつけるハンマー攻撃
			float progress = pa.stateTimer / pa.stateDuration;
			float smash = 0.0f;
			if (progress < 0.5f) {
				smash = progress / 0.5f; // 膨らんで振り下ろす
			} else {
				smash = 1.0f - ((progress - 0.5f) / 0.5f);
			}
			// もっとダイナミックに
			float scaleBase = 1.0f + (1.8f * smash); // 大きく膨らむ
			tc.scale.x = 1.2f * scaleBase;
			tc.scale.y = std::max(0.1f, 0.8f * scaleBase * (1.0f - smash * 0.95f)); // 極限まで潰れる
			tc.scale.z = 1.2f * scaleBase * (1.0f + smash * 2.5f);                  // さらに前方に伸びる

			// 見た目に合わせて当たり判定を「体の根元から先端まで」にする
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				bc->center.z = tc.scale.z * 0.75f;
			}

			// 時間が来たら待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
		}
	}
};

// 回避: 向きへ高速移動 (壁や段差の手前で止まる)。途中は無敵
class DodgeState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& pi = frame.registry.get<PlayerInputComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			// 回避中は移動とジャンプの入力を消す
			pi.moveDir = {0.0f, 0.0f};
			pi.jumpRequested = false;

			// 回避移動
			float step = pa.dodgeSpeed * ctx.dt;
			// 前方の壁までの距離で止める
			if (ctx.scene) {
				float wallDistance = step + 0.7f;
				if (ctx.scene->RayCast(
				        {tc.translate.x, tc.translate.y + 0.4f, tc.translate.z}, {pa.dodgeDirection.x, 0.0f, pa.dodgeDirection.z}, step + 0.7f, static_cast<uint32_t>(entity), wallDistance)) {
					step = (std::max)(0.0f, wallDistance - 0.7f);
				}
				// 移動先の地面が無いか、段差が大きければ動かない
				const float currentGround = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 2.0f, static_cast<uint32_t>(entity));
				const float nextGround =
				    ctx.scene->GetHeightAt(tc.translate.x + pa.dodgeDirection.x * step, tc.translate.z + pa.dodgeDirection.z * step, tc.translate.y + 2.0f, static_cast<uint32_t>(entity));
				if (nextGround < -999.0f || (currentGround > -999.0f && std::abs(nextGround - currentGround) > 1.2f))
					step = 0.0f;
			}
			tc.translate.x += pa.dodgeDirection.x * step;
			tc.translate.z += pa.dodgeDirection.z * step;

			// シュッと伸び縮みしながら移動
			float progress = pa.stateTimer / pa.stateDuration;
			float stretch = std::sin(progress * 3.14159f);
			tc.scale.x = 1.2f - (stretch * 0.5f);
			tc.scale.y = 0.8f - (stretch * 0.3f);
			tc.scale.z = 1.2f + (stretch * 1.5f);

			// 無敵時間
			if (registry.all_of<HealthComponent>(entity)) {
				auto& hc = registry.get<HealthComponent>(entity);
				if (pa.stateTimer >= 0.02f && pa.stateTimer <= 0.22f) {
					hc.invincibleTime = 0.1f;
				}
			}

			// 時間が来たら待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
		}
	}
};

// 液状化: 押している間は薄い水たまりのように広がり、無敵になる
class LiquefyState final : public State {
public:
	// 液状化を終えるときの後片付け
	void Exit(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		// 液状化の速さと固定を元に戻す
		EndLiquefy(registry, entity, registry.get<PlayerActionComponent>(entity));
	}
	void Update(StateContext& frame) const override {
		// よく使う値と、液状化の入力
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& pi = frame.registry.get<PlayerInputComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const bool liquefyInput = frame.liquefyInput;
		{
			// 離したら待機へ
			if (!liquefyInput) {

				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
				return;
			}

			// 固定されていなければ液状化を始める
			if (!pa.liquefyLocked) {
				frame.system.BeginLiquefy(registry, entity, pa, pi, tc, ctx);
			}

			// 入力を消し、固定の位置に留める
			pi.moveDir = {0.0f, 0.0f};
			pi.jumpRequested = false;
			tc.translate = pa.liquefyAnchorPos;
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				rb->velocity.y = 0.0f;
			}
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->isGrounded = true;
			}

			// 流れているか・広がりの進み具合・波打ち
			bool isMoving = pa.CurrentLiquefyFlowSpeed() > 0.1f;
			float t = std::min(1.0f, pa.stateTimer * 8.0f);
			float wave = std::sin(pa.totalTime * 12.0f) * 0.20f;
			if (isMoving) {
				// 流れの向きへなめらかに向く
				DirectX::XMFLOAT3 flowDir = frame.system.GetCameraMoveDirection(pi, ctx, tc);
				float targetRot = std::atan2(flowDir.x, flowDir.z);
				float diff = targetRot - tc.rotate.y;
				while (diff > DirectX::XM_PI)
					diff -= DirectX::XM_2PI;
				while (diff < -DirectX::XM_PI)
					diff += DirectX::XM_2PI;
				tc.rotate.y += diff * std::min(1.0f, 14.0f * ctx.dt);

				// 移動中は横へ大きく広がり、前後にゆるく波打つ薄い流れにする。
				tc.scale.x = 2.45f + (2.15f * t) - wave * 0.45f;
				tc.scale.y = 0.40f - (0.34f * t);
				tc.scale.z = 2.80f + (2.95f * t) + wave * 0.75f;
			} else {
				// 停止中はまとまりが崩れて、かなり広く浅い水たまりのようになる。
				tc.scale.x = 1.8f + (4.7f * t) + wave * 1.1f;
				tc.scale.y = 0.65f - (0.58f * t);
				tc.scale.z = 1.7f + (4.2f * t) - wave * 1.0f;
			}
			// 薄すぎないようにする
			tc.scale.y = std::max(0.040f, tc.scale.y);

			// 無敵状態を維持
			if (registry.all_of<HealthComponent>(entity)) {
				auto& hc = registry.get<HealthComponent>(entity);
				hc.invincibleTime = 0.2f; // 毎フレーム少し先まで無敵を更新
			}
		}
	}
};

// よろけ: 体の形を戻して一定時間待つ
class StaggerState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		// 溜めを消し、通常の形にする
		registry.get<PlayerActionComponent>(entity).chargeTimer = 0.0f;
		if (auto* transform = registry.try_get<TransformComponent>(entity))
			transform->scale = {1.2f, 0.92f, 1.2f};
	}
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		if (auto* transform = registry.try_get<TransformComponent>(entity))
			transform->scale = {1.2f, .92f, 1.2f};
		// 時間が来たら待機へ
		if (pa.stateTimer >= pa.stateDuration) {
			ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
		}
	}
};

// 溜め: 震えながら潰れ、カメラを引く。離したら溜め時間に応じて発射、ハンマー入力で爆発
class ChargingState final : public State {
public:
	// 溜め中はいつでも回避で中断できる
	bool CanCancelToDodge(const PlayerActionComponent&) const override { return true; }
	void Update(StateContext& frame) const override {
		// よく使う値と、このフレームの入力
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		auto& targetCamOffset = frame.targetCamOffset;
		const bool attackInput = frame.attackInput;
		const bool hammerPressed = frame.hammerPressed;
		{
			// 溜めの時間を進める
			pa.chargeTimer += ctx.dt;
			// 溜め中の演出（小刻みに震えつつ、徐々に潰れる）
			float shake = std::sin(pa.chargeTimer * 50.0f) * 0.05f;
			tc.scale.x = 1.2f + shake;
			tc.scale.y = std::max(0.4f, 0.8f - pa.chargeTimer * 0.4f); // ペシャンコに
			tc.scale.z = 1.2f + shake;

			// 溜めている間、徐々にカメラを引く (最大 6.0)
			targetCamOffset = std::min(pa.chargeTimer * 5.0f, 6.0f);

			// 溜め中にハンマー入力 (右クリック) で爆発に派生する
			if (hammerPressed) {
				ChangeState(registry, entity, PlayerActionState::SpikeExplosion, 0.5f); // 0.5秒の爆発アクション
				pa.chargeTimer = 0.0f;
			}
			// 左クリックを離したかどうかの判定 (入力が途切れたら)
			else if (!attackInput) {
				if (pa.chargeTimer >= 0.4f) { // 一定時間以上で遠距離発射
					ChangeState(registry, entity, PlayerActionState::Shoot, 0.3f);
					pa.chargeTimer = 0.0f;
				} else { // 短い押しの攻撃は押した時点で済んでいる
					ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
					pa.chargeTimer = 0.0f;
				}
			}
			return;
		}
	}
};

// 発射: 反動で伸び、最初のフレームで水の弾を生成する (水が足りなければミストだけ出す)
class ShootState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		// 発射済みの記録を消す
		registry.get<PlayerActionComponent>(entity).shotEmitted = false;
	}
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			// 発射の反動演出 (前方に伸びる)
			float progress = pa.stateTimer / pa.stateDuration;
			float recoil = std::sin(progress * 3.14159f);
			tc.scale.z = 1.2f + (recoil * 0.8f);
			tc.scale.y = 0.8f + (recoil * 0.2f);
			tc.scale.x = 1.2f - (recoil * 0.2f);

			// 状態に入った最初のフレームで弾を生成予約
			if (!pa.shotEmitted) {
				pa.shotEmitted = true;
				// 向いている方向
				float facing = tc.rotate.y;
				float dx = std::sin(facing);
				float dz = std::cos(facing);

				// 水 (体力) を 5 使う。足りなければ水切れ
				bool hasWater = true;
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					float cost = 5.0f; // 水の消費量
					if (!hc.TryConsumeHp(cost)) {
						hasWater = false; // 水切れ
					}
				}

				// 水の弾を生成待ちに積む
				if (hasWater) {
					ProjectileSpawnData ps;
					ps.pos = {tc.translate.x + dx * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dz * 1.5f};
					ps.rot = tc.rotate;
					ps.dir = {dx, 0.0f, dz};
					frame.system.pendingProjectiles_.push_back(ps);
				} else {
					// シュゥゥ…というミスト（水切れ）
					if (ctx.scene) {
						frame.system.pendingMists_.push_back({
						    {tc.translate.x + dx * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dz * 1.5f},
                            {dx,                         0.0f,                  dz                        }
                        });
					}
				}
			}

			// 時間が来たら待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

// トゲ爆発: 縮んでから全方位へ膨らみ、爆発の判定とエフェクトを出す
class SpikeExplosionState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		auto& targetCamOffset = frame.targetCamOffset;
		{
			// 進み具合
			float progress = pa.stateTimer / pa.stateDuration;
			targetCamOffset = 6.0f * (1.0f - progress); // 爆発中は引いたカメラを徐々に元へ戻す

			// アニメーション: 一瞬縮んでから全方位に膨張
			if (progress < 0.2f) {
				float shrink = progress / 0.2f;
				tc.scale.x = std::lerp(1.2f, 0.4f, shrink);
				tc.scale.y = std::lerp(0.8f, 0.2f, shrink);
				tc.scale.z = std::lerp(1.2f, 0.4f, shrink);
			} else {
				float exp = (progress - 0.2f) / 0.8f;
				float pop = std::sin(exp * DirectX::XM_PI); // 0 -> 1 -> 0
				tc.scale.x = 1.0f + (pop * 2.5f);
				tc.scale.y = 0.8f + (pop * 1.5f);
				tc.scale.z = 1.0f + (pop * 2.5f);
			}

			// 爆発の瞬間にエフェクトとHitboxを生成予約
			if (pa.stateTimer >= 0.1f && pa.stateTimer - ctx.dt < 0.1f) {
				frame.system.pendingExplosions_.push_back({tc.translate});

				// カメラシェイク
				if (ctx.camera)
					ctx.camera->StartShake(0.3f, 0.4f);
			}

			// 時間が来たら待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

// 火炎: 口を開けた形になり、一定間隔で炎と攻撃判定を出す
class FireBreathState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			// 進み具合
			float progress = pa.stateTimer / pa.stateDuration;

			// プレイヤーの変形 (前方に口を開けているような形)
			tc.scale.x = 1.2f - std::sin(progress * 3.14f) * 0.2f;
			tc.scale.y = 0.8f + std::sin(progress * 3.14f) * 0.2f;
			tc.scale.z = 1.2f + std::sin(progress * 3.14f) * 0.5f;

			// 炎のエフェクトとHitboxを一定間隔で生成
			if (pa.stateTimer > 0.0f) {
				// 向いている方向
				float facing = tc.rotate.y;
				float dx = std::sin(facing);
				float dz = std::cos(facing);

				// パーティクル生成予約 (炎) - 0.1秒に1回
				if (std::fmod(pa.stateTimer, 0.1f) < ctx.dt) {
					if (ctx.scene) {
						frame.system.pendingFireBreaths_.push_back({
						    {tc.translate.x, tc.translate.y, tc.translate.z},
                            {dx,             0.0f,           dz            }
                        });
					}
				}
			}

			// 時間が来たら待機へ
			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

// 分身ワープ: 放物線を描いて跳び、着地でエフェクトを出す (跳んでいる間は通常の移動を止める)
class DecoyWarpState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 跳んでいる間は通常の移動を止める (元の設定を覚えておく)
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* movement = registry.try_get<CharacterMovementComponent>(entity)) {
			registry.get<PlayerActionComponent>(entity).warpMovementEnabled = movement->enabled;
			movement->enabled = false;
		}
	}
	void Exit(EntityStateContext& frame) const override {
		// 通常の移動の設定を元に戻す
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* movement = registry.try_get<CharacterMovementComponent>(entity))
			movement->enabled = registry.get<PlayerActionComponent>(entity).warpMovementEnabled;
	}
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			// 進み具合 (0〜1)
			float progress = pa.stateTimer / pa.stateDuration;
			progress = std::clamp(progress, 0.0f, 1.0f);

			// 弧を描くジャンプ (XとZは線形補間、Yは放物線)
			tc.translate.x = std::lerp(pa.warpStartPos.x, pa.warpEndPos.x, progress);
			tc.translate.z = std::lerp(pa.warpStartPos.z, pa.warpEndPos.z, progress);

			// 放物線の高さ (ボスを飛び越えるため高めに設定)
			float jumpHeight = 12.0f;
			float parabola = 4.0f * progress * (1.0f - progress); // 0 -> 1 -> 0
			tc.translate.y = std::lerp(pa.warpStartPos.y, pa.warpEndPos.y, progress) + (parabola * jumpHeight);

			// ジャンプ中のスライムの変形 (伸びる)
			tc.scale.x = 0.8f;
			tc.scale.y = 1.5f;
			tc.scale.z = 0.8f;

			if (pa.stateTimer >= pa.stateDuration) {
				// 着地時にエフェクト発生
				frame.system.pendingWarpEffects_.push_back(tc.translate);
				// カメラシェイク
				if (ctx.camera)
					ctx.camera->StartShake(0.2f, 0.3f);
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

// 状態の列挙値から状態オブジェクトを返す
static const State& GetState(PlayerActionState state) {
	// 各状態のオブジェクト (変更されないので共有できる)
	static const IdleState idle;
	static const SlimeSpikeState slimespike;
	static const SlimeHammerState slimehammer;
	static const DodgeState dodge;
	static const LiquefyState liquefy;
	static const StaggerState stagger;
	static const ChargingState charging;
	static const ShootState shoot;
	static const SpikeExplosionState spikeexplosion;
	static const FireBreathState firebreath;
	static const DecoyWarpState decoywarp;
	// 状態の列挙値の順に並べる (数が合っているかを確かめる)
	static const State* const states[] = {&idle, &slimespike, &slimehammer, &dodge, &liquefy, &stagger, &charging, &shoot, &spikeexplosion, &firebreath, &decoywarp};
	static_assert(std::size(states) == static_cast<size_t>(PlayerActionState::DecoyWarp) + 1);
	const auto index = static_cast<size_t>(state);
	return *states[index < std::size(states) ? index : 0];
}
