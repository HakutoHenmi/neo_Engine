#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class PoisonTrap : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

private:
	float poisonDamage_ = 3.0f;
	float poisonRange_ = 6.0f;
	float poisonInterval_ = 0.2f;
	float poisonTimer_ = 0.0f;

	int connectedTankCount = 0;
	float connectionCheckTimer_ = 0.0f;
	bool isConnectedToTank_ = false;

private:
	void Debug(bool connected);
	void UpdateConnection(entt::entity entity, GameScene* scene);
	bool IsEnemyInRange(entt::entity entity, GameScene* scene, float range);
	void CreatePoisonAttackArea(entt::entity entity, GameScene* scene);
};

} // namespace Game