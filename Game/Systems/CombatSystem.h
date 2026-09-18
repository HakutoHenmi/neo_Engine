#pragma once
#include "ISystem.h"
#include "EnemyAISystem.h" // ★追加: パリィ成功時の敵AI連携
#include <cmath>
#include <unordered_set>

namespace Game {

// ★ CombatSystem: Hitbox vs Hurtbox の衝突判定 + パリィ処理
class CombatSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// --- パリィ歪みエフェクトのアニメーション ---
		// ※RingEffectScriptへ移行したため削除


		// --- 前フレームのヒット済みペアをクリア ---
		hitPairs_.clear();

		// --- 全Hitboxエンティティを収集 ---
		auto hitboxView = registry.view<HitboxComponent, TransformComponent>();
		auto hurtboxView = registry.view<HurtboxComponent, TransformComponent>();
		std::vector<std::function<void()>> deferredActions;

		for (auto hbEntity : hitboxView) {
			auto& hb = hitboxView.get<HitboxComponent>(hbEntity);
			if (!hb.enabled || !hb.isActive) continue;

			auto& hbTc = hitboxView.get<TransformComponent>(hbEntity);

			float hbSin = std::sin(hbTc.rotate.y);
			float hbCos = std::cos(hbTc.rotate.y);
			DirectX::XMFLOAT3 hbWorldCenter = {
				hbTc.translate.x + (hb.center.x * hbCos + hb.center.z * hbSin),
				hbTc.translate.y + hb.center.y,
				hbTc.translate.z + (-hb.center.x * hbSin + hb.center.z * hbCos)
			};
			DirectX::XMFLOAT3 hbSweepSize = {0.0f, 0.0f, 0.0f};
			if (registry.all_of<CanAttackEffectComponent, RigidbodyComponent>(hbEntity)) {
				const auto& rb = registry.get<RigidbodyComponent>(hbEntity);
				const DirectX::XMFLOAT3 frameMove = {
					rb.velocity.x * ctx.dt,
					rb.velocity.y * ctx.dt,
					rb.velocity.z * ctx.dt
				};
				// 高速な缶弾がフレーム間で敵を通り抜けないよう、直前位置まで判定を伸ばす。
				hbWorldCenter.x -= frameMove.x * 0.5f;
				hbWorldCenter.y -= frameMove.y * 0.5f;
				hbWorldCenter.z -= frameMove.z * 0.5f;
				hbSweepSize = {std::abs(frameMove.x), std::abs(frameMove.y), std::abs(frameMove.z)};
			}

			// Hitboxの所有者のタグを取得
			TagType hbTag = TagType::Untagged;
			if (registry.all_of<TagComponent>(hbEntity)) {
				hbTag = registry.get<TagComponent>(hbEntity).tag;
			}
			// ★修正: 弾（Projectile）の攻撃判定は、プレイヤー陣営（Player）として扱うことで自分自身へのヒットを防ぐ
			if (hbTag == TagType::Projectile) {
				hbTag = TagType::Player;
			}

			for (auto hrEntity : hurtboxView) {
				// 自分自身とは衝突しない
				if (hbEntity == hrEntity) continue;

				auto& hr = hurtboxView.get<HurtboxComponent>(hrEntity);
				if (!hr.enabled) continue;

				// 同じタグ同士は衝突しない（味方同士の攻撃を防ぐ）
				TagType hrTag = TagType::Untagged;
				if (registry.all_of<TagComponent>(hrEntity)) {
					hrTag = registry.get<TagComponent>(hrEntity).tag;
				}
				if (hbTag == hrTag && hbTag != TagType::Untagged) continue;

				auto& hrTc = hurtboxView.get<TransformComponent>(hrEntity);

				// Hurtbox のワールド座標
				float hrSin = std::sin(hrTc.rotate.y);
				float hrCos = std::cos(hrTc.rotate.y);
				DirectX::XMFLOAT3 hrWorldCenter = {
					hrTc.translate.x + (hr.center.x * hrCos + hr.center.z * hrSin),
					hrTc.translate.y + hr.center.y,
					hrTc.translate.z + (-hr.center.x * hrSin + hr.center.z * hrCos)
				};

				// 回転を加味したAABBサイズの再計算（OBBを包むAABB）
				DirectX::XMFLOAT3 hbWorldSize = {
					std::abs(hb.size.x * hbCos) + std::abs(hb.size.z * hbSin) + hbSweepSize.x,
					hb.size.y + hbSweepSize.y,
					std::abs(hb.size.x * hbSin) + std::abs(hb.size.z * hbCos) + hbSweepSize.z
				};
				DirectX::XMFLOAT3 hrWorldSize = {
					std::abs(hr.size.x * hrCos) + std::abs(hr.size.z * hrSin),
					hr.size.y,
					std::abs(hr.size.x * hrSin) + std::abs(hr.size.z * hrCos)
				};

				// --- AABB衝突判定 ---
				if (!AABBOverlap(hbWorldCenter, hbWorldSize, hrWorldCenter, hrWorldSize)) continue;

				// ★追加: 既にこの攻撃でヒット済みの対象ならスキップ
				if (std::find(hb.hitTargets.begin(), hb.hitTargets.end(), hrEntity) != hb.hitTargets.end()) {
					continue;
				}

				// ★修正: 弾の場合は、ダメージが通る通らない（敵の無敵時間）に関わらず、触れた時点で即座に消滅させる
				if (hb.isProjectile) {
					if (registry.all_of<AutoDestroyComponent>(hbEntity)) {
						registry.get<AutoDestroyComponent>(hbEntity).timer = 0.0f;
					}
				}

				// --- 通常ダメージ処理 ---
				DirectX::XMFLOAT3 hitDir = {
					hrWorldCenter.x - hbWorldCenter.x,
					0.35f,
					hrWorldCenter.z - hbWorldCenter.z
				};
				float hitDirLen = std::sqrt(hitDir.x * hitDir.x + hitDir.y * hitDir.y + hitDir.z * hitDir.z);
				if (hitDirLen > 0.001f) {
					hitDir.x /= hitDirLen;
					hitDir.y /= hitDirLen;
					hitDir.z /= hitDirLen;
				} else {
					hitDir = { std::sin(hbTc.rotate.y), 0.35f, std::cos(hbTc.rotate.y) };
				}

				const auto* canEffect = registry.try_get<CanAttackEffectComponent>(hbEntity);
				const CanType incomingCanType = canEffect ? canEffect->canType : CanType::None;
				bool hitSuccess = ApplyDamage(registry, hrEntity, hb.damage * hr.damageMultiplier, ctx,
					hrWorldCenter, hitDir, hbTag, incomingCanType);
				if (canEffect) {
					// 状態弾は接触そのものを成功とし、敵の無敵時間中でも固有効果を与える。
					ApplyCanEffect(registry, hrEntity, hbWorldCenter, *canEffect);
				}

				// 無敵時間などでダメージが適用されなかった場合は、履歴に残さず（後で当たるように）スキップ
				if (!hitSuccess && !canEffect) continue;

				// ダメージが通ったのでヒット履歴に記録
				hb.hitTargets.push_back(hrEntity);
				
				uint64_t pairKey = MakePairKey(hbEntity, hrEntity);
				hitPairs_.insert(pairKey);

					// 攻撃側のヒットストップ
					if (registry.all_of<PlayerActionComponent>(hbEntity)) {
						auto& attackerPa = registry.get<PlayerActionComponent>(hbEntity);
						attackerPa.hitStopTimer = 0.08f;
					}

					// カメラシェイク（軽い）
					if (ctx.camera) {
						ctx.camera->StartShake(0.3f, 0.15f); // 時間, 振幅
					}

					// ★追加: ヒットエフェクト（パーティクル等）の生成
					// プレイヤーの攻撃（スライム攻撃）がヒットした時のみエフェクトを出す
					if (hb.tag == TagType::Player) {
						Engine::Vector3 attackDir = {
							std::sin(hbTc.rotate.y),
							0.0f,
							std::cos(hbTc.rotate.y)
						};
						DirectX::XMFLOAT3 spawnPos = {
							(hbWorldCenter.x + hrWorldCenter.x) * 0.5f - attackDir.x * 1.5f,
							(hbWorldCenter.y + hrWorldCenter.y) * 0.5f,
							(hbWorldCenter.z + hrWorldCenter.z) * 0.5f - attackDir.z * 1.5f
						};
						
						bool isExplosionAttack = false;
						if (registry.all_of<NameComponent>(hbEntity)) {
							if (registry.get<NameComponent>(hbEntity).name == "ExplosionHitbox") {
								isExplosionAttack = true;
							}
						}
						
						bool isProj = hb.isProjectile;

						deferredActions.push_back([&registry, spawnPos, attackDir, isExplosionAttack, isProj]() {
							auto effectEntity = registry.create();
							registry.emplace<NameComponent>(effectEntity, "HitEffect");
							auto& tc = registry.emplace<TransformComponent>(effectEntity);
							tc.translate = spawnPos;

							auto& sc = registry.emplace<ScriptComponent>(effectEntity);
							ScriptEntry entry;
							entry.scriptPath = "HitEffectScript";

							if (isExplosionAttack) {
								entry.parameterData = "isExplosionHit=1";
							} else {
								// ★追加: 近接攻撃や水流ビーム（弾）ヒット時に液体スプラッターを出す
								entry.parameterData = "isLiquidSplatter=1,dirX=" + std::to_string(attackDir.x) + ",dirZ=" + std::to_string(attackDir.z);
							}
							sc.scripts.push_back(entry);
						});
					}
			}
		}

