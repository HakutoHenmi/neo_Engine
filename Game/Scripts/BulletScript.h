#pragma once
#include "IScript.h"

namespace Game {

class BulletScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;


private:
	entt::entity target_ = entt::null;
	bool hasTarget_ = false;

	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 5.0f;
	float speed_ = 20.0f;
};

} // namespace Game