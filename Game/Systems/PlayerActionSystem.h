#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

// ★ プレイヤーのアクション状態
enum class PlayerActionState : uint32_t {
	Idle = 0,       // 待機
	Attack1,        // 攻撃1段目
	Attack2,        // 攻撃2段目
	Attack3,        // 攻撃3段目
	Dodge,          // 回避（ダッシュ回避）
	Parry,          // パリィ受付中
	ParrySuccess,   // パリィ成功演出中
	Stagger,        // のけぞり中
};

// ★ プレイヤーアクションコンポーネント
struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;

	float stateTimer = 0.0f;       // 現在のステート経過時間
	float stateDuration = 0.0f;    // 現在のステートの持続時間

	// 攻撃コンボ
	int comboStep = 0;             // 現在のコンボ段数 (0=なし, 1~3)
	bool comboQueued = false;      // コンボ入力バッファ（先行入力）
	float comboWindowStart = 0.0f; // コンボ受付開始時間
	float comboWindowEnd = 0.0f;   // コンボ受付終了時間

	// パリィ
	float parryWindowDuration = 0.4f; // パリィ受付フレーム（秒）
	float parryCooldown = 0.0f;        // パリィのクールダウン残り

	// 回避
	float dodgeDuration = 0.4f;       // 回避モーションの長さ
	float dodgeSpeed = 15.0f;         // 回避時の移動速度
	float dodgeCooldown = 0.0f;       // 回避クールダウン残り
	float dodgeInvincibleStart = 0.05f; // 回避無敵開始
	float dodgeInvincibleEnd = 0.3f;    // 回避無敵終了
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1}; // 回避方向

	// ヒットストップ
	float hitStopTimer = 0.0f;     // ヒットストップ残り時間
	float hitStopDuration = 0.08f; // デフォルトのヒットストップ時間

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

// ★ PlayerActionSystem: 攻撃・回避・パリィのステートマシン
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

			// --- ヒットストップ処理 ---
			if (pa.hitStopTimer > 0.0f) {
				pa.hitStopTimer -= ctx.dt;
				if (pa.hitStopTimer < 0.0f) pa.hitStopTimer = 0.0f;
				continue; // ヒットストップ中は一切の更新を止める
			}

			// --- クールダウン更新 ---
			if (pa.parryCooldown > 0.0f) pa.parryCooldown -= ctx.dt;
			if (pa.dodgeCooldown > 0.0f) pa.dodgeCooldown -= ctx.dt;

			// --- ステートタイマー更新 ---
			pa.stateTimer += ctx.dt;

			// --- 入力の読み取り ---
			bool attackInput = pi.attackRequested;
			// ★変更: 右クリック=パリィ、Shift=回避
			bool parryInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool dodgeInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

			// パリィ: 前フレームで押されていなかった場合のみ発動（エッジ検出）
			bool parryPressed = parryInput && !prevParry_;
			bool dodgePressed = dodgeInput && !prevDodge_;
			prevParry_ = parryInput;
			prevDodge_ = dodgeInput;

			// --- ステートマシン ---
			switch (pa.state) {
			case PlayerActionState::Idle:
				HandleIdleState(pa, pi, tc, ctx, attackInput, parryPressed, dodgePressed);
				break;

			case PlayerActionState::Attack1:
			case PlayerActionState::Attack2:
			case PlayerActionState::Attack3:
				HandleAttackState(registry, entity, pa, pi, tc, ctx, attackInput);
				break;

			case PlayerActionState::Dodge:
				HandleDodgeState(registry, entity, pa, tc, ctx);
				break;

			case PlayerActionState::Parry:
				HandleParryState(pa, ctx);
				break;

			case PlayerActionState::ParrySuccess:
				HandleParrySuccessState(pa, ctx);
				break;

			case PlayerActionState::Stagger:
				HandleStaggerState(pa, ctx);
				break;
			}

			// --- Hitbox の有効化/無効化 ---
			UpdateHitbox(registry, entity, pa);
		}
	}

	void Reset(entt::registry& registry) override {
		prevParry_ = false;
		prevDodge_ = false;

		auto view = registry.view<PlayerActionComponent>();
		for (auto entity : view) {
			auto& pa = registry.get<PlayerActionComponent>(entity);
			pa.state = PlayerActionState::Idle;
			pa.stateTimer = 0.0f;
			pa.comboStep = 0;
			pa.comboQueued = false;
			pa.hitStopTimer = 0.0f;
			pa.parryCooldown = 0.0f;
			pa.dodgeCooldown = 0.0f;
		}
	}

