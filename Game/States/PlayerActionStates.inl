// Included inside PlayerActionSystem; each state shares behavior, never actor data.
struct StateContext {
	PlayerActionSystem& system;
	entt::registry& registry;
	entt::entity entity;
	GameContext& ctx;
	bool attackInput, attackPressed, hammerPressed, dashPressed, liquefyInput;
	float& targetCamOffset;
};
class State : public Engine::IState<StateContext, EntityStateContext> {
public:
	virtual bool CanCancelToDodge(const PlayerActionComponent&) const { return false; }
	virtual void UpdateHitbox(EntityStateContext& frame) const {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			hitbox->isActive = false;
			hitbox->center.z = 0;
		}
	}
};

class IdleState final : public State {
public:
	void Update(StateContext& frame) const override {
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

class SlimeSpikeState final : public State {
public:
	bool CanCancelToDodge(const PlayerActionComponent& action) const override { return action.stateTimer >= .08f; }
	void UpdateHitbox(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			const auto& action = frame.registry.get<PlayerActionComponent>(frame.entity);
			const auto& transform = frame.registry.get<TransformComponent>(frame.entity);
			const bool wasActive = hitbox->isActive;
			hitbox->isActive = action.stateTimer >= .025f && action.stateTimer <= .19f;
			if (!wasActive && hitbox->isActive)
				hitbox->hitTargets.clear();
			hitbox->damage = 30;
			hitbox->size = {1.5f, 1.5f, transform.scale.z * 1.5f};
			hitbox->center.z = transform.scale.z * .75f;
		}
	}
	void Update(StateContext& frame) const override {
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

			// ★追加: 描画に合わせて当たり判定を「プレイヤーの根本から先端まで」に設定
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				// 物理コライダー: 伸びた長さの半分だけ前方にズラす
				bc->center.z = tc.scale.z * 0.75f;
			}

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

class SlimeHammerState final : public State {
public:
	bool CanCancelToDodge(const PlayerActionComponent& action) const override { return action.stateTimer >= .22f; }
	void UpdateHitbox(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
			const auto& action = frame.registry.get<PlayerActionComponent>(frame.entity);
			const auto& transform = frame.registry.get<TransformComponent>(frame.entity);
			const bool wasActive = hitbox->isActive;
			hitbox->isActive = action.stateTimer >= .14f && action.stateTimer <= .42f;
			if (!wasActive && hitbox->isActive)
				hitbox->hitTargets.clear();
			hitbox->damage = 40;
			hitbox->size = {5.0f, 4.0f, transform.scale.z * 1.5f};
			hitbox->center.z = transform.scale.z * .75f;
		}
	}
	void Update(StateContext& frame) const override {
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

			// ★追加: 描画に合わせて当たり判定を「プレイヤーの根本から先端まで」に設定
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				bc->center.z = tc.scale.z * 0.75f;
			}

			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
		}
	}
};

class DodgeState final : public State {
public:
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& pi = frame.registry.get<PlayerInputComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			pi.moveDir = {0.0f, 0.0f};
			pi.jumpRequested = false;

			// 回避移動
			float step = pa.dodgeSpeed * ctx.dt;
			if (ctx.scene) {
				float wallDistance = step + 0.7f;
				if (ctx.scene->RayCast(
				        {tc.translate.x, tc.translate.y + 0.4f, tc.translate.z}, {pa.dodgeDirection.x, 0.0f, pa.dodgeDirection.z}, step + 0.7f, static_cast<uint32_t>(entity), wallDistance)) {
					step = (std::max)(0.0f, wallDistance - 0.7f);
				}
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

			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
		}
	}
};

class LiquefyState final : public State {
public:
	void Exit(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		EndLiquefy(registry, entity, registry.get<PlayerActionComponent>(entity));
	}
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& pi = frame.registry.get<PlayerInputComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const bool liquefyInput = frame.liquefyInput;
		{
			if (!liquefyInput) {

				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
				return;
			}

			if (!pa.liquefyLocked) {
				frame.system.BeginLiquefy(registry, entity, pa, pi, tc, ctx);
			}

			pi.moveDir = {0.0f, 0.0f};
			pi.jumpRequested = false;
			tc.translate = pa.liquefyAnchorPos;
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				rb->velocity.y = 0.0f;
			}
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->isGrounded = true;
			}

			bool isMoving = pa.CurrentLiquefyFlowSpeed() > 0.1f;
			float t = std::min(1.0f, pa.stateTimer * 8.0f);
			float wave = std::sin(pa.totalTime * 12.0f) * 0.20f;
			if (isMoving) {
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
			tc.scale.y = std::max(0.040f, tc.scale.y);

			// 無敵状態を維持
			if (registry.all_of<HealthComponent>(entity)) {
				auto& hc = registry.get<HealthComponent>(entity);
				hc.invincibleTime = 0.2f; // 毎フレーム少し先まで無敵を更新
			}
		}
	}
};

