#pragma once
#include "../../externals/entt/entt.hpp"
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class MissileCanonScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

private:
	float attackRange_ = 50.0f;
	float attackInterval_ = 3.0f;
	float damage_ = 50.0f;
	float explosionRadius_ = 10.0f;
	float attackTimer_ = 0.0f;
	float rotationSpeed_ = 1.0f;

	int connectedTankCount = 0;
	int connectedCanonCount = 0;
	float connectionCheckTimer_ = 0.0f;
	bool isConnectedToTank_ = false;
	entt::entity currentTarget_ = entt::null;

private:
	void Debug(bool connected);
	void UpdateConnection(entt::entity entity, GameScene* scene);
};

} // namespace Game