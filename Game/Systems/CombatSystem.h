// CombatSystem.h
// 攻撃判定 (Hitbox) と被弾判定 (Hurtbox) の当たり判定と、ダメージ・缶の効果・ヒット演出を行うシステム。
#pragma once
#include "../CanHitEffects.h"
#include "BossActionSystem.h"
#include "EnemyAISystem.h" // 敵 AI の状態を参照する
#include "ISystem.h"
#include "PlayerActionSystem.h"
#include <cmath>
#include <unordered_set>

namespace Game {

// CombatSystem: 攻撃判定と被弾判定の当たり判定とダメージ処理
class CombatSystem : public ISystem {
public:
	// PLAY 中、有効な攻撃判定と被弾判定の全組み合わせを調べる
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: ヒット済みの組を消す
	void Reset(entt::registry& /*registry*/) override {
		hitPairs_.clear();
	}

private:
	std::unordered_set<uint64_t> hitPairs_; // 1フレーム内の重複ヒット防止

	// 2エンティティのペアキーを生成（順序無関係）
	// 小さい方を上位 32 ビット、大きい方を下位 32 ビットに詰める
	static uint64_t MakePairKey(entt::entity a, entt::entity b) {
		uint32_t ai = static_cast<uint32_t>(a);
		uint32_t bi = static_cast<uint32_t>(b);
		if (ai > bi) std::swap(ai, bi);
		return (static_cast<uint64_t>(ai) << 32) | static_cast<uint64_t>(bi);
	}

	// AABB同士の重なり判定
	static bool AABBOverlap(const DirectX::XMFLOAT3& c1, const DirectX::XMFLOAT3& s1,
	                         const DirectX::XMFLOAT3& c2, const DirectX::XMFLOAT3& s2) {
		// 両方の半分の大きさ
		float hx1 = s1.x * 0.5f, hy1 = s1.y * 0.5f, hz1 = s1.z * 0.5f;
		float hx2 = s2.x * 0.5f, hy2 = s2.y * 0.5f, hz2 = s2.z * 0.5f;
		// 全軸で中心の距離が半分の大きさの和より短ければ重なっている
		return (std::abs(c1.x - c2.x) < hx1 + hx2) &&
		       (std::abs(c1.y - c2.y) < hy1 + hy2) &&
		       (std::abs(c1.z - c2.z) < hz1 + hz2);
	}

	// 缶の効果を敵に与える (部位なら親へ。缶ごとの効果が無ければ状態異常だけ付ける)
	void ApplyCanEffect(entt::registry& registry, entt::entity target,
		const DirectX::XMFLOAT3& sourcePos, const CanAttackEffectComponent& effect) {
		// 部位に当たったら親 (ボス) を対象にする
		if (auto* part = registry.try_get<BodyPartComponent>(target)) {
			if (registry.valid(part->parentEntity)) target = part->parentEntity;
		}
		// 敵でなければ何もしない
		if (!registry.valid(target) || !registry.all_of<TagComponent, TransformComponent>(target)) return;
		if (registry.get<TagComponent>(target).tag != TagType::Enemy) return;

		// 缶ごとの命中の効果があれば与え、無ければ状態異常のコンポーネントだけ付ける
		if (const auto* behavior = FindCanHitEffect(effect.canType)) {
			behavior->Apply(registry, target, sourcePos, effect);
		} else {
			(void)registry.get_or_emplace<CanStatusComponent>(target);
		}
	}

	// プレイヤーが被弾したとき、落とした液体 (後で回収すると回復する) を周りへ飛び散らせる
	void SpawnLostFluidPickups(entt::registry& registry, entt::entity owner, float amount,
	                           const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitDir,
	                           GameContext& ctx) {
		if (amount <= 0.0f) return;

		// 落とす量に応じて 1〜24 個に分け、1 個あたりの回復量を決める
		const int pickupCount = std::clamp(static_cast<int>(std::ceil(amount * 0.5f)), 1, 24);
		const float hpPerPickup = amount / static_cast<float>(pickupCount);
		// 整数から 0〜1 の疑似乱数を作る
		auto hash01 = [](uint32_t n) {
			n = (n << 13U) ^ n;
			n = n * (n * n * 15731U + 789221U) + 1376312589U;
			return static_cast<float>(n & 0x7fffffffU) / static_cast<float>(0x7fffffffU);
		};
		// 持ち主の体の中心から出す
		DirectX::XMFLOAT3 origin = hitPos;
		if (registry.valid(owner) && registry.all_of<TransformComponent>(owner)) {
			const auto& ownerTc = registry.get<TransformComponent>(owner);
			origin = {ownerTc.translate.x, ownerTc.translate.y + 0.85f, ownerTc.translate.z};
		}

		// 見た目の GPU 流体をプレイヤーから取り出す (1 個あたり 10 粒)
		uint32_t firstVisualGroupId = 0;
		if (ctx.renderer) {
			Engine::Vector3 pos = {origin.x, origin.y, origin.z};
			Engine::Vector3 vel = {hitDir.x * 4.0f, 3.0f, hitDir.z * 4.0f};
			firstVisualGroupId = ctx.renderer->ExtractGPUFluidFromPlayer(pos, vel, pickupCount * 10);
		}

		// 周りに均等に、少しずつずらした角度で飛ばす
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

			// 液体のエンティティを作る
			auto pickup = registry.create();
			registry.emplace<NameComponent>(pickup, "LostFluidPickup");

			auto& tc = registry.emplace<TransformComponent>(pickup);
			tc.translate = {
				origin.x + outX * dist,
				origin.y + outY * dist,
				origin.z + outZ * dist
			};

			// 回収したときの回復量と、見た目の流体のまとまりの番号
			auto& lf = registry.emplace<LostFluidPickupComponent>(pickup);
			lf.owner = owner;
			lf.hpRestore = hpPerPickup;
			lf.staminaRestore = hpPerPickup * 0.75f;
			lf.visualGroupId = firstVisualGroupId + static_cast<uint32_t>(i);
			// 攻撃の向きに押されながら外へ飛ぶ速度
			lf.velocity = {
				hitDir.x * 2.4f + outX * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f),
				3.0f + outY * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f),
				hitDir.z * 2.4f + outZ * (7.5f + hash01(static_cast<uint32_t>(i) * 401U + 29U) * 2.5f)
			};

