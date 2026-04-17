#include "BaseScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

#include <cmath>
#include <sstream>

namespace Game {

void BaseScript::Start(entt::entity entity, GameScene* scene) {
	attackTimer_ = 0.0f;

	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<HealthComponent>(entity)) {
		HealthComponent& healthComponent = registry.emplace<HealthComponent>(entity);
		healthComponent.hp = 100.0f;
		healthComponent.maxHp = 100.0f;
	}

	if (!registry.all_of<HurtboxComponent>(entity)) {
		HurtboxComponent& hurtboxComponent = registry.emplace<HurtboxComponent>(entity);
		hurtboxComponent.tag = TagType::Body;
		hurtboxComponent.enabled = true;

		if (registry.all_of<BoxColliderComponent>(entity)) {
			hurtboxComponent.center = registry.get<BoxColliderComponent>(entity).center;
			hurtboxComponent.size = registry.get<BoxColliderComponent>(entity).size;
		} else if (registry.all_of<TransformComponent>(entity)) {
			TransformComponent& transformComponent = registry.get<TransformComponent>(entity);
			hurtboxComponent.size = {transformComponent.scale.x * 2.0f, transformComponent.scale.y * 2.0f, transformComponent.scale.z * 2.0f};
		}
	}
}

void BaseScript::Update(entt::entity entity, GameScene* scene, float dt) {
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

	TransformComponent& baseTransform = registry.get<TransformComponent>(entity);

	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	entt::entity target = entt::null;
	float bestDistance = attackRange_;

	const auto& enemies = scene->GetEntitiesByTag(TagType::Enemy);
	for (auto other : enemies) {
		if (!registry.valid(other) || !registry.all_of<TransformComponent>(other)) continue;
		
		auto& enemyTransform = registry.get<TransformComponent>(other);

		float differenceX = enemyTransform.translate.x - baseTransform.translate.x;
		float differenceY = enemyTransform.translate.y - baseTransform.translate.y;
		float differenceZ = enemyTransform.translate.z - baseTransform.translate.z;

		float distance = std::sqrt(differenceX * differenceX + differenceY * differenceY + differenceZ * differenceZ);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	if (target == entt::null) {
		baseTransform.rotate.y += rotationSpeed_ * dt;
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target);

	float toX = targetTransform.translate.x - baseTransform.translate.x;
	float toY = targetTransform.translate.y - baseTransform.translate.y;
	float toZ = targetTransform.translate.z - baseTransform.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	float desiredYaw = std::atan2(toX, toZ);
	float distanceXZ = std::sqrt(toX * toX + toZ * toZ);
	float desiredPitch = std::atan2(toY, distanceXZ);

	baseTransform.rotate.y = desiredYaw;
	baseTransform.rotate.x = -desiredPitch;

	if (attackTimer_ > 0.0f) {
		return;
	}

	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
	bulletTransform.translate = baseTransform.translate;

	float muzzleOffset = 1.5f;
	float cosX = std::cos(baseTransform.rotate.x);
	float sinX = std::sin(baseTransform.rotate.x);

	bulletTransform.translate.x += std::sin(baseTransform.rotate.y) * cosX * muzzleOffset;
	bulletTransform.translate.y += -sinX * muzzleOffset;
	bulletTransform.translate.z += std::cos(baseTransform.rotate.y) * cosX * muzzleOffset;

	bulletTransform.rotate = baseTransform.rotate;
	bulletTransform.scale = {0.2f, 0.2f, 0.2f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& meshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		meshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		meshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	HitboxComponent& hitboxComponent = registry.emplace<HitboxComponent>(bullet);
	hitboxComponent.isActive = true;
	hitboxComponent.damage = damage_;
	hitboxComponent.tag = TagType::Bullet;
	hitboxComponent.size = {0.2f, 0.2f, 0.2f};

	ScriptComponent& scriptComponent = registry.emplace<ScriptComponent>(bullet);
	scriptComponent.scripts.push_back({"BulletScript", "", nullptr});

	attackTimer_ = attackInterval_;
}

void BaseScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void BaseScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Rotation Speed", &rotationSpeed_, 0.1f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.1f, 0.1f, 10.0f);
	ImGui::DragFloat("Damage", &damage_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Attack Range", &attackRange_, 1.0f, 1.0f, 100.0f);
#endif
}

std::string BaseScript::SerializeParameters() {
	std::stringstream stringStream;
	stringStream << "rotationSpeed=" << rotationSpeed_ << ";";
	stringStream << "attackInterval=" << attackInterval_ << ";";
	stringStream << "damage=" << damage_ << ";";
	stringStream << "attackRange=" << attackRange_ << ";";
	return stringStream.str();
}

void BaseScript::DeserializeParameters(const std::string& data) {
	std::stringstream stringStream(data);
	std::string item;

	while (std::getline(stringStream, item, ';')) {
		size_t position = item.find('=');
		if (position == std::string::npos) {
			continue;
		}

		std::string key = item.substr(0, position);
		std::string value = item.substr(position + 1);

		if (key == "rotationSpeed") {
			rotationSpeed_ = std::stof(value);
		} else if (key == "attackInterval") {
			attackInterval_ = std::stof(value);
		} else if (key == "damage") {
			damage_ = std::stof(value);
		} else if (key == "attackRange") {
			attackRange_ = std::stof(value);
		}
	}
}

REGISTER_SCRIPT(BaseScript);

} // namespace Game