class StaggerState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
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
		if (pa.stateTimer >= pa.stateDuration) {
			ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
		}
	}
};

class ChargingState final : public State {
public:
	bool CanCancelToDodge(const PlayerActionComponent&) const override { return true; }
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		auto& targetCamOffset = frame.targetCamOffset;
		const bool attackInput = frame.attackInput;
		const bool hammerPressed = frame.hammerPressed;
		{
			pa.chargeTimer += ctx.dt;
			// 溜め中の演出（小刻みに震えつつ、徐々に潰れる）
			float shake = std::sin(pa.chargeTimer * 50.0f) * 0.05f;
			tc.scale.x = 1.2f + shake;
			tc.scale.y = std::max(0.4f, 0.8f - pa.chargeTimer * 0.4f); // ペシャンコに
			tc.scale.z = 1.2f + shake;

			// ★追加: 溜めている間、徐々にカメラを引く（最大6.0m）
			targetCamOffset = std::min(pa.chargeTimer * 5.0f, 6.0f);

			// ★追加: 溜め中に右クリック（ハンマーボタン）が押されたら爆発派生
			if (hammerPressed) {
				ChangeState(registry, entity, PlayerActionState::SpikeExplosion, 0.5f); // 0.5秒の爆発アクション
				pa.chargeTimer = 0.0f;
			}
			// 左クリックを離したかどうかの判定 (入力が途切れたら)
			else if (!attackInput) {
				if (pa.chargeTimer >= 0.4f) { // 一定時間以上で遠距離発射
					ChangeState(registry, entity, PlayerActionState::Shoot, 0.3f);
					pa.chargeTimer = 0.0f;
				} else { // The quick strike already happened on press.
					ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
					pa.chargeTimer = 0.0f;
				}
			}
			return;
		}
	}
};

class ShootState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		registry.get<PlayerActionComponent>(entity).shotEmitted = false;
	}
	void Update(StateContext& frame) const override {
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
				float facing = tc.rotate.y;
				float dx = std::sin(facing);
				float dz = std::cos(facing);

				bool hasWater = true;
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					float cost = 5.0f; // 水の消費量
					if (!hc.TryConsumeHp(cost)) {
						hasWater = false; // 水切れ
					}
				}

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

			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

class SpikeExplosionState final : public State {
public:
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		auto& targetCamOffset = frame.targetCamOffset;
		{
			float progress = pa.stateTimer / pa.stateDuration;
			targetCamOffset = 6.0f * (1.0f - progress); // ★追加: 爆発中は引いた状態から徐々に元に戻る

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

			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

class FireBreathState final : public State {
public:
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
			float progress = pa.stateTimer / pa.stateDuration;

			// プレイヤーの変形 (前方に口を開けているような形)
			tc.scale.x = 1.2f - std::sin(progress * 3.14f) * 0.2f;
			tc.scale.y = 0.8f + std::sin(progress * 3.14f) * 0.2f;
			tc.scale.z = 1.2f + std::sin(progress * 3.14f) * 0.5f;

			// 炎のエフェクトとHitboxを一定間隔で生成
			if (pa.stateTimer > 0.0f) {
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

			if (pa.stateTimer >= pa.stateDuration) {
				ChangeState(registry, entity, PlayerActionState::Idle, 0.0f);
			}
			return;
		}
	}
};

class DecoyWarpState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* movement = registry.try_get<CharacterMovementComponent>(entity)) {
			registry.get<PlayerActionComponent>(entity).warpMovementEnabled = movement->enabled;
			movement->enabled = false;
		}
	}
	void Exit(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* movement = registry.try_get<CharacterMovementComponent>(entity))
			movement->enabled = registry.get<PlayerActionComponent>(entity).warpMovementEnabled;
	}
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& pa = frame.registry.get<PlayerActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		{
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

static const State& GetState(PlayerActionState state) {
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
	static const State* const states[] = {&idle, &slimespike, &slimehammer, &dodge, &liquefy, &stagger, &charging, &shoot, &spikeexplosion, &firebreath, &decoywarp};
	static_assert(std::size(states) == static_cast<size_t>(PlayerActionState::DecoyWarp) + 1);
	const auto index = static_cast<size_t>(state);
	return *states[index < std::size(states) ? index : 0];
}
