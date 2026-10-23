// CylinderEffectScript.h
// 円柱のエフェクト (光の柱) を縦に伸ばしながら消し、時間が来たら自分を削除するスクリプト。
#pragma once
#include "IScript.h"

namespace Game {

class CylinderEffectScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	// 表示時間・経過時間・開始時と終了時の縦の大きさ
	float duration_ = 1.0f;
	float timer_ = 0.0f;
	float startScaleY_ = 0.1f;
	float endScaleY_ = 10.0f;
};

} // namespace Game
