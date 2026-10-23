// ObjectTypes.h
// ゲーム (アプリケーション層) で使う ECS コンポーネントと列挙型の定義をまとめたヘッダー。
// シーン上のエンティティ (entt::entity) はここにある構造体を組み合わせて振る舞いを持つ。
// エディタの保存・読み込み (EditorUI.cpp) もこの定義に合わせて JSON を読み書きする。
#pragma once
// Windows.h の min/max マクロが std::min / std::max と衝突しないようにする
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <type_traits>
#include <cmath>
#include <algorithm>
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>
#include "../Engine/ParticleEmitter.h"
#include <map>
#include <memory> 

// ECS ライブラリ (entt)。エンティティとコンポーネントの登録に使う
#include "../../externals/entt/entt.hpp"

namespace Game {

class IScript; // C++ スクリプト (IScript) の前方宣言。ScriptEntry が shared_ptr で保持する

// 旧ステージエディタで使っていた配置オブジェクトの種類 (Model は読み込んだメッシュ)
enum class ObjectType : uint32_t {
	Cube = 0,
	Slope = 1,
	Ball = 2,
	LongFloor = 3,
	Model = 999,
};

// メッシュとの当たり判定に使う頂点・インデックスの組
struct CollisionMeshData {
	std::vector<DirectX::XMVECTOR> vertices;
	std::vector<int> indices;
};

// 衝突情報 (GPU から読み戻す。並びとオフセットはシェーダー側の構造体と一致させる)
struct ContactInfo {
	DirectX::XMFLOAT3 normal;   // offset 0
	float depth;                // offset 12
	DirectX::XMFLOAT3 position; // offset 16
	uint32_t intersected;     // offset 28
};

// コンポーネントの種類。エディタで種類を判別するために Component 基底が持つ
// (列挙に無い実行時専用のコンポーネントは 999 を入れて区別している)
enum class ComponentType { 
	MeshRenderer, BoxCollider, Tag, Animator, Rigidbody, ParticleEmitter, 
	GpuMeshCollider, PlayerInput, CharacterMovement, CameraTarget,
	DirectionalLight, PointLight, SpotLight,
	AudioSource, AudioListener, Hitbox, Hurtbox, Health, // 音響・攻撃判定・ステータス
	Script, // スクリプト
	RectTransform, UIImage, UIText, UIButton, // UI
	River, // 川 (スプラインに沿った水面)
	Variable, // 汎用の変数置き場
	WorldSpaceUI, // ワールド空間 UI (頭上の HP バーなど)
	Motion, // モーションエディタ用キーフレーム
	PlayerAction, // プレイヤーアクション (攻撃・パリィ・回避)
	BossAction,   // ボスの行動ステートマシン
	BodyPart,     // ボスの部位破壊
	FluidEmitter  // 流体エミッター (水など)
};
// 全コンポーネント共通の基底。種類と有効/無効だけを持つ
struct Component { 
	ComponentType type = ComponentType::MeshRenderer; 
	bool enabled = true; 
	Component() = default;
	Component(ComponentType t) : type(t), enabled(true) {}
};

// エンティティの表示名 (Hierarchy に表示され、保存時の名前にもなる)
struct NameComponent {
	std::string name = "Object";
	NameComponent() = default;
	NameComponent(const std::string& n) : name(n) {}
};

// 親子関係。parentId が有効なら親の行列を掛けてワールド行列を求める
struct HierarchyComponent {
	entt::entity parentId = entt::null;
};

// 位置・回転 (ラジアン)・拡大率。ワールド行列の元になる
struct TransformComponent : public Component {
	DirectX::XMFLOAT3 translate = {0, 0, 0};
	DirectX::XMFLOAT3 rotate = {0, 0, 0};
	DirectX::XMFLOAT3 scale = {1, 1, 1};
	
	TransformComponent() { type = ComponentType::RectTransform; }
	TransformComponent(const DirectX::XMFLOAT3& t, const DirectX::XMFLOAT3& r, const DirectX::XMFLOAT3& s)
		: translate(t), rotate(r), scale(s) { type = ComponentType::RectTransform; }

	// エンジン側の Transform 型へ変換する
	Engine::Transform GetTransform() const {
		Engine::Transform t;
		t.translate = {translate.x, translate.y, translate.z};
		t.rotate = {rotate.x, rotate.y, rotate.z};
		t.scale = {scale.x, scale.y, scale.z};
		return t;
	}
	// 拡大→回転→平行移動の順でローカル行列を作る
	Engine::Matrix4x4 ToMatrix() const {
		using namespace DirectX;
		XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
		XMMATRIX R = XMMatrixRotationRollPitchYaw(rotate.x, rotate.y, rotate.z);
		XMMATRIX T = XMMatrixTranslation(translate.x, translate.y, translate.z);
		Engine::Matrix4x4 out;
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&out), S * R * T);
		return out;
	}
};

