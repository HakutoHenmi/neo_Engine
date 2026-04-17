#pragma once
#include "../Engine/Input.h"
#include "IScript.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <cmath>
#include <deque>
#include "../../externals/entt/entt.hpp"
#include "../Engine/Matrix4x4.h"
#include "ObjectTypes.h"

namespace Game {

class PlayerScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float speed_ = 7.0f;
	float jumpPower_ = 8.0f;

	// 攻撃関連
	enum class AttackPhase { WindUp, Swing, Recovery };
	enum class SheatheState { Hand, Back, Transitioning };
	AttackPhase currentPhase_ = AttackPhase::WindUp;
	SheatheState sheatheState_ = SheatheState::Back;

	bool isSheathed_ = true;
	float sheatheTimer_ = 0.0f;
	const float AUTO_SHEATHE_TIME = 3.0f;

	int comboCount_ = 0;
	float attackTimer_ = 0.0f;
	bool isAttacking_ = false;
	bool attackQueued_ = false;
	bool prevAttackKeyDown_ = false;

	std::string swordName_ = "PlayerSword";

	void UpdateMovement(entt::entity entity, GameScene* /*scene*/, float dt);
	void UpdateAttack(entt::entity /*entity*/, GameScene* /*scene*/, float dt);
	void UpdateSword(entt::entity entity, GameScene* scene, float dt);

	float experience_ = 0.0f;
	bool isSubscribed_ = false;
	int debugSubscribeCount_ = 0;
	int debugReceiveCount_ = 0;
	float debugLastValue_ = 0.0f;

	// ★修正: 剣の軌跡 (DrawLine3Dベース)
	struct TrailPoint {
		Engine::Vector3 tip;
		Engine::Vector3 base;
		float life;
		float maxLife;
	};
	std::deque<TrailPoint> trailPoints_;
};

} // namespace Game
