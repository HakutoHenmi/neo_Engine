#pragma once
#include "../Scripts/WarningEffectScript.h"
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <cmath>

namespace Game {

// ★ 敵AIの状態
enum class EnemyAIState : uint32_t {
	Idle = 0,      // 待機（プレイヤーを向く）
	Chase,         // 追跡（距離が遠い場合）
	WindUp,        // 攻撃予備動作（パリィ猶予の目印）
	Attack,        // 攻撃中（Hitbox有効）
	Cooldown,      // 攻撃後の硬直
	Stunned,       // パリィされてスタン中
};

// ★ 敵AIコンポーネント
struct EnemyAIComponent : public Component {
	EnemyAIState state = EnemyAIState::Idle;
	float stateTimer = 0.0f;

	// 行動パラメータ
	float attackInterval = 3.0f;     // 攻撃間隔（秒）
	float windUpDuration = 0.6f;     // 予備動作の長さ
	float attackDuration = 0.3f;     // 攻撃判定の長さ
	float cooldownDuration = 1.0f;   // 攻撃後硬直
	float stunDuration = 1.5f;       // スタン時間（パリィされた時）

	float chaseSpeed = 4.0f;         // 追跡速度
	float chaseRange = 15.0f;        // この距離以上で追跡開始
	float attackRange = 4.0f;        // この距離以内で攻撃開始
	float aggroRange = 28.0f;
	float rotationSpeed = 8.0f;      // 振り向き速度
	bool hopper = false;
	float hopTimer = 0.0f;
	float hopInterval = 0.72f;
	float hopHeight = 1.65f;
	float groundY = -10000.0f;

	float idleTimer = 0.0f;          // Idle中の経過時間

	bool enabled = true;
	EnemyAIComponent() { type = ComponentType::Script; }
};

// ★ EnemyAISystem: 敵の簡易AIステートマシン
class EnemyAISystem : public ISystem {
public:
	static void ChangeState(entt::registry& registry, entt::entity entity, EnemyAIState next) {
		auto& ai = registry.get<EnemyAIComponent>(entity);
		EntityStateContext frame{registry, entity};
		GetState(ai.state).Exit(frame);
		ai.state = next;
		ai.stateTimer = 0.0f;
		GetState(next).Enter(frame);
	}

	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// プレイヤーの位置を取得
		DirectX::XMFLOAT3 playerPos = {0, 0, 0};
		bool playerFound = false;
		auto playerView = registry.view<TagComponent, TransformComponent>();
		for (auto e : playerView) {
			auto& tag = playerView.get<TagComponent>(e);
			if (tag.tag == TagType::Player) {
				playerPos = playerView.get<TransformComponent>(e).translate;
				playerFound = true;
				break;
			}
		}
		if (!playerFound) return;

		auto view = registry.view<EnemyAIComponent, TransformComponent>();
		for (auto entity : view) {
			auto& ai = view.get<EnemyAIComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!ai.enabled) continue;
			const float aiDt = ctx.dt * (ctx.combatFlow ? ctx.combatFlow->EnemyScale() : 1.0f);
			if (ai.hopper && ai.groundY < -999.0f) ai.groundY = tc.translate.y;
			if (const auto* status = registry.try_get<CanStatusComponent>(entity)) {
				if (status->freezeTimer > 0.0f || status->bubbleTimer > 0.0f) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = false;
					continue;
				}
			}
			if (ai.state == EnemyAIState::Attack) {
				if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = true;
			}

			// ★追加: サンドバッグモード時は敵の行動（AI更新）を停止する
			if (ctx.isSandbagMode) continue;

			// --- プレイヤーとの距離・方向計算 ---
			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			// --- ステートタイマー更新 ---
			ai.stateTimer += aiDt;

			// --- ステートマシン ---
			StateContext frame{*this, registry, entity, ctx, aiDt, dx, dz, dist, targetAngle};
			GetState(ai.state).Update(frame);
			if (ai.hopper) {
				if (ai.groundY > -999.0f) {
					if (ai.state == EnemyAIState::Chase || ai.state == EnemyAIState::Attack) {
						ai.hopTimer = std::fmod(ai.hopTimer + aiDt, ai.hopInterval);
						const float phase = ai.hopTimer / ai.hopInterval;
						tc.translate.y = ai.groundY + std::sin(phase * DirectX::XM_PI) * ai.hopHeight;
					} else {
						ai.hopTimer = 0.0f;
						tc.translate.y = ai.groundY;
					}
				}
			}
		}
		ResolveHopperSeparation(registry, ctx);
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<EnemyAIComponent>();
		for (auto entity : view) {
			auto& ai = registry.get<EnemyAIComponent>(entity);
			ChangeState(registry, entity, EnemyAIState::Idle);
			ai.idleTimer = 0.0f;
			ai.hopTimer = 0.0f;
			ai.groundY = registry.all_of<TransformComponent>(entity) ? registry.get<TransformComponent>(entity).translate.y : -10000.0f;

			// Hitbox無効化
			if (registry.all_of<HitboxComponent>(entity)) {
				registry.get<HitboxComponent>(entity).isActive = false;
			}
			// 色リセット
			if (registry.all_of<MeshRendererComponent>(entity)) {
				registry.get<MeshRendererComponent>(entity).color = ai.hopper ? DirectX::XMFLOAT4{1, 1, 1, 1} : DirectX::XMFLOAT4{1.0f, 0.2f, 0.2f, 1.0f};
			}
		}
	}

