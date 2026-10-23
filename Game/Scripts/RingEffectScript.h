// RingEffectScript.h
// 空間が波打つリング (衝撃波) を広げながら消し、時間が来たら自分を削除するスクリプト。
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
	// 表示時間・経過時間・開始時と終了時の大きさ・常にカメラを向くか
	float duration_ = 0.8f;
	float timer_ = 0.0f;
	float startScale_ = 5.0f;
	float endScale_ = 30.0f;
	bool isBillboard_ = true;
};

} // namespace Game
