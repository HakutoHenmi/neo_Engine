#pragma once
#include "IScript.h"
#include "../../externals/entt/entt.hpp"

namespace Game {

class ExperienceOrbScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float lifetime_ = 10.0f;
	float speed_ = 5.0f;
	float collectRange_ = 2.0f;
	float xpAmount_ = 10.0f;
};

} // namespace Game