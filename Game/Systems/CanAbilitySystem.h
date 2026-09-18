#pragma once

#include "ISystem.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Game {

class CanAbilitySystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		UpdateAcidPools(registry, ctx);
		UpdateStatuses(registry, ctx);
		UpdateShields(registry, ctx);
		UpdateVisuals(registry, ctx);
	}

	void Reset(entt::registry& registry) override {
		auto statusView = registry.view<CanStatusComponent>();
		for (auto entity : statusView) {
			auto& status = statusView.get<CanStatusComponent>(entity);
			status.freezeTimer = 0.0f;
			status.acidTimer = 0.0f;
			status.acidTickTimer = 0.0f;
			status.bubbleTimer = 0.0f;
		}
	}

private:
	struct VisualRequest {
		entt::entity target = entt::null;
		CanVisualKind kind = CanVisualKind::IceStatus;
	};

	std::vector<VisualRequest> visualRequests_;

	static bool IsEnemy(entt::registry& registry, entt::entity entity) {
		const auto* tag = registry.try_get<TagComponent>(entity);
		return tag && tag->tag == TagType::Enemy;
	}

	void UpdateAcidPools(entt::registry& registry, GameContext& ctx) {
		auto poolView = registry.view<AcidPoolComponent, TransformComponent>();
		for (auto poolEntity : poolView) {
			auto& pool = poolView.get<AcidPoolComponent>(poolEntity);
			auto& poolTc = poolView.get<TransformComponent>(poolEntity);
			pool.pulseTime += ctx.dt;
			const float pulse = 0.96f + std::sin(pool.pulseTime * 5.5f) * 0.06f;
			poolTc.scale.x = pool.radius * pulse;
			poolTc.scale.z = pool.radius * pulse;
			if (auto* mesh = registry.try_get<MeshRendererComponent>(poolEntity)) {
				mesh->color = {0.38f + pulse * 0.08f, 1.0f, 0.03f, 0.72f + (pulse - 0.90f)};
			}
			if (auto* light = registry.try_get<PointLightComponent>(poolEntity)) {
				light->intensity = 2.1f + (pulse - 0.90f) * 4.0f;
			}
			pool.tickTimer -= ctx.dt;
			if (pool.tickTimer > 0.0f) continue;
			pool.tickTimer = 0.55f;

			auto targetView = registry.view<TagComponent, TransformComponent, HealthComponent>();
			for (auto target : targetView) {
				if (targetView.get<TagComponent>(target).tag != TagType::Enemy) continue;
				auto& health = targetView.get<HealthComponent>(target);
				if (health.isDead || !health.enabled) continue;
				const auto& targetTc = targetView.get<TransformComponent>(target);
				const float dx = targetTc.translate.x - poolTc.translate.x;
				const float dz = targetTc.translate.z - poolTc.translate.z;
				if (dx * dx + dz * dz > pool.radius * pool.radius) continue;

				auto& status = registry.get_or_emplace<CanStatusComponent>(target);
				status.acidTimer = (std::max)(status.acidTimer, 5.5f);
				if (!ctx.isSandbagMode) {
					health.hp = (std::max)(0.0f, health.hp - 2.0f);
				}
				health.hitFlashTimer = (std::max)(health.hitFlashTimer, 0.05f);
			}
		}
	}

	void UpdateStatuses(entt::registry& registry, GameContext& ctx) {
		std::vector<entt::entity> removeStatuses;
		std::vector<entt::entity> destroyVisuals;
		auto view = registry.view<CanStatusComponent, TransformComponent>();

		for (auto entity : view) {
			auto& status = view.get<CanStatusComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			auto* health = registry.try_get<HealthComponent>(entity);
			auto* mesh = registry.try_get<MeshRendererComponent>(entity);

			if (mesh && !status.baseColorSaved) {
				status.baseColor = health && health->baseColorSaved ? health->baseColor : mesh->color;
				status.baseColorSaved = true;
			}

			status.freezeTimer = (std::max)(0.0f, status.freezeTimer - ctx.dt);
			status.acidTimer = (std::max)(0.0f, status.acidTimer - ctx.dt);
			status.bubbleTimer = (std::max)(0.0f, status.bubbleTimer - ctx.dt);

			if (status.acidTimer > 0.0f && health && health->enabled && !health->isDead) {
				status.acidTickTimer -= ctx.dt;
				if (status.acidTickTimer <= 0.0f) {
					status.acidTickTimer = 0.6f;
					if (!ctx.isSandbagMode && IsEnemy(registry, entity)) {
						health->hp = (std::max)(0.0f, health->hp - 3.0f);
					}
					health->hitFlashTimer = (std::max)(health->hitFlashTimer, 0.04f);
				}
			}

			if (status.bubbleTimer > 0.0f) {
				if (!status.bubblePositionSaved) {
					status.bubbleBaseY = tc.translate.y;
					status.bubblePositionSaved = true;
				}
				if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
					rb->velocity.y = 0.0f;
				}
				if (auto* movement = registry.try_get<CharacterMovementComponent>(entity)) {
					movement->isGrounded = false;
				}
				const bool isBoss = registry.all_of<BossActionComponent>(entity);
				const float liftHeight = isBoss ? 1.4f : 2.4f;
				const float bobHeight = isBoss ? 0.16f : 0.32f;
				tc.translate.y = status.bubbleBaseY + liftHeight + std::sin(status.bubbleTimer * 4.5f) * bobHeight;
			} else if (status.bubblePositionSaved) {
				float restoreY = status.bubbleBaseY;
				auto* movement = registry.try_get<CharacterMovementComponent>(entity);
				if (ctx.scene) {
					const float rayStartY = (std::max)(tc.translate.y, status.bubbleBaseY) + 4.0f;
					const float groundY = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, rayStartY,
						static_cast<uint32_t>(entity));
					if (groundY > -5000.0f) {
						restoreY = groundY + (movement ? movement->heightOffset : 0.0f);
						if (movement) movement->isGrounded = true;
					}
				}
				tc.translate.y = restoreY;
				if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) rb->velocity.y = 0.0f;
				status.bubblePositionSaved = false;
			}

			if (mesh && status.baseColorSaved && health && health->hitFlashTimer <= 0.0f) {
				if (status.bubbleTimer > 0.0f) {
					mesh->color = {0.58f, 0.88f, 1.0f, status.baseColor.w};
				} else if (status.freezeTimer > 0.0f) {
					mesh->color = {0.42f, 0.84f, 1.0f, status.baseColor.w};
				} else if (status.acidTimer > 0.0f) {
					mesh->color = {0.55f, 1.0f, 0.18f, status.baseColor.w};
				} else {
					mesh->color = status.baseColor;
				}
			}

			MaintainStatusVisual(registry, entity, status.freezeTimer, status.iceVisual, CanVisualKind::IceStatus, destroyVisuals);
			MaintainStatusVisual(registry, entity, status.acidTimer, status.acidVisual, CanVisualKind::AcidStatus, destroyVisuals);
			MaintainStatusVisual(registry, entity, status.bubbleTimer, status.bubbleVisual, CanVisualKind::BubbleStatus, destroyVisuals);

			if (status.freezeTimer <= 0.0f && status.acidTimer <= 0.0f && status.bubbleTimer <= 0.0f) {
				if (mesh && status.baseColorSaved) mesh->color = status.baseColor;
				removeStatuses.push_back(entity);
			}
		}

		DestroyEntities(registry, destroyVisuals);
		CreateRequestedVisuals(registry, ctx);
		for (auto entity : removeStatuses) {
			if (registry.valid(entity) && registry.all_of<CanStatusComponent>(entity)) {
				registry.remove<CanStatusComponent>(entity);
			}
		}
	}

	void MaintainStatusVisual(entt::registry& registry, entt::entity target, float timer,
		entt::entity& visual, CanVisualKind kind, std::vector<entt::entity>& destroyVisuals) {
		if (timer > 0.0f) {
			if (visual == entt::null || !registry.valid(visual)) {
				visualRequests_.push_back({target, kind});
			}
		} else if (visual != entt::null) {
			if (registry.valid(visual)) destroyVisuals.push_back(visual);
			visual = entt::null;
		}
	}

	void UpdateShields(entt::registry& registry, GameContext& ctx) {
		std::vector<entt::entity> removeShields;
		std::vector<entt::entity> destroyVisuals;
		auto view = registry.view<BubbleShieldComponent>();
		for (auto entity : view) {
			auto& shield = view.get<BubbleShieldComponent>(entity);
			shield.timer = (std::max)(0.0f, shield.timer - ctx.dt);
			if (shield.timer > 0.0f && shield.charges > 0) {
				if (shield.visualEntity == entt::null || !registry.valid(shield.visualEntity)) {
					visualRequests_.push_back({entity, CanVisualKind::BubbleShield});
				}
			} else {
				if (shield.visualEntity != entt::null && registry.valid(shield.visualEntity)) {
					destroyVisuals.push_back(shield.visualEntity);
				}
				removeShields.push_back(entity);
			}
		}

		DestroyEntities(registry, destroyVisuals);
		CreateRequestedVisuals(registry, ctx);
		for (auto entity : removeShields) {
			if (registry.valid(entity) && registry.all_of<BubbleShieldComponent>(entity)) {
				registry.remove<BubbleShieldComponent>(entity);
			}
		}
	}

	void UpdateVisuals(entt::registry& registry, GameContext& ctx) {
		std::vector<entt::entity> destroyVisuals;
		auto view = registry.view<CanStatusVisualComponent, TransformComponent>();
		for (auto entity : view) {
			auto& visual = view.get<CanStatusVisualComponent>(entity);
			auto& visualTc = view.get<TransformComponent>(entity);
			if (visual.target == entt::null || !registry.valid(visual.target) || !registry.all_of<TransformComponent>(visual.target)) {
				destroyVisuals.push_back(entity);
				continue;
			}

			const auto& targetTc = registry.get<TransformComponent>(visual.target);
			visualTc.translate = targetTc.translate;
			visualTc.rotate.y += ctx.dt * (visual.kind == CanVisualKind::AcidStatus ? 0.6f : 1.5f);
			const float targetRadius = (std::max)({std::abs(targetTc.scale.x), std::abs(targetTc.scale.y), std::abs(targetTc.scale.z), 1.0f});

			switch (visual.kind) {
			case CanVisualKind::IceStatus:
				visualTc.translate.y += targetRadius * 0.35f;
				visualTc.scale = {targetRadius * 1.25f, targetRadius * 1.25f, targetRadius * 1.25f};
				break;
			case CanVisualKind::AcidStatus:
				visualTc.translate.y -= targetRadius * 0.45f;
				visualTc.scale = {targetRadius * 1.7f, 0.05f, targetRadius * 1.7f};
				break;
			case CanVisualKind::BubbleStatus:
				visualTc.translate.y += targetRadius * 0.25f;
				visualTc.scale = {targetRadius * 1.7f, targetRadius * 1.7f, targetRadius * 1.7f};
				break;
			case CanVisualKind::BubbleShield:
				visualTc.translate.y += 0.4f;
				{
					const float shieldRadius = (std::max)(4.8f, targetRadius * 3.8f);
					const float breathe = 1.0f + std::sin(ctx.dt + shieldRadius + visualTc.rotate.y * 2.0f) * 0.025f;
					visualTc.scale = {shieldRadius * breathe, shieldRadius * breathe, shieldRadius * breathe};
					if (auto* mesh = registry.try_get<MeshRendererComponent>(entity)) {
						if (const auto* shield = registry.try_get<BubbleShieldComponent>(visual.target)) {
							if (shield->timer <= 2.0f) {
								const float blink = 0.5f + 0.5f * std::sin(shield->timer * 18.0f);
								mesh->color = {1.0f, 0.16f + blink * 0.30f, 0.28f + blink * 0.34f, 0.22f + blink * 0.58f};
							} else {
								mesh->color = {1.0f, 0.34f, 0.78f, 0.48f};
							}
						}
					}
				}
				break;
			}
		}
		DestroyEntities(registry, destroyVisuals);
	}

	void CreateRequestedVisuals(entt::registry& registry, GameContext& ctx) {
		for (const auto& request : visualRequests_) {
			if (!registry.valid(request.target) || !registry.all_of<TransformComponent>(request.target)) continue;

			auto visual = registry.create();
			registry.emplace<NameComponent>(visual, "CanStatusVisual");
			auto& tc = registry.emplace<TransformComponent>(visual);
			tc.translate = registry.get<TransformComponent>(request.target).translate;
			auto& link = registry.emplace<CanStatusVisualComponent>(visual);
			link.target = request.target;
			link.kind = request.kind;
			registry.emplace<TagComponent>(visual, TagType::VFX);

			auto& mesh = registry.emplace<MeshRendererComponent>(visual);
			mesh.modelPath = request.kind == CanVisualKind::AcidStatus
				? "Resources/Models/Cylinder/cylinder.obj"
				: "Resources/Models/player_ball/ball.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.useCubemap = false;
			mesh.shaderName = request.kind == CanVisualKind::AcidStatus ? "EmissiveGlow" : "ForceField";
			switch (request.kind) {
			case CanVisualKind::IceStatus: mesh.color = {0.30f, 0.86f, 1.0f, 0.34f}; break;
			case CanVisualKind::AcidStatus: mesh.color = {0.42f, 1.0f, 0.06f, 0.72f}; break;
			case CanVisualKind::BubbleStatus: mesh.color = {0.62f, 0.82f, 1.0f, 0.42f}; break;
			case CanVisualKind::BubbleShield: mesh.color = {1.0f, 0.34f, 0.78f, 0.48f}; break;
			}
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}

			auto& particles = registry.emplace<ParticleEmitterComponent>(visual);
			particles.emitter.params.name = "CanStatusAura";
			particles.emitter.params.emitRate = request.kind == CanVisualKind::AcidStatus ? 18.0f : 10.0f;
			particles.emitter.params.lifeTime = 0.45f;
			particles.emitter.params.startSize = {0.20f, 0.20f, 0.20f};
			particles.emitter.params.endSize = {0.55f, 0.55f, 0.55f};
			particles.emitter.params.velocityVariance = {1.2f, 1.2f, 1.2f};
			particles.emitter.params.acceleration = {0.0f, 0.8f, 0.0f};
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
			if (request.kind == CanVisualKind::AcidStatus) {
				particles.emitter.params.startColor = {0.55f, 1.0f, 0.10f, 0.8f};
				particles.emitter.params.endColor = {0.12f, 0.48f, 0.03f, 0.0f};
			} else if (request.kind == CanVisualKind::IceStatus) {
				particles.emitter.params.startColor = {0.75f, 0.98f, 1.0f, 0.8f};
				particles.emitter.params.endColor = {0.16f, 0.62f, 1.0f, 0.0f};
			} else {
				particles.emitter.params.startColor = {1.0f, 0.48f, 0.86f, 0.72f};
				particles.emitter.params.endColor = {0.35f, 0.82f, 1.0f, 0.0f};
			}

			if (request.kind == CanVisualKind::BubbleShield) {
				if (auto* shield = registry.try_get<BubbleShieldComponent>(request.target)) shield->visualEntity = visual;
			} else if (auto* status = registry.try_get<CanStatusComponent>(request.target)) {
				if (request.kind == CanVisualKind::IceStatus) status->iceVisual = visual;
				if (request.kind == CanVisualKind::AcidStatus) status->acidVisual = visual;
				if (request.kind == CanVisualKind::BubbleStatus) status->bubbleVisual = visual;
			}
		}
		visualRequests_.clear();
	}

	static void DestroyEntities(entt::registry& registry, const std::vector<entt::entity>& entities) {
		for (auto entity : entities) {
			if (registry.valid(entity)) registry.destroy(entity);
		}
	}
};

} // namespace Game
