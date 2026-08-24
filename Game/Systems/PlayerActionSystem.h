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


	float dodgeDuration = 0.4f;
	float dodgeSpeed = 15.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;
	float totalTime = 0.0f; // ★追加: ふわふわ用タイマー

	CanType currentCan = CanType::None; // ★追加: 現在滞在している缶
	entt::entity canEntity = entt::null; // ★追加: 缶のエンティティID
	float canDropProgress = 1.0f; // ★追加: 0.0 -> 1.0 (落ちてくるアニメーション用)
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

			// ★追加: 缶の追従とアニメーション
			if (pa.canEntity != entt::null && registry.valid(pa.canEntity)) {
				auto& canTc = registry.get<TransformComponent>(pa.canEntity);
				
				// 落下アニメーション
				if (pa.canDropProgress < 1.0f) {
					pa.canDropProgress += ctx.dt * 2.0f; // 0.5秒で落ちる
					if (pa.canDropProgress > 1.0f) pa.canDropProgress = 1.0f;
				}
				
				// イージング
				float t = pa.canDropProgress;
				float easeT = 1.0f - std::pow(1.0f - t, 3.0f); // easeOutCubic
				
				// ふわふわ
				float hoverOffset = std::sin(pa.totalTime * 3.0f) * 0.1f;
				
				// 目標位置 (プレイヤーの中心やや上)
				DirectX::XMFLOAT3 targetPos = tc.translate;
				targetPos.y += 0.8f + hoverOffset; // 体内(または少し上)
				
				if (pa.canDropProgress < 1.0f) {
					// 落下中
					float startY = tc.translate.y + 3.0f;
					canTc.translate.x = tc.translate.x;
					canTc.translate.z = tc.translate.z;
					canTc.translate.y = startY + (targetPos.y - startY) * easeT;
				} else {
					// 追従
					canTc.translate = targetPos;
				}
				
				// 回転
				canTc.rotate.y += ctx.dt * 2.0f;
				canTc.rotate.z = std::sin(pa.totalTime * 2.0f) * 0.2f;
			}


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

			bool dashInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			bool dashPressed = dashInput && !prevDodge_;
			prevDodge_ = dashInput;

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
					tc.scale.x = 0.9f;
					tc.scale.y = 1.2f;
					tc.scale.z = 0.9f;
				} else if (isMoving) {
					// 移動中：地面を這うように平べったく進む（跳ねない）
					// 高さは一定にしつつ、XとZを少し伸縮させて這っている感を出します
					float slither = std::sin(pa.stateTimer * 12.0f);
					tc.scale.x = 1.3f - slither * 0.05f;
					tc.scale.y = 0.65f; // 高さを固定（跳ねない）
					tc.scale.z = 1.3f + slither * 0.05f;
				} else {
					// 待機時：ゆっくり呼吸しておまんじゅう型
					float breathe = std::sin(pa.stateTimer * 3.0f);
					tc.scale.x = 1.2f + breathe * 0.05f;
					tc.scale.y = 0.8f - breathe * 0.05f;
					tc.scale.z = 1.2f + breathe * 0.05f;
				}

				if (liquefyInput) {
					BeginLiquefy(registry, entity, pa, pi, tc, ctx);
					TransitionTo(pa, PlayerActionState::Liquefy, 0.0f); // 長押し状態
				} else if (dashPressed && pa.dodgeCooldown <= 0.0f) {
					StartDodge(pa, pi, tc, ctx);
				} else if (attackPressed) {
					TransitionTo(pa, PlayerActionState::Charging, 0.0f); // 溜め状態へ移行
				} else if (hammerPressed) {
					TransitionTo(pa, PlayerActionState::SlimeHammer, 1.0f);
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
					} else { // 短い場合は通常の近接攻撃
						if (pa.currentCan == CanType::Fire) {
							// ★追加: 火炎の缶なら専用の炎攻撃へ
							TransitionTo(pa, PlayerActionState::FireBreath, 1.0f); // 1秒間の放射
						} else {
							TransitionTo(pa, PlayerActionState::SlimeSpike, 0.4f);
						}
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
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
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

				bool isMoving = pa.liquefyInitialFlowSpeed > 0.1f;
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
				tc.translate.x += pa.dodgeDirection.x * pa.dodgeSpeed * ctx.dt;
				tc.translate.z += pa.dodgeDirection.z * pa.dodgeSpeed * ctx.dt;

				// シュッと伸び縮みしながら移動
				float progress = pa.stateTimer / pa.stateDuration;
				float stretch = std::sin(progress * 3.14159f); 
				tc.scale.x = 1.2f - (stretch * 0.5f);
				tc.scale.y = 0.8f - (stretch * 0.3f);
				tc.scale.z = 1.2f + (stretch * 1.5f);

				// 無敵時間
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					if (pa.stateTimer >= 0.05f && pa.stateTimer <= 0.3f) {
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
					hb.isActive = (pa.stateTimer >= 0.05f && pa.stateTimer <= 0.3f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 30.0f; // ダメージ上昇
					// ★修正: 当たり判定(Hitbox)を、プレイヤーの根本から先端までカバーするように設定
					hb.size = {1.5f, 1.5f, currentTc.scale.z * 1.5f}; // サイズを伸ばす
					hb.center.z = currentTc.scale.z * 0.75f; // サイズの半分だけ前方にシフト
				} else if (pa.state == PlayerActionState::SlimeHammer) {
					bool wasActive = hb.isActive;
					// ★修正: ハンマー攻撃も判定発生を早め、長めに残す
					hb.isActive = (pa.stateTimer >= 0.3f && pa.stateTimer <= 0.7f);
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
		}
	}

private:
	struct ProjectileSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 dir;
	};
	std::vector<ProjectileSpawnData> pendingProjectiles_;

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

	// ★追加: 缶の切り替え処理
	void ChangeCan(entt::registry& registry, entt::entity /*playerEntity*/, PlayerActionComponent& pa, TransformComponent& ptc, CanType newCan, GameContext& ctx) {
		if (pa.canEntity != entt::null && registry.valid(pa.canEntity)) {
			if (ctx.scene) ctx.scene->DestroyObject(static_cast<uint32_t>(pa.canEntity));
		}
		pa.currentCan = newCan;
		pa.canEntity = entt::null;
		pa.canDropProgress = 0.0f;

		if (newCan != CanType::None) {
			if (ctx.scene) {
				entt::entity can = ctx.scene->CreateEntity("PlayerCan");
				pa.canEntity = can;
				
				auto& tc = registry.get<TransformComponent>(can);
				tc.scale = {0.3f, 0.4f, 0.3f};
				tc.translate = ptc.translate;
				tc.translate.y += 3.0f; // 上空から
				
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
		pa.dodgeCooldown = 0.6f;

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
