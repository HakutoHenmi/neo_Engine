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

				// --- 通常ダメージ処理 ---
					// --- 通常ダメージ処理 ---
					bool hitSuccess = ApplyDamage(registry, hrEntity, hb.damage * hr.damageMultiplier, ctx);

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

					// ★追加: ヒットエフェクト（パーティクル）の生成
					auto effectEntity = registry.create();
					auto& tc = registry.emplace<TransformComponent>(effectEntity);
					tc.translate = {
						(hbWorldCenter.x + hrWorldCenter.x) * 0.5f,
						(hbWorldCenter.y + hrWorldCenter.y) * 0.5f,
						(hbWorldCenter.z + hrWorldCenter.z) * 0.5f
					};
					auto& sc = registry.emplace<ScriptComponent>(effectEntity);
					ScriptEntry entry;
					entry.scriptPath = "HitEffectScript";
					sc.scripts.push_back(entry);
			}
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
	bool ApplyDamage(entt::registry& registry, entt::entity target, float damage, GameContext& ctx) {
		// --- ★追加: 部位破壊コンポーネント（BodyPart）がある場合 ---
		if (registry.all_of<BodyPartComponent>(target)) {
			auto& part = registry.get<BodyPartComponent>(target);
			if (part.isDestroyed) return false; // すでに破壊済みなら無視

			part.hp -= damage;
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
				ApplyDamage(registry, part.parentEntity, damage * part.damageMultiplierToParent, ctx);
			}
			return true; // 部位自体の処理はここで終わり
		}

		// --- 既存の本体HealthComponentの処理 ---
		if (!registry.all_of<HealthComponent>(target)) return false;
		auto& hc = registry.get<HealthComponent>(target);

		// 無敵時間中はダメージを受けない
		if (hc.invincibleTime > 0.0f) return false;

		hc.hp -= damage;
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
			if (pa.state != PlayerActionState::Dodge) { // 回避中は被弾しない
				pa.state = PlayerActionState::Stagger;
				pa.stateTimer = 0.0f;
				pa.stateDuration = 0.3f;
			} else {
                // 回避中のためダメージ無効
                // (すでにinvincibleTimeで弾かれているはずだが、念のため)
                return false;
            }
		}

        return true;
	}
};

} // namespace Game
