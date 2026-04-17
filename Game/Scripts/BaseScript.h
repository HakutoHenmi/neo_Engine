#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class BaseScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	float rotationSpeed_ = 1.0f;
	float attackInterval_ = 1.0f;
	float attackTimer_ = 0.0f;
	float damage_ = 10.0f;
	float attackRange_ = 30.0f;
};

} // namespace Game