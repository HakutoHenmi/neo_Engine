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
#include "../Engine/QuadTree.h"

namespace Game {

// 物理のシステム
class PhysicsSystem : public ISystem {
public:
	// PLAY 中の物理の更新
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: GPU の依頼と速度を消す
	void Reset(entt::registry& registry) override {
		// フレーム枠ごとの依頼を消す
		for (auto& requests : m_pendingGpuRequests) requests.clear();
		// すべての剛体の速度を 0 にする
		auto view = registry.view<RigidbodyComponent>();
		for (auto entity : view) {
			auto& rb = registry.get<RigidbodyComponent>(entity);
			rb.velocity = {0, 0, 0};
		}
	}

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
		::Engine::Vector3 aabbMin = { 0.0f, 0.0f, 0.0f };
		::Engine::Vector3 aabbMax = { 0.0f, 0.0f, 0.0f };
		::Engine::Matrix4x4 world = ::Engine::Matrix4x4::Identity();
	};
	// GPU に依頼した衝突判定 (対象と結果の番号)
	struct GpuRequest {
		entt::entity entity;
		uint32_t resultIdx;
	};

	// 動く箱・静的メッシュ・フレーム枠ごとの GPU の依頼・近くの候補
	std::vector<CollidableBox> m_dynamics;
	std::vector<CollidableMesh> m_statics;
	std::array<std::vector<GpuRequest>, 2> m_pendingGpuRequests;
	std::vector<uint32_t> m_nearbyEntities;

	// ワールド行列と箱コライダーから OBB の中心・軸・半分の大きさを求める
	static void GetObbAxes(const ::Engine::Matrix4x4& mat, const BoxColliderComponent& cb,
		::Engine::Vector3 axes[3], ::Engine::Vector3& center, ::Engine::Vector3& extents) {
		// 箱の中心をワールドへ変換する
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&mat));
		DirectX::XMVECTOR c = DirectX::XMVector3TransformCoord(
			DirectX::XMVectorSet(cb.center.x, cb.center.y, cb.center.z, 1.0f), worldMat);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&center), c);

		// 行列の各行を正規化したものが OBB の 3 本の軸
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[0]), DirectX::XMVector3Normalize(worldMat.r[0]));
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[1]), DirectX::XMVector3Normalize(worldMat.r[1]));
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[2]), DirectX::XMVector3Normalize(worldMat.r[2]));

		// 行列を分解して拡大率を取り出し、箱の半分の大きさに掛ける
		DirectX::XMVECTOR aScale, aRot, aTrans;
		DirectX::XMMatrixDecompose(&aScale, &aRot, &aTrans, worldMat);

		extents.x = cb.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(aScale));
		extents.y = cb.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(aScale));
		extents.z = cb.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(aScale));
	}
};

} // namespace Game
