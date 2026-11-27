// PhysicsSystem.h
// 剛体の移動 (重力・減衰)、箱同士の衝突 (OBB の分離軸判定)、
// 箱と静的メッシュの衝突 (GPU でまとめて判定し、次に同じフレーム枠を使うときに結果を反映) を行うシステム。
#pragma once
#include "ISystem.h"
#include <cmath>
#include <cfloat>
#include <vector>
#include <array>
#include <algorithm>
#include "Collision/QuadTree.h"

namespace Game {

// 物理のシステム
class PhysicsSystem : public ISystem {
public:
	// PLAY 中の物理の更新
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: GPU の依頼と速度を消す
	void Reset(entt::registry& registry) override;

private:
	// 衝突判定用の箱 (OBB の軸・中心・半分の大きさ・AABB)
	struct CollidableBox {
		entt::entity entity;
		::Engine::Vector3 axes[3], center, extents;
		::Engine::Vector3 aabbMin, aabbMax;
	};
	// 衝突判定用の静的メッシュ (モデル・AABB・ワールド行列)
	struct CollidableMesh {
		entt::entity entity = entt::null;
		uint32_t meshHandle = 0;
		::Engine::Vector3 aabbMin = {0.0f, 0.0f, 0.0f};
		::Engine::Vector3 aabbMax = {0.0f, 0.0f, 0.0f};
		::Engine::Matrix4x4 world = ::Engine::Matrix4x4::Identity();
	};
	// GPU に依頼した衝突判定 (対象と結果の番号)
	struct GpuRequest {
		entt::entity entity;
		uint32_t resultIdx;
	};

	// 動く箱・静的メッシュ・フレーム枠ごとの GPU の依頼・近くの候補
	std::vector<CollidableBox> dynamics_;
	std::vector<CollidableMesh> statics_;
	std::array<std::vector<GpuRequest>, 2> pendingGpuRequests_;
	std::vector<uint32_t> nearbyEntities_;

	// ワールド行列と箱コライダーから OBB の中心・軸・半分の大きさを求める
	static void GetObbAxes(
	    const ::Engine::Matrix4x4& mat, const BoxColliderComponent& cb, ::Engine::Vector3 axes[3],
	    ::Engine::Vector3& center, ::Engine::Vector3& extents);
};

} // namespace Game
