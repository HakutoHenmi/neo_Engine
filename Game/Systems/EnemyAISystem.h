// EnemyAISystem.h
// 旧アクションゲームの雑魚敵の AI。状態パターン (States/EnemyAIStates.inl) で動かす。
// 跳ねる敵 (hopper) は地面に沿って跳ね、互いやボスと重ならないよう押し出す。
#pragma once
#include "../Scripts/WarningEffectScript.h"
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <cmath>

namespace Game {

// 敵 AI の状態
enum class EnemyAIState : uint32_t {
	Idle = 0,      // 待機（プレイヤーを向く）
	Chase,         // 追跡（距離が遠い場合）
	WindUp,        // 攻撃予備動作（パリィ猶予の目印）
	Attack,        // 攻撃中（Hitbox有効）
	Cooldown,      // 攻撃後の硬直
	Stunned,       // パリィされてスタン中
};

// 敵 AI コンポーネント
struct EnemyAIComponent : public Component {
	// 今の状態と、その状態に入ってからの時間
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
	// プレイヤーに気付く距離
	float aggroRange = 28.0f;
	float rotationSpeed = 8.0f;      // 振り向き速度
	// 跳ねる敵の設定 (跳ねる周期・高さ・地面の高さ)
	bool hopper = false;
	float hopTimer = 0.0f;
	float hopInterval = 0.72f;
	float hopHeight = 1.65f;
	float groundY = -10000.0f;

	float idleTimer = 0.0f;          // Idle中の経過時間

	// AI を動かすか
	bool enabled = true;
	EnemyAIComponent() { type = ComponentType::Script; }
};

// EnemyAISystem: 敵の簡易な状態マシン
class EnemyAISystem : public ISystem {
public:
	// 状態を切り替える (前の状態の Exit → 新しい状態の Enter)
	static void ChangeState(entt::registry& registry, entt::entity entity, EnemyAIState next) {
		// 前の状態を抜け、状態と経過時間を更新して、新しい状態に入る
		auto& ai = registry.get<EnemyAIComponent>(entity);
		EntityStateContext frame{registry, entity};
		GetState(ai.state).Exit(frame);
		ai.state = next;
		ai.stateTimer = 0.0f;
		GetState(next).Enter(frame);
	}

	// PLAY 中、各敵の状態を更新する
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

		// AI とトランスフォームを持つ敵を処理する
		auto view = registry.view<EnemyAIComponent, TransformComponent>();
		for (auto entity : view) {
			auto& ai = view.get<EnemyAIComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!ai.enabled) continue;
			// コンボ中は敵の時間を遅くする
			const float aiDt = ctx.dt * (ctx.combatFlow ? ctx.combatFlow->EnemyScale() : 1.0f);
			// 跳ねる敵の地面の高さを最初に記録する
			if (ai.hopper && ai.groundY < -999.0f) ai.groundY = tc.translate.y;
			// 凍結・泡で動けない間は攻撃判定を止めて何もしない
			if (const auto* status = registry.try_get<CanStatusComponent>(entity)) {
				if (status->freezeTimer > 0.0f || status->bubbleTimer > 0.0f) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = false;
					continue;
				}
			}
			// 攻撃中は攻撃判定を有効にする
			if (ai.state == EnemyAIState::Attack) {
				if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = true;
			}

