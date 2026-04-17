#include "ISystem.h"
#include <cmath>
#include "../Engine/Time/TimeManager.h" 
#include "../Engine/QuadTree.h"
#include "../Scripts/HitDistortionScript.h"
#include "GameScene.h"

namespace Game {

class CombatSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// --- QuadTree の構築 (Hurtbox と BoxCollider を対象) ---
		::Engine::PhysicsQuadTree qt(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);
		
		m_hurters.clear();
		auto hurtboxView = registry.view<HurtboxComponent, TransformComponent>();
		for (auto entity : hurtboxView) {
			auto& hb = hurtboxView.get<HurtboxComponent>(entity);
			auto& tc = hurtboxView.get<TransformComponent>(entity);
			if (!hb.enabled) continue;

			::Engine::Matrix4x4 world = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(entity)) : tc.ToMatrix();
			DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
			
			// AABBを計算してQuadTreeに登録
			DirectX::XMVECTOR center = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hb.center)), worldMat);
			DirectX::XMVECTOR scale, rot, trans;
			DirectX::XMMatrixDecompose(&scale, &rot, &trans, worldMat);
			float ex = hb.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(scale));
			float ey = hb.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(scale));
			float ez = hb.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(scale));

			float cx = DirectX::XMVectorGetX(center), cy = DirectX::XMVectorGetY(center), cz = DirectX::XMVectorGetZ(center);
			
			HurtDeviceInfo info;
			info.entity = entity;
			info.center = {cx, cy, cz};
			info.extents = {ex, ey, ez};
			
			DirectX::XMFLOAT4X4 wm;
			DirectX::XMStoreFloat4x4(&wm, worldMat);
			info.axes[0] = { wm._11, wm._12, wm._13 };
			info.axes[1] = { wm._21, wm._22, wm._23 };
			info.axes[2] = { wm._31, wm._32, wm._33 };

			// 正規化
			for(int i=0; i<3; ++i) {
				float len = std::sqrt(info.axes[i].x*info.axes[i].x + info.axes[i].y*info.axes[i].y + info.axes[i].z*info.axes[i].z);
				if(len > 0.0001f) { info.axes[i].x /= len; info.axes[i].y /= len; info.axes[i].z /= len; }
			}

			uint32_t idx = static_cast<uint32_t>(m_hurters.size());
			m_hurters.push_back(info);
			qt.Insert(idx, cx - ex, cz - ez, cx + ex, cz + ez);
		}

		// Hitboxを持つエンティティの更新
		auto attackerView = registry.view<HitboxComponent, TransformComponent>();
		for (auto attackerEntity : attackerView) {
			auto& hitbox = attackerView.get<HitboxComponent>(attackerEntity);
			if (!hitbox.enabled || !hitbox.isActive) continue;

			::Engine::Matrix4x4 aWorld = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(attackerEntity)) : registry.get<TransformComponent>(attackerEntity).ToMatrix();
			DirectX::XMMATRIX aWorldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&aWorld));
			DirectX::XMVECTOR aCenter = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hitbox.center)), aWorldMat);
			
			DirectX::XMVECTOR aAxes[3] = {
				DirectX::XMVector3Normalize(aWorldMat.r[0]),
				DirectX::XMVector3Normalize(aWorldMat.r[1]),
				DirectX::XMVector3Normalize(aWorldMat.r[2])
			};
			
			DirectX::XMVECTOR aScale, aRot, aTrans;
			DirectX::XMMatrixDecompose(&aScale, &aRot, &aTrans, aWorldMat);
			float aExtents[3] = {
				hitbox.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(aScale)),
				hitbox.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(aScale)),
				hitbox.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(aScale))
			};

			float acx = DirectX::XMVectorGetX(aCenter), acz = DirectX::XMVectorGetZ(aCenter);
			m_nearbyIndices.clear();
			qt.Query(acx - aExtents[0], acz - aExtents[2], acx + aExtents[0], acz + aExtents[2], m_nearbyIndices);

			for (uint32_t hurtIdx : m_nearbyIndices) {
				const auto& hurtInfo = m_hurters[hurtIdx];
				entt::entity defenderEntity = hurtInfo.entity;
				if (attackerEntity == defenderEntity) continue;

				DirectX::XMVECTOR dCenter = DirectX::XMLoadFloat3(&hurtInfo.center);
				DirectX::XMVECTOR dAxes[3] = {
					DirectX::XMLoadFloat3(&hurtInfo.axes[0]),
					DirectX::XMLoadFloat3(&hurtInfo.axes[1]),
					DirectX::XMLoadFloat3(&hurtInfo.axes[2])
				};
				float dExtents[3] = { hurtInfo.extents.x, hurtInfo.extents.y, hurtInfo.extents.z };

				if (CheckObbOverlap(aCenter, aAxes, aExtents, dCenter, dAxes, dExtents)) {
					TagType aTag = registry.all_of<TagComponent>(attackerEntity) ? registry.get<TagComponent>(attackerEntity).tag : TagType::Untagged;
					TagType dTag = registry.all_of<TagComponent>(defenderEntity) ? registry.get<TagComponent>(defenderEntity).tag : TagType::Untagged;
					
					bool skipDamage = false;
					if (aTag == TagType::PlayerSword || aTag == TagType::Sword) { if (dTag != TagType::Enemy) skipDamage = true; }
					if (aTag != TagType::Untagged && aTag == dTag) skipDamage = true;
					if (skipDamage) continue;

					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							auto& hurtbox = registry.get<HurtboxComponent>(defenderEntity);
							hc.hp -= hitbox.damage * hurtbox.damageMultiplier;
							hc.invincibleTime = 0.5f;

							if (ctx.scene) {
								auto hitDistortion = ctx.scene->CreateEntity("HitDistortion_VFX");
								if (registry.all_of<BoxColliderComponent>(hitDistortion)) registry.remove<BoxColliderComponent>(hitDistortion);
								if (registry.all_of<HurtboxComponent>(hitDistortion)) registry.remove<HurtboxComponent>(hitDistortion);
								if (registry.all_of<RigidbodyComponent>(hitDistortion)) registry.remove<RigidbodyComponent>(hitDistortion);

								auto& tc_hit = registry.get<TransformComponent>(hitDistortion);
								DirectX::XMStoreFloat3(&tc_hit.translate, dCenter);
								tc_hit.scale = { 1, 1, 1 };

								auto& mrc_hit = registry.emplace<MeshRendererComponent>(hitDistortion);
								mrc_hit.shaderName = "Distortion";
								mrc_hit.texturePath = "Resources/Textures/normal.png";
								mrc_hit.modelPath = "Resources/Models/Plane/cube.obj";
								
								if (ctx.renderer) {
									mrc_hit.modelHandle = ctx.renderer->LoadObjMesh(mrc_hit.modelPath);
									mrc_hit.textureHandle = ctx.renderer->LoadTexture2D(mrc_hit.texturePath);
								}
								mrc_hit.color = { 1, 1, 1, 2.0f };

								auto& sc_hit = registry.emplace<ScriptComponent>(hitDistortion);
								sc_hit.scripts.push_back({ "HitDistortionScript", "", std::make_shared<HitDistortionScript>(), false });

								auto& tcTag_hit = registry.emplace<TagComponent>(hitDistortion);
								tcTag_hit.tag = TagType::HitDistortion_VFX;
							}

							hc.hitFlashTimer = 0.2f;
							::Engine::TimeManager::GetInstance().SetHitstop(0.1f);
							ApplyKnockback(registry, attackerEntity, defenderEntity);

							if (aTag == TagType::Bullet && ctx.scene) {
								ctx.scene->DestroyObject(static_cast<uint32_t>(attackerEntity));
								break; // 弾は消えるのでループ抜ける
							}
						}
					}
				}
			}
		}
	}

