#pragma once
#include "ISystem.h"
#include "../Scenes/GameScene.h"
#include "EnemyAISystem.h"
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
					// ダメージポップアップの生成
					auto dmgEntity = registry.create();
					auto& dnc = registry.emplace<DamageNumberComponent>(dmgEntity);
					dnc.damage = diff;
					dnc.lifetime = 1.0f;
					dnc.maxLifetime = 1.0f;
					// プレイヤーなら赤、敵なら白
					if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Player) {
						dnc.color = {1.0f, 0.2f, 0.2f};
					} else {
						dnc.color = {1.0f, 1.0f, 1.0f};
					}
					// 発生位置
					if (registry.all_of<TransformComponent>(entity)) {
						const auto& tc = registry.get<TransformComponent>(entity);
						dnc.startPos = {tc.translate.x, tc.translate.y + 1.5f, tc.translate.z};
					}
				}
			}
			lastHp_[eid] = hc.hp;

			if (hc.hp <= 0.0f && !hc.isDead) {
				bool isEnemy = false;
				if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Enemy) {
					isEnemy = true;
				}

				if (isEnemy && registry.all_of<MeshRendererComponent>(entity)) {
					auto& mr = registry.get<MeshRendererComponent>(entity);
					if (mr.shaderName != "Dissolve") {
						mr.shaderName = "Dissolve";
						mr.color.w = 1.0f; // ディゾルブ開始は不透明から

						if (!registry.all_of<AutoDestroyComponent>(entity)) {
							registry.emplace<AutoDestroyComponent>(entity).timer = 1.5f; // 1.5秒かけて消滅
						}

						// 攻撃・被弾判定の無効化
						if (registry.all_of<HurtboxComponent>(entity)) registry.get<HurtboxComponent>(entity).enabled = false;
						if (registry.all_of<HitboxComponent>(entity)) registry.get<HitboxComponent>(entity).isActive = false;
						if (registry.all_of<BoxColliderComponent>(entity)) registry.get<BoxColliderComponent>(entity).enabled = false;
						
						// AI更新停止
						if (registry.all_of<EnemyAIComponent>(entity)) registry.get<EnemyAIComponent>(entity).enabled = false;

						hc.enabled = false; // 以降のダメージ処理を止める（isDeadはfalseのまま残す）
					}
				} else {
					hc.isDead = true;
				}
			}
		}

		// --- ダメージ数字コンポーネントの更新 ---
		auto dmgView = registry.view<DamageNumberComponent>();
		for (auto entity : dmgView) {
			auto& dnc = dmgView.get<DamageNumberComponent>(entity);
			dnc.lifetime -= ctx.dt;
			if (dnc.lifetime <= 0.0f) {
				if (ctx.scene) {
					ctx.scene->DestroyObject(static_cast<uint32_t>(entity));
				}
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
