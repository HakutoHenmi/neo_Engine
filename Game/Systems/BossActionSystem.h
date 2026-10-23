// BossActionSystem.h
// 旧アクションゲームのボスの行動を状態パターン (States/BossActionStates.inl) で動かすシステム。
#pragma once
#include "../Scripts/WarningEffectScript.h"
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <cmath>

namespace Game {

// ボスの行動のシステム
class BossActionSystem : public ISystem {
public:
	// 状態を切り替える (前の状態の Exit → 新しい状態の Enter)。死亡中は待機への復帰以外を受け付けない
	static void ChangeState(entt::registry& registry, entt::entity entity, BossState next) {
		// 死亡中は待機以外への切り替えを受け付けない
		auto& boss = registry.get<BossActionComponent>(entity);
		if (boss.state == BossState::Dead && next != BossState::Idle)
			return;
		// 前の状態を抜け、状態と経過時間を更新して、新しい状態に入る
		EntityStateContext frame{registry, entity};
		GetState(boss.state).Exit(frame);
		boss.state = next;
		boss.stateTimer = 0.0f;
		GetState(next).Enter(frame);
	}

	// 更新: コンボ中は敵の時間を遅くし、プレイヤーとの位置関係を求めて現在の状態を更新する
	void Update(entt::registry& registry, GameContext& inputContext) override {
		// コンボが 2 以上のときは敵の時間を遅くする (入力のコンテキストは変えずに複製する)
		GameContext ctx = inputContext;
		if (ctx.combatFlow && ctx.combatFlow->Combo() >= 2) ctx.dt *= ctx.combatFlow->EnemyScale();
		if (!ctx.isPlaying) return;

		// プレイヤーの位置を探す
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

		// ボスの行動とトランスフォームを持つエンティティを処理する
		auto view = registry.view<BossActionComponent, TransformComponent>();
		for (auto entity : view) {
			auto& boss = view.get<BossActionComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!boss.enabled) continue;
			// 凍結・泡で動けない間は攻撃判定を止めて何もしない
			if (const auto* status = registry.try_get<CanStatusComponent>(entity)) {
				if (status->freezeTimer > 0.0f || status->bubbleTimer > 0.0f) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) hitbox->isActive = false;
					// ビームの攻撃判定も止める
					if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
						if (auto* beamHitbox = registry.try_get<HitboxComponent>(boss.currentBeamEntity)) beamHitbox->isActive = false;
					}
					continue;
				}
			}
			// 攻撃中は攻撃判定を有効に保つ
			if (boss.state == BossState::Attack) {
				if (auto* hitbox = registry.try_get<HitboxComponent>(entity))
					hitbox->isActive = true;
				// ビームがあれば、その攻撃判定も有効にする
				if (registry.valid(boss.currentBeamEntity)) {
					if (auto* hitbox = registry.try_get<HitboxComponent>(boss.currentBeamEntity))
						hitbox->isActive = true;
				}
			}

			// サンドバッグモードではボスの行動 (AI) を止める
			if (ctx.isSandbagMode) continue;

			// プレイヤーとの差分・距離・向き
			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			// 攻撃パターンを選んでいないのに行動中なら待機へ戻す
			if ((boss.state == BossState::Run || boss.state == BossState::Chase || boss.state == BossState::WindUp || boss.state == BossState::Attack || boss.state == BossState::Cooldown) &&
			    !HasPattern(boss)) {
				ChangeState(registry, entity, BossState::Idle);
				continue;
			}
			// 状態の経過時間を進め、回転の向きを消す
			boss.stateTimer += ctx.dt;
			boss.turnDirection = 0.0f;

			// 状態の更新に渡す情報をまとめて、今の状態の Update を呼ぶ
			StateContext frame{*this, registry, entity, ctx, playerPos, dx, dz, dist, targetAngle};
			GetState(boss.state).Update(frame);
		}
	}

	// PLAY 開始時: 全ボスを待機に戻し、ビームを消す
	void Reset(entt::registry& registry) override {
		auto view = registry.view<BossActionComponent>();
		for (auto entity : view) {
			// 待機に戻してから、残っていたビームを消す
			const auto beam = registry.get<BossActionComponent>(entity).currentBeamEntity;
			ChangeState(registry, entity, BossState::Idle);
			if (registry.valid(beam))
				registry.destroy(beam);
		}
	}

private:

	// 目標の角度へ少しずつ向きを変える (回転方向も記録する)
	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt, BossActionComponent& boss) {
		// 差を -π〜π に直して、近い向きへ回す
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		boss.turnDirection = diff;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}

	// 攻撃の予兆 (赤い点滅のスクリプト) を付ける
	static void ShowWarningEffect(entt::registry& registry, entt::entity entity, float duration) {
		// スクリプトのコンポーネントが無ければ付ける
		if (!registry.all_of<ScriptComponent>(entity)) {
			registry.emplace<ScriptComponent>(entity);
		}
		auto& sc = registry.get<ScriptComponent>(entity);
		// 既に予兆のスクリプトがあれば二重に付けない
		bool hasWarning = false;
		for (const auto& entry : sc.scripts) {
			if (entry.scriptPath == "WarningEffectScript") hasWarning = true;
		}
		// 予兆の長さを JSON で渡してスクリプトを追加する
		if (!hasWarning) {
			sc.scripts.push_back({"WarningEffectScript", "{ \"duration\": " + std::to_string(duration) + " }", std::make_shared<WarningEffectScript>(), false});
		}
	}
// 各状態の定義 (クラスの中に読み込む)
#include "../States/BossActionStates.inl"
};

} // namespace Game
