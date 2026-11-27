// MotionSystem.h
// キーフレームのモーション (MotionComponent) を再生してトランスフォームへ反映するシステム。
// 停止中はエディタ用にモーションの軌跡とキーフレームを線で描く。
#pragma once
#include "ISystem.h"

namespace Game {

// モーションのシステム
class MotionSystem : public ISystem {
public:
	// 再生中なら時刻を進め、現在の時刻の姿勢を反映する
	void Update(entt::registry& registry, GameContext& ctx) override;

	// 時刻に応じた位置 (スプライン補間)・回転と拡大 (線形補間) を求める
	void ApplyMotion(const MotionComponent& motion, TransformComponent& transform);

	// 停止中 (エディタ): 軌跡を黄色の線、キーフレームを十字で描く (選択中は赤)
	void Draw(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: 有効なモーションを最初から再生する
	void Reset(entt::registry& registry) override;
};

} // namespace Game
