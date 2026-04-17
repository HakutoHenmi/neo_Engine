#pragma once
#include "IScript.h"
#include "../../externals/entt/entt.hpp"

namespace Game {

class ExperienceHopper : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float spawnTimer_ = 0.0f;
	float spawnInterval_ = 5.0f;
};

} // namespace Game