// 描画色だけを上書きしたいときに付ける色
struct ColorComponent {
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
};

// エディタ専用の状態。locked はビューポートでクリック選択させない設定
struct EditorStateComponent {
	bool locked = false;
	bool isPendingDestroy = false;
};

// メッシュを描画するための情報 (モデル・テクスチャ・シェーダー・色)
struct MeshRendererComponent : public Component {
	// Renderer が返すモデル/テクスチャのハンドル (0 は未読み込み)
	uint32_t modelHandle = 0;
	uint32_t textureHandle = 0;
	// 保存用のファイルパス (読み込み時にここからハンドルを作り直す)
	std::string modelPath;
	std::string texturePath;
	// 乗算色 (RGBA)
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	// マテリアルとライトマップの設定 (UV の繰り返し・ずらし、焼き込み光)
	DirectX::XMFLOAT2 uvTiling = {1, 1};
	DirectX::XMFLOAT2 uvOffset = {0, 0};
	uint32_t lightmapHandle = 0;
	std::string lightmapPath;
	std::vector<uint32_t> extraTextureHandles; // 追加テクスチャのハンドル (法線マップなど)
	std::vector<std::string> extraTexturePaths; // 追加テクスチャのファイルパス
	std::string shaderName = "Default"; // 使用するシェーダー名
	float reflectivity = 0.0f; // 環境マップの反射率 (0.0〜1.0)
	bool useCubemap = true; // キューブマップを使うか (課題要件のため初期値は true)
	MeshRendererComponent() { type = ComponentType::MeshRenderer; }
};

// 箱型の当たり判定。center はローカルオフセット、size は各軸の大きさ
struct BoxColliderComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0};
	DirectX::XMFLOAT3 size = {1, 1, 1};
	bool isTrigger = false;
	BoxColliderComponent() { type = ComponentType::BoxCollider; }
};

// GPU メッシュコライダーの判定方式
enum class MeshCollisionType {
	Mesh,   // 全ポリゴン
	Convex, // 凸包近似（簡易化）
};

// GPU で判定するメッシュコライダー
struct GpuMeshColliderComponent : public Component {
	uint32_t meshHandle = 0;
	std::string meshPath = "";
	MeshCollisionType collisionType = MeshCollisionType::Mesh; // 判定方式 (全ポリゴンか凸包近似か)
	bool isTrigger = false;
	bool isIntersecting = false; // 最新の判定結果 (重なっていれば true)
	GpuMeshColliderComponent() { type = ComponentType::GpuMeshCollider; }
};

// スキニングモデルのアニメーション再生状態
struct AnimatorComponent : public Component {
	std::string currentAnimation;
	float time = 0.0f;
	float speed = 1.0f;
	bool isPlaying = false;
	bool loop = true;
	bool drawSkeleton = false; // デバッグ用に骨 (スケルトン) を線で描くか
	// 直前のアニメーションと、切り替え時に混ぜる (クロスフェード) ための情報
	
	std::string prevAnimation;
	float prevTime = 0.0f;
	bool prevLoop = true;
	float crossfadeTimer = 0.0f;
	float crossfadeDuration = 0.0f;
	
	AnimatorComponent() { type = ComponentType::Animator; }
};

// エンティティの役割を表すタグ。検索 (GameScene::GetEntitiesByTag) や当たり判定の相手判定に使う
enum class TagType : uint32_t {
	Untagged = 0,
	Player,
	Enemy,
	Bullet,
	Projectile,
	Wall,
	Default,
	VFX,
	PauseUI // ポーズメニューの UI
};

// タグ → 文字列 (保存・表示用)
inline const char* TagToString(TagType tag) {
	switch (tag) {
	case TagType::Player: return "Player";
	case TagType::Enemy: return "Enemy";
	case TagType::Bullet: return "Bullet";
	case TagType::Projectile: return "Projectile";
	case TagType::Wall: return "Wall";
	case TagType::VFX: return "VFX";
	case TagType::Default: return "Default";
	case TagType::PauseUI: return "PauseUI";
	default: return "Untagged";
	}
}