			registry.emplace<TagComponent>(pickup, TagType::VFX);
		}
	}

	// ダメージを与える。実際に通ったら true を返す (部位・シールド・回避・凍結や泡の追加効果を含む)
	bool ApplyDamage(entt::registry& registry, entt::entity target, float damage, GameContext& ctx,
	                 DirectX::XMFLOAT3 hitPos = {0.0f, 0.0f, 0.0f},
	                 DirectX::XMFLOAT3 hitDir = {0.0f, 0.35f, 1.0f},
	                 TagType attackerTag = TagType::Untagged,
	                 CanType incomingCanType = CanType::None) {
		// 部位 (BodyPart) の場合
		if (registry.all_of<BodyPartComponent>(target)) {
			auto& part = registry.get<BodyPartComponent>(target);
			if (part.isDestroyed) return false; // すでに破壊済みなら無視

			// サンドバッグモードかつ敵ならHPを減らさない
			// 部位の持ち主 (ボス) が敵か
			bool isEnemy = false;
			if (registry.valid(part.parentEntity) && registry.all_of<TagComponent>(part.parentEntity)) {
				isEnemy = (registry.get<TagComponent>(part.parentEntity).tag == TagType::Enemy);
			} else if (registry.all_of<TagComponent>(target)) {
				isEnemy = (registry.get<TagComponent>(target).tag == TagType::Enemy);
			}

			if (!(ctx.isSandbagMode && isEnemy)) {
				part.hp -= damage;
			}
			
			// 部位の体力が尽きたら破壊する
			if (part.hp <= 0.0f) {
				part.hp = 0.0f;
				part.isDestroyed = true;
				
				// 部位破壊時のイベント：親（ボス）を大ダウン状態にする
				if (registry.valid(part.parentEntity) && registry.all_of<BossActionComponent>(part.parentEntity)) {
					BossActionSystem::ChangeState(registry, part.parentEntity, BossState::Down);
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

		// 酸の状態の敵はプレイヤーの攻撃で 1.25 倍のダメージを受ける
		if (attackerTag == TagType::Player) {
			if (const auto* status = registry.try_get<CanStatusComponent>(target)) {
				if (status->acidTimer > 0.0f) damage *= 1.25f;
			}
		}

		// 本体の HealthComponent の処理
		if (!registry.all_of<HealthComponent>(target)) return false;
		auto& hc = registry.get<HealthComponent>(target);

		// 無敵時間中はダメージを受けない
		if (hc.invincibleTime > 0.0f) return false;

		// プレイヤーは泡のシールドがあれば 1 回分だけ敵の攻撃を防ぐ
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
		// 回避中・液状化中のプレイヤーはダメージを受けない
		if (isRealPlayer && registry.all_of<PlayerActionComponent>(target)) {
			auto& pa = registry.get<PlayerActionComponent>(target);
			if (pa.state == PlayerActionState::Dodge || pa.state == PlayerActionState::Liquefy) {
				return false;
			}
		}

		// サンドバッグモードかつ敵ならHPを減らさない
		// 被弾側が敵か
		bool isEnemyBase = false;
		if (registry.all_of<TagComponent>(target)) {
			isEnemyBase = (registry.get<TagComponent>(target).tag == TagType::Enemy);
		}

		// プレイヤーの攻撃が状態異常の敵に当たったときの追加の効果
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
					// 泡で浮かせていた高さを地面へ戻す
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
					// ボスは少し、雑魚は大きく攻撃の向きへ押し出す
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
					// 吹き飛ばす速度も与える
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
		
		// 体力を減らし (サンドバッグモードの敵は減らさない)、実際に減った量を求める
		float appliedDamage = 0.0f;
		if (!(ctx.isSandbagMode && isEnemyBase)) {
			float beforeHp = hc.Hp();
			hc.ApplyDamage(damage);
			appliedDamage = (std::max)(0.0f, beforeHp - hc.Hp());
		}

		// プレイヤーが被弾したら、減った分の液体を落として後で回収できるようにする
		if (isRealPlayer && appliedDamage > 0.0f) {
			hc.damageTakenCount++;
			float missingHp = (std::max)(0.0f, hc.MaxHp() - hc.Hp());
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
			PlayerActionSystem::ChangeState(registry, target, PlayerActionState::Stagger, 0.3f);
		}

        return true;
	}
};

} // namespace Game
