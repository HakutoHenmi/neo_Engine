#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

enum class PlayerActionState : uint32_t {
	Idle = 0,
	SlimeSpike,   // 素早いトゲ攻撃
	SlimeHammer,  // 重い叩きつけ攻撃
	Dodge,        // 回避 (廃止・または予約)
	Liquefy,      // 液状化 (無敵)
	Stagger       // のけぞり
};

struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;
	float stateTimer = 0.0f;
	float stateDuration = 0.0f;

	float dodgeDuration = 0.4f;
	float dodgeSpeed = 15.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

class PlayerActionSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<PlayerActionComponent, PlayerInputComponent, TransformComponent>();
		for (auto entity : view) {
			auto& pa = view.get<PlayerActionComponent>(entity);
			auto& pi = view.get<PlayerInputComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!pa.enabled || !pi.enabled) continue;

			if (pa.hitStopTimer > 0.0f) {
				pa.hitStopTimer -= ctx.dt;
				if (pa.hitStopTimer < 0.0f) pa.hitStopTimer = 0.0f;
				continue;
			}

			if (pa.dodgeCooldown > 0.0f) pa.dodgeCooldown -= ctx.dt;

			pa.stateTimer += ctx.dt;

			bool attackInput = pi.attackRequested; // 左クリック (PlayerInputSystem.h)
			bool attackPressed = attackInput && !prevAttack_;
			prevAttack_ = attackInput;

			// 右クリックでハンマー攻撃
			bool hammerInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool hammerPressed = hammerInput && !prevHammer_;
			prevHammer_ = hammerInput;

			// Shift長押しで液状化
			bool liquefyInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

			switch (pa.state) {
			case PlayerActionState::Idle:
			{
				bool isMoving = (std::abs(pi.moveDir.x) > 0.01f || std::abs(pi.moveDir.y) > 0.01f);
				bool isGrounded = true;
				if (registry.all_of<CharacterMovementComponent>(entity)) {
					isGrounded = registry.get<CharacterMovementComponent>(entity).isGrounded;
				}

				if (!isGrounded) {
					// 空中：縦に伸びる
					tc.scale.x = 0.9f;
					tc.scale.y = 1.2f;
					tc.scale.z = 0.9f;
				} else if (isMoving) {
					// 移動中：ポヨンポヨン跳ねるように変形
					float bounce = std::abs(std::sin(pa.stateTimer * 15.0f)); 
					tc.scale.x = 1.2f - bounce * 0.2f;
					tc.scale.y = 0.8f + bounce * 0.4f; // 上に跳ねる
					tc.scale.z = 1.2f - bounce * 0.2f;
				} else {
					// 待機時：ゆっくり呼吸しておまんじゅう型
					float breathe = std::sin(pa.stateTimer * 3.0f);
					tc.scale.x = 1.2f + breathe * 0.05f;
					tc.scale.y = 0.8f - breathe * 0.05f;
					tc.scale.z = 1.2f + breathe * 0.05f;
				}

				if (liquefyInput) {
					TransitionTo(pa, PlayerActionState::Liquefy, 0.0f); // 長押し状態
				} else if (attackPressed) {
					TransitionTo(pa, PlayerActionState::SlimeSpike, 0.4f);
				} else if (hammerPressed) {
					TransitionTo(pa, PlayerActionState::SlimeHammer, 1.0f);
				}
				break;
			}

			case PlayerActionState::SlimeSpike:
			{
				// 素早く前方に伸びるトゲ攻撃
				float progress = pa.stateTimer / pa.stateDuration;
				float spike = 0.0f;
				if (progress < 0.2f) {
					spike = progress / 0.2f; // 急に伸びる
				} else {
					spike = 1.0f - ((progress - 0.2f) / 0.8f); // 戻る
				}
				tc.scale.x = 1.2f - (spike * 0.7f);
				tc.scale.y = 0.8f - (spike * 0.3f);
				tc.scale.z = 1.2f + (spike * 3.0f); // 鋭く伸びる

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::SlimeHammer:
			{
				// 膨らんでから前方に叩きつけるハンマー攻撃
				float progress = pa.stateTimer / pa.stateDuration;
				float smash = 0.0f;
				if (progress < 0.5f) {
					smash = progress / 0.5f; // 膨らんで振り下ろす
				} else {
					smash = 1.0f - ((progress - 0.5f) / 0.5f);
				}
				float scaleBase = 1.0f + (1.2f * smash); // 大きく膨らむ
				tc.scale.x = 1.2f * scaleBase;
				tc.scale.y = 0.8f * scaleBase * (1.0f - smash * 0.6f); // 縦に潰れる
				tc.scale.z = 1.2f * scaleBase * (1.0f + smash * 0.6f); // 前方に伸びる

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Liquefy:
			{
				if (!liquefyInput) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
					break;
				}

				// スムーズにペシャンコになる
				float t = std::min(1.0f, pa.stateTimer * 8.0f);
				tc.scale.x = 1.2f + (1.8f * t); // 1.2 -> 3.0
				tc.scale.y = 0.8f - (0.65f * t); // 0.8 -> 0.15
				tc.scale.z = 1.2f + (1.8f * t); // 1.2 -> 3.0

				// 無敵状態を維持
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					hc.invincibleTime = 0.2f; // 毎フレーム少し先まで無敵を更新
				}
			}
			break;

			case PlayerActionState::Dodge:
			{
				// 回避移動
				tc.translate.x += pa.dodgeDirection.x * pa.dodgeSpeed * ctx.dt;
				tc.translate.z += pa.dodgeDirection.z * pa.dodgeSpeed * ctx.dt;

				// シュッと伸び縮みしながら移動
				float progress = pa.stateTimer / pa.stateDuration;
				float stretch = std::sin(progress * 3.14159f); 
				tc.scale.x = 1.2f - (stretch * 0.5f);
				tc.scale.y = 0.8f - (stretch * 0.3f);
				tc.scale.z = 1.2f + (stretch * 1.5f);

				// 無敵時間
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					if (pa.stateTimer >= 0.05f && pa.stateTimer <= 0.3f) {
						hc.invincibleTime = 0.1f;
					}
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Stagger:
				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			// Hitbox（攻撃判定）の更新
			if (registry.all_of<HitboxComponent>(entity)) {
				auto& hb = registry.get<HitboxComponent>(entity);
				if (pa.state == PlayerActionState::SlimeSpike) {
					bool wasActive = hb.isActive;
					hb.isActive = (pa.stateTimer >= 0.1f && pa.stateTimer <= 0.25f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 15.0f;
					hb.size = {1.5f, 1.5f, 5.0f}; // 縦長の当たり判定
				} else if (pa.state == PlayerActionState::SlimeHammer) {
					bool wasActive = hb.isActive;
					hb.isActive = (pa.stateTimer >= 0.4f && pa.stateTimer <= 0.6f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 40.0f;
					hb.size = {5.0f, 4.0f, 5.0f}; // 巨大な当たり判定
				} else {
					hb.isActive = false;
				}
			}

			// ★超重要: 変形（スケール変化）に合わせて、常に底面が地面にくっつくように物理の浮遊オフセットを動的に同期する
			if (registry.all_of<CharacterMovementComponent>(entity)) {
				auto& cm = registry.get<CharacterMovementComponent>(entity);
				float newOffset = tc.scale.y * 0.5f; 
				float diff = newOffset - cm.heightOffset;

				// 接地中であれば、オフセットが変化した分だけ自身のY座標も直接補正する。
				// これをしないと、潰れた瞬間に足元が浮いて重力がかかり、ガクガク（ジッター）する原因になる。
				if (cm.isGrounded) {
					tc.translate.y += diff;
				}
				
				cm.heightOffset = newOffset;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		prevAttack_ = false;
		prevHammer_ = false;
		prevDodge_ = false;

		auto view = registry.view<PlayerActionComponent>();
		for (auto entity : view) {
			auto& pa = registry.get<PlayerActionComponent>(entity);
			pa.state = PlayerActionState::Idle;
			pa.stateTimer = 0.0f;
			pa.hitStopTimer = 0.0f;
			pa.dodgeCooldown = 0.0f;
		}
	}

private:
	bool prevAttack_ = false;
	bool prevHammer_ = false;
	bool prevDodge_ = false;

	void TransitionTo(PlayerActionComponent& pa, PlayerActionState newState, float duration) {
		pa.state = newState;
		pa.stateTimer = 0.0f;
		pa.stateDuration = duration;
	}

	void StartDodge(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		TransitionTo(pa, PlayerActionState::Dodge, pa.dodgeDuration);
		pa.dodgeCooldown = 0.6f;

		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);

		if (len > 0.01f && ctx.camera) {
			auto camRot = ctx.camera->Rotation();
			float cy = std::cos(camRot.y);
			float sy = std::sin(camRot.y);
			float dx = ix * cy + iz * sy;
			float dz = -ix * sy + iz * cy;
			pa.dodgeDirection = { dx, 0.0f, dz };
		} else {
			float facing = tc.rotate.y;
			pa.dodgeDirection = { -std::sin(facing), 0.0f, -std::cos(facing) };
		}
	}
};

} // namespace Game
