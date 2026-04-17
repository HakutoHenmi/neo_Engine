#pragma once
#include "IScript.h"

namespace Game {

class MissileBulletScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	struct Vector3 {
		float x;
		float y;
		float z;
	};

	float LerpFloat(float start, float end, float t);
	void CreateExplosionAttackArea(entt::entity entity, GameScene* scene);

	entt::entity target_ = entt::null;
	bool hasTarget_ = false;

	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 8.0f;

	float arcHeight_ = 12.0f;
	float flightTime_ = 0.0f;
	float maxFlightTime_ = 1.2f;
	float damage_ = 5.0f;
	float explosionRadius_ = 10.0f;

	Vector3 startPosition_ = {0.0f, 0.0f, 0.0f};
};

} // namespace Game