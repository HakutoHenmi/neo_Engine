#pragma once
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
	float rotationSpeed = 8.0f;      // 振り向き速度

	float idleTimer = 0.0f;          // Idle中の経過時間

	bool enabled = true;
	EnemyAIComponent() { type = ComponentType::Script; }
};

// ★ EnemyAISystem: 敵の簡易AIステートマシン
class EnemyAISystem : public ISystem {
public:
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

			// --- プレイヤーとの距離・方向計算 ---
			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			// --- ステートタイマー更新 ---
			ai.stateTimer += ctx.dt;

			// --- ステートマシン ---
			switch (ai.state) {

			case EnemyAIState::Idle:
				// プレイヤーの方を向く
				SmoothRotate(tc, targetAngle, ai.rotationSpeed, ctx.dt);

				ai.idleTimer += ctx.dt;

				// 距離が遠い → 追跡
				if (dist > ai.chaseRange) {
					TransitionTo(ai, EnemyAIState::Chase);
				}
				// 攻撃間隔が経過 & 攻撃範囲内 → 攻撃予備動作
				else if (ai.idleTimer >= ai.attackInterval && dist <= ai.attackRange) {
					ai.idleTimer = 0.0f;
					TransitionTo(ai, EnemyAIState::WindUp);

					// ★ 予備動作の視覚フィードバック: スケールを少し膨らませる
					if (registry.all_of<MeshRendererComponent>(entity)) {
						auto& mr = registry.get<MeshRendererComponent>(entity);
						mr.color = {1.0f, 0.8f, 0.0f, 1.0f}; // 黄色に光る（警告色）
					}
				}
				break;

			case EnemyAIState::Chase:
				// プレイヤーの方を向きながら近づく
				SmoothRotate(tc, targetAngle, ai.rotationSpeed, ctx.dt);

				if (dist > 0.5f) {
					float moveSpeed = ai.chaseSpeed * ctx.dt;
					float nx = dx / dist;
					float nz = dz / dist;
					tc.translate.x += nx * moveSpeed;
					tc.translate.z += nz * moveSpeed;
				}

				// 攻撃範囲に入ったらIdleへ
				if (dist <= ai.attackRange) {
					ai.idleTimer = ai.attackInterval * 0.5f; // 少し待ってから攻撃
					TransitionTo(ai, EnemyAIState::Idle);
				}
				break;

			case EnemyAIState::WindUp:
				// 予備動作中: プレイヤーをロックオン
				SmoothRotate(tc, targetAngle, ai.rotationSpeed * 0.5f, ctx.dt);

				// スケールパルス（膨張→収縮）で攻撃予告
				{
					float pulse = 1.0f + 0.1f * std::sin(ai.stateTimer * 20.0f);
					tc.scale.x = 2.0f * pulse;
					tc.scale.z = 2.0f * pulse;
				}

				if (ai.stateTimer >= ai.windUpDuration) {
					// 攻撃開始！
					TransitionTo(ai, EnemyAIState::Attack);

					// Hitbox有効化
					if (registry.all_of<HitboxComponent>(entity)) {
						auto& hb = registry.get<HitboxComponent>(entity);
						hb.isActive = true;
					}
					// 攻撃色（明るい赤）
					if (registry.all_of<MeshRendererComponent>(entity)) {
						auto& mr = registry.get<MeshRendererComponent>(entity);
						mr.color = {1.0f, 0.0f, 0.0f, 1.0f};
					}
					// スケール戻す
					tc.scale.x = 2.0f;
					tc.scale.z = 2.0f;
				}
				break;

			case EnemyAIState::Attack:
				// 攻撃中: 前方に少し突進
				{
					float facing = tc.rotate.y;
					float thrust = 8.0f * ctx.dt;
					tc.translate.x += std::sin(facing) * thrust;
					tc.translate.z += std::cos(facing) * thrust;
				}

				if (ai.stateTimer >= ai.attackDuration) {
					// 攻撃終了 → クールダウン
					TransitionTo(ai, EnemyAIState::Cooldown);

					// Hitbox無効化
					if (registry.all_of<HitboxComponent>(entity)) {
						registry.get<HitboxComponent>(entity).isActive = false;
					}
					// 通常色に戻す
					if (registry.all_of<MeshRendererComponent>(entity)) {
						auto& mr = registry.get<MeshRendererComponent>(entity);
						mr.color = {1.0f, 0.2f, 0.2f, 1.0f};
					}
				}
				break;

			case EnemyAIState::Cooldown:
				// 攻撃後の隙（プレイヤーの反撃チャンス）
				if (ai.stateTimer >= ai.cooldownDuration) {
					TransitionTo(ai, EnemyAIState::Idle);
				}
				break;

			case EnemyAIState::Stunned:
				// パリィされてスタン中（CombatSystemが設定する）
				// 色を青白く
				if (ai.stateTimer < 0.1f && registry.all_of<MeshRendererComponent>(entity)) {
					auto& mr = registry.get<MeshRendererComponent>(entity);
					mr.color = {0.5f, 0.5f, 1.0f, 1.0f}; // スタン色
				}

				if (ai.stateTimer >= ai.stunDuration) {
					TransitionTo(ai, EnemyAIState::Idle);
					// 色を戻す
					if (registry.all_of<MeshRendererComponent>(entity)) {
						auto& mr = registry.get<MeshRendererComponent>(entity);
						mr.color = {1.0f, 0.2f, 0.2f, 1.0f};
					}
				}
				break;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<EnemyAIComponent>();
		for (auto entity : view) {
			auto& ai = registry.get<EnemyAIComponent>(entity);
			ai.state = EnemyAIState::Idle;
			ai.stateTimer = 0.0f;
			ai.idleTimer = 0.0f;

			// Hitbox無効化
			if (registry.all_of<HitboxComponent>(entity)) {
				registry.get<HitboxComponent>(entity).isActive = false;
			}
			// 色リセット
			if (registry.all_of<MeshRendererComponent>(entity)) {
				registry.get<MeshRendererComponent>(entity).color = {1.0f, 0.2f, 0.2f, 1.0f};
			}
		}
	}

private:
	void TransitionTo(EnemyAIComponent& ai, EnemyAIState newState) {
		ai.state = newState;
		ai.stateTimer = 0.0f;
	}

	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt) {
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}
};

} // namespace Game
