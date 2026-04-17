#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class PoisonAttackArea : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

private:
	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 0.5f;
};

} // namespace Game