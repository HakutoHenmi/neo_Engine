#pragma once
#include "ISystem.h"
#include "../Scenes/GameScene.h"
#include "EnemyAISystem.h"
#include "PlayerActionSystem.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Game {

class HealthSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			if (!hc.enabled || hc.isDead) continue;

			// 演出タイマーの更新
			if (hc.hitFlashTimer > 0.0f) {
				hc.hitFlashTimer -= ctx.dt;
				if (registry.all_of<MeshRendererComponent>(entity)) {
					auto& mr = registry.get<MeshRendererComponent>(entity);
					
					// 初回ヒット時に元の色を保存
					if (!hc.baseColorSaved) {
						hc.baseColor = mr.color;
						hc.baseColorSaved = true;
					}

					if (hc.hitFlashTimer <= 0.0f) {
						hc.hitFlashTimer = 0.0f;
						mr.color = hc.baseColor; // 元の色に戻す
					} else {
						// フラッシュ中（白く光らせる）
						mr.color = { 2.0f, 2.0f, 2.0f, 1.0f }; 
					}
				}
			}

			if (hc.hitStopTimer > 0.0f) {
				hc.hitStopTimer -= ctx.dt;
				if (hc.hitStopTimer < 0.0f) hc.hitStopTimer = 0.0f;
			}

			if (hc.invincibleTime > 0.0f) {
				hc.invincibleTime -= ctx.dt;
				if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
			}

			// ダメージ検知用の簡易ロジック
			uint32_t eid = static_cast<uint32_t>(entity);
			if (lastHp_.find(eid) != lastHp_.end()) {
				float diff = lastHp_[eid] - hc.hp;
				if (diff > 0.1f) {
					// ダメージポップアップの生成
					auto dmgEntity = registry.create();
					auto& dnc = registry.emplace<DamageNumberComponent>(dmgEntity);
					dnc.damage = diff;
					dnc.lifetime = 1.0f;
					dnc.maxLifetime = 1.0f;
					// プレイヤーなら赤、敵なら白
					if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Player) {
						dnc.color = {1.0f, 0.2f, 0.2f};
					} else {
						dnc.color = {1.0f, 1.0f, 1.0f};
					}
					// 発生位置
					if (registry.all_of<TransformComponent>(entity)) {
						const auto& tc = registry.get<TransformComponent>(entity);
						dnc.startPos = {tc.translate.x, tc.translate.y + 1.5f, tc.translate.z};
					}
				}
			}
			lastHp_[eid] = hc.hp;

			if (hc.hp <= 0.0f && !hc.isDead) {
				bool isEnemy = false;
				bool isPlayer = registry.all_of<PlayerInputComponent>(entity);
				if (registry.all_of<TagComponent>(entity)) {
					const auto tag = registry.get<TagComponent>(entity).tag;
					isEnemy = tag == TagType::Enemy;
					isPlayer = isPlayer || tag == TagType::Player;
				}

				if (isEnemy && registry.all_of<MeshRendererComponent>(entity)) {
					if (registry.all_of<BossActionComponent>(entity)) {
						// ボスの場合は専用の死亡ステートに移行する（BossTestScript側でアニメーションを再生して消去する）
						auto& boss = registry.get<BossActionComponent>(entity);
						if (boss.state != BossState::Dead) {
							boss.state = BossState::Dead;
							boss.stateTimer = 0.0f;
							
							if (registry.all_of<HurtboxComponent>(entity)) registry.get<HurtboxComponent>(entity).enabled = false;
							if (registry.all_of<HitboxComponent>(entity)) registry.get<HitboxComponent>(entity).isActive = false;
							if (registry.all_of<BoxColliderComponent>(entity)) registry.get<BoxColliderComponent>(entity).enabled = false;
							
							hc.enabled = false;
						}
					} else {
						auto& mr = registry.get<MeshRendererComponent>(entity);
						if (mr.shaderName != "Dissolve") {
							mr.shaderName = "Dissolve";
							mr.color.w = 1.0f; // ディゾルブ開始は不透明から

							if (!registry.all_of<AutoDestroyComponent>(entity)) {
								registry.emplace<AutoDestroyComponent>(entity).timer = 1.5f; // 1.5秒かけて消滅
							}

							// 攻撃・被弾判定の無効化
							if (registry.all_of<HurtboxComponent>(entity)) registry.get<HurtboxComponent>(entity).enabled = false;
							if (registry.all_of<HitboxComponent>(entity)) registry.get<HitboxComponent>(entity).isActive = false;
							if (registry.all_of<BoxColliderComponent>(entity)) registry.get<BoxColliderComponent>(entity).enabled = false;
							
							// AI更新停止
							if (registry.all_of<EnemyAIComponent>(entity)) registry.get<EnemyAIComponent>(entity).enabled = false;

							hc.enabled = false; // 以降のダメージ処理を止める（isDeadはfalseのまま残す）
						}
					}
				} else {
					if (isPlayer) {
						BeginPlayerDeath(registry, entity, hc, ctx);
					}
					hc.isDead = true;
				}
			}
		}

		// --- ダメージ数字コンポーネントの更新 ---
		entt::entity playerEntity = entt::null;
		TransformComponent* playerTransform = nullptr;
		HealthComponent* playerHealth = nullptr;
		auto playerView = registry.view<PlayerInputComponent, TransformComponent, HealthComponent>();
		auto playerIt = playerView.begin();
		if (playerIt != playerView.end()) {
			auto entity = *playerIt;
			playerEntity = entity;
			playerTransform = &playerView.get<TransformComponent>(entity);
			playerHealth = &playerView.get<HealthComponent>(entity);
		}

		auto fluidView = registry.view<LostFluidPickupComponent, TransformComponent>();
		for (auto entity : fluidView) {
			auto& fluid = fluidView.get<LostFluidPickupComponent>(entity);
			auto& tc = fluidView.get<TransformComponent>(entity);
			if (!fluid.enabled) continue;

			bool lifetimeExpired = false;
			if (fluid.lifetime > 0.0f) {
				fluid.lifetime -= ctx.dt;
				lifetimeExpired = (fluid.lifetime <= 0.0f);
			}
			if (fluid.magnetDelay > 0.0f) {
				fluid.magnetDelay -= ctx.dt;
			}

			bool shouldDestroy = false;
			if (lifetimeExpired || !registry.valid(fluid.owner)) {
				shouldDestroy = true;
			}

			if (!shouldDestroy && playerEntity != entt::null && fluid.owner == playerEntity && playerTransform && playerHealth) {
				float dx = playerTransform->translate.x - tc.translate.x;
				float dy = (playerTransform->translate.y + 0.8f) - tc.translate.y;
				float dz = playerTransform->translate.z - tc.translate.z;
				float distSq = dx * dx + dy * dy + dz * dz;
				float groundDistSq = dx * dx + dz * dz;

				if (fluid.magnetDelay <= 0.0f && groundDistSq < fluid.magnetRadius * fluid.magnetRadius) {
					float dist = std::sqrt((std::max)(distSq, 0.0001f));
					float groundDist = std::sqrt((std::max)(groundDistSq, 0.0001f));
					float pull = 42.0f * (1.0f - std::clamp(groundDist / fluid.magnetRadius, 0.0f, 1.0f));
					fluid.velocity.x += (dx / dist) * pull * ctx.dt;
					fluid.velocity.y += (dy / dist) * pull * ctx.dt;
					fluid.velocity.z += (dz / dist) * pull * ctx.dt;
				}

				if (fluid.magnetDelay <= 0.0f && groundDistSq < fluid.collectRadius * fluid.collectRadius) {
					playerHealth->hp = (std::min)(playerHealth->maxHp, playerHealth->hp + fluid.hpRestore);
					playerHealth->stamina = (std::min)(playerHealth->maxStamina, playerHealth->stamina + fluid.staminaRestore);
					if (ctx.renderer) {
						ctx.renderer->AbsorbLostGPUFluidGroup(fluid.visualGroupId);
					}
					shouldDestroy = true;
				}
			}

			if (!shouldDestroy) {
				fluid.velocity.y -= 18.0f * ctx.dt;
				fluid.velocity.x *= 0.995f;
				fluid.velocity.z *= 0.995f;

				tc.translate.x += fluid.velocity.x * ctx.dt;
				tc.translate.y += fluid.velocity.y * ctx.dt;
				tc.translate.z += fluid.velocity.z * ctx.dt;

				if (tc.translate.y < 0.25f) {
					tc.translate.y = 0.25f;
					fluid.velocity.y *= -0.25f;
					fluid.velocity.x *= 0.72f;
					fluid.velocity.z *= 0.72f;
				}

				fluid.visualEmitTimer = 0.0f;
				if (ctx.renderer) {
					Engine::Vector3 visualPos = {tc.translate.x, tc.translate.y, tc.translate.z};
					ctx.renderer->SyncLostGPUFluidGroup(fluid.visualGroupId, visualPos);
				}
			}

			if (shouldDestroy && ctx.scene) {
				if (registry.valid(fluid.owner) && registry.all_of<HealthComponent>(fluid.owner)) {
					auto& ownerHealth = registry.get<HealthComponent>(fluid.owner);
					ownerHealth.recoverableFluid = (std::max)(0.0f, ownerHealth.recoverableFluid - fluid.hpRestore);
				}
				fluid.enabled = false;
				ctx.scene->DestroyObject(static_cast<uint32_t>(entity));
			}
		}

		auto dmgView = registry.view<DamageNumberComponent>();
		for (auto entity : dmgView) {
			auto& dnc = dmgView.get<DamageNumberComponent>(entity);
			dnc.lifetime -= ctx.dt;
			if (dnc.lifetime <= 0.0f) {
				if (ctx.scene) {
					ctx.scene->DestroyObject(static_cast<uint32_t>(entity));
				}
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			hc.invincibleTime = 0.0f;
			hc.isDead = false;
			hc.recoverableFluid = 0.0f;
			if (hc.hp <= 0) hc.hp = hc.maxHp;
		}
		auto fluidView = registry.view<LostFluidPickupComponent>();
		std::vector<entt::entity> fluidsToDestroy;
		for (auto entity : fluidView) {
			fluidsToDestroy.push_back(entity);
		}
		for (auto entity : fluidsToDestroy) {
			if (registry.valid(entity)) {
				registry.destroy(entity);
			}
		}
		lastHp_.clear();
	}

