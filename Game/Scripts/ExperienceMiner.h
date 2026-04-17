#pragma once
#include "IScript.h"
#include "../../externals/entt/entt.hpp"

namespace Game {

class ExperienceMiner : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
};

} // namespace Game