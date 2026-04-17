#pragma once
#include "ISystem.h"
#include <unordered_map>

namespace Game {

class HealthSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			if (!hc.enabled || hc.isDead) continue;

			// 演出タイマーの更新
			if (hc.hitFlashTimer > 0.0f) {
				hc.hitFlashTimer -= ctx.dt;
				if (registry.all_of<MeshRendererComponent>(entity)) {
					auto& mr = registry.get<MeshRendererComponent>(entity);
					
					// 初回ヒット時に元の色を保存
					if (!hc.baseColorSaved) {
						hc.baseColor = mr.color;
						hc.baseColorSaved = true;
					}

					if (hc.hitFlashTimer <= 0.0f) {
						hc.hitFlashTimer = 0.0f;
						mr.color = hc.baseColor; // 元の色に戻す
					} else {
						// フラッシュ中（白く光らせる）
						mr.color = { 2.0f, 2.0f, 2.0f, 1.0f }; 
					}
				}
			}

			if (hc.hitStopTimer > 0.0f) {
				hc.hitStopTimer -= ctx.dt;
				if (hc.hitStopTimer < 0.0f) hc.hitStopTimer = 0.0f;
			}

			if (hc.invincibleTime > 0.0f) {
				hc.invincibleTime -= ctx.dt;
				if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
			}

			// ダメージ検知用の簡易ロジック
			uint32_t eid = static_cast<uint32_t>(entity);
			if (lastHp_.find(eid) != lastHp_.end()) {
				float diff = lastHp_[eid] - hc.hp;
				if (diff > 0.1f) {
					// ダメージポップアップ（簡易版、WorldSpaceUIコンポーネントが存在する場合）
				}
			}
			lastHp_[eid] = hc.hp;

			if (hc.hp <= 0.0f && !hc.isDead) {
				hc.isDead = true;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			hc.invincibleTime = 0.0f;
			hc.isDead = false;
			if (hc.hp <= 0) hc.hp = hc.maxHp;
		}
		lastHp_.clear();
	}

private:
	std::unordered_map<uint32_t, float> lastHp_;
};

} // namespace Game
