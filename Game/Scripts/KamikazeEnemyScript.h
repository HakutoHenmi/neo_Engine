#pragma once
#include "IScript.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class KamikazeEnemyScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	void Explode(entt::entity entity, GameScene* scene);

	float speed_ = 6.0f;           // 通常の敵より速い
	float sightRange_ = 25.0f;     // 感知範囲
	float triggerRange_ = 2.5f;    // 自爆する距離
	float explosionRadius_ = 6.0f; // 爆発ダメージ半径
	float damage_ = 50.0f;         // 爆発ダメージ
	bool isExploded_ = false;
	size_t playerIndexCache_ = static_cast<size_t>(-1);
};

} // namespace Game