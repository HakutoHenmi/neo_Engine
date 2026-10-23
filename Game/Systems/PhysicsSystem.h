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
	void Update(entt::registry& registry, GameContext& ctx) override {
		// PLAY 中でなければ何もしない
		if (!ctx.isPlaying) return;
		// このフレーム枠の前回の使用が完了しているので、その GPU のメッシュ接触結果を
		// 剛体を動かす前に反映し、その後で下の処理がこの枠を再利用する
		// 今のフレーム枠 (2 つを交互に使う) の GPU への依頼の一覧
		auto& frameRequests = m_pendingGpuRequests[ctx.renderer
			? ctx.renderer->CurrentFrameIndex() % m_pendingGpuRequests.size() : 0];
		// 前回この枠で依頼した GPU の接触結果を 1 件ずつ反映する
		if (ctx.renderer) for (const auto& req : frameRequests) {
			if (!registry.valid(req.entity) || !registry.all_of<TransformComponent, RigidbodyComponent>(req.entity)) continue;
			ContactInfo ci{};
			if (!ctx.renderer->GetCollisionResult(req.resultIdx, ci)) continue;
			auto& tc = registry.get<TransformComponent>(req.entity);
			auto& rb = registry.get<RigidbodyComponent>(req.entity);
			if (!rb.enabled) continue;
			// めり込んでいれば、法線の向きに深さの分だけ押し戻す
			if (ci.depth > 0.0f && !rb.isKinematic) {
				tc.translate.x += ci.normal.x * ci.depth;
				tc.translate.y += ci.normal.y * ci.depth;
				tc.translate.z += ci.normal.z * ci.depth;
			}
			// 壁へ向かう速度の成分 (法線との内積が負の分) を消す
			DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
			DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&ci.normal));
			float dotVN = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, n));
			if (dotVN < 0 && !rb.isKinematic) {
				vel = DirectX::XMVectorSubtract(vel, DirectX::XMVectorScale(n, dotVN));
				DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
			}
		}
		// 反映し終えたので、この枠の依頼を空にする
		frameRequests.clear();

		// 移動は剛体が担当し、衝突形状の有無には関係しない
		auto dynamicView = registry.view<RigidbodyComponent, TransformComponent>();
		for (auto entity : dynamicView) {
			auto& rb = dynamicView.get<RigidbodyComponent>(entity);
			auto& tc = dynamicView.get<TransformComponent>(entity);
			// 無効・キネマティック (自分で動かす) の剛体は動かさない
			if (!rb.enabled || rb.isKinematic) continue;

			// キャラクター移動を持つものは、重力と縦の移動をそちらに任せる
			float damping = 3.0f;
			bool isGrounded = false;
			bool hasCMS = false;
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity); cm && cm->enabled) {
				isGrounded = cm->isGrounded;
				hasCMS = true;
			}

			// 重力 (接地中とキャラクター移動を持つものは除く)
			if (rb.useGravity && !isGrounded && !hasCMS) {
				float gravity = 9.8f;
				rb.velocity.y -= gravity * ctx.dt;
			}
			// 水平の速度を減衰させ、速度の分だけ位置を進める (縦はキャラクター移動が無いときだけ)
			if (!rb.isKinematic) {
				rb.velocity.x -= rb.velocity.x * damping * ctx.dt;
				rb.velocity.z -= rb.velocity.z * damping * ctx.dt;

				tc.translate.x += rb.velocity.x * ctx.dt;
				if (!hasCMS) {
					tc.translate.y += rb.velocity.y * ctx.dt;
				}
				tc.translate.z += rb.velocity.z * ctx.dt;
			}
		}
		// 位置が変わったのでワールド行列のキャッシュを捨てる
		if (ctx.scene) ctx.scene->InvalidateWorldMatrices();

		// 剛体を持たない固い箱は静的な障害物。無効やトリガーのコライダーは
		// 重力を止めず、接触の応答も作らない
		m_dynamics.clear();
		m_statics.clear();
		auto boxes = registry.view<BoxColliderComponent, TransformComponent>();
		for (auto entity : boxes) {
			auto& bc = boxes.get<BoxColliderComponent>(entity);
			auto& tc = boxes.get<TransformComponent>(entity);
			// 無効・トリガーのコライダーはとばす
			if (!bc.enabled || bc.isTrigger) continue;

			// ワールド行列から OBB の軸・中心・半分の大きさと、それを囲む AABB を求める
			CollidableBox cb;
			cb.entity = entity;
			::Engine::Matrix4x4 world = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(entity)) : tc.ToMatrix();
			GetObbAxes(world, bc, cb.axes, cb.center, cb.extents);
			
			// OBB の 8 つの角から、それを囲む AABB を求める
			cb.aabbMin = {FLT_MAX, FLT_MAX, FLT_MAX};
			cb.aabbMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
			for (int k = 0; k < 8; ++k) {
				Engine::Vector3 p = cb.center;
				p.x += ((k & 1) ? 1 : -1) * cb.axes[0].x * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].x * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].x * cb.extents.z;
				p.y += ((k & 1) ? 1 : -1) * cb.axes[0].y * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].y * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].y * cb.extents.z;
				p.z += ((k & 1) ? 1 : -1) * cb.axes[0].z * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].z * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].z * cb.extents.z;
				cb.aabbMin.x = std::min(cb.aabbMin.x, p.x); cb.aabbMin.y = std::min(cb.aabbMin.y, p.y); cb.aabbMin.z = std::min(cb.aabbMin.z, p.z);
				cb.aabbMax.x = std::max(cb.aabbMax.x, p.x); cb.aabbMax.y = std::max(cb.aabbMax.y, p.y); cb.aabbMax.z = std::max(cb.aabbMax.z, p.z);
			}
			// 衝突判定用の箱の一覧へ加える
			m_dynamics.push_back(cb);
		}
		// 動く箱が一つも無ければ衝突判定は不要
		const bool hasDynamicBox = std::any_of(m_dynamics.begin(), m_dynamics.end(), [&](const CollidableBox& box) {
			const auto* body = registry.try_get<RigidbodyComponent>(box.entity);
			return body && body->enabled && !body->isKinematic;
		});
		if (!hasDynamicBox) return;

		// 静的オブジェクト (GpuMeshCollider + Transform)
		auto staticView = registry.view<GpuMeshColliderComponent, TransformComponent>();
		// GPU のメッシュコライダーを持つ静的オブジェクトを集める
		for (auto entity : staticView) {
			auto& gmc = staticView.get<GpuMeshColliderComponent>(entity);
			auto& tc = staticView.get<TransformComponent>(entity);
			// 無効、または描画が無ければとばす
			if (!gmc.enabled || !ctx.renderer) continue;

			auto* model = ctx.renderer->GetModel(gmc.meshHandle);
			if (!model) continue;

			// メッシュのハンドルとワールド行列を記録する
			CollidableMesh cm;
			cm.entity = entity;
			cm.meshHandle = gmc.meshHandle;
			
			::Engine::Transform engineTc;
			engineTc.translate = {tc.translate.x, tc.translate.y, tc.translate.z};
			engineTc.rotate = {tc.rotate.x, tc.rotate.y, tc.rotate.z};
			engineTc.scale = {tc.scale.x, tc.scale.y, tc.scale.z};
			cm.world = engineTc.ToMatrix();
			
			// モデルの範囲の 8 つの角をワールドへ変換して AABB を求める
			const auto& data = model->GetData();
			cm.aabbMin = {FLT_MAX, FLT_MAX, FLT_MAX};
			cm.aabbMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
			for (int k = 0; k < 8; ++k) {
				::Engine::Vector3 p = {(k & 1) ? data.max.x : data.min.x, (k & 2) ? data.max.y : data.min.y, (k & 4) ? data.max.z : data.min.z};
				p = ::Engine::TransformCoord(p, cm.world);
				cm.aabbMin.x = std::min(cm.aabbMin.x, p.x); cm.aabbMin.y = std::min(cm.aabbMin.y, p.y); cm.aabbMin.z = std::min(cm.aabbMin.z, p.z);
				cm.aabbMax.x = std::max(cm.aabbMax.x, p.x); cm.aabbMax.y = std::max(cm.aabbMax.y, p.y); cm.aabbMax.z = std::max(cm.aabbMax.z, p.z);
			}
			m_statics.push_back(cm);
		}

		// 四分木の構築 (XZ 平面で近いものだけを調べるため)
		// ±4000 の範囲を 6 段・1 マス 10 件までの四分木に分ける
		::Engine::PhysicsQuadTree dynamicQT(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);
		::Engine::PhysicsQuadTree staticQT(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);

		// 動く箱と静的メッシュをそれぞれの四分木へ登録する
		for (size_t i = 0; i < m_dynamics.size(); ++i) {
			dynamicQT.Insert((uint32_t)i, m_dynamics[i].aabbMin.x, m_dynamics[i].aabbMin.z, m_dynamics[i].aabbMax.x, m_dynamics[i].aabbMax.z);
		}
		for (size_t i = 0; i < m_statics.size(); ++i) {
			staticQT.Insert((uint32_t)i, m_statics[i].aabbMin.x, m_statics[i].aabbMin.z, m_statics[i].aabbMax.x, m_statics[i].aabbMax.z);
		}

		// パス1: 箱同士の衝突 (CPU / OBB の分離軸判定)
		// 押し出しを 4 回繰り返して、多数の重なりを安定させる
		for (int iteration = 0; iteration < 4; ++iteration) {
			for (size_t i = 0; i < m_dynamics.size(); ++i) {
				auto& d1 = m_dynamics[i];
				// 近くにある箱だけを四分木で探す
				m_nearbyEntities.clear();
				dynamicQT.Query(d1.aabbMin.x, d1.aabbMin.z, d1.aabbMax.x, d1.aabbMax.z, m_nearbyEntities);

				for (uint32_t j : m_nearbyEntities) {
					// 同じ組を 2 回調べないよう、後ろの番号とだけ比べる
					if (j <= i) continue;
					auto& d2 = m_dynamics[j];
					// 両方とも動かない箱なら調べない
					auto* rb1 = registry.try_get<RigidbodyComponent>(d1.entity);
					auto* rb2 = registry.try_get<RigidbodyComponent>(d2.entity);
					const bool dynamic1 = rb1 && rb1->enabled && !rb1->isKinematic;
					const bool dynamic2 = rb2 && rb2->enabled && !rb2->isKinematic;
					if (!dynamic1 && !dynamic2) continue;

					// 外接球で大まかに判定してから詳しく調べる
					float dx = d1.center.x - d2.center.x, dy = d1.center.y - d2.center.y, dz = d1.center.z - d2.center.z;
					float distSq = dx * dx + dy * dy + dz * dz;
					float sphereR1 = std::sqrt(d1.extents.x * d1.extents.x + d1.extents.y * d1.extents.y + d1.extents.z * d1.extents.z);
					float sphereR2 = std::sqrt(d2.extents.x * d2.extents.x + d2.extents.y * d2.extents.y + d2.extents.z * d2.extents.z);
					if (distSq > (sphereR1 + sphereR2) * (sphereR1 + sphereR2)) continue;

					// キャラクター (プレイヤー・敵) か、弾やエフェクトかを見分ける
					bool isC1 = false, isC2 = false;
					bool isProj1 = false, isProj2 = false;
					if (auto* tag1 = registry.try_get<TagComponent>(d1.entity)) {
						isC1 = (tag1->tag == TagType::Player || tag1->tag == TagType::Enemy);
						isProj1 = (tag1->tag == TagType::Projectile || tag1->tag == TagType::VFX);
					}
					if (auto* tag2 = registry.try_get<TagComponent>(d2.entity)) {
						isC2 = (tag2->tag == TagType::Player || tag2->tag == TagType::Enemy);
						isProj2 = (tag2->tag == TagType::Projectile || tag2->tag == TagType::VFX);
					}

					// エフェクトや弾がプレイヤーや敵を物理的に押し出さないようにする
					if ((isProj1 && isC2) || (isProj2 && isC1) || (isProj1 && isProj2)) {
						continue;
					}

					// 両方の箱の 3 本の軸を読み込む
					DirectX::XMVECTOR aAxes[3] = {
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d1.axes[0])),
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d1.axes[1])),
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d1.axes[2]))
					};
					DirectX::XMVECTOR bAxes[3] = {
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d2.axes[0])),
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d2.axes[1])),
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d2.axes[2]))
					};

					// 分離軸の候補 (両方の箱の軸 3+3 本と、その外積 9 本)
					DirectX::XMVECTOR testAxes[15];
					for (int k = 0; k < 3; ++k) testAxes[k] = aAxes[k];
					for (int k = 0; k < 3; ++k) testAxes[k + 3] = bAxes[k];
					for (int k = 0; k < 3; ++k) {
						for (int l = 0; l < 3; ++l) {
							testAxes[6 + k * 3 + l] = DirectX::XMVector3Cross(aAxes[k], bAxes[l]);
						}
					}

					// 最小の重なりと、その押し出しの向き (MTV)
					float minOverlap = FLT_MAX;
					::Engine::Vector3 mtv = {0, 0, 0};
					bool collision = true;
					DirectX::XMVECTOR relPos = DirectX::XMVectorSet(dx, dy, dz, 0.0f);

					// 各分離軸に両方の箱を投影し、重なりを調べる
					for (int k = 0; k < 15; ++k) {
						DirectX::XMVECTOR L = testAxes[k];
						// 外積が 0 に近い軸 (平行な辺) はとばし、それ以外は正規化する
						float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(L));
						if (lenSq < 0.001f) continue;
						L = DirectX::XMVectorScale(L, 1.0f / std::sqrt(lenSq));

						// 箱の軸への投影の半分の長さ
						float rA = 0, rB = 0;
						for (int m = 0; m < 3; ++m) {
							rA += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(aAxes[m], L))) * (m == 0 ? d1.extents.x : (m == 1 ? d1.extents.y : d1.extents.z));
							rB += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(bAxes[m], L))) * (m == 0 ? d2.extents.x : (m == 1 ? d2.extents.y : d2.extents.z));
						}

						// 中心の距離の投影より短ければ、その軸で離れている (衝突していない)
						float overlap = rA + rB - std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)));
						if (overlap <= 0.0f) { collision = false; break; }
						
						// キャラクター同士は上下方向に押し出しにくくする
						float weight = 1.0f;
						if (isC1 && isC2 && std::abs(DirectX::XMVectorGetY(L)) > 0.5f) weight = 10.0f;

						// 最も重なりの小さい軸を押し出しの向きにする (向きは d2 → d1)
						if (overlap / weight < minOverlap) {
							minOverlap = overlap;
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&mtv), L);
							if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)) < 0) {
								mtv.x = -mtv.x; mtv.y = -mtv.y; mtv.z = -mtv.z;
							}
						}
					}

					// 衝突していれば、重なりを押し出し、近づく速度を打ち消す
					if (collision) {
						// 動く方だけを押し出す (両方動くなら半分ずつ)
						float move1 = !dynamic1 ? 0.0f : (!dynamic2 ? 1.0f : 0.5f);
						float move2 = !dynamic2 ? 0.0f : (!dynamic1 ? 1.0f : 0.5f);

						// キャラクター同士は水平方向にだけ押し出す
						if (isC1 && isC2) {
							mtv.y = 0;
							float xzLen = std::sqrt(mtv.x * mtv.x + mtv.z * mtv.z);
							if (xzLen > 0.001f) { mtv.x /= xzLen; mtv.z /= xzLen; }
						}

						// d1 を MTV の向きへ、d2 を反対へ押し出し、判定用の中心も更新する
						if (move1 > 0) {
							auto& tc1 = registry.get<TransformComponent>(d1.entity);
							tc1.translate.x += mtv.x * minOverlap * move1; tc1.translate.y += mtv.y * minOverlap * move1; tc1.translate.z += mtv.z * minOverlap * move1;
							d1.center.x += mtv.x * minOverlap * move1; d1.center.y += mtv.y * minOverlap * move1; d1.center.z += mtv.z * minOverlap * move1;
						}
						if (move2 > 0) {
							auto& tc2 = registry.get<TransformComponent>(d2.entity);
							tc2.translate.x -= mtv.x * minOverlap * move2; tc2.translate.y -= mtv.y * minOverlap * move2; tc2.translate.z -= mtv.z * minOverlap * move2;
							d2.center.x -= mtv.x * minOverlap * move2; d2.center.y -= mtv.y * minOverlap * move2; d2.center.z -= mtv.z * minOverlap * move2;
						}

						// 近づく速度があれば、その分の力積を与えて打ち消す
						DirectX::XMVECTOR v1 = !dynamic1 ? DirectX::XMVectorZero() : DirectX::XMLoadFloat3(&rb1->velocity);
						DirectX::XMVECTOR v2 = !dynamic2 ? DirectX::XMVectorZero() : DirectX::XMLoadFloat3(&rb2->velocity);
						DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&mtv));
						float relVel = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(v1, v2), n));
						if (relVel < 0) {
							DirectX::XMVECTOR impulse = DirectX::XMVectorScale(n, -relVel / ((dynamic1 ? 1.f : 0.f) + (dynamic2 ? 1.f : 0.f)));
							if (dynamic1) DirectX::XMStoreFloat3(&rb1->velocity, DirectX::XMVectorAdd(v1, impulse));
							if (dynamic2) DirectX::XMStoreFloat3(&rb2->velocity, DirectX::XMVectorSubtract(v2, impulse));
						}
					}
				}
			}
		}

		// パス2: GPU でのメッシュ衝突をまとめて依頼する
		uint32_t nextResultIdx = 0;

		// 動く箱と静的メッシュの両方があるときだけ GPU に依頼する
		if (ctx.renderer && !m_dynamics.empty() && !m_statics.empty()) {
			ctx.renderer->BeginCollisionCheck(1024);

			for (auto& d : m_dynamics) {
				// キネマティックな剛体は GPU の接触結果を使わない
				const auto* body = registry.try_get<RigidbodyComponent>(d.entity);
				if (!body || !body->enabled || body->isKinematic) continue;
				// CharacterMovementSystem がメッシュへのレイで地形の高さと壁を既に解決している。
				// GPU での BVH 判定を重ねると処理が二重になり、
				// キャラクターの反応がフレーム枠の一巡分遅れる
				if (const auto* movement = registry.try_get<CharacterMovementComponent>(d.entity); movement && movement->enabled) continue;
				// 動く箱のトランスフォーム
				auto& tc = registry.get<TransformComponent>(d.entity);
				auto& bc = registry.get<BoxColliderComponent>(d.entity);
				
				::Engine::Transform dynTransform;
				dynTransform.translate = {tc.translate.x, tc.translate.y, tc.translate.z};
				dynTransform.rotate = {tc.rotate.x, tc.rotate.y, tc.rotate.z};
				dynTransform.scale = {tc.scale.x, tc.scale.y, tc.scale.z};
				
				// 近くの静的メッシュを四分木で探す
				m_nearbyEntities.clear();
				staticQT.Query(d.aabbMin.x, d.aabbMin.z, d.aabbMax.x, d.aabbMax.z, m_nearbyEntities);

				for (uint32_t sIdx : m_nearbyEntities) {
					auto& s = m_statics[sIdx];
					// AABB が重ならなければとばす
					if (d.aabbMax.y < s.aabbMin.y || d.aabbMin.y > s.aabbMax.y ||
					    d.aabbMax.x < s.aabbMin.x || d.aabbMin.x > s.aabbMax.x ||
					    d.aabbMax.z < s.aabbMin.z || d.aabbMin.z > s.aabbMax.z) continue;

					// 1 フレームに依頼できるのは 1024 件まで
					if (nextResultIdx >= 1024) break;

					// 静的メッシュのトランスフォーム
					auto& sTc = registry.get<TransformComponent>(s.entity);
					::Engine::Transform staticTransform;
					staticTransform.translate = {sTc.translate.x, sTc.translate.y, sTc.translate.z};
					staticTransform.rotate = {sTc.rotate.x, sTc.rotate.y, sTc.rotate.z};
					staticTransform.scale = {sTc.scale.x, sTc.scale.y, sTc.scale.z};

					// GPU に判定を依頼し、結果の番号を記録する (次にこの枠を使うときに読む)
					uint32_t rIdx = nextResultIdx++;
					ctx.renderer->DispatchCollision(0, s.meshHandle, dynTransform, bc, staticTransform, rIdx);
					frameRequests.push_back({d.entity, rIdx});
				}
				if (nextResultIdx >= 1024) break;
			}

			// 依頼を終える
			ctx.renderer->EndCollisionCheck();

		}
		// 押し出しで位置が変わったのでワールド行列のキャッシュを捨てる
		if (ctx.scene) ctx.scene->InvalidateWorldMatrices();
	}

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
