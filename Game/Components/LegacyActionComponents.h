// LegacyActionComponents.h
// 旧アクションゲーム (缶・ボス戦) のコンポーネント: 武器と缶、状態異常、入力・移動・カメラ追従、
// 攻撃判定と食らい判定、落とした体の回収、パリィ、ボスの行動と部位。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "CoreComponents.h"

namespace Game {

// 武器の種類 (旧アクションゲーム用)
enum class WeaponType {
	Greatsword,    // 大剣
	DualBlades,    // 双剣
	MultiBlades    // 浮遊機巧剣（ファンネルソード）
};

// 缶の種類 (旧アクションゲーム用。属性ごとに攻撃効果が変わる)
enum class CanType : uint32_t {
	None = 0,
	Fire,
	Water,
	Thunder,
	Soda,
	Ice,
	Magnet,
	Acid,
	Bubble
};

// 缶の攻撃が当たった相手に付ける効果 (種類・残り時間・強さ)。旧アクションゲーム用
struct CanAttackEffectComponent : public Component {
	CanType canType = CanType::None;
	float duration = 0.0f;
	float strength = 0.0f;
	CanAttackEffectComponent() { type = static_cast<ComponentType>(999); }
};

// 缶の状態異常 (凍結・酸・泡) の残り時間と、見た目用エンティティ。旧アクションゲーム用
struct CanStatusComponent : public Component {
	float freezeTimer = 0.0f;
	float acidTimer = 0.0f;
	float acidTickTimer = 0.0f;
	float bubbleTimer = 0.0f;
	float bubbleBaseY = 0.0f;
	bool bubblePositionSaved = false;
	bool baseColorSaved = false;
	DirectX::XMFLOAT4 baseColor = {1, 1, 1, 1};
	entt::entity iceVisual = entt::null;
	entt::entity acidVisual = entt::null;
	entt::entity bubbleVisual = entt::null;
	CanStatusComponent() { type = static_cast<ComponentType>(999); }
};

// 酸の水たまり。範囲内の敵に一定間隔でダメージを与える
struct AcidPoolComponent : public Component {
	float radius = 5.0f;
	float tickTimer = 0.0f;
	float pulseTime = 0.0f;
	AcidPoolComponent() { type = static_cast<ComponentType>(999); }
};

// 泡のシールド。残り時間と防げる回数
struct BubbleShieldComponent : public Component {
	float timer = 0.0f;
	int charges = 0;
	entt::entity visualEntity = entt::null;
	BubbleShieldComponent() { type = static_cast<ComponentType>(999); }
};

// 状態異常の見た目の種類
enum class CanVisualKind : uint32_t {
	IceStatus,
	AcidStatus,
	BubbleStatus,
	BubbleShield
};

// 状態異常の見た目エンティティが、どの対象のどの効果を表すか
struct CanStatusVisualComponent : public Component {
	entt::entity target = entt::null;
	CanVisualKind kind = CanVisualKind::IceStatus;
	CanStatusVisualComponent() { type = static_cast<ComponentType>(999); }
};

// プレイヤーの武器管理 (旧アクションゲーム用)
struct PlayerWeaponComponent : public Component {
	WeaponType currentWeapon = WeaponType::Greatsword;
	std::vector<entt::entity> weaponEntities; // 武器の見た目用エンティティ
	PlayerWeaponComponent() { type = static_cast<ComponentType>(999); }
};

// プレイヤー入力 (このフレームでしたいこと)。PlayerInputSystem が書き込む
struct PlayerInputComponent : public Component {
	DirectX::XMFLOAT2 moveDir = {0, 0};
	bool jumpRequested = false;
	bool attackRequested = false;

	// マウス操作によるカメラの旋回量 (入力の意思)
	float cameraYaw = 0.0f;
	float cameraPitch = 0.0f;
	
	entt::entity lockedEnemy = entt::null; // ロックオン中の敵エンティティ

	bool isRadialMenuOpen = false; // 缶選択のラジアルメニューを開いているか
	CanType selectedCan = CanType::None; // 選択中の缶

