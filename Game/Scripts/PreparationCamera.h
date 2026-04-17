#pragma once
#include "IScript.h"

namespace Game {

class PreparationCamera : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void UpdateMovement(entt::entity entity, GameScene* scene, float dt);

private:
};

} // namespace Game