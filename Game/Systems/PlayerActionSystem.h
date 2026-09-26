#pragma once
#include "ISystem.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace Game {

enum class PlayerActionState : uint32_t {
	Idle = 0,
	SlimeSpike,   // 素早いトゲ攻撃
	SlimeHammer,  // 重い叩きつけ攻撃
	Dodge,        // 回避 (廃止・または予約)
	Liquefy,      // 液状化 (無敵)
	Stagger,      // のけぞり
	Charging,     // 溜め中
	Shoot,        // 発射アクション
	SpikeExplosion, // ★追加: トゲトゲ大爆発
	FireBreath,   // ★追加: 炎の缶の攻撃
	DecoyWarp     // ★追加: デコイワープ（弧を描いて移動）
};

struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;
	float stateTimer = 0.0f;
	float stateDuration = 0.0f;
	float chargeTimer = 0.0f; // 溜めタイマー


	float dodgeDuration = 0.26f;
	float dodgeSpeed = 23.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;
	float totalTime = 0.0f; // ★追加: ふわふわ用タイマー

	CanType currentCan = CanType::None; // ★追加: 現在滞在している缶
	entt::entity canEntity = entt::null; // ★追加: 缶のエンティティID
	DirectX::XMFLOAT3 canFollowPos = {0.0f, 0.0f, 0.0f};
	bool canFollowInitialized = false;
	float canRevealTimer = 1.0f;
	bool prevRadialMenuOpen = false; // ★追加: 前回のラジアルメニュー開閉状態

	DirectX::XMFLOAT3 warpStartPos = {0,0,0}; // ★追加: ワープ開始位置
	DirectX::XMFLOAT3 warpEndPos = {0,0,0};   // ★追加: ワープ終了位置

	float sodaGas = 100.0f;
	float sodaAimTimer = 0.0f;
	float sodaEmitTimer = 0.0f;
	bool sodaAiming = false;
	DirectX::XMFLOAT3 sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
	float liquefyBaseSpeed = 0.0f;
	bool liquefySpeedApplied = false;
	DirectX::XMFLOAT3 liquefyAnchorPos = {0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 liquefyFlowDir = {0.0f, 0.0f, 1.0f};
	float liquefyInitialFlowSpeed = 0.0f;
	bool liquefyLocked = false;
	float CurrentLiquefyFlowSpeed() const {
		// The controller is anchored while liquefied. Its entry momentum must
		// fade instead of driving the puddle away for the entire hold.
		return liquefyInitialFlowSpeed * std::exp(-8.0f * stateTimer);
	}
	float canPrimaryCooldown = 0.0f;
	float canSecondaryCooldown = 0.0f;
	float iceTrailEmitTimer = 0.0f;
	float iceSlideBaseSpeed = 0.0f;
	bool iceSlideApplied = false;
	float magnetEffectTimer = 0.0f;

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

class PlayerActionSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<PlayerActionComponent, PlayerInputComponent, TransformComponent>();
		std::vector<entt::entity> entities(view.begin(), view.end());
		for (auto entity : entities) {
			if (!registry.valid(entity)) continue;
			auto& pa = registry.get<PlayerActionComponent>(entity);
			auto& pi = registry.get<PlayerInputComponent>(entity);
			auto& tc = registry.get<TransformComponent>(entity);
			if (!pa.enabled || !pi.enabled) continue;

			if (pa.state != PlayerActionState::Liquefy && pa.liquefySpeedApplied) {
				RestoreLiquefySpeed(registry, entity, pa);
			}
			if (pa.state != PlayerActionState::Liquefy && pa.liquefyLocked) {
				pa.liquefyLocked = false;
				pa.liquefyInitialFlowSpeed = 0.0f;
			}

			if (pa.hitStopTimer > 0.0f) {
				pa.hitStopTimer -= ctx.dt;
				if (pa.hitStopTimer < 0.0f) pa.hitStopTimer = 0.0f;
				continue;
			}

			if (pa.dodgeCooldown > 0.0f) pa.dodgeCooldown -= ctx.dt;
			pa.canPrimaryCooldown = (std::max)(0.0f, pa.canPrimaryCooldown - ctx.dt);
			pa.canSecondaryCooldown = (std::max)(0.0f, pa.canSecondaryCooldown - ctx.dt);
			if (pa.currentCan != CanType::Ice && pa.iceSlideApplied) {
				RestoreIceSlide(registry, entity, pa);
			}

			pa.stateTimer += ctx.dt;
			pa.totalTime += ctx.dt; // ★追加
			
			// ★追加: デフォルトでコライダーのオフセットをリセット
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				bc->center = { 0.0f, 0.0f, 0.0f };
			}

			// ★追加: ラジアルメニューによる缶の切り替え
			bool currentRadialMenuOpen = pi.isRadialMenuOpen;
			if (!currentRadialMenuOpen && pa.prevRadialMenuOpen) {
				if (pi.selectedCan != pa.currentCan) {
					pendingCanChanges_.push_back({entity, pi.selectedCan});
				}
			}
			pa.prevRadialMenuOpen = currentRadialMenuOpen;

			// The can follows the final player transform after movement and physics.


			float targetCamOffset = 0.0f;

			bool attackInput = pi.attackRequested; // 左クリック (PlayerInputSystem.h)
			bool attackPressed = attackInput && !prevAttack_;
			prevAttack_ = attackInput;

			// 右クリックでハンマー攻撃（またはデコイ発動）
			bool hammerInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool hammerPressed = hammerInput && !prevHammer_;
			bool hammerReleased = !hammerInput && prevHammer_;
			prevHammer_ = hammerInput;

			if (pa.currentCan == CanType::Soda) {
				HandleSodaCan(registry, entity, pa, pi, tc, ctx, attackInput, hammerInput, hammerReleased, targetCamOffset);
				attackPressed = false;
				hammerPressed = false;
			} else {
				pa.sodaAiming = false;
				pa.sodaAimTimer = 0.0f;
			}

			if (pa.currentCan == CanType::Ice || pa.currentCan == CanType::Magnet ||
				pa.currentCan == CanType::Acid || pa.currentCan == CanType::Bubble) {
				HandleAdvancedCan(registry, entity, pa, pi, tc, ctx, attackPressed, hammerInput, hammerPressed);
				attackPressed = false;
				hammerPressed = false;
			}

			bool dashInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			bool dashPressed = dashInput && !prevDodge_;
			prevDodge_ = dashInput;
			// Cancel the recovery of a liquid attack into a dodge.
			if (dashPressed && pa.dodgeCooldown <= 0.0f &&
				((pa.state == PlayerActionState::SlimeSpike && pa.stateTimer >= 0.08f) ||
				 (pa.state == PlayerActionState::SlimeHammer && pa.stateTimer >= 0.22f) ||
				 pa.state == PlayerActionState::Charging)) {
				pa.chargeTimer = 0.0f;
				StartDodge(pa, pi, tc, ctx);
			}

			bool waterLiquefyInput = (pa.currentCan == CanType::Water && hammerInput);
			bool liquefyInput = waterLiquefyInput;
			if (waterLiquefyInput) {
				hammerPressed = false;
			}

			// ★追加: デコイワープ処理 (黄色の缶選択時)
			if (hammerPressed && pa.currentCan == CanType::Thunder) {
				hammerPressed = false; // ハンマー攻撃には派生させない
				
				// ★追加: デコイは1個しか出せないようにチェック
				bool hasDecoy = false;
				for (auto e : registry.view<TagComponent>()) {
					if (registry.get<TagComponent>(e).tag == TagType::Player && !registry.all_of<PlayerInputComponent>(e)) {
						hasDecoy = true;
						break;
					}
				}

				if (!hasDecoy) {
					entt::entity targetEnemy = pi.lockedEnemy;
					// ロックオンがない場合は一番近い敵を探す
					if (targetEnemy == entt::null || !registry.valid(targetEnemy)) {
						float minDist = 999999.0f;
						for (auto e : registry.view<TagComponent, TransformComponent>()) {
							if (registry.get<TagComponent>(e).tag == TagType::Enemy) {
								auto& eTc = registry.get<TransformComponent>(e);
								float dx = eTc.translate.x - tc.translate.x;
								float dy = eTc.translate.y - tc.translate.y;
								float dz = eTc.translate.z - tc.translate.z;
								float dist = dx*dx + dy*dy + dz*dz;
								if (dist < minDist) {
									minDist = dist;
									targetEnemy = e;
								}
							}
						}
					}

					if (targetEnemy != entt::null && registry.valid(targetEnemy)) {
						auto& bossTc = registry.get<TransformComponent>(targetEnemy);
						
						// 1. デコイ（分身）の生成を予約
						pendingDecoys_.push_back({ tc.translate, tc.rotate, tc.scale });

						// 2. プレイヤーのワープ設定
						float dx = bossTc.translate.x - tc.translate.x;
						float dz = bossTc.translate.z - tc.translate.z;
						float len = std::sqrt(dx*dx + dz*dz);
						if (len > 0.001f) {
							dx /= len; dz /= len;
						} else {
							dx = 0; dz = 1;
						}

						// ワープ元エフェクト
						pendingWarpEffects_.push_back(tc.translate);

						// ボスの反対側（ボスからさらに奥へ大きく飛び越す）
						float warpDist = 12.0f;
						pa.warpStartPos = tc.translate;
						pa.warpEndPos.x = bossTc.translate.x + dx * warpDist;
						pa.warpEndPos.y = tc.translate.y; // 高さはそのままか床
						pa.warpEndPos.z = bossTc.translate.z + dz * warpDist;
						
						// プレイヤーをボスの方に向かせる
						tc.rotate.y = std::atan2(-dx, -dz);
						
						// 少し無敵を付与
						if (auto* hc = registry.try_get<HealthComponent>(entity)) {
							hc->invincibleTime = 0.8f; // ワープ中〜着地後少し無敵
						}
						// デコイワープステートへ移行 (1.0秒で大きくボスを飛び越える)
						if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
							cm->enabled = false; // ★追加: ワープ中は物理挙動(重力や地面スナップ)を無効化する
						}
						TransitionTo(pa, PlayerActionState::DecoyWarp, 1.0f);
					}
				}
			}

			switch (pa.state) {
			case PlayerActionState::Idle:
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
					BeginLiquefy(registry, entity, pa, pi, tc, ctx);
					TransitionTo(pa, PlayerActionState::Liquefy, 0.0f); // 長押し状態
				} else if (dashPressed && pa.dodgeCooldown <= 0.0f) {
					StartDodge(pa, pi, tc, ctx);
				} else if (attackPressed) {
					pa.chargeTimer = 0.0f;
					TransitionTo(pa, pa.currentCan == CanType::Fire ? PlayerActionState::FireBreath : PlayerActionState::SlimeSpike,
						pa.currentCan == CanType::Fire ? 0.6f : 0.26f);
				} else if (hammerPressed) {
					TransitionTo(pa, PlayerActionState::SlimeHammer, 0.58f);
				}
				break;
			}

			case PlayerActionState::Charging:
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
					TransitionTo(pa, PlayerActionState::SpikeExplosion, 0.5f); // 0.5秒の爆発アクション
					pa.chargeTimer = 0.0f;
				}
				// 左クリックを離したかどうかの判定 (入力が途切れたら)
				else if (!attackInput) {
					if (pa.chargeTimer >= 0.4f) { // 一定時間以上で遠距離発射
						TransitionTo(pa, PlayerActionState::Shoot, 0.3f);
						pa.chargeTimer = 0.0f;
					} else { // The quick strike already happened on press.
						TransitionTo(pa, PlayerActionState::Idle, 0.0f);
						pa.chargeTimer = 0.0f;
					}
				}
				break;
			}

			case PlayerActionState::SpikeExplosion:
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
					pendingExplosions_.push_back({ tc.translate });
					
					// カメラシェイク
					if (ctx.camera) ctx.camera->StartShake(0.3f, 0.4f);
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			case PlayerActionState::Shoot:
			{
				// 発射の反動演出 (前方に伸びる)
				float progress = pa.stateTimer / pa.stateDuration;
				float recoil = std::sin(progress * 3.14159f);
				tc.scale.z = 1.2f + (recoil * 0.8f);
				tc.scale.y = 0.8f + (recoil * 0.2f);
				tc.scale.x = 1.2f - (recoil * 0.2f);

				// 状態に入った最初のフレームで弾を生成予約
				if (pa.stateTimer <= ctx.dt * 1.5f && pa.stateTimer > 0.0f) { 
					float facing = tc.rotate.y;
					float dx = std::sin(facing);
					float dz = std::cos(facing);

					bool hasWater = true;
					if (registry.all_of<HealthComponent>(entity)) {
						auto& hc = registry.get<HealthComponent>(entity);
						float cost = 5.0f; // 水の消費量
						if (hc.hp >= cost) {
							hc.hp -= cost;
						} else {
							hasWater = false; // 水切れ
						}
					}

					if (hasWater) {
						ProjectileSpawnData ps;
						ps.pos = { tc.translate.x + dx * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dz * 1.5f };
						ps.rot = tc.rotate;
						ps.dir = { dx, 0.0f, dz };
						pendingProjectiles_.push_back(ps);
					} else {
						// シュゥゥ…というミスト（水切れ）
						if (ctx.scene) {
							pendingMists_.push_back({
								{ tc.translate.x + dx * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dz * 1.5f },
								{ dx, 0.0f, dz }
							});
						}
					}
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			case PlayerActionState::FireBreath:
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
							pendingFireBreaths_.push_back({
								{ tc.translate.x, tc.translate.y, tc.translate.z },
								{ dx, 0.0f, dz }
							});
						}
					}
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			case PlayerActionState::SlimeSpike:
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
				tc.scale.z = 1.2f + (spike * 12.0f); // 針のように長く伸びる
				
				// ★追加: 描画に合わせて当たり判定を「プレイヤーの根本から先端まで」に設定
				if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
					// 物理コライダー: 伸びた長さの半分だけ前方にズラす
					bc->center.z = tc.scale.z * 0.75f;
				}

				if (pa.stateTimer >= pa.stateDuration) {
					if (attackInput) {
						pa.chargeTimer = pa.stateDuration;
						TransitionTo(pa, PlayerActionState::Charging, 0.0f);
					} else {
						TransitionTo(pa, PlayerActionState::Idle, 0.0f);
					}
				}
			}
			break;

			case PlayerActionState::SlimeHammer:
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
				tc.scale.z = 1.2f * scaleBase * (1.0f + smash * 2.5f); // さらに前方に伸びる
				
				// ★追加: 描画に合わせて当たり判定を「プレイヤーの根本から先端まで」に設定
				if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
					bc->center.z = tc.scale.z * 0.75f;
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Liquefy:
			{
				if (!liquefyInput) {
					EndLiquefy(registry, entity, pa);
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
					break;
				}

				if (!pa.liquefyLocked) {
					BeginLiquefy(registry, entity, pa, pi, tc, ctx);
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
					DirectX::XMFLOAT3 flowDir = GetCameraMoveDirection(pi, ctx, tc);
					float targetRot = std::atan2(flowDir.x, flowDir.z);
					float diff = targetRot - tc.rotate.y;
					while (diff > DirectX::XM_PI) diff -= DirectX::XM_2PI;
					while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
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
			break;

			case PlayerActionState::Dodge:
			{
				pi.moveDir = {0.0f, 0.0f};
				pi.jumpRequested = false;

				// 回避移動
				float step = pa.dodgeSpeed * ctx.dt;
				if (ctx.scene) {
					float wallDistance = step + 0.7f;
					if (ctx.scene->RayCast({tc.translate.x, tc.translate.y + 0.4f, tc.translate.z},
						{pa.dodgeDirection.x, 0.0f, pa.dodgeDirection.z}, step + 0.7f,
						static_cast<uint32_t>(entity), wallDistance)) {
						step = (std::max)(0.0f, wallDistance - 0.7f);
					}
					const float currentGround = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 2.0f, static_cast<uint32_t>(entity));
					const float nextGround = ctx.scene->GetHeightAt(tc.translate.x + pa.dodgeDirection.x * step,
						tc.translate.z + pa.dodgeDirection.z * step, tc.translate.y + 2.0f, static_cast<uint32_t>(entity));
					if (nextGround < -999.0f || (currentGround > -999.0f && std::abs(nextGround - currentGround) > 1.2f)) step = 0.0f;
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
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Stagger:
				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;

			case PlayerActionState::DecoyWarp:
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
					pendingWarpEffects_.push_back(tc.translate);
					// カメラシェイク
					if (ctx.camera) ctx.camera->StartShake(0.2f, 0.3f);
					
					if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
						cm->enabled = true; // ★追加: 着地したら物理挙動を元に戻す
					}
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}
			}

			// ★追加: 水量（HP）による全体スケールの適用
			// (すべての変形が終わった後に掛けることで、どの状態でも水量が反映される)
			if (registry.all_of<HealthComponent>(entity)) {
				auto& hc = registry.get<HealthComponent>(entity);
				float waterScale = 0.4f + 0.6f * (std::max(0.0f, hc.hp) / hc.maxHp); // 最小で40%の大きさ
				tc.scale.x *= waterScale;
				tc.scale.y *= waterScale;
				tc.scale.z *= waterScale;
			}

			// ★追加: カメラのズームアウトオフセットの適用（滑らかに）
			if (registry.all_of<CameraTargetComponent>(entity)) {
				auto& ct = registry.get<CameraTargetComponent>(entity);
				ct.distanceOffset += (targetCamOffset - ct.distanceOffset) * 10.0f * ctx.dt;
			}

			// ★追加: イテレーション中の CreateEntity() 呼び出しによって TransformComponent のメモリが再配置された場合に備え、参照を再取得する
			auto& currentTc = registry.get<TransformComponent>(entity);

			// Hitbox（攻撃判定）の更新
			if (registry.all_of<HitboxComponent>(entity)) {
				auto& hb = registry.get<HitboxComponent>(entity);
				if (pa.state == PlayerActionState::SlimeSpike) {
					bool wasActive = hb.isActive;
					// ★修正: 最も水が伸びる瞬間(0.08秒)から縮む途中までしっかり判定を残す
					hb.isActive = (pa.stateTimer >= 0.025f && pa.stateTimer <= 0.19f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 30.0f; // ダメージ上昇
					// ★修正: 当たり判定(Hitbox)を、プレイヤーの根本から先端までカバーするように設定
					hb.size = {1.5f, 1.5f, currentTc.scale.z * 1.5f}; // サイズを伸ばす
					hb.center.z = currentTc.scale.z * 0.75f; // サイズの半分だけ前方にシフト
				} else if (pa.state == PlayerActionState::SlimeHammer) {
					bool wasActive = hb.isActive;
					// ★修正: ハンマー攻撃も判定発生を早め、長めに残す
					hb.isActive = (pa.stateTimer >= 0.14f && pa.stateTimer <= 0.42f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 40.0f;
					// ★修正: ハンマーの当たり判定
					hb.size = {5.0f, 4.0f, currentTc.scale.z * 1.5f};
					hb.center.z = currentTc.scale.z * 0.75f;
				} else {
					hb.isActive = false;
					hb.center.z = 0.0f; // アイドル時は戻す
				}
			}

			// ★超重要: 変形（スケール変化）に合わせて、常に底面が地面にくっつくように物理の浮遊オフセットを動的に同期する
			if (registry.all_of<CharacterMovementComponent>(entity)) {
				auto& cm = registry.get<CharacterMovementComponent>(entity);
				float newOffset = currentTc.scale.y * 0.5f; 
				float diff = newOffset - cm.heightOffset;

				// 接地中であれば、オフセットが変化した分だけ自身のY座標も直接補正する。
				// これをしないと、潰れた瞬間に足元が浮いて重力がかかり、ガクガク（ジッター）する原因になる。
				if (cm.isGrounded) {
					currentTc.translate.y += diff;
				}
				
				cm.heightOffset = newOffset;
			}
		}

		// --- 弾の遅延生成 ---
		for (const auto& ps : pendingProjectiles_) {
			if (ctx.scene) {
				entt::entity proj = ctx.scene->CreateEntity("PlayerProjectile");
				auto& ptc = registry.get<TransformComponent>(proj);
				ptc.translate = ps.pos;
				ptc.rotate = ps.rot;
				ptc.scale = { 0.8f, 0.8f, 8.0f }; // さらに太く長い超高圧水流ビームに巨大化
				
				// 弾本体の初期化
				auto& mr = registry.emplace<MeshRendererComponent>(proj);
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.color = { 0.2f, 0.8f, 1.0f, 0.8f }; // 半透明の水色
				if (ctx.renderer) {
					mr.modelHandle = ctx.renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = ctx.renderer->LoadTexture2D(mr.texturePath);
				}
				
				auto& rb = registry.emplace<RigidbodyComponent>(proj);
				rb.useGravity = false;
				float speed = 120.0f; // 高速で飛ぶ
				rb.velocity = { ps.dir.x * speed, 0.0f, ps.dir.z * speed };
				
				auto& bc = registry.emplace<BoxColliderComponent>(proj);
				bc.size = { 2.0f, 2.0f, 8.0f }; // コライダーも拡大
				
				auto& hb = registry.emplace<HitboxComponent>(proj);
				hb.isActive = true;
				hb.damage = 30.0f; // ダメージ上昇
				hb.tag = TagType::Player;
				hb.size = { 3.0f, 3.0f, 10.0f }; // ヒットボックスも特大化
				hb.isProjectile = true; // ★追加
				
				// ★重要: タグは Projectile に戻し、GetEntitiesByTag(Player) で誤認されるのを防ぐ
				registry.emplace<TagComponent>(proj, TagType::Projectile);
				registry.emplace<AutoDestroyComponent>(proj).timer = 0.5f; // すぐに消える
				
				// 弾の軌跡用パーティクル（勢いのある水しぶき）
				auto& pe = registry.emplace<ParticleEmitterComponent>(proj);
				pe.emitter.params.name = "ProjTrail";
				pe.emitter.params.emitRate = 400; // 密度を倍増
				pe.emitter.params.lifeTime = 0.3f; // 少し長く残る
				pe.emitter.params.startColor = { 0.8f, 1.0f, 1.0f, 0.8f }; 
				pe.emitter.params.endColor = { 0.2f, 0.8f, 1.0f, 0.0f }; 
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f }; // パーティクルも巨大化
				pe.emitter.params.endSize = { 0.2f, 0.2f, 0.2f };
				pe.emitter.params.useBillboard = true; 
				pe.emitter.params.isAdditive = true; 
				pe.emitter.params.texturePath = "Resources/Textures/white1x1.png"; 
				pe.emitter.params.startVelocity = { -ps.dir.x * 10.0f, 2.0f, -ps.dir.z * 10.0f }; // 激しく散る
				pe.emitter.params.velocityVariance = { 2.0f, 2.0f, 2.0f }; // 散らばり具合をアップ
			}
		}
		pendingProjectiles_.clear();

		SpawnCanProjectiles(registry, ctx);
		SpawnAcidPools(registry, ctx);
		SpawnIceTrails(registry, ctx);
		SpawnMagnetEffects(registry, ctx);

		// --- 爆発エフェクトの遅延生成 ---
		for (const auto& exp : pendingExplosions_) {
			if (ctx.scene) {
				auto effectEntity = ctx.scene->CreateEntity("ExplosionEffectVisual");
				auto& eTc = registry.get<TransformComponent>(effectEntity);
				eTc.translate = exp.pos;
				
				auto& sc = registry.emplace<ScriptComponent>(effectEntity);
				ScriptEntry entry;
				entry.scriptPath = "HitEffectScript";
				entry.parameterData = "isExplosion=1";
				sc.scripts.push_back(entry);
				
				auto hitboxEntity = ctx.scene->CreateEntity("ExplosionHitbox");
				registry.emplace<TagComponent>(hitboxEntity, TagType::Player); 
				auto& hTc = registry.get<TransformComponent>(hitboxEntity);
				hTc.translate = exp.pos;

				auto& hb = registry.emplace<HitboxComponent>(hitboxEntity);
				hb.isActive = true;
				hb.size = {8.0f, 4.0f, 8.0f};
				hb.center = {0, 1.0f, 0};
				hb.damage = 40.0f;
				hb.tag = TagType::Player;
				hb.isProjectile = false;

				registry.emplace<AutoDestroyComponent>(hitboxEntity).timer = 0.5f;
			}
		}
		pendingExplosions_.clear();

		// --- ミストの遅延生成 ---
		for (const auto& mist : pendingMists_) {
			if (ctx.scene) {
				entt::entity m = ctx.scene->CreateEntity("MistEffect");
				auto& mtc = registry.get<TransformComponent>(m);
				mtc.translate = mist.pos;
				
				auto& pe = registry.emplace<ParticleEmitterComponent>(m);
				pe.emitter.params.name = "Mist";
				pe.emitter.params.emitRate = 0;       // 自動放出なし
				pe.emitter.params.burstCount = 40;    // 一気に放出
				pe.emitter.params.lifeTime = 0.6f;
				pe.emitter.params.startColor = { 0.8f, 0.9f, 1.0f, 0.5f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.2f, 0.2f, 0.2f };
				pe.emitter.params.endSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.startVelocity = { mist.dir.x * 3.0f, 0.5f, mist.dir.z * 3.0f };
				pe.emitter.params.velocityVariance = { 1.0f, 0.5f, 1.0f };
				registry.emplace<AutoDestroyComponent>(m).timer = 0.8f; // パーティクル消滅まで待つ
			}
		}
		pendingMists_.clear();

		for (const auto& gas : pendingSodaGas_) {
			if (ctx.scene) {
				entt::entity g = ctx.scene->CreateEntity("SodaGas");
				auto& gtc = registry.get<TransformComponent>(g);
				gtc.translate = gas.pos;

				auto& pe = registry.emplace<ParticleEmitterComponent>(g);
				pe.emitter.params.name = "SodaGas";
				pe.emitter.params.emitRate = 0;
				pe.emitter.params.burstCount = static_cast<int>(std::clamp(18.0f * gas.intensity, 8.0f, 36.0f));
				pe.emitter.params.lifeTime = 0.35f;
				pe.emitter.params.startColor = { 0.75f, 1.0f, 0.95f, 0.7f };
				pe.emitter.params.endColor = { 0.2f, 0.9f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.18f, 0.18f, 0.18f };
				pe.emitter.params.endSize = { 0.9f, 0.9f, 0.9f };
				pe.emitter.params.startVelocity = { gas.dir.x * 8.0f, gas.dir.y * 8.0f, gas.dir.z * 8.0f };
				pe.emitter.params.velocityVariance = { 1.5f, 1.0f, 1.5f };
				pe.emitter.params.acceleration = { 0.0f, 1.0f, 0.0f };
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.shaderName = "SoftParticleAdditive";
				pe.emitter.params.texturePath = "Resources/Textures/ball.png";
				registry.emplace<AutoDestroyComponent>(g).timer = 0.5f;
			}
		}
		pendingSodaGas_.clear();

		// --- 炎ブレスの遅延生成 (火炎放射エフェクト) ---
		for (const auto& fire : pendingFireBreaths_) {
			if (ctx.scene) {
				// パーティクル発生地点 (プレイヤーの少し前方、高さ1m)
				entt::entity flame = ctx.scene->CreateEntity("FlameEffect");
				auto& ftc = registry.get<TransformComponent>(flame);
				ftc.translate = { fire.pos.x + fire.dir.x * 1.5f, fire.pos.y + 1.0f, fire.pos.z + fire.dir.z * 1.5f };
				
				auto& pe = registry.emplace<ParticleEmitterComponent>(flame);
				pe.emitter.params.name = "Flamethrower";
				pe.emitter.params.emitRate = 60;       
				pe.emitter.params.burstCount = 0;      
				pe.emitter.params.lifeTime = 0.4f;     // 寿命を短くして炎っぽく
				
				// 加算ブレンドでは色が蓄積されるため、最初は明るい黄色〜オレンジ、最後は赤
				pe.emitter.params.startColor = { 1.0f, 0.8f, 0.2f, 1.0f }; 
				pe.emitter.params.endColor = { 1.0f, 0.1f, 0.0f, 0.0f };   
				
				// サイズは中くらいから大きく広がる
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f }; 
				pe.emitter.params.endSize = { 4.0f, 4.0f, 4.0f }; 
				
				pe.emitter.params.startVelocity = { fire.dir.x * 10.0f, 0.0f, fire.dir.z * 10.0f }; 
				pe.emitter.params.velocityVariance = { 2.0f, 1.0f, 2.0f }; 
				
				pe.emitter.params.acceleration = { 0.0f, 2.0f, 0.0f }; // 少し上に昇るようにする
				
				// ★修正: 黒い背景を透明にするため加算ブレンドを有効にする
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.shaderName = "SoftParticleAdditive";
				pe.emitter.params.texturePath = "Resources/Textures/ball.png"; 
				
				registry.emplace<AutoDestroyComponent>(flame).timer = 0.5f;

				// 火炎の当たり判定 (発生地点から前方に広い空間)
				auto hitboxEntity = ctx.scene->CreateEntity("FireHitbox");
				registry.emplace<TagComponent>(hitboxEntity, TagType::Player); 
				auto& hTc = registry.get<TransformComponent>(hitboxEntity);
				// Hitboxはパーティクルの発生地点よりさらに少し前に置く
				hTc.translate = { ftc.translate.x + fire.dir.x * 2.0f, ftc.translate.y, ftc.translate.z + fire.dir.z * 2.0f };

				auto& hb = registry.emplace<HitboxComponent>(hitboxEntity);
				hb.isActive = true;
				hb.size = {5.0f, 4.0f, 5.0f}; // 広範囲をカバー
				hb.center = {0, 0, 0};
				hb.damage = 1.5f; 
				hb.tag = TagType::Player;
				hb.isProjectile = false; 

				registry.emplace<AutoDestroyComponent>(hitboxEntity).timer = 0.1f;
			}
		}
		pendingFireBreaths_.clear();

		// ★追加: デコイの遅延生成
		for (const auto& decoy : pendingDecoys_) {
			if (ctx.scene) {
				entt::entity d = ctx.scene->CreateEntity("Decoy");
				auto& dtc = registry.get<TransformComponent>(d);
				dtc.translate = decoy.pos;
				dtc.rotate = decoy.rot;
				dtc.scale = decoy.scale;

				// デコイの見た目（1回だけ大量に流体パーティクルを放出する。毎フレーム出すと重くなるため）
				if (ctx.renderer) {
					Engine::Vector4 decoyColor = { 1.0f, 0.9f, 0.1f, 1.0f }; // 黄色
					// type = 2.0f として放出し、デコイ用の引力コアに集まるようにする
					ctx.renderer->EmitGPUFluid({ decoy.pos.x, decoy.pos.y + 1.0f, decoy.pos.z }, { 0, -2, 0 }, decoyColor, 2000, 2.0f);
				}
				// 敵に狙わせるために Player タグをつける
				registry.emplace<TagComponent>(d, TagType::Player);
				
				// デコイが無敵で数秒耐えるようにする
				auto& hc = registry.emplace<HealthComponent>(d);
				hc.hp = 9999.0f;
				hc.maxHp = 9999.0f;
				
				// 5秒で自動消滅
				registry.emplace<AutoDestroyComponent>(d).timer = 5.0f;

				// デコイオーラエフェクト
				auto& pe = registry.emplace<ParticleEmitterComponent>(d);
				pe.emitter.params.name = "DecoyAura";
				pe.emitter.params.emitRate = 40;
				pe.emitter.params.lifeTime = 0.6f;
				pe.emitter.params.startColor = { 1.0f, 0.9f, 0.2f, 0.8f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 0.0f, 0.0f };
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.endSize = { 0.1f, 0.1f, 0.1f };
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.texturePath = "Resources/Textures/ball.png";
			}
		}
		pendingDecoys_.clear();

		// ★追加: ワープエフェクトの遅延生成
		for (const auto& wPos : pendingWarpEffects_) {
			if (ctx.scene) {
				entt::entity m = ctx.scene->CreateEntity("WarpEffect");
				auto& mtc = registry.get<TransformComponent>(m);
				mtc.translate = wPos;
				
				auto& pe = registry.emplace<ParticleEmitterComponent>(m);
				pe.emitter.params.name = "WarpSparks";
				pe.emitter.params.emitRate = 0;       
				pe.emitter.params.burstCount = 50;    
				pe.emitter.params.lifeTime = 0.5f;
				pe.emitter.params.startColor = { 1.0f, 0.9f, 0.2f, 1.0f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.3f, 0.3f, 0.3f };
				pe.emitter.params.endSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.velocityVariance = { 5.0f, 5.0f, 5.0f };
				pe.emitter.params.isAdditive = true;
				registry.emplace<AutoDestroyComponent>(m).timer = 0.6f;
			}
		}
		pendingWarpEffects_.clear();

		// ★追加: 遅延させていた缶の切り替え処理を実行 (イテレータ無効化対策)
		for (const auto& change : pendingCanChanges_) {
			if (registry.valid(change.playerEntity) && registry.all_of<PlayerActionComponent, TransformComponent>(change.playerEntity)) {
				auto& pa = registry.get<PlayerActionComponent>(change.playerEntity);
				auto& tc = registry.get<TransformComponent>(change.playerEntity);
				ChangeCan(registry, change.playerEntity, pa, tc, change.newCan, ctx);
			}
		}
		pendingCanChanges_.clear();
	}

	void Reset(entt::registry& registry) override {
		prevAttack_ = false;
		prevHammer_ = false;
		prevDodge_ = false;

		auto view = registry.view<PlayerActionComponent>();
		for (auto entity : view) {
			auto& pa = registry.get<PlayerActionComponent>(entity);
			pa.state = PlayerActionState::Idle;
			pa.stateTimer = 0.0f;
			pa.chargeTimer = 0.0f;
			pa.hitStopTimer = 0.0f;
			pa.dodgeCooldown = 0.0f;
			pa.sodaGas = 100.0f;
			pa.sodaAimTimer = 0.0f;
			pa.sodaEmitTimer = 0.0f;
			pa.sodaAiming = false;
			pa.sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
			pa.liquefyBaseSpeed = 0.0f;
			pa.liquefySpeedApplied = false;
			pa.liquefyAnchorPos = {0.0f, 0.0f, 0.0f};
			pa.liquefyFlowDir = {0.0f, 0.0f, 1.0f};
			pa.liquefyInitialFlowSpeed = 0.0f;
			pa.liquefyLocked = false;
			pa.canFollowInitialized = false;
			pa.canRevealTimer = 1.0f;
			pa.canPrimaryCooldown = 0.0f;
			pa.canSecondaryCooldown = 0.0f;
			pa.iceTrailEmitTimer = 0.0f;
			if (pa.iceSlideApplied) RestoreIceSlide(registry, entity, pa);
			pa.magnetEffectTimer = 0.0f;
		}
	}

