#pragma once
#include "IScript.h"

namespace Game {

class RingEffectScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	float duration_ = 0.8f;
	float timer_ = 0.0f;
	float startScale_ = 5.0f;
	float endScale_ = 30.0f;
	bool isBillboard_ = true;
};

} // namespace Game
