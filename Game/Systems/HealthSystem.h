// HealthSystem.h
// 旧アクションゲームの体力の処理 (被弾の光・無敵時間・ダメージ数字・死亡・落とした液体の回収)。
#pragma once
#include "../Scenes/GameScene.h"
#include "BossActionSystem.h"
#include "EnemyAISystem.h"
#include "ISystem.h"
#include "PlayerActionSystem.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Game {

// 体力のシステム
class HealthSystem : public ISystem {
public:
	// PLAY 中、体力の演出と死亡を処理する
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: 体力の状態を戻し、落とした液体を消す
	void Reset(entt::registry& registry) override;

private:
	// 整数から 0〜1 の疑似乱数を作る
	static float Hash01(uint32_t n) {
		n = (n << 13U) ^ n;
		n = n * (n * n * 15731U + 789221U) + 1376312589U;
		return static_cast<float>(n & 0x7fffffffU) / static_cast<float>(0x7fffffffU);
	}

	// プレイヤーの死亡: 入力・行動・移動・判定・表示を止め、液体を飛び散らせる
	void BeginPlayerDeath(entt::registry& registry, entt::entity entity, HealthComponent& hc, GameContext& ctx);

	// プレイヤーの死亡時に、液体を全方位へ飛び散らせる (見た目は GPU 流体)
	void SpawnPlayerDeathBurst(entt::registry& registry, entt::entity owner, GameContext& ctx);

	// 前フレームの体力 (ダメージ量を求めるため)
	std::unordered_map<uint32_t, float> lastHp_;
};

} // namespace Game
