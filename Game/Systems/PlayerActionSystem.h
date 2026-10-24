// PlayerActionSystem.h
// 旧アクションゲームのプレイヤー (スライムと缶) の行動。
// 入力から状態 (States/PlayerActionStates.inl) を動かし、缶ごとの能力・弾やエフェクトの生成・体の大きさを管理する。
#pragma once
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace Game {

// プレイヤーの行動の状態
enum class PlayerActionState : uint32_t {
	Idle = 0,
	SlimeSpike,   // 素早いトゲ攻撃
	SlimeHammer,  // 重い叩きつけ攻撃
	Dodge,        // 回避 (廃止・または予約)
	Liquefy,      // 液状化 (無敵)
	Stagger,      // のけぞり
	Charging,     // 溜め中
	Shoot,        // 発射アクション
	SpikeExplosion, // トゲトゲの大爆発
	FireBreath,   // 炎の缶の攻撃
	DecoyWarp     // 分身ワープ (弧を描いて移動する)
};

// プレイヤーの行動コンポーネント (状態・時間・入力の前回値・回避・缶・炭酸・液状化・各缶の能力の値)
struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;
	float stateTimer = 0.0f;
	float stateDuration = 0.0f;
	bool previousAttack = false, previousHammer = false, previousDodge = false;
	bool shotEmitted = false;
	bool warpMovementEnabled = true;
	float chargeTimer = 0.0f; // 溜めタイマー


	float dodgeDuration = 0.26f;
	float dodgeSpeed = 23.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;
	float totalTime = 0.0f; // ふわふわ揺れる演出用の時間

	CanType currentCan = CanType::None; // 現在入っている缶
	entt::entity canEntity = entt::null; // 缶のエンティティ
	DirectX::XMFLOAT3 canFollowPos = {0.0f, 0.0f, 0.0f};
	bool canFollowInitialized = false;
	float canRevealTimer = 1.0f;
	bool prevRadialMenuOpen = false; // 前フレームのラジアルメニューの開閉

	DirectX::XMFLOAT3 warpStartPos = {0,0,0}; // ワープの開始位置
	DirectX::XMFLOAT3 warpEndPos = {0,0,0};   // ワープの終了位置

	// 炭酸の缶 (ガスの量・狙い・噴射・漂う速度)
	float sodaGas = 100.0f;
	float sodaAimTimer = 0.0f;
	float sodaEmitTimer = 0.0f;
	bool sodaAiming = false;
	DirectX::XMFLOAT3 sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
	// 液状化 (元の速さ・固定位置・流れる向きと速さ)
	float liquefyBaseSpeed = 0.0f;
	bool liquefySpeedApplied = false;
	DirectX::XMFLOAT3 liquefyAnchorPos = {0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 liquefyFlowDir = {0.0f, 0.0f, 1.0f};
	float liquefyInitialFlowSpeed = 0.0f;
	bool liquefyLocked = false;
	// 液状化中に流れる速さ (入ったときの勢いを時間で弱める)
	float CurrentLiquefyFlowSpeed() const {
		// 液状化中は位置を固定している。入ったときの勢いは、押している間ずっと
		// 水たまりを押し流すのではなく、だんだん弱まるようにする
		return liquefyInitialFlowSpeed * std::exp(-8.0f * stateTimer);
	}
	// 缶の能力の待ち時間と、氷・磁石の演出の時間
	float canPrimaryCooldown = 0.0f;
	float canSecondaryCooldown = 0.0f;
	float iceTrailEmitTimer = 0.0f;
	float iceSlideBaseSpeed = 0.0f;
	bool iceSlideApplied = false;
	float magnetEffectTimer = 0.0f;

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

// プレイヤーの行動システム
class PlayerActionSystem : public ISystem {
public:
	// 状態を切り替える (前の状態の Exit → 新しい状態の Enter)
	static void ChangeState(entt::registry& registry, entt::entity entity, PlayerActionState next, float duration = 0.0f);

	// PLAY 中の更新: 各プレイヤーの入力から行動を決め、最後に弾やエフェクトをまとめて生成する
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: 全プレイヤーの行動の状態を初期化する
	void Reset(entt::registry& registry) override;

private:
	// 後でまとめて生成するもののデータ (水の弾・缶の弾・酸の水たまり・氷の跡・磁石・ミスト・炭酸ガス・炎・爆発・分身)
	struct ProjectileSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 dir;
	};
	// 生成待ちの一覧
	std::vector<ProjectileSpawnData> pendingProjectiles_;

	struct CanProjectileSpawnData {
		CanType type = CanType::None;
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		DirectX::XMFLOAT3 dir = {0, 0, 1};
	};
	std::vector<CanProjectileSpawnData> pendingCanProjectiles_;

	struct AcidPoolSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
	};
	std::vector<AcidPoolSpawnData> pendingAcidPools_;

	struct IceTrailSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		float yaw = 0.0f;
	};
	std::vector<IceTrailSpawnData> pendingIceTrails_;

	struct MagnetEffectSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		bool pulse = false;
	};
	std::vector<MagnetEffectSpawnData> pendingMagnetEffects_;

	struct MistSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
		float intensity = 1.0f;
	};
	std::vector<MistSpawnData> pendingMists_;

	struct SodaGasSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
		float intensity = 1.0f;
	};
	std::vector<SodaGasSpawnData> pendingSodaGas_;

	struct FireBreathSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
	};
	std::vector<FireBreathSpawnData> pendingFireBreaths_;

	struct ExplosionSpawnData {
		DirectX::XMFLOAT3 pos;
	};
	std::vector<ExplosionSpawnData> pendingExplosions_;

	// 分身の生成データ
	struct DecoySpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 scale;
	};
	std::vector<DecoySpawnData> pendingDecoys_;
	std::vector<DirectX::XMFLOAT3> pendingWarpEffects_;

	// 後で行う缶の切り替え
	struct PendingCanChange {
		entt::entity playerEntity;
		CanType newCan;
	};
	std::vector<PendingCanChange> pendingCanChanges_;


	// 液状化を始める: 地面に固定し、カメラの向きへ流れる向きと速さを決める
	void BeginLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx);

	// 液状化を終える
	static void EndLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		RestoreLiquefySpeed(registry, entity, pa);
		pa.liquefyLocked = false;
		pa.liquefyInitialFlowSpeed = 0.0f;
	}

	// 液状化中は移動の速さを変える (元の速さを覚えておく)
	void ApplyLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		// 最初だけ元の速さを覚え、速くする
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.liquefySpeedApplied) {
				pa.liquefyBaseSpeed = cm->speed;
				pa.liquefySpeedApplied = true;
			}
			cm->speed = std::max(pa.liquefyBaseSpeed * 1.75f, pa.liquefyBaseSpeed + 4.0f);
		}
	}

	// 液状化で変えた移動の速さを元に戻す
	static void RestoreLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (pa.liquefySpeedApplied) {
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->speed = pa.liquefyBaseSpeed;
			}
			pa.liquefyBaseSpeed = 0.0f;
			pa.liquefySpeedApplied = false;
		}
	}

	// プレイヤーの前方向 (Y 軸の回転から求める)
	DirectX::XMFLOAT3 GetCameraForward(const TransformComponent& tc, GameContext& ctx) const {
		float yaw = tc.rotate.y;
		// カメラがあればカメラの向き
		if (ctx.camera) {
			yaw = ctx.camera->Rotation().y;
		}
		return { std::sin(yaw), 0.0f, std::cos(yaw) };
	}

	// 入力をカメラ基準のワールドの移動方向に直す
	DirectX::XMFLOAT3 GetCameraMoveDirection(const PlayerInputComponent& pi, GameContext& ctx, const TransformComponent& tc) const;

	// 水平の速さを上限までに抑える
	void ClampHorizontalVelocity(DirectX::XMFLOAT3& velocity, float maxSpeed) const {
		float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
		float maxSq = maxSpeed * maxSpeed;
		if (speedSq > maxSq) {
			float s = maxSpeed / std::sqrt(speedSq);
			velocity.x *= s;
			velocity.z *= s;
		}
	}

	// 炭酸ガスの噴射を生成待ちに積む
	void QueueSodaGas(const TransformComponent& tc, const DirectX::XMFLOAT3& exhaustDir, float intensity) {
		pendingSodaGas_.push_back({
			{ tc.translate.x, tc.translate.y + 0.25f, tc.translate.z },
			exhaustDir,
			intensity
		});
	}

	// 炭酸の缶: 攻撃でガスを噴いて加速し、右クリックで狙って離すと溜めた分だけ飛び出す
	void HandleSodaCan(
		entt::registry& registry,
		entt::entity entity,
		PlayerActionComponent& pa,
		const PlayerInputComponent& pi,
		TransformComponent& tc,
		GameContext& ctx,
		bool boostInput,
		bool aimInput,
		bool aimReleased,
		float& targetCamOffset) {
		// 漂う速度で動かし、減衰させる
		if (std::abs(pa.sodaDriftVelocity.x) > 0.01f || std::abs(pa.sodaDriftVelocity.z) > 0.01f) {
			tc.translate.x += pa.sodaDriftVelocity.x * ctx.dt;
			tc.translate.z += pa.sodaDriftVelocity.z * ctx.dt;
			float damp = std::max(0.0f, 1.0f - 3.8f * ctx.dt);
			pa.sodaDriftVelocity.x *= damp;
			pa.sodaDriftVelocity.z *= damp;
		}

		// 待機中でメニューを開いていなければガスを使える
		bool canUseGas = pa.state == PlayerActionState::Idle && !pi.isRadialMenuOpen;
		bool usingGas = false;
		pa.sodaEmitTimer += ctx.dt;

		// 使えないときは狙いを解除してガスを回復する
		if (!canUseGas) {
			pa.sodaAiming = false;
			pa.sodaAimTimer = 0.0f;
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 10.0f * ctx.dt);
			return;
		}

		// 剛体とキャラクター移動
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		auto* cm = registry.try_get<CharacterMovementComponent>(entity);

		// 噴射: 上へ浮かせ、移動の向きへ漂い、ガスを使う
		if (boostInput && pa.sodaGas > 0.0f && rb && cm) {
			DirectX::XMFLOAT3 moveDir = GetCameraMoveDirection(pi, ctx, tc);
			rb->velocity.y = std::min(8.0f, std::max(rb->velocity.y + 18.0f * ctx.dt, 2.5f));
			cm->isGrounded = false;
			pa.sodaDriftVelocity.x += moveDir.x * 7.0f * ctx.dt;
			pa.sodaDriftVelocity.z += moveDir.z * 7.0f * ctx.dt;
			ClampHorizontalVelocity(pa.sodaDriftVelocity, 7.5f);
			pa.sodaGas = std::max(0.0f, pa.sodaGas - 24.0f * ctx.dt);
			targetCamOffset = std::max(targetCamOffset, 2.0f);
			usingGas = true;

			// 一定間隔でガスの粒を出す
			if (pa.sodaEmitTimer >= 0.07f) {
				QueueSodaGas(tc, { -moveDir.x * 0.35f, -1.0f, -moveDir.z * 0.35f }, 0.9f);
				pa.sodaEmitTimer = 0.0f;
			}
		}

		// 狙い: カメラの向きを向き、溜めて、カメラを引く
		if (aimInput) {
			DirectX::XMFLOAT3 forward = GetCameraForward(tc, ctx);
			pa.sodaAiming = true;
			pa.sodaAimTimer = std::min(pa.sodaAimTimer + ctx.dt, 1.2f);
			tc.rotate.y = std::atan2(forward.x, forward.z);
			targetCamOffset = std::max(targetCamOffset, 3.5f);

			if (pa.sodaEmitTimer >= 0.12f) {
				QueueSodaGas(tc, { -forward.x * 0.3f, -0.4f, -forward.z * 0.3f }, 0.45f);
				pa.sodaEmitTimer = 0.0f;
			}
		// 狙いを離したら、溜めとガスの量に応じた強さで前上へ飛び出す
		} else if (pa.sodaAiming) {
			if (aimReleased && pa.sodaGas > 8.0f && rb && cm) {
				DirectX::XMFLOAT3 forward = GetCameraForward(tc, ctx);
				float charge = std::clamp(pa.sodaAimTimer / 1.1f, 0.35f, 1.0f);
				float cost = 28.0f + 22.0f * charge;
				float gasPower = std::clamp(pa.sodaGas / cost, 0.35f, 1.0f);
				float power = charge * gasPower;

				rb->velocity.y = std::max(rb->velocity.y, 12.0f + 8.0f * power);
				cm->isGrounded = false;
				pa.sodaDriftVelocity.x += forward.x * (12.0f + 10.0f * power);
				pa.sodaDriftVelocity.z += forward.z * (12.0f + 10.0f * power);
				ClampHorizontalVelocity(pa.sodaDriftVelocity, 18.0f);
				pa.sodaGas = std::max(0.0f, pa.sodaGas - cost);
				QueueSodaGas(tc, { -forward.x * 0.9f, -0.7f, -forward.z * 0.9f }, 1.8f);
				if (ctx.camera) ctx.camera->StartShake(0.18f, 0.25f);
				usingGas = true;
			}
			pa.sodaAiming = false;
			pa.sodaAimTimer = 0.0f;
		}

		// 使っていないときはガスを回復する
		if (!usingGas && !boostInput && !aimInput) {
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 12.0f * ctx.dt);
		}
	}

	// 能力を出す方向 (ロックオン中は敵の方、そうでなければ前方)
	DirectX::XMFLOAT3 GetAbilityDirection(entt::registry& registry, const PlayerInputComponent& pi,
		const TransformComponent& tc, GameContext& ctx) const {
		// ロックオン中の敵への水平の向き
		if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy) && registry.all_of<TransformComponent>(pi.lockedEnemy)) {
			const auto& targetTc = registry.get<TransformComponent>(pi.lockedEnemy);
			float dx = targetTc.translate.x - tc.translate.x;
			float dz = targetTc.translate.z - tc.translate.z;
			float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.001f) return {dx / len, 0.0f, dz / len};
		}
		return GetCameraForward(tc, ctx);
	}

	// 氷の缶の滑走: 移動を速くする (元の速さを覚えておく)
	void ApplyIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.iceSlideApplied) {
				pa.iceSlideBaseSpeed = cm->speed;
				pa.iceSlideApplied = true;
			}
			cm->speed = (std::max)(pa.iceSlideBaseSpeed * 2.1f, pa.iceSlideBaseSpeed + 7.0f);
		}
	}

	// 氷の滑走で変えた移動の速さを元に戻す
	void RestoreIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (!pa.iceSlideApplied) return;
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) cm->speed = pa.iceSlideBaseSpeed;
		pa.iceSlideBaseSpeed = 0.0f;
		pa.iceSlideApplied = false;
	}

	// 氷・磁石・酸・泡の缶: 攻撃で弾 (磁石は引き寄せ)、右クリックで缶ごとの補助能力
	void HandleAdvancedCan(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa,
		const PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx,
		bool attackPressed, bool secondaryInput, bool secondaryPressed) {
		// メニュー中・待機以外なら滑走を戻して何もしない
		if (pi.isRadialMenuOpen || pa.state != PlayerActionState::Idle) {
			if (pa.iceSlideApplied) RestoreIceSlide(registry, entity, pa);
			return;
		}

		// 能力の向き
		const DirectX::XMFLOAT3 dir = GetAbilityDirection(registry, pi, tc, ctx);
		// 攻撃: 磁石は周りを押し返す波、それ以外は弾を撃つ
		if (attackPressed && pa.canPrimaryCooldown <= 0.0f) {
			if (pa.currentCan == CanType::Magnet) {
				pendingMagnetEffects_.push_back({{tc.translate.x, tc.translate.y + 0.4f, tc.translate.z}, true});
				pa.canPrimaryCooldown = 1.1f;
				if (ctx.camera) ctx.camera->StartShake(0.12f, 0.18f);
			} else {
				pendingCanProjectiles_.push_back({
					pa.currentCan,
					{tc.translate.x + dir.x * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dir.z * 1.5f},
					dir
				});
				pa.canPrimaryCooldown = pa.currentCan == CanType::Bubble ? 0.75f : 0.42f;
			}
		}

		// 氷: 右クリックの間は滑走し、足元に氷の跡を残す
		if (pa.currentCan == CanType::Ice) {
			if (secondaryInput) {
				ApplyIceSlide(registry, entity, pa);
				pa.iceTrailEmitTimer -= ctx.dt;
				if (pa.iceTrailEmitTimer <= 0.0f) {
					// 跡の高さは地面に合わせる
					float trailY = tc.translate.y - 0.72f;
					if (ctx.scene) {
						const float groundY = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 2.5f, static_cast<uint32_t>(entity));
						if (groundY > -5000.0f) trailY = groundY + 0.06f;
					}
					pendingIceTrails_.push_back({{tc.translate.x, trailY, tc.translate.z}, tc.rotate.y});
					pa.iceTrailEmitTimer = 0.13f;
				}
			} else {
				RestoreIceSlide(registry, entity, pa);
			}
		// 磁石: 右クリックの間は周りの敵を引き寄せる (ボスは遅い)
		} else if (pa.currentCan == CanType::Magnet) {
			if (secondaryInput) {
				pa.magnetEffectTimer -= ctx.dt;
				if (pa.magnetEffectTimer <= 0.0f) {
					pendingMagnetEffects_.push_back({{tc.translate.x, tc.translate.y + 0.5f, tc.translate.z}, false});
					pa.magnetEffectTimer = 0.16f;
				}

				auto enemies = registry.view<TagComponent, TransformComponent, HealthComponent>();
				for (auto target : enemies) {
					if (enemies.get<TagComponent>(target).tag != TagType::Enemy) continue;
					auto& health = enemies.get<HealthComponent>(target);
					if (health.IsDead() || !health.enabled) continue;
					auto& targetTc = enemies.get<TransformComponent>(target);
					float dx = tc.translate.x - targetTc.translate.x;
					float dz = tc.translate.z - targetTc.translate.z;
					float dist = std::sqrt(dx * dx + dz * dz);
					if (dist < 2.5f || dist > 32.0f) continue;
					const float pullSpeed = registry.all_of<BossActionComponent>(target) ? 7.0f : 21.0f;
					targetTc.translate.x += (dx / dist) * pullSpeed * ctx.dt;
					targetTc.translate.z += (dz / dist) * pullSpeed * ctx.dt;
				}
			}
		// 酸: 右クリックで前方に酸の水たまりを作る (待ち時間 5 秒)
		} else if (pa.currentCan == CanType::Acid) {
			if (secondaryPressed && pa.canSecondaryCooldown <= 0.0f) {
				const float poolX = tc.translate.x + dir.x * 4.0f;
				const float poolZ = tc.translate.z + dir.z * 4.0f;
				float poolY = tc.translate.y - 0.7f;
				if (ctx.scene) {
					const float groundY = ctx.scene->GetHeightAt(poolX, poolZ, tc.translate.y + 3.0f, static_cast<uint32_t>(entity));
					if (groundY > -5000.0f) poolY = groundY + 0.08f;
				}
				pendingAcidPools_.push_back({{poolX, poolY, poolZ}});
				pa.canSecondaryCooldown = 5.0f;
			}
		// 泡: 右クリックで 1 回分のシールドを張る (待ち時間 9 秒)
		} else if (pa.currentCan == CanType::Bubble) {
			if (secondaryPressed && pa.canSecondaryCooldown <= 0.0f) {
				auto& shield = registry.get_or_emplace<BubbleShieldComponent>(entity);
				shield.timer = 6.0f;
				shield.charges = 1;
				pa.canSecondaryCooldown = 9.0f;
			}
		}
	}

	// 生成待ちの缶の弾を作る
	void SpawnCanProjectiles(entt::registry& registry, GameContext& ctx);

	// 生成待ちの酸の水たまりを作る
	void SpawnAcidPools(entt::registry& registry, GameContext& ctx);

	// 生成待ちの氷の跡を作る
	void SpawnIceTrails(entt::registry& registry, GameContext& ctx);

	// 生成待ちの磁石のエフェクトを作る
	void SpawnMagnetEffects(entt::registry& registry, GameContext& ctx);

	// 缶を切り替える
	void ChangeCan(entt::registry& registry, entt::entity playerEntity, PlayerActionComponent& pa, TransformComponent& ptc, CanType newCan, GameContext& ctx);

	// 回避を始める: 入力の向き (カメラ基準) か、入力が無ければ前方へ
	void StartDodge(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx);
// 各状態の定義 (クラスの中に読み込む)
#include "../States/PlayerActionStates.inl"
};

} // namespace Game