		// ループ後に生成処理を実行
		for (auto& action : deferredActions) {
			action();
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		hitPairs_.clear();
	}

private:
	std::unordered_set<uint64_t> hitPairs_; // 1フレーム内の重複ヒット防止

	// 2エンティティのペアキーを生成（順序無関係）
	static uint64_t MakePairKey(entt::entity a, entt::entity b) {
		uint32_t ai = static_cast<uint32_t>(a);
		uint32_t bi = static_cast<uint32_t>(b);
		if (ai > bi) std::swap(ai, bi);
		return (static_cast<uint64_t>(ai) << 32) | static_cast<uint64_t>(bi);
	}

	// AABB同士の重なり判定
	static bool AABBOverlap(const DirectX::XMFLOAT3& c1, const DirectX::XMFLOAT3& s1,
	                         const DirectX::XMFLOAT3& c2, const DirectX::XMFLOAT3& s2) {
		float hx1 = s1.x * 0.5f, hy1 = s1.y * 0.5f, hz1 = s1.z * 0.5f;
		float hx2 = s2.x * 0.5f, hy2 = s2.y * 0.5f, hz2 = s2.z * 0.5f;
		return (std::abs(c1.x - c2.x) < hx1 + hx2) &&
		       (std::abs(c1.y - c2.y) < hy1 + hy2) &&
		       (std::abs(c1.z - c2.z) < hz1 + hz2);
	}

