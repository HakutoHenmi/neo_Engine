#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"

namespace Game {

class BulletTank : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float rotationSpeed_ = 1.0f;
};

} // namespace Game