private:
	struct ProjectileSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 dir;
	};
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

	// ★追加: デコイ生成データ
	struct DecoySpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 scale;
	};
	std::vector<DecoySpawnData> pendingDecoys_;
	std::vector<DirectX::XMFLOAT3> pendingWarpEffects_;

	struct PendingCanChange {
		entt::entity playerEntity;
		CanType newCan;
	};
	std::vector<PendingCanChange> pendingCanChanges_;

	bool prevAttack_ = false;
	bool prevHammer_ = false;
	bool prevDodge_ = false;

	void BeginLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		RestoreLiquefySpeed(registry, entity, pa);
		pa.liquefyAnchorPos = tc.translate;
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (ctx.scene) {
				float groundHeight = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
				if (groundHeight > -5000.0f) {
					pa.liquefyAnchorPos.y = std::max(pa.liquefyAnchorPos.y, groundHeight + cm->heightOffset);
				}
			}
			cm->isGrounded = true;
		}
		pa.liquefyLocked = true;

		DirectX::XMFLOAT3 flowDir = GetCameraForward(tc, ctx);
		float flowSpeed = 0.0f;
		float inputLen = std::sqrt(pi.moveDir.x * pi.moveDir.x + pi.moveDir.y * pi.moveDir.y);
		if (inputLen > 0.01f) {
			flowDir = GetCameraMoveDirection(pi, ctx, tc);
			float moveSpeed = 0.0f;
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				moveSpeed = cm->speed;
			}
			flowSpeed = moveSpeed * std::min(1.0f, inputLen);
			tc.rotate.y = std::atan2(flowDir.x, flowDir.z);
		} else if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
			flowSpeed = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
			if (flowSpeed > 0.01f) {
				flowDir = {rb->velocity.x / flowSpeed, 0.0f, rb->velocity.z / flowSpeed};
			}
		}

		pa.liquefyFlowDir = flowDir;
		pa.liquefyInitialFlowSpeed = std::min(flowSpeed, 14.0f);
		pi.moveDir = {0.0f, 0.0f};
		pi.jumpRequested = false;
		if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
			rb->velocity.x = 0.0f;
			rb->velocity.y = 0.0f;
			rb->velocity.z = 0.0f;
		}
	}

	void EndLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		RestoreLiquefySpeed(registry, entity, pa);
		pa.liquefyLocked = false;
		pa.liquefyInitialFlowSpeed = 0.0f;
	}

	void ApplyLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.liquefySpeedApplied) {
				pa.liquefyBaseSpeed = cm->speed;
				pa.liquefySpeedApplied = true;
			}
			cm->speed = std::max(pa.liquefyBaseSpeed * 1.75f, pa.liquefyBaseSpeed + 4.0f);
		}
	}

	void RestoreLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (pa.liquefySpeedApplied) {
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->speed = pa.liquefyBaseSpeed;
			}
			pa.liquefyBaseSpeed = 0.0f;
			pa.liquefySpeedApplied = false;
		}
	}

	DirectX::XMFLOAT3 GetCameraForward(const TransformComponent& tc, GameContext& ctx) const {
		float yaw = tc.rotate.y;
		if (ctx.camera) {
			yaw = ctx.camera->Rotation().y;
		}
		return { std::sin(yaw), 0.0f, std::cos(yaw) };
	}

	DirectX::XMFLOAT3 GetCameraMoveDirection(const PlayerInputComponent& pi, GameContext& ctx, const TransformComponent& tc) const {
		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);
		if (len <= 0.001f) {
			return GetCameraForward(tc, ctx);
		}
		ix /= len;
		iz /= len;

		float yaw = tc.rotate.y;
		if (ctx.camera) {
			yaw = ctx.camera->Rotation().y;
		}
		float cy = std::cos(yaw);
		float sy = std::sin(yaw);
		return {
			ix * cy + iz * sy,
			0.0f,
			-ix * sy + iz * cy
		};
	}

	void ClampHorizontalVelocity(DirectX::XMFLOAT3& velocity, float maxSpeed) const {
		float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
		float maxSq = maxSpeed * maxSpeed;
		if (speedSq > maxSq) {
			float s = maxSpeed / std::sqrt(speedSq);
			velocity.x *= s;
			velocity.z *= s;
		}
	}

	void QueueSodaGas(const TransformComponent& tc, const DirectX::XMFLOAT3& exhaustDir, float intensity) {
		pendingSodaGas_.push_back({
			{ tc.translate.x, tc.translate.y + 0.25f, tc.translate.z },
			exhaustDir,
			intensity
		});
	}

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
		if (std::abs(pa.sodaDriftVelocity.x) > 0.01f || std::abs(pa.sodaDriftVelocity.z) > 0.01f) {
			tc.translate.x += pa.sodaDriftVelocity.x * ctx.dt;
			tc.translate.z += pa.sodaDriftVelocity.z * ctx.dt;
			float damp = std::max(0.0f, 1.0f - 3.8f * ctx.dt);
			pa.sodaDriftVelocity.x *= damp;
			pa.sodaDriftVelocity.z *= damp;
		}

		bool canUseGas = pa.state == PlayerActionState::Idle && !pi.isRadialMenuOpen;
		bool usingGas = false;
		pa.sodaEmitTimer += ctx.dt;

		if (!canUseGas) {
			pa.sodaAiming = false;
			pa.sodaAimTimer = 0.0f;
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 10.0f * ctx.dt);
			return;
		}

		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		auto* cm = registry.try_get<CharacterMovementComponent>(entity);

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

			if (pa.sodaEmitTimer >= 0.07f) {
				QueueSodaGas(tc, { -moveDir.x * 0.35f, -1.0f, -moveDir.z * 0.35f }, 0.9f);
				pa.sodaEmitTimer = 0.0f;
			}
		}

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

		if (!usingGas && !boostInput && !aimInput) {
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 12.0f * ctx.dt);
		}
	}

	DirectX::XMFLOAT3 GetAbilityDirection(entt::registry& registry, const PlayerInputComponent& pi,
		const TransformComponent& tc, GameContext& ctx) const {
		if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy) && registry.all_of<TransformComponent>(pi.lockedEnemy)) {
			const auto& targetTc = registry.get<TransformComponent>(pi.lockedEnemy);
			float dx = targetTc.translate.x - tc.translate.x;
			float dz = targetTc.translate.z - tc.translate.z;
			float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.001f) return {dx / len, 0.0f, dz / len};
		}
		return GetCameraForward(tc, ctx);
	}

	void ApplyIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.iceSlideApplied) {
				pa.iceSlideBaseSpeed = cm->speed;
				pa.iceSlideApplied = true;
			}
			cm->speed = (std::max)(pa.iceSlideBaseSpeed * 2.1f, pa.iceSlideBaseSpeed + 7.0f);
		}
	}

	void RestoreIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (!pa.iceSlideApplied) return;
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) cm->speed = pa.iceSlideBaseSpeed;
		pa.iceSlideBaseSpeed = 0.0f;
		pa.iceSlideApplied = false;
	}

	void HandleAdvancedCan(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa,
		const PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx,
		bool attackPressed, bool secondaryInput, bool secondaryPressed) {
		if (pi.isRadialMenuOpen || pa.state != PlayerActionState::Idle) {
			if (pa.iceSlideApplied) RestoreIceSlide(registry, entity, pa);
			return;
		}

		const DirectX::XMFLOAT3 dir = GetAbilityDirection(registry, pi, tc, ctx);
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

		if (pa.currentCan == CanType::Ice) {
			if (secondaryInput) {
				ApplyIceSlide(registry, entity, pa);
				pa.iceTrailEmitTimer -= ctx.dt;
				if (pa.iceTrailEmitTimer <= 0.0f) {
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
					if (health.isDead || !health.enabled) continue;
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
		} else if (pa.currentCan == CanType::Bubble) {
			if (secondaryPressed && pa.canSecondaryCooldown <= 0.0f) {
				auto& shield = registry.get_or_emplace<BubbleShieldComponent>(entity);
				shield.timer = 6.0f;
				shield.charges = 1;
				pa.canSecondaryCooldown = 9.0f;
			}
		}
	}

	void SpawnCanProjectiles(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingCanProjectiles_) {
			if (!ctx.scene) continue;
			auto projectile = ctx.scene->CreateEntity("CanProjectile");
			auto& tc = registry.get<TransformComponent>(projectile);
			tc.translate = spawn.pos;
			tc.rotate.y = std::atan2(spawn.dir.x, spawn.dir.z);
			tc.scale = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f} : DirectX::XMFLOAT3{0.62f, 0.62f, 0.62f};

			auto& mesh = registry.emplace<MeshRendererComponent>(projectile);
			mesh.modelPath = "Resources/Models/player_ball/ball.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.useCubemap = false;
			mesh.shaderName = spawn.type == CanType::Bubble ? "ForceField" : "EmissiveGlow";
			if (spawn.type == CanType::Ice) mesh.color = {0.34f, 0.88f, 1.0f, 0.95f};
			if (spawn.type == CanType::Acid) mesh.color = {0.56f, 1.0f, 0.08f, 0.95f};
			if (spawn.type == CanType::Bubble) mesh.color = {1.0f, 0.38f, 0.82f, 0.72f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}

			auto& rb = registry.emplace<RigidbodyComponent>(projectile);
			rb.useGravity = false;
			const float speed = spawn.type == CanType::Ice ? 68.0f : spawn.type == CanType::Acid ? 56.0f : 48.0f;
			rb.velocity = {spawn.dir.x * speed, 0.0f, spawn.dir.z * speed};
			auto& collider = registry.emplace<BoxColliderComponent>(projectile);
			collider.size = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{2.4f, 2.4f, 2.4f} : DirectX::XMFLOAT3{1.6f, 1.6f, 1.6f};
			collider.isTrigger = true;

			auto& hitbox = registry.emplace<HitboxComponent>(projectile);
			hitbox.isActive = true;
			hitbox.damage = spawn.type == CanType::Ice ? 12.0f : spawn.type == CanType::Acid ? 8.0f : 4.0f;
			hitbox.tag = TagType::Player;
			hitbox.size = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{3.4f, 3.4f, 3.4f} : DirectX::XMFLOAT3{2.6f, 3.2f, 2.6f};
			hitbox.isProjectile = true;
			registry.emplace<TagComponent>(projectile, TagType::Projectile);
			auto& effect = registry.emplace<CanAttackEffectComponent>(projectile);
			effect.canType = spawn.type;
			effect.duration = spawn.type == CanType::Ice ? 1.8f : spawn.type == CanType::Acid ? 6.0f : 5.5f;
			effect.strength = 1.0f;
			registry.emplace<AutoDestroyComponent>(projectile).timer = 1.4f;

			auto& particles = registry.emplace<ParticleEmitterComponent>(projectile);
			particles.emitter.params.name = "CanProjectileTrail";
			particles.emitter.params.emitRate = 75.0f;
			particles.emitter.params.lifeTime = 0.32f;
			particles.emitter.params.startSize = {0.38f, 0.38f, 0.38f};
			particles.emitter.params.endSize = {0.08f, 0.08f, 0.08f};
			particles.emitter.params.startVelocity = {-spawn.dir.x * 2.0f, 0.2f, -spawn.dir.z * 2.0f};
			particles.emitter.params.velocityVariance = {0.6f, 0.6f, 0.6f};
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
			if (spawn.type == CanType::Ice) {
				particles.emitter.params.startColor = {0.78f, 0.98f, 1.0f, 0.9f};
				particles.emitter.params.endColor = {0.16f, 0.58f, 1.0f, 0.0f};
			} else if (spawn.type == CanType::Acid) {
				particles.emitter.params.startColor = {0.62f, 1.0f, 0.12f, 0.9f};
				particles.emitter.params.endColor = {0.12f, 0.48f, 0.02f, 0.0f};
			} else {
				particles.emitter.params.startColor = {1.0f, 0.52f, 0.88f, 0.8f};
				particles.emitter.params.endColor = {0.32f, 0.78f, 1.0f, 0.0f};
			}
		}
		pendingCanProjectiles_.clear();
	}

	void SpawnAcidPools(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingAcidPools_) {
			if (!ctx.scene) continue;
			auto pool = ctx.scene->CreateEntity("AcidPool");
			auto& tc = registry.get<TransformComponent>(pool);
			tc.translate = spawn.pos;
			tc.scale = {6.0f, 0.10f, 6.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(pool);
			mesh.modelPath = "Resources/Models/Cylinder/cylinder.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "EmissiveGlow";
			mesh.useCubemap = false;
			mesh.color = {0.44f, 1.0f, 0.04f, 0.76f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			registry.emplace<TagComponent>(pool, TagType::VFX);
			registry.emplace<AcidPoolComponent>(pool).radius = 6.0f;
			registry.emplace<AutoDestroyComponent>(pool).timer = 6.0f;
			auto& light = registry.emplace<PointLightComponent>(pool);
			light.color = {0.42f, 1.0f, 0.06f};
			light.intensity = 2.4f;
			light.range = 11.0f;
			auto& particles = registry.emplace<ParticleEmitterComponent>(pool);
			particles.emitter.params.name = "AcidPoolBubbles";
			particles.emitter.params.emitRate = 64.0f;
			particles.emitter.params.shape = Engine::EmissionShape::Sphere;
			particles.emitter.params.shapeRadius = 5.2f;
			particles.emitter.params.lifeTime = 0.6f;
			particles.emitter.params.startColor = {0.62f, 1.0f, 0.08f, 0.8f};
			particles.emitter.params.endColor = {0.10f, 0.42f, 0.02f, 0.0f};
			particles.emitter.params.startSize = {0.28f, 0.28f, 0.28f};
			particles.emitter.params.endSize = {0.85f, 0.85f, 0.85f};
			particles.emitter.params.startVelocity = {0.0f, 1.5f, 0.0f};
			particles.emitter.params.velocityVariance = {0.6f, 0.5f, 0.6f};
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
		}
		pendingAcidPools_.clear();
	}

	void SpawnIceTrails(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingIceTrails_) {
			if (!ctx.scene) continue;
			auto trail = ctx.scene->CreateEntity("IceTrail");
			auto& tc = registry.get<TransformComponent>(trail);
			tc.translate = spawn.pos;
			tc.rotate.y = spawn.yaw;
			tc.scale = {1.35f, 0.045f, 2.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(trail);
			mesh.modelPath = "Resources/Models/cube/cube.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "EmissiveGlow";
			mesh.useCubemap = false;
			mesh.color = {0.34f, 0.86f, 1.0f, 0.52f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			registry.emplace<TagComponent>(trail, TagType::VFX);
			registry.emplace<AutoDestroyComponent>(trail).timer = 1.2f;
		}
		pendingIceTrails_.clear();
	}

	void SpawnMagnetEffects(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingMagnetEffects_) {
			if (!ctx.scene) continue;
			auto effect = ctx.scene->CreateEntity(spawn.pulse ? "MagnetRepulse" : "MagnetPullAura");
			auto& tc = registry.get<TransformComponent>(effect);
			tc.translate = spawn.pos;
			tc.scale = spawn.pulse ? DirectX::XMFLOAT3{15.0f, 0.24f, 15.0f} : DirectX::XMFLOAT3{20.0f, 0.12f, 20.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(effect);
			mesh.modelPath = "Resources/Models/player_ball/ball.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "ForceField";
			mesh.useCubemap = false;
			mesh.color = spawn.pulse ? DirectX::XMFLOAT4{0.88f, 0.28f, 1.0f, 0.48f} : DirectX::XMFLOAT4{0.38f, 0.55f, 1.0f, 0.35f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			registry.emplace<TagComponent>(effect, spawn.pulse ? TagType::Player : TagType::VFX);
			registry.emplace<AutoDestroyComponent>(effect).timer = spawn.pulse ? 0.24f : 0.20f;
			if (spawn.pulse) {
				auto& hitbox = registry.emplace<HitboxComponent>(effect);
				hitbox.isActive = true;
				hitbox.damage = 6.0f;
				hitbox.tag = TagType::Player;
				hitbox.size = {30.0f, 8.0f, 30.0f};
				auto& canEffect = registry.emplace<CanAttackEffectComponent>(effect);
				canEffect.canType = CanType::Magnet;
				canEffect.strength = 14.0f;
			}
			auto& particles = registry.emplace<ParticleEmitterComponent>(effect);
			particles.emitter.params.name = spawn.pulse ? "MagnetRepulseParticles" : "MagnetPullParticles";
			particles.emitter.params.emitRate = 0.0f;
			particles.emitter.params.burstCount = spawn.pulse ? 48 : 12;
			particles.emitter.params.lifeTime = 0.35f;
			particles.emitter.params.startColor = {0.86f, 0.32f, 1.0f, 0.9f};
			particles.emitter.params.endColor = {0.22f, 0.56f, 1.0f, 0.0f};
			particles.emitter.params.startSize = {0.22f, 0.22f, 0.22f};
			particles.emitter.params.endSize = {0.72f, 0.72f, 0.72f};
			if (spawn.pulse) {
				particles.emitter.params.velocityVariance = {7.0f, 2.0f, 7.0f};
			} else {
				particles.emitter.params.velocityVariance = {9.0f, 2.0f, 9.0f};
			}
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
		}
		pendingMagnetEffects_.clear();
	}

	// ★追加: 缶の切り替え処理
	void ChangeCan(entt::registry& registry, entt::entity playerEntity, PlayerActionComponent& pa, TransformComponent& ptc, CanType newCan, GameContext& ctx) {
		if (pa.canEntity != entt::null && registry.valid(pa.canEntity)) {
			if (ctx.scene) ctx.scene->DestroyObject(static_cast<uint32_t>(pa.canEntity));
		}
		if (pa.iceSlideApplied) RestoreIceSlide(registry, playerEntity, pa);
		pa.currentCan = newCan;
		pa.canEntity = entt::null;
		pa.canFollowPos = ptc.translate;
		pa.canFollowInitialized = true;

		if (newCan != CanType::None) {
			if (ctx.scene) {
				entt::entity can = ctx.scene->CreateEntity("PlayerCan");
				pa.canEntity = can;
				
				auto& tc = registry.get<TransformComponent>(can);
				tc.scale = {0.3f, 0.4f, 0.3f};
				tc.translate = ptc.translate;
				tc.translate.y += 0.35f; // Keep it inside the slime from its first frame.
				
				auto& mr = registry.emplace<MeshRendererComponent>(can);
				mr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.useCubemap = true;
				if (newCan == CanType::Soda) {
					mr.color = {0.25f, 1.0f, 0.85f, 1.0f};
				}

				// ★追加: 缶を地面判定レイキャストに引っかからないようにVFXタグを付与
				registry.emplace<TagComponent>(can, TagType::VFX);
				
				if (newCan == CanType::Fire) {
					mr.color = {1.0f, 0.2f, 0.1f, 1.0f}; // 赤
				} else if (newCan == CanType::Water) {
					mr.color = {0.2f, 0.5f, 1.0f, 1.0f}; // 青
				} else if (newCan == CanType::Thunder) {
					mr.color = {1.0f, 0.9f, 0.1f, 1.0f}; // 黄
				} else if (newCan == CanType::Ice) {
					mr.color = {0.35f, 0.88f, 1.0f, 1.0f};
				} else if (newCan == CanType::Magnet) {
					mr.color = {0.78f, 0.28f, 1.0f, 1.0f};
				} else if (newCan == CanType::Acid) {
					mr.color = {0.55f, 1.0f, 0.10f, 1.0f};
				} else if (newCan == CanType::Bubble) {
					mr.color = {1.0f, 0.38f, 0.78f, 1.0f};
				}
				
				if (ctx.renderer) {
					mr.modelHandle = ctx.renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = ctx.renderer->LoadTexture2D(mr.texturePath);
				}
			}
		}
	}

	void TransitionTo(PlayerActionComponent& pa, PlayerActionState newState, float duration) {
		pa.state = newState;
		pa.stateTimer = 0.0f;
		pa.stateDuration = duration;
	}

	void StartDodge(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		TransitionTo(pa, PlayerActionState::Dodge, pa.dodgeDuration);
		pa.dodgeCooldown = 0.42f;

		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);

		if (len > 0.01f && ctx.camera) {
			auto camRot = ctx.camera->Rotation();
			float cy = std::cos(camRot.y);
			float sy = std::sin(camRot.y);
			float dx = ix * cy + iz * sy;
			float dz = -ix * sy + iz * cy;
			pa.dodgeDirection = { dx, 0.0f, dz };
		} else {
			float facing = tc.rotate.y;
			pa.dodgeDirection = { -std::sin(facing), 0.0f, -std::cos(facing) };
		}
	}
};

} // namespace Game
