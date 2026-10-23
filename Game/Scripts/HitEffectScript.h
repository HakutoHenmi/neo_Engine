// HitEffectScript.h
// 攻撃が当たったときに飛び散るスライムの破片エフェクト (旧アクションゲーム用)。
// 近接・爆発・爆発の命中・液体の飛び散りなど、種類ごとに破片の飛び方を変える。
#pragma once
#include "IScript.h"

#include <vector>
#include "../../Engine/Matrix4x4.h"

namespace Game {

// 破片1つ分の状態 (エンティティ・速度・寿命・大きさ、触手のような螺旋運動のパラメータ)
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
	bool isExplosionSpike = false;                    // 爆発のトゲ
	bool isLiquidParticle = false;                    // エンティティを持たない軽量な液体の粒
	Engine::Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 液体の粒の色
};

// 命中エフェクトのスクリプト
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
	// 飛ばしている破片
	std::vector<SlimeFragmentData> fragments_;
	// エフェクトの種類 (近接攻撃か)
	bool isMelee_ = false;
	bool isExplosion_ = false;    // 全方位爆発用
	bool isExplosionHit_ = false; // 爆発が敵に当たった時のエフェクト用
	bool isLiquidSplatter_ = false; // 液体の飛び散り
	float attackDirX_ = 0.0f;     // 攻撃の向き X
	float attackDirZ_ = 0.0f;     // 攻撃の向き Z
	bool isFinished_ = false;     // 終了したか
};


} // namespace Game