private:
	struct HurtDeviceInfo {
		entt::entity entity;
		DirectX::XMFLOAT3 center;
		DirectX::XMFLOAT3 extents;
		DirectX::XMFLOAT3 axes[3];
	};
	std::vector<HurtDeviceInfo> m_hurters;
	std::vector<uint32_t> m_nearbyIndices;

	static bool CheckObbOverlap(DirectX::XMVECTOR cA, DirectX::XMVECTOR* axesA, float* extA, 
						      DirectX::XMVECTOR cB, DirectX::XMVECTOR* axesB, float* extB) {
		DirectX::XMVECTOR L_axes[15];
		for (int i = 0; i < 3; ++i) { L_axes[i] = axesA[i]; L_axes[i + 3] = axesB[i]; }
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				L_axes[6 + i * 3 + j] = DirectX::XMVector3Cross(axesA[i], axesB[j]);
			}
		}

		DirectX::XMVECTOR relPos = DirectX::XMVectorSubtract(cA, cB);
		for (int i = 0; i < 15; ++i) {
			DirectX::XMVECTOR L = L_axes[i];
			float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(L));
			if (lenSq < 0.001f) continue;
			L = DirectX::XMVectorScale(L, 1.0f / std::sqrt(lenSq));

			float rA = 0, rB = 0;
			for (int m = 0; m < 3; ++m) {
				rA += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesA[m], L))) * extA[m];
				rB += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesB[m], L))) * extB[m];
			}
			float dist = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)));
			if (dist > rA + rB) return false;
		}
		return true;
	}

	static void ApplyKnockback(entt::registry& registry, entt::entity attacker, entt::entity defender) {
		if (!registry.all_of<RigidbodyComponent>(defender) || !registry.all_of<TransformComponent>(attacker) || !registry.all_of<TransformComponent>(defender)) return;
		auto& dRb = registry.get<RigidbodyComponent>(defender);
		if (dRb.isKinematic) return;

		auto& aTc = registry.get<TransformComponent>(attacker);
		auto& dTc = registry.get<TransformComponent>(defender);
		float dx = dTc.translate.x - aTc.translate.x;
		float dz = dTc.translate.z - aTc.translate.z;
		float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > 0.001f) {
			float knockbackPower = 35.0f;
			dRb.velocity.x += (dx / dist) * knockbackPower;
			dRb.velocity.z += (dz / dist) * knockbackPower;
			dRb.velocity.y += 10.0f;
		}
	}
};

} // namespace Game
