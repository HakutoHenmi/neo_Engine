#pragma once
#include "IScript.h"

#include <vector>

namespace Game {

struct SlimeFragmentData {
	entt::entity entity = entt::null;
	float vx = 0;
	float vy = 0;
	float vz = 0;
	float life = 0.0f;
	float maxLife = 0.0f;
	float baseScale = 1.0f;
	// 螺旋(触手)用パラメータ
	bool isTentacle = false;
	float angle = 0.0f;
	float angularSpeed = 0.0f;
	float radius = 0.0f;
	float axisX = 0.0f;
	float axisY = 1.0f;
	float axisZ = 0.0f;
	float speedAlongAxis = 0.0f;
	float cx = 0.0f;
	float cy = 0.0f;
	float cz = 0.0f;
	bool isTentacleTrail = false;
	bool isExplosionSpike = false; // ★追加: 爆発用トゲ
};

class HitEffectScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	float duration_ = 1.5f; // すべての破片が消えるまで待つ
	float timer_ = 0.0f;
	std::vector<SlimeFragmentData> fragments_;
	bool isMelee_ = false;
	bool isExplosion_ = false; // 全方位爆発用
	bool isExplosionHit_ = false; // 爆発が敵に当たった時のエフェクト用
	float attackDirX_ = 0.0f; // ★追加: 攻撃方向X
	float attackDirZ_ = 0.0f; // ★追加: 攻撃方向Z
	bool isFinished_ = false; // ★追加: 終了フラグ
};

// Dummy line for rebuild

} // namespace Game
