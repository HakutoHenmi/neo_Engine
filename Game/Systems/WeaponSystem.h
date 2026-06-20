#pragma once
#include "ISystem.h"
#include "../ObjectTypes.h"
#include "../Engine/Renderer.h"
#include "../Systems/PlayerActionSystem.h"
#include <cmath>
#include <vector>

namespace Game {

class WeaponSystem : public ISystem {
public:
	void Update(entt::registry& /*registry*/, GameContext& /*ctx*/) override {
		// スライム自機システムでは武器エンティティを使用しないためスキップ
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<PlayerWeaponComponent>();
		for (auto entity : view) {
			auto& pw = registry.get<PlayerWeaponComponent>(entity);
			pw.weaponEntities.clear();
		}
	}
};

} // namespace Game
