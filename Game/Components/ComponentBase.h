// ComponentBase.h
// コンポーネントの共通部分: 基底の Component、コンポーネントの種類の番号、当たりの情報、エンティティの別名。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
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
#include "../../Engine/ParticleEmitter.h"
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

// エンティティ ID をそのまま扱うための別名
using Entity = entt::entity;

} // namespace Game
