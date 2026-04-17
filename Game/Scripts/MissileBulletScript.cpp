#include "MissileBulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <cmath>

namespace Game {

float MissileBulletScript::LerpFloat(float start, float end, float t) { return start + (end - start) * t; }

void MissileBulletScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	flightTime_ = 0.0f;
	hasTarget_ = false;
	target_ = entt::null;

	if (!scene) {
		return;
	}

	float hasTargetValue = GetVar(entity, scene, "HasTarget", 0.0f);

	if (hasTargetValue > 0.5f) {
		float targetEntityValue = GetVar(entity, scene, "TargetEntity", -1.0f);

		if (targetEntityValue >= 0.0f) {
			uint32_t targetEntityId = static_cast<uint32_t>(targetEntityValue);
			target_ = static_cast<entt::entity>(targetEntityId);
			hasTarget_ = true;
		}
	}

	damage_ = GetVar(entity, scene, "Damage", 10.0f);
	explosionRadius_ = GetVar(entity, scene, "ExplosionRadius", 10.0f);

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	startPosition_.x = missileTransform.translate.x;
	startPosition_.y = missileTransform.translate.y;
	startPosition_.z = missileTransform.translate.z;
}

void MissileBulletScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	lifeTime_ += dt;
	flightTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!hasTarget_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!registry.valid(target_)) {
		CreateExplosionAttackArea(entity, scene);
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!registry.all_of<TransformComponent>(target_)) {
		CreateExplosionAttackArea(entity, scene);
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

	float t = flightTime_ / maxFlightTime_;

	if (t < 0.0f) {
		t = 0.0f;
	}

	if (t > 1.0f) {
		t = 1.0f;
	}

	float targetX = targetTransform.translate.x;
	float targetY = targetTransform.translate.y;
	float targetZ = targetTransform.translate.z;

	float baseX = LerpFloat(startPosition_.x, targetX, t);
	float baseY = LerpFloat(startPosition_.y, targetY, t);
	float baseZ = LerpFloat(startPosition_.z, targetZ, t);

	float heightOffset = 4.0f * arcHeight_ * t * (1.0f - t);

	missileTransform.translate.x = baseX;
	missileTransform.translate.y = baseY + heightOffset;
	missileTransform.translate.z = baseZ;

	float nextT = t + 0.02f;

	if (nextT > 1.0f) {
		nextT = 1.0f;
	}

	float nextBaseX = LerpFloat(startPosition_.x, targetX, nextT);
	float nextBaseY = LerpFloat(startPosition_.y, targetY, nextT);
	float nextBaseZ = LerpFloat(startPosition_.z, targetZ, nextT);

	float nextHeightOffset = 4.0f * arcHeight_ * nextT * (1.0f - nextT);

	float directionX = nextBaseX - missileTransform.translate.x;
	float directionY = (nextBaseY + nextHeightOffset) - missileTransform.translate.y;
	float directionZ = nextBaseZ - missileTransform.translate.z;

	float directionLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);

	if (directionLength > 0.0001f) {
		directionX /= directionLength;
		directionY /= directionLength;
		directionZ /= directionLength;

		float yaw = std::atan2(directionX, directionZ);
		float horizontalLength = std::sqrt(directionX * directionX + directionZ * directionZ);
		float pitch = std::atan2(-directionY, horizontalLength);

		missileTransform.rotate.y = yaw;
		missileTransform.rotate.x = pitch;
	}

	if (flightTime_ >= maxFlightTime_) {
		CreateExplosionAttackArea(entity, scene);
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

}

void MissileBulletScript::CreateExplosionAttackArea(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	const TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	entt::entity explosionAttackArea = registry.create();

	if (scene->GetRenderer()) {
		MeshRendererComponent& explosionMeshRenderer = registry.emplace<MeshRendererComponent>(explosionAttackArea);
		explosionMeshRenderer.modelHandle = scene->GetRenderer()->LoadObjMesh("Resources/Models/cube/cube.obj");
		explosionMeshRenderer.textureHandle = scene->GetRenderer()->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	TagComponent& explosionTag = registry.emplace<TagComponent>(explosionAttackArea);
	explosionTag.tag = TagType::Bullet;

	TransformComponent& explosionTransform = registry.emplace<TransformComponent>(explosionAttackArea);
	explosionTransform.translate = missileTransform.translate;
	explosionTransform.rotate = {0.0f, 0.0f, 0.0f};
	explosionTransform.scale = {explosionRadius_, explosionRadius_, explosionRadius_};

	ScriptComponent& explosionScript = registry.emplace<ScriptComponent>(explosionAttackArea);
	explosionScript.scripts.push_back({"ExplosionAttackArea", "", nullptr});

	SetVar(explosionAttackArea, scene, "Damage", damage_);
	SetVar(explosionAttackArea, scene, "ExplosionRadius", explosionRadius_);
}

void MissileBulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(MissileBulletScript);

} // namespace Game