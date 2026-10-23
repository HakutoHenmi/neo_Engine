// WarningEffectScript.h
// 攻撃の予兆として対象のメッシュを赤く点滅させるスクリプト。終わると元の色に戻す。
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
	float duration_ = 0.6f; // 予兆の表示時間 (攻撃の溜め時間に合わせる)
	float timer_ = 0.0f;
	
	// 元の色を覚えておき、終了時に戻す
	bool originalColorSaved_ = false;
	float origR_ = 1.0f;
	float origG_ = 1.0f;
	float origB_ = 1.0f;
	float origA_ = 1.0f;
	bool isFinished_ = false;
};

} // namespace Game