	// パリィ機能は削除されました
	void ApplyCanEffect(entt::registry& registry, entt::entity target,
		const DirectX::XMFLOAT3& sourcePos, const CanAttackEffectComponent& effect) {
		if (auto* part = registry.try_get<BodyPartComponent>(target)) {
			if (registry.valid(part->parentEntity)) target = part->parentEntity;
		}
		if (!registry.valid(target) || !registry.all_of<TagComponent, TransformComponent>(target)) return;
		if (registry.get<TagComponent>(target).tag != TagType::Enemy) return;

		auto& targetTc = registry.get<TransformComponent>(target);
		if (effect.canType == CanType::Magnet) {
			float dx = targetTc.translate.x - sourcePos.x;
			float dz = targetTc.translate.z - sourcePos.z;
			float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.001f) {
				const float strength = registry.all_of<BossActionComponent>(target) ? effect.strength * 0.35f : effect.strength;
				targetTc.translate.x += (dx / len) * strength;
				targetTc.translate.z += (dz / len) * strength;
			}
			return;
		}

		auto& status = registry.get_or_emplace<CanStatusComponent>(target);
		if (effect.canType == CanType::Ice) {
			const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
			status.freezeTimer = (std::max)(status.freezeTimer, duration);
		} else if (effect.canType == CanType::Acid) {
			status.acidTimer = (std::max)(status.acidTimer, effect.duration);
			status.acidTickTimer = 0.0f;
		} else if (effect.canType == CanType::Bubble) {
			const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
			status.bubbleTimer = (std::max)(status.bubbleTimer, duration);
			if (!status.bubblePositionSaved) {
				status.bubbleBaseY = targetTc.translate.y;
				status.bubblePositionSaved = true;
			}
		}
	}

	// ダメージ適用 (成功したらtrue)
	void SpawnLostFluidPickups(entt::registry& registry, entt::entity owner, float amount,
	                           const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitDir,
	                           GameContext& ctx) {
		if (amount <= 0.0f) return;

		const int pickupCount = std::clamp(static_cast<int>(std::ceil(amount * 0.5f)), 1, 24);
		const float hpPerPickup = amount / static_cast<float>(pickupCount);
		auto hash01 = [](uint32_t n) {
			n = (n << 13U) ^ n;
			n = n * (n * n * 15731U + 789221U) + 1376312589U;
			return static_cast<float>(n & 0x7fffffffU) / static_cast<float>(0x7fffffffU);
		};
		DirectX::XMFLOAT3 origin = hitPos;
		if (registry.valid(owner) && registry.all_of<TransformComponent>(owner)) {
			const auto& ownerTc = registry.get<TransformComponent>(owner);
			origin = {ownerTc.translate.x, ownerTc.translate.y + 0.85f, ownerTc.translate.z};
		}

		uint32_t firstVisualGroupId = 0;
		if (ctx.renderer) {
			Engine::Vector3 pos = {origin.x, origin.y, origin.z};
			Engine::Vector3 vel = {hitDir.x * 4.0f, 3.0f, hitDir.z * 4.0f};
			firstVisualGroupId = ctx.renderer->ExtractGPUFluidFromPlayer(pos, vel, pickupCount * 10);
		}

		for (int i = 0; i < pickupCount; ++i) {
			float t = static_cast<float>(i) / static_cast<float>(pickupCount);
			float angle = t * 6.283185307f + (hash01(static_cast<uint32_t>(i) * 193U + 7U) - 0.5f) * 0.75f;
			float lift = 0.22f + hash01(static_cast<uint32_t>(i) * 271U + 11U) * 0.32f;
			float dist = 0.45f + hash01(static_cast<uint32_t>(i) * 313U + 17U) * 0.35f;
			float outX = std::cos(angle);
			float outY = lift;
			float outZ = std::sin(angle);
			float outLen = std::sqrt(outX * outX + outY * outY + outZ * outZ);
			if (outLen > 0.001f) {
				outX /= outLen;
				outY /= outLen;
				outZ /= outLen;
			}

			auto pickup = registry.create();
			registry.emplace<NameComponent>(pickup, "LostFluidPickup");

			auto& tc = registry.emplace<TransformComponent>(pickup);
			tc.translate = {
				origin.x + outX * dist,
				origin.y + outY * dist,
				origin.z + outZ * dist
			};

			auto& lf = registry.emplace<LostFluidPickupComponent>(pickup);
			lf.owner = owner;
			lf.hpRestore = hpPerPickup;
			lf.staminaRestore = hpPerPickup * 0.75f;
			lf.visualGroupId = firstVisualGroupId + static_cast<uint32_t>(i);
			lf.velocity = {
				hitDir.x * 2.4f + outX * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f),
				3.0f + outY * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f),
				hitDir.z * 2.4f + outZ * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f)
			};

			registry.emplace<TagComponent>(pickup, TagType::VFX);
		}
	}

	bool ApplyDamage(entt::registry& registry, entt::entity target, float damage, GameContext& ctx,
	                 DirectX::XMFLOAT3 hitPos = {0.0f, 0.0f, 0.0f},
	                 DirectX::XMFLOAT3 hitDir = {0.0f, 0.35f, 1.0f},
	                 TagType attackerTag = TagType::Untagged,
	                 CanType incomingCanType = CanType::None) {
		// --- ★追加: 部位破壊コンポーネント（BodyPart）がある場合 ---
		if (registry.all_of<BodyPartComponent>(target)) {
			auto& part = registry.get<BodyPartComponent>(target);
			if (part.isDestroyed) return false; // すでに破壊済みなら無視

			// サンドバッグモードかつ敵ならHPを減らさない
			bool isEnemy = false;
			if (registry.valid(part.parentEntity) && registry.all_of<TagComponent>(part.parentEntity)) {
				isEnemy = (registry.get<TagComponent>(part.parentEntity).tag == TagType::Enemy);
			} else if (registry.all_of<TagComponent>(target)) {
				isEnemy = (registry.get<TagComponent>(target).tag == TagType::Enemy);
			}

			if (!(ctx.isSandbagMode && isEnemy)) {
				part.hp -= damage;
			}
			
			if (part.hp <= 0.0f) {
				part.hp = 0.0f;
				part.isDestroyed = true;
				
				// 部位破壊時のイベント：親（ボス）を大ダウン状態にする
				if (registry.valid(part.parentEntity) && registry.all_of<BossActionComponent>(part.parentEntity)) {
					auto& boss = registry.get<BossActionComponent>(part.parentEntity);
					boss.state = BossState::Down;
					boss.stateTimer = 0.0f;
				}
				
				// 破壊エフェクトやHurtbox無効化
				if (registry.all_of<HurtboxComponent>(target)) {
					registry.get<HurtboxComponent>(target).enabled = false;
				}
			}

			// 親エンティティ（HealthComponent持ち）にダメージを伝播させる
			if (registry.valid(part.parentEntity) && registry.all_of<HealthComponent>(part.parentEntity)) {
				ApplyDamage(registry, part.parentEntity, damage * part.damageMultiplierToParent, ctx,
					hitPos, hitDir, attackerTag, incomingCanType);
			}
			return true; // 部位自体の処理はここで終わり
		}

		if (attackerTag == TagType::Player) {
			if (const auto* status = registry.try_get<CanStatusComponent>(target)) {
				if (status->acidTimer > 0.0f) damage *= 1.25f;
			}
		}

		// --- 既存の本体HealthComponentの処理 ---
		if (!registry.all_of<HealthComponent>(target)) return false;
		auto& hc = registry.get<HealthComponent>(target);

		// 無敵時間中はダメージを受けない
		if (hc.invincibleTime > 0.0f) return false;

		bool isRealPlayer = registry.all_of<PlayerInputComponent>(target);
		if (isRealPlayer && attackerTag == TagType::Enemy) {
			if (auto* shield = registry.try_get<BubbleShieldComponent>(target)) {
				if (shield->timer > 0.0f && shield->charges > 0) {
					shield->charges--;
					shield->timer = 0.0f;
					hc.invincibleTime = 0.35f;
					hc.hitFlashTimer = 0.08f;
					if (ctx.camera) ctx.camera->StartShake(0.16f, 0.22f);
					return true;
				}
			}
		}
		if (isRealPlayer && registry.all_of<PlayerActionComponent>(target)) {
			auto& pa = registry.get<PlayerActionComponent>(target);
			if (pa.state == PlayerActionState::Dodge || pa.state == PlayerActionState::Liquefy) {
				return false;
			}
		}

		// サンドバッグモードかつ敵ならHPを減らさない
		bool isEnemyBase = false;
		if (registry.all_of<TagComponent>(target)) {
			isEnemyBase = (registry.get<TagComponent>(target).tag == TagType::Enemy);
		}

		if (attackerTag == TagType::Player && isEnemyBase) {
			if (auto* status = registry.try_get<CanStatusComponent>(target)) {
				if (status->freezeTimer > 0.0f && incomingCanType != CanType::Ice) {
					// 凍結中の次の攻撃で氷を砕き、攻撃力に応じた追加ダメージを与える。
					damage += (std::max)(18.0f, damage * 0.5f);
					status->freezeTimer = 0.0f;
					hc.hitFlashTimer = (std::max)(hc.hitFlashTimer, 0.16f);
					hc.hitStopTimer = (std::max)(hc.hitStopTimer, 0.10f);
					if (ctx.camera) ctx.camera->StartShake(0.18f, 0.28f);
				}

				if (status->bubbleTimer > 0.0f && incomingCanType != CanType::Bubble) {
					// 泡は次の攻撃で破裂し、敵を攻撃方向へ吹き飛ばす。
					status->bubbleTimer = 0.0f;
					if (status->bubblePositionSaved) {
						if (auto* targetTc = registry.try_get<TransformComponent>(target)) {
							float restoreY = status->bubbleBaseY;
							auto* movement = registry.try_get<CharacterMovementComponent>(target);
							if (ctx.scene) {
								const float rayStartY = (std::max)(targetTc->translate.y, status->bubbleBaseY) + 4.0f;
								const float groundY = ctx.scene->GetHeightAt(targetTc->translate.x, targetTc->translate.z,
									rayStartY, static_cast<uint32_t>(target));
								if (groundY > -5000.0f) {
									restoreY = groundY + (movement ? movement->heightOffset : 0.0f);
									if (movement) movement->isGrounded = true;
								}
							}
							targetTc->translate.y = restoreY;
						}
						status->bubblePositionSaved = false;
					}
					const bool isBoss = registry.all_of<BossActionComponent>(target);
					const float pushDistance = isBoss ? 9.0f : 12.0f;
					if (auto* targetTc = registry.try_get<TransformComponent>(target)) {
						targetTc->translate.x += hitDir.x * pushDistance;
						targetTc->translate.z += hitDir.z * pushDistance;
						// 吹き飛ばした先の地形へ再度スナップし、段差で床下へ入らないようにする。
						if (ctx.scene) {
							auto* movement = registry.try_get<CharacterMovementComponent>(target);
							const float groundY = ctx.scene->GetHeightAt(targetTc->translate.x, targetTc->translate.z,
								(std::max)(targetTc->translate.y, status->bubbleBaseY) + 8.0f,
								static_cast<uint32_t>(target));
							if (groundY > -5000.0f) {
								targetTc->translate.y = groundY + (movement ? movement->heightOffset : 0.0f);
								if (movement) movement->isGrounded = true;
							}
						}
					}
					if (auto* targetRb = registry.try_get<RigidbodyComponent>(target)) {
						const float pushSpeed = isBoss ? 10.0f : 18.0f;
						targetRb->velocity.x += hitDir.x * pushSpeed;
						targetRb->velocity.y = 0.0f;
						targetRb->velocity.z += hitDir.z * pushSpeed;
					}
					hc.hitStopTimer = (std::max)(hc.hitStopTimer, 0.08f);
					if (ctx.camera) ctx.camera->StartShake(0.16f, 0.22f);
				}
			}
		}
		
		float appliedDamage = 0.0f;
		if (!(ctx.isSandbagMode && isEnemyBase)) {
			float beforeHp = hc.hp;
			hc.hp = (std::max)(0.0f, hc.hp - damage);
			appliedDamage = (std::max)(0.0f, beforeHp - hc.hp);
		}

		if (isRealPlayer && appliedDamage > 0.0f) {
			hc.damageTakenCount++;
			float missingHp = (std::max)(0.0f, hc.maxHp - hc.hp);
			hc.recoverableFluid = (std::min)(missingHp, hc.recoverableFluid + appliedDamage);
			SpawnLostFluidPickups(registry, target, appliedDamage, hitPos, hitDir, ctx);
		}
		hc.hitFlashTimer = (std::max)(hc.hitFlashTimer, 0.1f); // ヒットフラッシュ演出
		hc.hitStopTimer = (std::max)(hc.hitStopTimer, 0.05f); // 被弾側の軽いヒットストップ

		// 被弾後の短い無敵時間
		hc.invincibleTime = 0.2f;

		// ダメージイベント
		if (ctx.eventSystem) {
			ctx.eventSystem->Emit("OnDamage", static_cast<float>(static_cast<uint32_t>(target)));
		}

		// プレイヤーが被弾した場合、のけぞりステートへ
		if (registry.all_of<PlayerActionComponent>(target)) {
			auto& pa = registry.get<PlayerActionComponent>(target);
			pa.state = PlayerActionState::Stagger;
			pa.stateTimer = 0.0f;
			pa.stateDuration = 0.3f;
		}

        return true;
	}
};

} // namespace Game
