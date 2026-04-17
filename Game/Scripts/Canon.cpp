#include "Canon.h"
#include "BulletScript.h"
#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TagComponent>(entity)) {
		return false;
	}

	return registry.get<TagComponent>(entity).tag == tagName;
}

static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.valid(a)) {
		return false;
	}

	if (!registry.valid(b)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(a)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(b)) {
		return false;
	}

	const TransformComponent& transformA = registry.get<TransformComponent>(a);
	const TransformComponent& transformB = registry.get<TransformComponent>(b);

	float diffX = transformB.translate.x - transformA.translate.x;
	float diffY = transformB.translate.y - transformA.translate.y;
	float diffZ = transformB.translate.z - transformA.translate.z;

	float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

	if (distance <= connectRange) {
		return true;
	}

	return false;
}

static void CollectConnectedBulletTanks(
    entt::registry& registry, entt::entity currentPipe, std::unordered_set<entt::entity>& visitedPipes, std::unordered_set<entt::entity>& foundTanks, const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allTanks, float connectRange) {
	visitedPipes.insert(currentPipe);

	for (entt::entity tank : allTanks) {
		if (IsConnectedSphere(registry, currentPipe, tank, connectRange)) {
			foundTanks.insert(tank);
		}
	}

	for (entt::entity otherPipe : allPipes) {
		if (otherPipe == currentPipe) {
			continue;
		}

		if (visitedPipes.count(otherPipe) > 0) {
			continue;
		}

		if (!IsConnectedSphere(registry, currentPipe, otherPipe, connectRange)) {
			continue;
		}

		CollectConnectedBulletTanks(registry, otherPipe, visitedPipes, foundTanks, allPipes, allTanks, connectRange);
	}
}

static void CollectConnectedCanons(
    entt::registry& registry, entt::entity currentPipe, std::unordered_set<entt::entity>& visitedPipes, std::unordered_set<entt::entity>& foundCanons, const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allCanons, float connectRange) {
	visitedPipes.insert(currentPipe);

	for (entt::entity canon : allCanons) {
		if (IsConnectedSphere(registry, currentPipe, canon, connectRange)) {
			foundCanons.insert(canon);
		}
	}

	for (entt::entity otherPipe : allPipes) {
		if (otherPipe == currentPipe) {
			continue;
		}

		if (visitedPipes.count(otherPipe) > 0) {
			continue;
		}

		if (!IsConnectedSphere(registry, currentPipe, otherPipe, connectRange)) {
			continue;
		}

		CollectConnectedCanons(registry, otherPipe, visitedPipes, foundCanons, allPipes, allCanons, connectRange);
	}
}

void Canon::Start(entt::entity /*entity*/, GameScene* /*scene*/) { attackTimer_ = 0.0f; }

