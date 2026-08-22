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
					std::abs(hb.size.x * hbCos) + std::abs(hb.size.z * hbSin),
					hb.size.y,
					std::abs(hb.size.x * hbSin) + std::abs(hb.size.z * hbCos)
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

				bool hitSuccess = ApplyDamage(registry, hrEntity, hb.damage * hr.damageMultiplier, ctx, hrWorldCenter, hitDir, hbTag);

				// 無敵時間などでダメージが適用されなかった場合は、履歴に残さず（後で当たるように）スキップ
				if (!hitSuccess) continue;

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
	                 TagType attackerTag = TagType::Untagged) {
		(void)attackerTag;
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
				ApplyDamage(registry, part.parentEntity, damage * part.damageMultiplierToParent, ctx, hitPos, hitDir, attackerTag);
			}
			return true; // 部位自体の処理はここで終わり
		}

		// --- 既存の本体HealthComponentの処理 ---
		if (!registry.all_of<HealthComponent>(target)) return false;
		auto& hc = registry.get<HealthComponent>(target);

		// 無敵時間中はダメージを受けない
		if (hc.invincibleTime > 0.0f) return false;

		bool isRealPlayer = registry.all_of<PlayerInputComponent>(target);
		if (isRealPlayer && registry.all_of<PlayerActionComponent>(target)) {
			auto& pa = registry.get<PlayerActionComponent>(target);
			if (pa.state == PlayerActionState::Dodge) {
				return false;
			}
		}

		// サンドバッグモードかつ敵ならHPを減らさない
		bool isEnemyBase = false;
		if (registry.all_of<TagComponent>(target)) {
			isEnemyBase = (registry.get<TagComponent>(target).tag == TagType::Enemy);
		}
		
		float appliedDamage = 0.0f;
		if (!(ctx.isSandbagMode && isEnemyBase)) {
			float beforeHp = hc.hp;
			hc.hp = (std::max)(0.0f, hc.hp - damage);
			appliedDamage = (std::max)(0.0f, beforeHp - hc.hp);
		}

		if (isRealPlayer && appliedDamage > 0.0f) {
			float missingHp = (std::max)(0.0f, hc.maxHp - hc.hp);
			hc.recoverableFluid = (std::min)(missingHp, hc.recoverableFluid + appliedDamage);
			SpawnLostFluidPickups(registry, target, appliedDamage, hitPos, hitDir, ctx);
		}
		hc.hitFlashTimer = 0.1f; // ヒットフラッシュ演出
		hc.hitStopTimer = 0.05f; // 被弾側の軽いヒットストップ

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
