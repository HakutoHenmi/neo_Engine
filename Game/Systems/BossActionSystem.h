#pragma once
#include "ISystem.h"
#include <cmath>
#include "../Scripts/WarningEffectScript.h"

namespace Game {

class BossActionSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// プレイヤーの位置を取得
		DirectX::XMFLOAT3 playerPos = {0, 0, 0};
		bool playerFound = false;
		auto playerView = registry.view<TagComponent, TransformComponent>();
		for (auto e : playerView) {
			if (playerView.get<TagComponent>(e).tag == TagType::Player) {
				playerPos = playerView.get<TransformComponent>(e).translate;
				playerFound = true;
				break;
			}
		}
		if (!playerFound) return;

		auto view = registry.view<BossActionComponent, TransformComponent>();
		for (auto entity : view) {
			auto& boss = view.get<BossActionComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!boss.enabled) continue;
			
			// ★追加: サンドバッグモード時はボスの行動（AI更新）を停止する
			if (ctx.isSandbagMode) continue;

			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			boss.stateTimer += ctx.dt;
			boss.turnDirection = 0.0f;

			switch (boss.state) {
			case BossState::Idle:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed, ctx.dt, boss);
				if (boss.stateTimer > 1.0f) { // 待機時間
					// アタックパターンを選ぶ (距離ベースなどのロジックを入れる)
					if (!boss.patterns.empty()) {
						boss.currentPatternIndex = rand() % boss.patterns.size();
						auto& p = boss.patterns[boss.currentPatternIndex];
						if (dist > p.range) {
							TransitionTo(boss, BossState::Chase);
						} else {
							TransitionTo(boss, BossState::WindUp);
							ShowWarningEffect(registry, entity, p.windUpDuration);
						}
					}
				}
				break;

			case BossState::Chase:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed, ctx.dt, boss);
				if (dist > 0.5f) {
					float nx = dx / dist;
					float nz = dz / dist;
					tc.translate.x += nx * boss.chaseSpeed * ctx.dt;
					tc.translate.z += nz * boss.chaseSpeed * ctx.dt;
				}
				if (boss.currentPatternIndex >= 0) {
					if (dist <= boss.patterns[boss.currentPatternIndex].range) {
						TransitionTo(boss, BossState::WindUp);
						ShowWarningEffect(registry, entity, boss.patterns[boss.currentPatternIndex].windUpDuration);
					}
				}
				break;

			case BossState::WindUp:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed * 0.3f, ctx.dt, boss);
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					if (boss.stateTimer >= p.windUpDuration) {
						TransitionTo(boss, BossState::Attack);
						if (registry.all_of<HitboxComponent>(entity)) {
							auto& hb = registry.get<HitboxComponent>(entity);
							hb.isActive = true;
							hb.damage = p.damage;
							hb.hitTargets.clear(); // 多段ヒット防止の履歴をクリア
						}
					}
				}
				break;

			case BossState::Attack:
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					// 前進力（攻撃の前半だけ前進させることで、攻撃後の不自然な滑りを防止）
					if (boss.stateTimer < p.activeDuration * 0.5f) {
						float facing = tc.rotate.y;
						tc.translate.x += std::sin(facing) * p.thrustForce * ctx.dt;
						tc.translate.z += std::cos(facing) * p.thrustForce * ctx.dt;
					}

					if (boss.stateTimer >= p.activeDuration) {
						TransitionTo(boss, BossState::Cooldown);
						if (registry.all_of<HitboxComponent>(entity)) {
							registry.get<HitboxComponent>(entity).isActive = false;
						}
					}
				}
				break;

			case BossState::Cooldown:
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					if (boss.stateTimer >= p.recoveryDuration) {
						TransitionTo(boss, BossState::Idle);
						boss.currentPatternIndex = -1;
					}
				}
				break;

			case BossState::Stunned:
				if (boss.stateTimer >= boss.stunDuration) {
					TransitionTo(boss, BossState::Idle);
					boss.currentPatternIndex = -1;
				}
				break;

			case BossState::Down:
				// 部位破壊時などの大ダウン
				if (boss.stateTimer >= boss.stunDuration * 2.0f) {
					TransitionTo(boss, BossState::Idle);
					boss.currentPatternIndex = -1;
				}
				break;

			case BossState::Dead:
				// 何もしない
				break;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<BossActionComponent>();
		for (auto entity : view) {
			auto& boss = view.get<BossActionComponent>(entity);
			boss.state = BossState::Idle;
			boss.stateTimer = 0.0f;
			boss.currentPatternIndex = -1;
			if (registry.all_of<HitboxComponent>(entity)) {
				registry.get<HitboxComponent>(entity).isActive = false;
			}
		}
	}

private:
	void TransitionTo(BossActionComponent& boss, BossState newState) {
		boss.state = newState;
		boss.stateTimer = 0.0f;
	}

	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt, BossActionComponent& boss) {
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		boss.turnDirection = diff;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}

	void ShowWarningEffect(entt::registry& registry, entt::entity entity, float duration) {
		if (!registry.all_of<ScriptComponent>(entity)) {
			registry.emplace<ScriptComponent>(entity);
		}
		auto& sc = registry.get<ScriptComponent>(entity);
		bool hasWarning = false;
		for (const auto& entry : sc.scripts) {
			if (entry.scriptPath == "WarningEffectScript") hasWarning = true;
		}
		if (!hasWarning) {
			sc.scripts.push_back({"WarningEffectScript", "{ \"duration\": " + std::to_string(duration) + " }", std::make_shared<WarningEffectScript>(), false});
		}
	}
};

} // namespace Game
