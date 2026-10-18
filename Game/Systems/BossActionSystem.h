#pragma once
#include "../Scripts/WarningEffectScript.h"
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <cmath>

namespace Game {

class BossActionSystem : public ISystem {
public:
	static void ChangeState(entt::registry& registry, entt::entity entity, BossState next) {
		auto& boss = registry.get<BossActionComponent>(entity);
		if (boss.state == BossState::Dead && next != BossState::Idle)
			return;
		EntityStateContext frame{registry, entity};
		GetState(boss.state).Exit(frame);
		boss.state = next;
		boss.stateTimer = 0.0f;
		GetState(next).Enter(frame);
	}

	void Update(entt::registry& registry, GameContext& inputContext) override {
		GameContext ctx = inputContext;
		if (ctx.combatFlow && ctx.combatFlow->Combo() >= 2) ctx.dt *= ctx.combatFlow->EnemyScale();
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
			if (const auto* status = registry.try_get<CanStatusComponent>(entity)) {
				if (status->freezeTimer > 0.0f || status->bubbleTimer > 0.0f) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = false;
					if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
						if (auto* beamHitbox = registry.try_get<HitboxComponent>(boss.currentBeamEntity)) beamHitbox->isActive = false;
					}
					continue;
				}
			}
			if (boss.state == BossState::Attack) {
				if (auto* hitbox = registry.try_get<HitboxComponent>(entity))
					hitbox->isActive = true;
				if (registry.valid(boss.currentBeamEntity)) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(boss.currentBeamEntity))
						hitbox->isActive = true;
				}
			}

			// ★追加: サンドバッグモード時はボスの行動（AI更新）を停止する
			if (ctx.isSandbagMode) continue;

			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			if ((boss.state == BossState::Run || boss.state == BossState::Chase || boss.state == BossState::WindUp || boss.state == BossState::Attack || boss.state == BossState::Cooldown) &&
			    !HasPattern(boss)) {
				ChangeState(registry, entity, BossState::Idle);
				continue;
			}
			boss.stateTimer += ctx.dt;
			boss.turnDirection = 0.0f;

			StateContext frame{*this, registry, entity, ctx, playerPos, dx, dz, dist, targetAngle};
			GetState(boss.state).Update(frame);
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<BossActionComponent>();
		for (auto entity : view) {
			const auto beam = registry.get<BossActionComponent>(entity).currentBeamEntity;
			ChangeState(registry, entity, BossState::Idle);
			if (registry.valid(beam))
				registry.destroy(beam);
		}
	}

private:

	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt, BossActionComponent& boss) {
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		boss.turnDirection = diff;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}

	static void ShowWarningEffect(entt::registry& registry, entt::entity entity, float duration) {
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
#include "../States/BossActionStates.inl"
};

} // namespace Game