	PlayerInputComponent() { type = ComponentType::PlayerInput; }
};

// キャラクターの移動能力 (速さ・ジャンプ力・重力) と接地状態
struct CharacterMovementComponent : public Component {
	float speed = 5.0f;
	float jumpPower = 12.0f;
	float gravity = 9.8f;
	float velocityY = 0.0f;
	float heightOffset = 1.0f; // 地面からの高さオフセット (足元を地面に合わせる)
	bool isGrounded = false;
	bool enabled = true;
	CharacterMovementComponent() { type = ComponentType::CharacterMovement; }
};

// カメラが追いかける対象と、その距離・高さ・追従の滑らかさ
struct CameraTargetComponent : public Component {
	float distance = 10.0f;
	float distanceOffset = 0.0f; // アクション中だけ一時的にズームアウトする量
	float height = 3.0f;
	float smoothSpeed = 5.0f;
	CameraTargetComponent() { type = ComponentType::CameraTarget; }
};

// 攻撃判定 (この箱に Hurtbox が重なるとダメージ)
struct HitboxComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0}; // ローカルオフセット
	DirectX::XMFLOAT3 size = {1, 1, 1};    // 判定サイズ
	float damage = 10.0f;                  // ダメージ量
	bool isActive = false;                 // 有効フラグ（攻撃アニメ中のみtrue等）
	TagType tag = TagType::Default;           // 識別タグ (剣・弾など)
	std::vector<entt::entity> hitTargets; // 1回の攻撃で既に当たった相手 (多段ヒット防止)
	bool isProjectile = false;             // 弾として飛ぶ判定か
	HitboxComponent() { type = ComponentType::Hitbox; }
};

// Hurtbox コンポーネント (食らい判定)
struct HurtboxComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0}; // ローカルオフセット
	DirectX::XMFLOAT3 size = {1, 1, 1};    // 判定サイズ
	TagType tag = TagType::Default;              // 識別タグ
	float damageMultiplier = 1.0f;         // ダメージ倍率 (頭部=2.0等)
	HurtboxComponent() { type = ComponentType::Hurtbox; }
};

// 被弾で散った体液 (拾うと HP・スタミナが戻る)。旧アクションゲーム用
struct LostFluidPickupComponent : public Component {
	entt::entity owner = entt::null;
	DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
	float hpRestore = 1.0f;
	float staminaRestore = 1.0f;
	float lifetime = -1.0f;          // 残り表示時間 (負の値は回収されるかシーンを戻すまで残る)
	float magnetDelay = 0.9f;
	float magnetRadius = 6.5f;
	float collectRadius = 3.0f;
	float visualEmitTimer = 0.0f;
	uint32_t visualGroupId = 0;

	LostFluidPickupComponent() { type = static_cast<ComponentType>(999); }
};

// パリィ時の画面の歪みエフェクト (地面の衝撃波にも使う)
struct ParryDistortionComponent {
	float timer = 0.0f;
	float duration = 0.5f;
	float startScale = 0.5f;
	float endScale = 12.0f;
	bool isBillboard = true; // 常にカメラを向くか (地面の衝撃波は false)
};

// ボスの攻撃の種類
enum class BossAttackType {
	Thrust,       // 突進（デフォルト）
	TailSpin,     // 尻尾なぎ払い（大回転）
	JumpPress,    // 飛びかかりプレス（衝撃波）
	Punch,        // パンチ
	BeamSweep     // ビームでのなぎ払い
};

// ボスの攻撃パターン1つ分 (溜め・攻撃・硬直の長さ、射程、威力)
struct BossActionPattern {
	std::string name;
	BossAttackType type = BossAttackType::Thrust; // 攻撃のタイプ
	float windUpDuration = 0.8f;
	float activeDuration = 0.3f;
	float recoveryDuration = 1.0f;
	float range = 5.0f;
	float damage = 20.0f;
	float thrustForce = 10.0f; // 攻撃時の前進力（Thrust用）
};

// ボスの状態 (待機・追跡・溜め・攻撃・硬直・怯み・ダウン・死亡・逃走)
enum class BossState : uint32_t {
	Idle = 0, Chase, WindUp, Attack, Cooldown, Stunned, Down, Dead, Run
};

// ボスの行動を管理する状態と、攻撃パターンの一覧
struct BossActionComponent : public Component {
	BossState state = BossState::Idle;
	float stateTimer = 0.0f;
	
	float chaseSpeed = 3.0f;
	float rotationSpeed = 4.0f;
	float stunDuration = 2.0f; // パリィされたときの怯み時間
	
	int currentPatternIndex = -1;
	std::vector<BossActionPattern> patterns;
	entt::entity currentBeamEntity = entt::null; // ビーム攻撃中に生成したビームのエンティティ
	float turnDirection = 0.0f;
	
	BossActionComponent() { type = ComponentType::BossAction; }
};

// ボスの部位 (壊せる部位の HP と、本体へのダメージ伝達)
struct BodyPartComponent : public Component {
	entt::entity parentEntity = entt::null; // 本体 (BossAction を持つエンティティ)
	float hp = 100.0f;
	float maxHp = 100.0f;
	bool isDestroyed = false;
	float damageMultiplierToParent = 0.5f; // 部位へのダメージを本体へどれだけ伝えるか
	std::string partName = "Part";
	BodyPartComponent() { type = ComponentType::BodyPart; }
};

} // namespace Game