void Canon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	connectionCheckTimer_ -= dt;
	if (connectionCheckTimer_ <= 0.0f) {
		connectionCheckTimer_ = 0.5f;
		UpdateConnection(entity, scene);
	}

	float powerRate = 0.0f;

	if (connectedCanonCount > 0) {
		powerRate = static_cast<float>(connectedTankCount) / static_cast<float>(connectedCanonCount);
	}

	float currentAttackInterval = attackInterval_;

	if (powerRate > 0.0f) {
		currentAttackInterval = attackInterval_ / powerRate;
	}

	// Debug(isConnectedToTank_); // ★削除: Update 内での ImGui 呼び出しは例外の原因となる可能性があるため

	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	if (!isConnectedToTank_) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	if (registry.valid(currentTarget_)) {
		if (!registry.all_of<TransformComponent>(currentTarget_)) {
			currentTarget_ = entt::null;
		}
	} else {
		currentTarget_ = entt::null;
	}

	if (currentTarget_ == entt::null) {
		float bestDistance = attackRange_;

		const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

		for (entt::entity other : enemies) {
			if (!registry.valid(other)) {
				continue;
			}

			if (!registry.all_of<TransformComponent>(other)) {
				continue;
			}

			TransformComponent& enemyTransform = registry.get<TransformComponent>(other);

			float diffX = enemyTransform.translate.x - canonTransform.translate.x;
			float diffY = enemyTransform.translate.y - canonTransform.translate.y;
			float diffZ = enemyTransform.translate.z - canonTransform.translate.z;

			float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			if (distance < bestDistance) {
				bestDistance = distance;
				currentTarget_ = other;
			}
		}
	}

	if (currentTarget_ == entt::null) {
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(currentTarget_);

	float toX = targetTransform.translate.x - canonTransform.translate.x;
	float toZ = targetTransform.translate.z - canonTransform.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	float desiredYaw = std::atan2(toX, toZ);
	float toY = targetTransform.translate.y - canonTransform.translate.y;
	float distanceXZ = std::sqrt(toX * toX + toZ * toZ);
	float desiredPitch = std::atan2(toY, distanceXZ);

	canonTransform.rotate.y = desiredYaw;
	canonTransform.rotate.x = -desiredPitch;

	if (attackTimer_ > 0.0f) {
		return;
	}

	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
	bulletTransform.translate = canonTransform.translate;

	float baseHeight = 0.0f;
	bulletTransform.translate.y += baseHeight;

	float muzzleOffset = 2.5f;
	float cosX = std::cos(canonTransform.rotate.x);
	float sinX = std::sin(canonTransform.rotate.x);

	bulletTransform.translate.x += std::sin(canonTransform.rotate.y) * cosX * muzzleOffset;
	bulletTransform.translate.y += -sinX * muzzleOffset;
	bulletTransform.translate.z += std::cos(canonTransform.rotate.y) * cosX * muzzleOffset;

	bulletTransform.rotate = canonTransform.rotate;
	bulletTransform.scale = {0.3f, 0.3f, 0.3f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	HitboxComponent& bulletHitbox = registry.emplace<HitboxComponent>(bullet);
	bulletHitbox.isActive = true;
	bulletHitbox.damage = damage_;
	bulletHitbox.tag = TagType::Bullet;
	bulletHitbox.size = {1.0f, 1.0f, 1.0f};

	ScriptComponent& bulletScriptComponent = registry.emplace<ScriptComponent>(bullet);
	bulletScriptComponent.scripts.push_back({"BulletScript", "", nullptr});

	SetVar(bullet, scene, "HasTarget", 1.0f);
	SetVar(bullet, scene, "TargetEntity", static_cast<float>(static_cast<uint32_t>(currentTarget_)));

	attackTimer_ = currentAttackInterval;
}

void Canon::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void Canon::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Rotate Speed", &rotationSpeed_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);

	ImGui::Separator();
	ImGui::Text("Status (Debug)");
	ImGui::Text("Rotation: %.2f", rotationSpeed_);
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
	ImGui::Text("Connected Canons: %d", connectedCanonCount);
	ImGui::Text("Connected to Tank: %s", isConnectedToTank_ ? "YES" : "NO");
#endif
}

void Canon::UpdateConnection(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();
	float connectRange = 2.5f;

	const std::vector<entt::entity>& allPipes = scene->GetEntitiesByTag(TagType::Pipe);
	const std::vector<entt::entity>& allTanks = scene->GetEntitiesByTag(TagType::BulletTank);
	const std::vector<entt::entity>& allCanons = scene->GetEntitiesByTag(TagType::Canon);

	std::unordered_set<entt::entity> foundTanks;
	std::unordered_set<entt::entity> visitedPipesForTanks;

	for (entt::entity pipe : allPipes) {
		if (!IsConnectedSphere(registry, entity, pipe, connectRange)) {
			continue;
		}

		CollectConnectedBulletTanks(registry, pipe, visitedPipesForTanks, foundTanks, allPipes, allTanks, connectRange);
	}

	connectedTankCount = static_cast<int>(foundTanks.size());

	if (connectedTankCount > 0) {
		isConnectedToTank_ = true;
	} else {
		isConnectedToTank_ = false;
	}

	std::unordered_set<entt::entity> foundCanons;
	std::unordered_set<entt::entity> visitedPipesForCanons;

	for (entt::entity pipe : allPipes) {
		if (!IsConnectedSphere(registry, entity, pipe, connectRange)) {
			continue;
		}

		CollectConnectedCanons(registry, pipe, visitedPipesForCanons, foundCanons, allPipes, allCanons, connectRange);
	}

	foundCanons.insert(entity);
	connectedCanonCount = static_cast<int>(foundCanons.size());
}

void Canon::Debug(bool /*connected*/) {
	// 以前はここで ImGui::Begin を呼んでいたが、Update からの呼び出しは危険なため廃止。
	// 代わりに OnEditorUI を使用する。
}

REGISTER_SCRIPT(Canon);

} // namespace Game