private:
	bool prevParry_ = false;
	bool prevDodge_ = false;

	// 各攻撃段のパラメータ
	struct AttackParams {
		float duration;      // モーション全体の長さ
		float hitStart;      // Hitbox有効開始
		float hitEnd;        // Hitbox有効終了
		float comboStart;    // コンボ受付開始
		float comboEnd;      // コンボ受付終了
		float damage;        // ダメージ
	};

	static AttackParams GetAttackParams(int step) {
		switch (step) {
		case 1: return { 0.45f, 0.10f, 0.25f, 0.20f, 0.40f, 15.0f }; // 素早い横斬り
		case 2: return { 0.50f, 0.12f, 0.28f, 0.25f, 0.45f, 20.0f }; // 逆横斬り
		case 3: return { 0.65f, 0.15f, 0.35f, 0.50f, 0.60f, 35.0f }; // 強縦斬り
		default: return { 0.45f, 0.10f, 0.25f, 0.20f, 0.40f, 15.0f };
		}
	}

	void TransitionTo(PlayerActionComponent& pa, PlayerActionState newState, float duration) {
		pa.state = newState;
		pa.stateTimer = 0.0f;
		pa.stateDuration = duration;
	}

	// --- Idle ---
	void HandleIdleState(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc,
		GameContext& ctx, bool attackInput, bool parryPressed, bool dodgePressed) {
		pa.comboStep = 0;
		pa.comboQueued = false;

		// 攻撃 -> Attack1
		if (attackInput) {
			StartAttack(pa, 1);
			return;
		}

		// パリィ -> Parry
		if (parryPressed && pa.parryCooldown <= 0.0f) {
			TransitionTo(pa, PlayerActionState::Parry, pa.parryWindowDuration);
			pa.parryCooldown = 0.5f; // パリィ後0.5秒のクールダウン
			return;
		}

		// 回避 -> Dodge
		if (dodgePressed && pa.dodgeCooldown <= 0.0f) {
			StartDodge(pa, pi, tc, ctx);
			return;
		}
	}

	void StartAttack(PlayerActionComponent& pa, int step) {
		auto params = GetAttackParams(step);
		pa.comboStep = step;
		pa.comboQueued = false;
		pa.comboWindowStart = params.comboStart;
		pa.comboWindowEnd = params.comboEnd;

		PlayerActionState state = PlayerActionState::Attack1;
		if (step == 2) state = PlayerActionState::Attack2;
		if (step == 3) state = PlayerActionState::Attack3;
		TransitionTo(pa, state, params.duration);
	}

	// --- Attack ---
	void HandleAttackState(entt::registry& /*registry*/, entt::entity /*entity*/,
		PlayerActionComponent& pa, PlayerInputComponent& /*pi*/, TransformComponent& /*tc*/,
		GameContext& /*ctx*/, bool attackInput) {
		// コンボ受付ウィンドウ内に攻撃入力があればバッファリング
		if (attackInput && pa.stateTimer >= pa.comboWindowStart && pa.stateTimer <= pa.comboWindowEnd) {
			pa.comboQueued = true;
		}

		// モーション終了
		if (pa.stateTimer >= pa.stateDuration) {
			if (pa.comboQueued && pa.comboStep < 3) {
				// 次のコンボ段へ
				StartAttack(pa, pa.comboStep + 1);
			} else {
				// コンボ終了 -> Idle
				TransitionTo(pa, PlayerActionState::Idle, 0.0f);
			}
		}
	}

	// --- Dodge ---
	void StartDodge(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		TransitionTo(pa, PlayerActionState::Dodge, pa.dodgeDuration);
		pa.dodgeCooldown = 0.6f;

		// 移動入力がある場合はその方向、なければ後ろに回避
		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);

		if (len > 0.01f && ctx.camera) {
			// カメラ基準の方向に変換
			auto camRot = ctx.camera->Rotation();
			float cy = std::cos(camRot.y);
			float sy = std::sin(camRot.y);
			float dx = ix * cy + iz * sy;
			float dz = -ix * sy + iz * cy;
			pa.dodgeDirection = { dx, 0.0f, dz };
		} else {
			// 後方回避（キャラの向きの逆方向）
			float facing = tc.rotate.y;
			pa.dodgeDirection = { -std::sin(facing), 0.0f, -std::cos(facing) };
		}
	}

	void HandleDodgeState(entt::registry& registry, entt::entity entity,
		PlayerActionComponent& pa, TransformComponent& tc, GameContext& ctx) {
		// 回避移動
		tc.translate.x += pa.dodgeDirection.x * pa.dodgeSpeed * ctx.dt;
		tc.translate.z += pa.dodgeDirection.z * pa.dodgeSpeed * ctx.dt;

		// 回避中の無敵判定
		if (registry.all_of<HealthComponent>(entity)) {
			auto& hc = registry.get<HealthComponent>(entity);
			if (pa.stateTimer >= pa.dodgeInvincibleStart && pa.stateTimer <= pa.dodgeInvincibleEnd) {
				hc.invincibleTime = 0.1f; // 無敵フレームを維持
			}
		}

		// 終了
		if (pa.stateTimer >= pa.stateDuration) {
			TransitionTo(pa, PlayerActionState::Idle, 0.0f);
		}
	}

	// --- Parry ---
	void HandleParryState(PlayerActionComponent& pa, GameContext& /*ctx*/) {
		// パリィ受付時間が過ぎたらIdleへ
		if (pa.stateTimer >= pa.stateDuration) {
			TransitionTo(pa, PlayerActionState::Idle, 0.0f);
		}
	}

	// --- ParrySuccess ---
	void HandleParrySuccessState(PlayerActionComponent& pa, GameContext& /*ctx*/) {
		// パリィ成功演出（ヒットストップ後の硬直）
		if (pa.stateTimer >= pa.stateDuration) {
			TransitionTo(pa, PlayerActionState::Idle, 0.0f);
		}
	}

	// --- Stagger ---
	void HandleStaggerState(PlayerActionComponent& pa, GameContext& /*ctx*/) {
		if (pa.stateTimer >= pa.stateDuration) {
			TransitionTo(pa, PlayerActionState::Idle, 0.0f);
		}
	}

	// --- Hitboxの有効/無効を攻撃タイミングに合わせて制御 ---
	void UpdateHitbox(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (!registry.all_of<HitboxComponent>(entity)) return;
		auto& hb = registry.get<HitboxComponent>(entity);

		bool isAttacking = (pa.state == PlayerActionState::Attack1 ||
		                    pa.state == PlayerActionState::Attack2 ||
		                    pa.state == PlayerActionState::Attack3);

		if (isAttacking) {
			auto params = GetAttackParams(pa.comboStep);
			hb.isActive = (pa.stateTimer >= params.hitStart && pa.stateTimer <= params.hitEnd);
			hb.damage = params.damage;
		} else {
			hb.isActive = false;
		}
	}
};

} // namespace Game
