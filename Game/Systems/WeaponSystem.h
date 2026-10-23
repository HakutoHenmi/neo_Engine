// WeaponSystem.h
// 旧アクションゲームの武器の見た目を管理するシステム (現在のスライムでは処理なし)。
#pragma once
#include "ISystem.h"
#include "../ObjectTypes.h"
#include "../Engine/Renderer.h"
#include "../Systems/PlayerActionSystem.h"
#include <cmath>
#include <vector>

namespace Game {

// 武器のシステム
class WeaponSystem : public ISystem {
public:
	void Update(entt::registry& /*registry*/, GameContext& /*ctx*/) override {
		// スライムの自機は武器エンティティを使わないので何もしない
	}

	// PLAY 開始時に武器の見た目エンティティの一覧を空にする
	void Reset(entt::registry& registry) override {
		// プレイヤーの武器コンポーネントを持つエンティティの武器の一覧を消す
		auto view = registry.view<PlayerWeaponComponent>();
		for (auto entity : view) {
			auto& pw = registry.get<PlayerWeaponComponent>(entity);
			pw.weaponEntities.clear();
		}
	}
};

} // namespace Game