private:
	static float Hash01(uint32_t n) {
		n = (n << 13U) ^ n;
		n = n * (n * n * 15731U + 789221U) + 1376312589U;
		return static_cast<float>(n & 0x7fffffffU) / static_cast<float>(0x7fffffffU);
	}

	void BeginPlayerDeath(entt::registry& registry, entt::entity entity, HealthComponent& hc, GameContext& ctx) {
		if (auto* pi = registry.try_get<PlayerInputComponent>(entity)) {
			pi->enabled = false;
			pi->moveDir = {0.0f, 0.0f};
			pi->jumpRequested = false;
			pi->attackRequested = false;
			pi->cameraYaw = 0.0f;
			pi->cameraPitch = 0.0f;
			pi->lockedEnemy = entt::null;
			pi->isRadialMenuOpen = false;
		}
		if (auto* pa = registry.try_get<PlayerActionComponent>(entity)) {
			pa->enabled = false;
			pa->state = PlayerActionState::Idle;
			pa->stateTimer = 0.0f;
			pa->stateDuration = 0.0f;
			pa->sodaAiming = false;
			pa->sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
		}
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			cm->enabled = false;
			cm->velocityY = 0.0f;
		}
		if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
			rb->velocity = {0.0f, 0.0f, 0.0f};
			rb->enabled = false;
		}
		if (auto* hb = registry.try_get<HitboxComponent>(entity)) {
			hb->isActive = false;
		}
		if (auto* hurt = registry.try_get<HurtboxComponent>(entity)) {
			hurt->enabled = false;
		}
		if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
			bc->enabled = false;
		}
		if (auto* mr = registry.try_get<MeshRendererComponent>(entity)) {
			mr->enabled = false;
		}

		hc.recoverableFluid = 0.0f;
		SpawnPlayerDeathBurst(registry, entity, ctx);
	}

	void SpawnPlayerDeathBurst(entt::registry& registry, entt::entity owner, GameContext& ctx) {
		if (!registry.valid(owner) || !registry.all_of<TransformComponent>(owner)) return;

		const auto& ownerTc = registry.get<TransformComponent>(owner);
		const DirectX::XMFLOAT3 origin = {
			ownerTc.translate.x,
			ownerTc.translate.y + 0.85f,
			ownerTc.translate.z
		};

		const int pickupCount = 72;
		uint32_t firstVisualGroupId = 0;
		if (ctx.renderer) {
			const Engine::Vector3 pos = {origin.x, origin.y, origin.z};
			const Engine::Vector3 vel = {0.0f, 0.75f, 0.0f};
			firstVisualGroupId = ctx.renderer->ExtractGPUFluidFromPlayer(pos, vel, pickupCount * 10);
		}

		for (int i = 0; i < pickupCount; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(pickupCount);
			const float angle = t * 6.283185307f + (Hash01(static_cast<uint32_t>(i) * 193U + 7U) - 0.5f) * 0.65f;
			const float lift = 0.18f + Hash01(static_cast<uint32_t>(i) * 271U + 11U) * 0.42f;
			const float outX = std::cos(angle);
			const float outY = lift;
			const float outZ = std::sin(angle);
			const float outLen = std::sqrt(outX * outX + outY * outY + outZ * outZ);
			const float nx = outLen > 0.001f ? outX / outLen : 1.0f;
			const float ny = outLen > 0.001f ? outY / outLen : 0.25f;
			const float nz = outLen > 0.001f ? outZ / outLen : 0.0f;
			const float speed = 8.5f + Hash01(static_cast<uint32_t>(i) * 401U + 29U) * 6.0f;
			const float startDist = 0.35f + Hash01(static_cast<uint32_t>(i) * 313U + 17U) * 0.45f;

			auto pickup = registry.create();
			registry.emplace<NameComponent>(pickup, "PlayerDeathFluid");

			auto& tc = registry.emplace<TransformComponent>(pickup);
			tc.translate = {
				origin.x + nx * startDist,
				origin.y + ny * startDist,
				origin.z + nz * startDist
			};

			auto& lf = registry.emplace<LostFluidPickupComponent>(pickup);
			lf.owner = owner;
			lf.hpRestore = 0.0f;
			lf.staminaRestore = 0.0f;
			lf.lifetime = 1.7f;
			lf.magnetDelay = 99.0f;
			lf.magnetRadius = 0.0f;
			lf.collectRadius = 0.0f;
			lf.visualGroupId = firstVisualGroupId + static_cast<uint32_t>(i);
			lf.velocity = {
				nx * speed,
				2.4f + ny * speed,
				nz * speed
			};

			registry.emplace<TagComponent>(pickup, TagType::VFX);
		}
	}

	std::unordered_map<uint32_t, float> lastHp_;
};

} // namespace Game
