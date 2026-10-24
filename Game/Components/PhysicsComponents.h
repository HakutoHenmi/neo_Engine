// PhysicsComponents.h
// 物理と当たり判定のコンポーネント: 箱の当たり判定・メッシュの当たり判定・剛体。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "ComponentBase.h"

namespace Game {

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

// 物理挙動。速度・重力の有無・キネマティック (外部からだけ動かす) か
struct RigidbodyComponent : public Component {
	DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
	bool useGravity = true;
	bool isKinematic = false;
	RigidbodyComponent() { type = ComponentType::Rigidbody; }
};

} // namespace Game