// 文字列 → タグ (読み込み用)。知らない文字列は Untagged
inline TagType StringToTag(const std::string& s) {
	if (s == "Player") return TagType::Player;
	if (s == "Enemy") return TagType::Enemy;
	if (s == "Bullet") return TagType::Bullet;
	if (s == "Projectile") return TagType::Projectile;
	if (s == "Wall") return TagType::Wall;
	if (s == "VFX") return TagType::VFX;
	if (s == "Default") return TagType::Default;
	if (s == "PauseUI") return TagType::PauseUI;
	return TagType::Untagged;
}

// タグを持たせるコンポーネント
struct TagComponent : public Component {
	TagType tag = TagType::Untagged;
	TagComponent() { type = ComponentType::Tag; }
	TagComponent(TagType t) : tag(t) { type = ComponentType::Tag; }
};

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

// 物理挙動。速度・重力の有無・キネマティック (外部からだけ動かす) か
struct RigidbodyComponent : public Component {
	DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
	bool useGravity = true;
	bool isKinematic = false;
	RigidbodyComponent() { type = ComponentType::Rigidbody; }
};

// CPU パーティクルの発生源
struct ParticleEmitterComponent : public Component {
	Engine::ParticleEmitter emitter;
	std::string assetPath = ""; // 保存されたパーティクル設定ファイル (.json) のパス
	bool isInitialized = false;

	ParticleEmitterComponent() { type = ComponentType::ParticleEmitter; }
};

// ライト (平行光源・点光源・スポットライト)
// 平行光源 (太陽光)
struct DirectionalLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	DirectionalLightComponent() { type = ComponentType::DirectionalLight; }
};

// 点光源
struct PointLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 10.0f;
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f}; // 減衰係数 (一定・線形・二次)
	PointLightComponent() { type = ComponentType::PointLight; }
};

// スポットライト
struct SpotLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 20.0f;
	float innerCos = 0.98f; // 内側の角度の cos (ここまでは最大の明るさ)
	float outerCos = 0.90f; // 外側の角度の cos (ここで 0 になる)
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f};
	SpotLightComponent() { type = ComponentType::SpotLight; }
};

// 音の分類 (音量設定を BGM と効果音で分けるため)
enum class AudioCategory { BGM, SE };

// 音の発信源 (再生するファイル・音量・ループ・3D 減衰)
struct AudioSourceComponent : public Component {
	std::string soundPath = "";       // 音声ファイルパス
	uint32_t soundHandle = 0xFFFFFFFF; // Audio::Load()で取得したハンドル
	size_t voiceHandle = 0;            // 再生中のボイスハンドル
	float volume = 1.0f;               // 音量 (0.0〜1.0)
	bool loop = false;                 // ループ再生
	bool playOnStart = false;          // Play時に自動再生
	bool is3D = true;                  // 3Dサウンド（距離減衰あり）
	float maxDistance = 50.0f;         // 減衰最大距離
	bool isPlaying = false;            // 再生中フラグ
	AudioCategory category = AudioCategory::SE; // 音のカテゴリ (BGM / 効果音)
	AudioSourceComponent() { type = ComponentType::AudioSource; }
};

// 音を聞く位置 (通常はカメラに付ける)
struct AudioListenerComponent : public Component {
	AudioListenerComponent() { type = ComponentType::AudioListener; }
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

// HP・スタミナと被弾演出の状態。HP は直接書き換えず関数経由で操作する
class HealthComponent : public Component {
public:
    // 現在値の取得
    float Hp() const { return hp_; }
    float MaxHp() const { return maxHp_; }
    float Stamina() const { return stamina_; }
    float MaxStamina() const { return maxStamina_; }
    bool IsDead() const { return isDead_; }

    // 初期化・エディタでの変更・保存データの復元用に値を直接設定する
    // 既存のシーンファイルやサンドバッグの値を保つため、ここでは範囲に収めない
    void SetHp(float value) { hp_ = value; }
    void SetMaxHp(float value) { maxHp_ = value; }
    void SetDead(bool dead) { isDead_ = dead; }
    // ダメージ・回復 (0〜最大値に収める)
    void ApplyDamage(float damage) { hp_ = (std::max)(0.0f, hp_ - damage); }
    void RecoverHp(float amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }
    void RecoverStamina(float amount) { stamina_ = (std::min)(maxStamina_, stamina_ + amount); }
    // HP を消費して技を使う (足りなければ false)
    bool TryConsumeHp(float cost) {
        if (hp_ >= cost) {
            hp_ -= cost;
            return true;
        }
        return false;
    }
    // HP が 0 以下なら最大値まで戻す (リスタート用)
    void RestoreHpIfDepleted() { if (hp_ <= 0) hp_ = maxHp_; }