private:
	bool MoveHopperTo(entt::entity entity, EnemyAIComponent& ai, TransformComponent& tc,
		float x, float z, GameContext& ctx) {
		if (!ctx.scene) return false;
		const float ground = ctx.scene->GetHeightAt(x, z, ai.groundY + 3.0f, static_cast<uint32_t>(entity));
		if (ground < -999.0f || std::abs(ground - ai.groundY) > 1.1f) return false;
		const float hopOffset = (std::max)(0.0f, tc.translate.y - ai.groundY);
		tc.translate = {x, ground + hopOffset, z};
		ai.groundY = ground;
		return true;
	}

	void ResolveHopperSeparation(entt::registry& registry, GameContext& ctx) {
		if (!ctx.scene) return;
		auto view = registry.view<EnemyAIComponent, TransformComponent>();
		std::vector<entt::entity> hoppers;
		for (auto e : view) {
			const auto& ai = view.get<EnemyAIComponent>(e);
			if (ai.enabled && ai.hopper) hoppers.push_back(e);
		}
		for (int pass = 0; pass < 2; ++pass) {
			for (size_t i = 0; i < hoppers.size(); ++i) {
				const auto a = hoppers[i];
				auto& at = registry.get<TransformComponent>(a);
				auto& aa = registry.get<EnemyAIComponent>(a);
				for (size_t j = i + 1; j < hoppers.size(); ++j) {
					const auto b = hoppers[j];
					auto& bt = registry.get<TransformComponent>(b);
					auto& ba = registry.get<EnemyAIComponent>(b);
					if (std::abs(aa.groundY - ba.groundY) > 2.0f) continue;
					float dx = bt.translate.x - at.translate.x;
					float dz = bt.translate.z - at.translate.z;
					float distance = std::sqrt(dx * dx + dz * dz);
					if (distance >= 2.7f) continue;
					const float overlapDistance = distance;
					if (distance < 0.001f) { dx = 1.0f; dz = 0.0f; distance = 1.0f; }
					const float push = (2.7f - overlapDistance) * 0.5f + 0.005f;
					const float nx = dx / distance, nz = dz / distance;
					const bool movedA = MoveHopperTo(a, aa, at, at.translate.x - nx * push, at.translate.z - nz * push, ctx);
					const bool movedB = MoveHopperTo(b, ba, bt, bt.translate.x + nx * push, bt.translate.z + nz * push, ctx);
					if (!movedA && movedB) MoveHopperTo(b, ba, bt, bt.translate.x + nx * push, bt.translate.z + nz * push, ctx);
					if (!movedB && movedA) MoveHopperTo(a, aa, at, at.translate.x - nx * push, at.translate.z - nz * push, ctx);
				}
			}
		}
		// The boss is a larger solid obstacle for the small enemies.
		auto bosses = registry.view<BossActionComponent, TransformComponent>();
		for (auto bossEntity : bosses) {
			if (!bosses.get<BossActionComponent>(bossEntity).enabled) continue;
			const auto& bossTc = bosses.get<TransformComponent>(bossEntity);
			for (auto e : hoppers) {
				auto& tc = registry.get<TransformComponent>(e);
				auto& ai = registry.get<EnemyAIComponent>(e);
				if (std::abs(ai.groundY - bossTc.translate.y) > 4.5f) continue;
				float dx = tc.translate.x - bossTc.translate.x;
				float dz = tc.translate.z - bossTc.translate.z;
				float distance = std::sqrt(dx * dx + dz * dz);
				if (distance >= 4.5f) continue;
				const float overlapDistance = distance;
				if (distance < 0.001f) { dx = 1.0f; dz = 0.0f; distance = 1.0f; }
				MoveHopperTo(e, ai, tc, tc.translate.x + dx / distance * (4.5f - overlapDistance),
					tc.translate.z + dz / distance * (4.5f - overlapDistance), ctx);
			}
		}
	}

	static void SetNormalColor(EntityStateContext& frame) {
		if (auto* mesh = frame.registry.try_get<MeshRendererComponent>(frame.entity)) {
			mesh->color = frame.registry.get<EnemyAIComponent>(frame.entity).hopper ? DirectX::XMFLOAT4{1, 1, 1, 1} : DirectX::XMFLOAT4{1, .2f, .2f, 1};
		}
	}
	static void ShowWarningEffect(entt::registry& registry, entt::entity entity, float duration) {
		{
			if (!registry.all_of<ScriptComponent>(entity)) {
				registry.emplace<ScriptComponent>(entity);
			}
			auto& sc = registry.get<ScriptComponent>(entity);
			
			// すでにアタッチされていなければ追加
			bool hasWarning = false;
			for (const auto& entry : sc.scripts) {
				if (entry.scriptPath == "WarningEffectScript") hasWarning = true;
			}
			if (!hasWarning) {
				sc.scripts.push_back({"WarningEffectScript", "{ \"duration\": " + std::to_string(duration) + " }", std::make_shared<WarningEffectScript>(), false});
			}
		}
	}

	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt) {
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}
#include "../States/EnemyAIStates.inl"
};

} // namespace Game
