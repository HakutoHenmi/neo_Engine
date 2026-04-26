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
	Charging,       // 溜め中（大剣用）
	ChargeAttack1,  // 溜め攻撃1段目
	ChargeAttack2,  // 溜め攻撃2段目
	ChargeAttack3,  // 溜め攻撃3段目（大技）
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

	// 溜め攻撃用
	float chargeTimer = 0.0f;
	float currentDamageMultiplier = 1.0f;
	int chargeLevel = 0;           // 溜めコンボ段数 (0=なし, 1~3)
	float holdTimer = 0.0f;        // ボタン押下時間（タップ/ホールド判定用）
	bool isHolding = false;        // ボタン長押し中か

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
			bool attackPressed = attackInput && !prevAttack_;
			bool attackReleased = !attackInput && prevAttack_;
			(void)attackReleased; // 警告抑制（Charging内で直接attackInputを使用）
			prevAttack_ = attackInput;

			// ★変更: 右クリック=パリィ、Shift=回避
			bool parryInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool dodgeInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

			// パリィ: 前フレームで押されていなかった場合のみ発動（エッジ検出）
			bool parryPressed = parryInput && !prevParry_;
			bool dodgePressed = dodgeInput && !prevDodge_;
			prevParry_ = parryInput;
			prevDodge_ = dodgeInput;

			// ★追加: PlayerWeaponComponentがなければ追加
			if (!registry.all_of<PlayerWeaponComponent>(entity)) {
				registry.emplace<PlayerWeaponComponent>(entity);
			}
			WeaponType currentWeapon = registry.get<PlayerWeaponComponent>(entity).currentWeapon;

			// --- ステートマシン ---
			switch (pa.state) {
			case PlayerActionState::Idle:
				HandleIdleState(pa, pi, tc, ctx, attackInput, attackPressed, parryPressed, dodgePressed, currentWeapon);
				break;

			case PlayerActionState::Charging:
				HandleChargeState(pa, attackInput, currentWeapon, ctx);
				break;

			case PlayerActionState::Attack1:
			case PlayerActionState::Attack2:
			case PlayerActionState::Attack3:
				HandleAttackState(registry, entity, pa, pi, tc, ctx, attackPressed, currentWeapon);
				break;

			case PlayerActionState::ChargeAttack1:
			case PlayerActionState::ChargeAttack2:
			case PlayerActionState::ChargeAttack3:
				HandleChargeAttackState(pa, attackInput, attackPressed, currentWeapon);
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
			UpdateHitbox(registry, entity, pa, currentWeapon);
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
	bool prevAttack_ = false;
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
		DirectX::XMFLOAT3 hitboxSize; // 当たり判定のサイズ
	};

	static AttackParams GetAttackParams(int step, WeaponType weaponType) {
		if (weaponType == WeaponType::Greatsword) {
			// 大剣 通常コンボ（タップ連打）
			switch (step) {
			case 1: return { 0.7f, 0.3f, 0.5f, 0.5f, 0.7f, 35.0f, {4, 4, 4} };
			case 2: return { 0.8f, 0.3f, 0.5f, 0.6f, 0.8f, 45.0f, {4, 4, 4} };
			case 3: return { 1.0f, 0.5f, 0.8f, 0.8f, 1.0f, 70.0f, {5, 5, 5} };
			}
		} 
		else if (weaponType == WeaponType::DualBlades) {
			switch (step) {
			case 1: return { 0.3f, 0.1f, 0.2f, 0.15f, 0.3f, 8.0f, {2.5f, 2.5f, 3} };
			case 2: return { 0.3f, 0.1f, 0.2f, 0.15f, 0.3f, 10.0f, {2.5f, 2.5f, 3} };
			case 3: return { 0.8f, 0.2f, 0.7f, 0.6f,  0.8f, 25.0f, {6, 3, 6} };
			}
		}
		else if (weaponType == WeaponType::MultiBlades) {
			switch (step) {
			case 1: return { 0.4f, 0.1f, 0.3f, 0.2f, 0.4f, 15.0f, {2, 2, 8} };
			case 2: return { 0.4f, 0.1f, 0.3f, 0.2f, 0.4f, 15.0f, {2, 2, 8} };
			case 3: return { 1.0f, 0.5f, 0.8f, 0.8f, 1.0f, 80.0f, {12, 3, 12} };
			}
		}
		return { 0.45f, 0.10f, 0.25f, 0.20f, 0.40f, 15.0f, {1.5f, 1.5f, 4.0f} };
	}

	// 大剣 溜めコンボ専用パラメータ
	static AttackParams GetChargeAttackParams(int chargeLevel) {
		switch (chargeLevel) {
		case 1: return { 0.9f, 0.35f, 0.6f, 0.7f, 0.9f, 60.0f, {5, 5, 5} };  // 力強い縦斬り
		case 2: return { 1.1f, 0.4f, 0.7f, 0.8f, 1.1f, 100.0f, {7, 4, 7} };  // 大薙ぎ払い
		case 3: return { 1.5f, 0.6f, 1.0f, 1.2f, 1.5f, 200.0f, {8, 6, 8} };  // 天墜斬（大技）
		}
		return { 0.9f, 0.35f, 0.6f, 0.7f, 0.9f, 60.0f, {5, 5, 5} };
	}

	void TransitionTo(PlayerActionComponent& pa, PlayerActionState newState, float duration) {
		pa.state = newState;
		pa.stateTimer = 0.0f;
		pa.stateDuration = duration;
	}

	// --- Idle ---
	void HandleIdleState(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc,
		GameContext& ctx, bool attackInput, bool attackPressed, bool parryPressed, bool dodgePressed, WeaponType currentWeapon) {
		pa.comboStep = 0;
		pa.comboQueued = false;

		if (currentWeapon == WeaponType::Greatsword) {
			// 大剣: ボタン押した瞬間にholdTimerを開始
			if (attackPressed) {
				pa.holdTimer = 0.0f;
				pa.isHolding = true;
				pa.chargeLevel = 0; // 溜めコンボリセット
			}
			if (pa.isHolding) {
				pa.holdTimer += ctx.dt;
				if (!attackInput) {
					// ボタンを離した = タップ → 通常攻撃1段目
					pa.isHolding = false;
					StartAttack(pa, 1, currentWeapon, 1.0f);
					return;
				}
				if (pa.holdTimer >= 0.2f) {
					// 0.2秒以上長押し → 溜め状態に遷移
					pa.isHolding = false;
					pa.chargeLevel = 1;
					StartCharge(pa, 1);
					return;
				}
			}
		} else {
			// 他の武器: タップで即攻撃
			if (attackPressed) {
				StartAttack(pa, 1, currentWeapon, 1.0f);
				return;
			}
		}

		if (parryPressed && pa.parryCooldown <= 0.0f) {
			TransitionTo(pa, PlayerActionState::Parry, pa.parryWindowDuration);
			pa.parryCooldown = 0.5f;
			return;
		}
		if (dodgePressed && pa.dodgeCooldown <= 0.0f) {
			StartDodge(pa, pi, tc, ctx);
			return;
		}
	}

	// --- Charging（溜め中）: ボタンを離すまで攻撃を出さない ---
	void StartCharge(PlayerActionComponent& pa, int level) {
		pa.chargeLevel = level;
		pa.comboQueued = false;
		pa.chargeTimer = 0.0f;
		TransitionTo(pa, PlayerActionState::Charging, 999.0f);
	}

	void HandleChargeState(PlayerActionComponent& pa, bool attackInput, WeaponType currentWeapon, GameContext& ctx) {
		(void)currentWeapon;
		pa.chargeTimer += ctx.dt;
		float maxCharge = 1.5f; // 1.5秒で最大溜め

		if (!attackInput) {
			// ボタンを離した → 溜め攻撃発動！
			float chargeRatio = std::min(pa.chargeTimer, maxCharge) / maxCharge;
			// 溜め段に応じた基礎倍率 + 溜め時間による追加倍率
			float baseMult = 1.0f + (pa.chargeLevel - 1) * 0.5f; // Lv1=1.0, Lv2=1.5, Lv3=2.0
			float chargeMult = 1.0f + chargeRatio * 1.5f;         // 溜め0=1.0倍, 最大=2.5倍
			pa.currentDamageMultiplier = baseMult * chargeMult;
			StartChargeAttack(pa, pa.chargeLevel);
			return;
		}
		// ボタンを押している間は最大溜めのまま待機する
	}

	void StartChargeAttack(PlayerActionComponent& pa, int level) {
		auto params = GetChargeAttackParams(level);
		pa.comboStep = level;
		pa.comboQueued = false;
		pa.comboWindowStart = params.comboStart;
		pa.comboWindowEnd = params.comboEnd;

		PlayerActionState state = PlayerActionState::ChargeAttack1;
		if (level == 2) state = PlayerActionState::ChargeAttack2;
		if (level == 3) state = PlayerActionState::ChargeAttack3;
		TransitionTo(pa, state, params.duration);
	}

	// --- ChargeAttack（溜め攻撃モーション中）---
	void HandleChargeAttackState(PlayerActionComponent& pa, bool attackInput, bool attackPressed, WeaponType currentWeapon) {
		// 先行入力（モーション中いつでも受け付け）
		if (attackPressed) {
			pa.comboQueued = true;
		}

		// コンボ受付開始時間を過ぎていて、かつバッファがあればキャンセルして次の段へ
		bool canComboCancel = (pa.comboQueued && pa.stateTimer >= pa.comboWindowStart);
		
		// モーション終了 または キャンセル可能
		if (pa.stateTimer >= pa.stateDuration || canComboCancel) {
			if (pa.comboQueued && pa.chargeLevel < 3) {
				// 次の段へ。ホールド中なら溜めへ、離していれば通常攻撃へ
				if (attackInput) {
					StartCharge(pa, pa.chargeLevel + 1);
				} else {
					StartAttack(pa, pa.chargeLevel + 1, currentWeapon, 1.0f);
				}
			} else if (pa.stateTimer >= pa.stateDuration) {
				// 溜め3段目完了 または コンボ入力なし → Idle
				TransitionTo(pa, PlayerActionState::Idle, 0.0f);
			}
		}
	}

	// --- 通常コンボ ---
	void StartAttack(PlayerActionComponent& pa, int step, WeaponType currentWeapon, float multiplier) {
		auto params = GetAttackParams(step, currentWeapon);
		pa.comboStep = step;
		pa.comboQueued = false;
		pa.comboWindowStart = params.comboStart;
		pa.comboWindowEnd = params.comboEnd;
		pa.currentDamageMultiplier = multiplier;

		PlayerActionState state = PlayerActionState::Attack1;
		if (step == 2) state = PlayerActionState::Attack2;
		if (step == 3) state = PlayerActionState::Attack3;
		TransitionTo(pa, state, params.duration);
	}

	// --- Attack ---
	void HandleAttackState(entt::registry& /*registry*/, entt::entity /*entity*/,
		PlayerActionComponent& pa, PlayerInputComponent& /*pi*/, TransformComponent& /*tc*/,
		GameContext& /*ctx*/, bool attackPressed, WeaponType currentWeapon) {
		// 先行入力（モーション中いつでも受け付け）
		if (attackPressed) {
			pa.comboQueued = true;
		}

		// コンボ受付開始時間を過ぎていて、かつバッファがあればキャンセルして次の段へ
		bool canComboCancel = (pa.comboQueued && pa.stateTimer >= pa.comboWindowStart);

		// モーション終了 または キャンセル可能
		if (pa.stateTimer >= pa.stateDuration || canComboCancel) {
			if (pa.comboQueued && pa.comboStep < 3) {
				StartAttack(pa, pa.comboStep + 1, currentWeapon, 1.0f);
			} else if (pa.stateTimer >= pa.stateDuration) {
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
	void UpdateHitbox(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, WeaponType currentWeapon) {
		if (!registry.all_of<HitboxComponent>(entity)) return;
		auto& hb = registry.get<HitboxComponent>(entity);

		bool isAttacking = (pa.state == PlayerActionState::Attack1 ||
		                    pa.state == PlayerActionState::Attack2 ||
		                    pa.state == PlayerActionState::Attack3);
		bool isChargeAttacking = (pa.state == PlayerActionState::ChargeAttack1 ||
		                          pa.state == PlayerActionState::ChargeAttack2 ||
		                          pa.state == PlayerActionState::ChargeAttack3);

		if (isAttacking || isChargeAttacking) {
			AttackParams params;
			if (isChargeAttacking) {
				params = GetChargeAttackParams(pa.chargeLevel);
			} else {
				params = GetAttackParams(pa.comboStep, currentWeapon);
			}
			
			// 新しく攻撃判定が発生した瞬間（false -> true）にヒット履歴をクリア
			bool wasActive = hb.isActive;
			hb.isActive = (pa.stateTimer >= params.hitStart && pa.stateTimer <= params.hitEnd);
			if (!wasActive && hb.isActive) {
				hb.hitTargets.clear();
			}
			
			hb.damage = params.damage * pa.currentDamageMultiplier;
			hb.size = params.hitboxSize;
		} else {
			hb.isActive = false;
			hb.hitTargets.clear();
		}
	}
};

} // namespace Game