	float recoverableFluid = 0.0f;   // 被弾で散った、回収可能な体液量
	int damageTakenCount = 0;        // 被ダメージ回数（リザルト表示用）
	float invincibleTime = 0.0f;     // 残り無敵時間（ゼロ以上なら無敵）

	// ヒット演出用 (白く光る時間・一時停止の時間・元の色)
	float hitFlashTimer = 0.0f;      // ヒットフラッシュ（白く光る）
	float hitStopTimer = 0.0f;       // ヒットストップ（一時停止）
	DirectX::XMFLOAT4 baseColor = { 1, 1, 1, 1 }; // 元の色保存用
	bool baseColorSaved = false;

	HealthComponent() { type = ComponentType::Health; }
// HP・スタミナの実体 (外から直接書き換えさせない)
private:
    float hp_ = 100.0f;
    float maxHp_ = 100.0f;
    float stamina_ = 100.0f;
    float maxStamina_ = 100.0f;
    bool isDead_ = false;
};

// UI の配置 (スクリーン座標・大きさ・アンカー・ピボット・回転)
struct RectTransformComponent : public Component {
	DirectX::XMFLOAT2 pos = {0, 0};   // スクリーン座標
	DirectX::XMFLOAT2 size = {100, 100};
	DirectX::XMFLOAT2 anchor = {0.5f, 0.5f}; // 0.0〜1.0
	DirectX::XMFLOAT2 pivot = {0.5f, 0.5f};
	float rotation = 0.0f;
	RectTransformComponent() { type = ComponentType::RectTransform; }
};

// UI の画像。9-Slice は角を伸ばさずに拡縮する設定
struct UIImageComponent : public Component {
	uint32_t textureHandle = 0;
	std::string texturePath = "";
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	bool is9Slice = false;
	float borderTop = 10.0f;
	float borderBottom = 10.0f;
	float borderLeft = 10.0f;
	float borderRight = 10.0f;
	int layer = 0; // 描画レイヤー (大きいほど手前)
	UIImageComponent() { type = ComponentType::UIImage; }
};

// UI の文字列
struct UITextComponent : public Component {
	std::string text = "New Text";
	float fontSize = 24.0f;
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	std::string fontPath = "C:\\Windows\\Fonts\\msgothic.ttc"; // 使用するフォントファイル
	UITextComponent() { type = ComponentType::UIText; }
};

// UI のボタン。ホバー/押下状態と色、押されたときに呼ぶ名前
struct UIButtonComponent : public Component {
	bool isHovered = false;
	bool isPressed = false;
	DirectX::XMFLOAT4 normalColor = {1, 1, 1, 1};
	DirectX::XMFLOAT4 hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
	DirectX::XMFLOAT4 pressedColor = {0.6f, 0.6f, 0.6f, 1.0f};
	std::string onClickCallback = ""; // スクリプト側のメソッド名など
	
	// クリック判定の範囲を見た目とは別に調整する (中心のずれと拡大率)
	DirectX::XMFLOAT2 hitboxOffset = {0.0f, 0.0f};
	DirectX::XMFLOAT2 hitboxScale = {1.0f, 1.0f};

	UIButtonComponent() { type = ComponentType::UIButton; }
};

// スクリプト1つ分の情報 (クラス名・保存パラメータ・実体)
struct ScriptEntry {
	std::string scriptPath = "";      // スクリプトのクラス名 (例: "PlayerScript")
	std::string parameterData = "";   // 初期化用パラメータ(JSON)
	std::shared_ptr<IScript> instance = nullptr; // C++スクリプトのインスタンス
	bool isStarted = false; // PlayモードでStart()が呼ばれたか
};

// スクリプトコンポーネント (1つのエンティティに複数のスクリプトを付けられる)
struct ScriptComponent : public Component {
	std::vector<ScriptEntry> scripts;
	ScriptComponent() { type = ComponentType::Script; }
};

// 川 (制御点を通るスプラインに沿って水面メッシュを作る)
struct RiverComponent : public Component {
	std::vector<DirectX::XMFLOAT3> points; // スプライン制御点 (ローカル座標)
	float width = 2.0f;                    // 川の基本幅
	float flowSpeed = 1.0f;                // 流れの速さ
	float uvScale = 1.0f;
	uint32_t meshHandle = 0;               // 動的生成メッシュハンドル
	std::string texturePath = "Resources/Water/water.png";
	RiverComponent() { type = ComponentType::River; }
};

// ワールド空間UIコンポーネント
struct WorldSpaceUIComponent : public Component {
	bool showHealthBar = true;
	bool showDamageNumbers = true;
	DirectX::XMFLOAT3 offset = {0, 1.2f, 0};
	float barWidth = 60.0f;
	float barHeight = 6.0f;
	WorldSpaceUIComponent() { type = ComponentType::WorldSpaceUI; }
};

// ダメージ数字の表示
struct DamageNumberComponent : public Component {
	float damage = 0.0f;
	float lifetime = 1.0f;
	float maxLifetime = 1.0f;
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT3 startPos = {0,0,0};
	DamageNumberComponent() { type = static_cast<ComponentType>(999); } // 数字が浮かび上がって消えるまでの表示状態 (実行時専用)
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

// 指定時間が経つと自動で削除される
struct AutoDestroyComponent : public Component {
	float timer = 1.0f;
	AutoDestroyComponent() { type = static_cast<ComponentType>(999); }
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

// 汎用の変数置き場 (スクリプト間で数値・文字列を受け渡す)
struct VariableComponent : public Component {
	std::map<std::string, float> values;
	std::map<std::string, std::string> strings; // 文字列の値
	VariableComponent() { type = ComponentType::Variable; }

