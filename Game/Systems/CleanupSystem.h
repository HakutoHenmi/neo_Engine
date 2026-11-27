// CleanupSystem.h
// 不要になったエンティティ (射程外の弾・時間切れ・死亡) を毎フレームまとめて破棄するシステム。
#pragma once
#include "ISystem.h"

namespace Game {

// 後片付けのシステム
class CleanupSystem : public ISystem {
public:
	// PLAY 中、不要なエンティティを集めて破棄する
	void Update(entt::registry& registry, GameContext& ctx) override;
};

} // namespace Game
