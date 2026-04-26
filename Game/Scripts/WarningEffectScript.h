#pragma once
#include "IScript.h"

namespace Game {

class WarningEffectScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	float duration_ = 0.6f; // 予兆エフェクトの表示時間（WindUp時間に合わせる）
	float timer_ = 0.0f;
	
	// 初期色を保存して後で戻すため
	bool originalColorSaved_ = false;
	float origR_ = 1.0f;
	float origG_ = 1.0f;
	float origB_ = 1.0f;
	float origA_ = 1.0f;
};

} // namespace Game