			// サンドバッグモードでは敵の行動 (AI) を止める
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
			// 跳ねる敵は追跡と攻撃の間だけ跳ねる (sin の山で高さを付ける)
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
		// 跳ねる敵同士とボスとの重なりを解消する
		ResolveHopperSeparation(registry, ctx);
	}

	// PLAY 開始時: 全敵を待機に戻し、攻撃判定と色を元に戻す
	void Reset(entt::registry& registry) override {
		auto view = registry.view<EnemyAIComponent>();
		for (auto entity : view) {
			auto& ai = registry.get<EnemyAIComponent>(entity);
			// 待機へ戻し、各タイマーと地面の高さを初期化する
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
	// 跳ねる敵を指定の位置へ動かす (段差が 1.1 を超える所へは動かない)
	bool MoveHopperTo(entt::entity entity, EnemyAIComponent& ai, TransformComponent& tc,
		float x, float z, GameContext& ctx) {
		// 移動先の地面の高さを調べる (見つからないか段差が大きければ動かない)
		if (!ctx.scene) return false;
		const float ground = ctx.scene->GetHeightAt(x, z, ai.groundY + 3.0f, static_cast<uint32_t>(entity));
		if (ground < -999.0f || std::abs(ground - ai.groundY) > 1.1f) return false;
		// 跳ねている高さは保ったまま移動先へ置く
		const float hopOffset = (std::max)(0.0f, tc.translate.y - ai.groundY);
		tc.translate = {x, ground + hopOffset, z};
		ai.groundY = ground;
		return true;
	}

	// 跳ねる敵同士が 2.7 以内で重ならないよう、互いに半分ずつ押し出す (2回繰り返す)
	void ResolveHopperSeparation(entt::registry& registry, GameContext& ctx) {
		if (!ctx.scene) return;
		// 有効な跳ねる敵を集める
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
					// 高さが 2 以上違う組 (別の段) はとばす
					if (std::abs(aa.groundY - ba.groundY) > 2.0f) continue;
					float dx = bt.translate.x - at.translate.x;
					float dz = bt.translate.z - at.translate.z;
					float distance = std::sqrt(dx * dx + dz * dz);
					if (distance >= 2.7f) continue;
					const float overlapDistance = distance;
					// 完全に重なっていれば x 方向へ押し出す
					if (distance < 0.001f) { dx = 1.0f; dz = 0.0f; distance = 1.0f; }
					// 重なりの半分ずつ (少し多めに) 押し出す
					const float push = (2.7f - overlapDistance) * 0.5f + 0.005f;
					const float nx = dx / distance, nz = dz / distance;
					const bool movedA = MoveHopperTo(a, aa, at, at.translate.x - nx * push, at.translate.z - nz * push, ctx);
					const bool movedB = MoveHopperTo(b, ba, bt, bt.translate.x + nx * push, bt.translate.z + nz * push, ctx);
					// 片方が動けなかったら、もう片方をさらに押し出す
					if (!movedA && movedB) MoveHopperTo(b, ba, bt, bt.translate.x + nx * push, bt.translate.z + nz * push, ctx);
					if (!movedB && movedA) MoveHopperTo(a, aa, at, at.translate.x - nx * push, at.translate.z - nz * push, ctx);
				}
			}
		}
		// ボスは小さな敵にとって大きな障害物として扱う
		auto bosses = registry.view<BossActionComponent, TransformComponent>();
		for (auto bossEntity : bosses) {
			if (!bosses.get<BossActionComponent>(bossEntity).enabled) continue;
			const auto& bossTc = bosses.get<TransformComponent>(bossEntity);
			for (auto e : hoppers) {
				auto& tc = registry.get<TransformComponent>(e);
				auto& ai = registry.get<EnemyAIComponent>(e);
				// ボスと同じ高さの敵が、ボスの半径 4.5 より内側にいたら外へ出す
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

	// 敵の通常の色 (跳ねる敵は白、それ以外は赤)
	static void SetNormalColor(EntityStateContext& frame) {
		if (auto* mesh = frame.registry.try_get<MeshRendererComponent>(frame.entity)) {
			mesh->color = frame.registry.get<EnemyAIComponent>(frame.entity).hopper ? DirectX::XMFLOAT4{1, 1, 1, 1} : DirectX::XMFLOAT4{1, .2f, .2f, 1};
		}
	}
	// 攻撃の予兆 (赤い点滅のスクリプト) を付ける
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

	// 目標の角度へ少しずつ向きを変える
	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt) {
		// 差を -π〜π に直して、近い向きへ回す
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}
// 各状態の定義 (クラスの中に読み込む)
#include "../States/EnemyAIStates.inl"
};

} // namespace Game