	float GetValue(const std::string& key, float defaultVal = 0.0f) const {
		auto it = values.find(key);
		return (it != values.end()) ? it->second : defaultVal;
	}
	void SetValue(const std::string& key, float val) {
		values[key] = val;
	}

	std::string GetString(const std::string& key, const std::string& defaultVal = "") const {
		auto it = strings.find(key);
		return (it != strings.end()) ? it->second : defaultVal;
	}
	void SetString(const std::string& key, const std::string& val) {
		strings[key] = val;
	}
};

// モーションエディタで作るキーフレームアニメーション
struct MotionComponent : public Component {
	// キーフレーム (時刻と、その時点の位置・回転・拡大)
	struct Keyframe {
		float time = 0.0f;
		DirectX::XMFLOAT3 translate = {0, 0, 0};
		DirectX::XMFLOAT3 rotate = {0, 0, 0};
		DirectX::XMFLOAT3 scale = {1, 1, 1};
	};

	// 1つのモーション (キーフレーム列と長さ、ループ)
	struct MotionClip {
		std::string name;
		std::vector<Keyframe> keyframes;
		float totalDuration = 1.0f;
		bool loop = false;
	};

	// 名前で引くモーション一覧と、再生中の状態
	std::map<std::string, MotionClip> clips;
	std::string activeClip = "Default";
	
	float currentTime = 0.0f;
	bool isPlaying = false;
	int selectedKeyframe = -1; // エディタで選択中のキーフレーム番号

	MotionComponent() { 
		type = ComponentType::Motion; 
		// 最初から1つ「Default」クリップを用意しておく
		MotionClip defaultClip;
		defaultClip.name = "Default";
		defaultClip.keyframes.push_back({0.0f, {0,0,0}, {0,0,0}, {1,1,1}});
		defaultClip.keyframes.push_back({1.0f, {5,0,0}, {0,0,0}, {1,1,1}});
		clips["Default"] = defaultClip;
	}

	// 指定した名前のモーションを最初から再生する
	void PlayAnimation(const std::string& clipName) {
		if (clips.find(clipName) != clips.end()) {
			activeClip = clipName;
			currentTime = 0.0f;
			isPlaying = true;
		}
	}
};


// エンティティ ID をそのまま扱うための別名
using Entity = entt::entity;


// 流体エミッター (毎フレーム GPU 流体に粒子を追加する)
struct FluidEmitterComponent : public Component {
	int emitCountPerFrame = 400;
	float emissionRemainder = 0.0f; // 1フレームに出しきれなかった端数 (実行時専用)
	bool retainWater = false; // 水のプレビュー: 落ち着いた粒子をリセットまで残す
	int particleLimit = 0; // 出す粒子の上限 (0 は上限なしで出し続ける)
	int emittedParticles = 0; // 前回の流体リセットから出した数 (実行時専用)
	DirectX::XMFLOAT4 color = {0.2f, 0.8f, 1.0f, 1.0f}; // 湧き水の色
	DirectX::XMFLOAT3 velocity = {0.0f, -5.0f, 0.0f};
	float fluidType = 1.0f; // 流体の種類 (0 = プレイヤーの核, 1 = 水しぶき)
	
	FluidEmitterComponent() { type = ComponentType::FluidEmitter; }
};

// プレイヤーの丸影を追従させるための影エンティティ
struct PlayerShadowComponent : public Component {
	entt::entity shadowEntity = entt::null;
	PlayerShadowComponent() { type = static_cast<ComponentType>(999); }
};

} // namespace Game
