#include "MissileCanonScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <cmath>
#include <unordered_set>
#include <vector>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static bool IsConnectedSphere(entt::registry& registry, entt::entity entityA, entt::entity entityB, float connectRange) {
	if (!registry.valid(entityA)) {
		return false;
	}

	if (!registry.valid(entityB)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(entityA)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(entityB)) {
		return false;
	}

	const TransformComponent& transformA = registry.get<TransformComponent>(entityA);
	const TransformComponent& transformB = registry.get<TransformComponent>(entityB);

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

void MissileCanonScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) { attackTimer_ = 0.0f; }

void MissileCanonScript::Update(entt::entity entity, GameScene* scene, float dt) {
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

	currentTarget_ = entt::null;
	float bestDistance = attackRange_;

	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - canonTransform.translate.x;
		float diffY = enemyTransform.translate.y - canonTransform.translate.y;
		float diffZ = enemyTransform.translate.z - canonTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance < bestDistance) {
			bestDistance = distance;
			currentTarget_ = enemy;
		}
	}

	if (currentTarget_ == entt::null) {
		return;
	}

	const TransformComponent& targetTransform = registry.get<TransformComponent>(currentTarget_);

	float toTargetX = targetTransform.translate.x - canonTransform.translate.x;
	float toTargetY = targetTransform.translate.y - canonTransform.translate.y;
	float toTargetZ = targetTransform.translate.z - canonTransform.translate.z;

	float distanceXZ = std::sqrt(toTargetX * toTargetX + toTargetZ * toTargetZ);

	if (distanceXZ <= 0.0001f) {
		return;
	}

	canonTransform.rotate.y = std::atan2(toTargetX, toTargetZ);
	canonTransform.rotate.x = -std::atan2(toTargetY, distanceXZ);

	if (attackTimer_ > 0.0f) {
		return;
	}

	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
	bulletTransform.translate = canonTransform.translate;

	float launchForwardOffset = 2.5f;
	float launchHeightOffset = 1.5f;

	bulletTransform.translate.x += std::sin(canonTransform.rotate.y) * launchForwardOffset;
	bulletTransform.translate.y += launchHeightOffset;
	bulletTransform.translate.z += std::cos(canonTransform.rotate.y) * launchForwardOffset;

	bulletTransform.rotate = canonTransform.rotate;
	bulletTransform.rotate.x = -0.8f;
	bulletTransform.scale = {0.5f, 0.5f, 1.2f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	//HitboxComponent& bulletHitbox = registry.emplace<HitboxComponent>(bullet);
	//bulletHitbox.isActive = false;
	//bulletHitbox.damage = 0;
	//bulletHitbox.tag = TagType::Bullet;
	//bulletHitbox.size = {1.0f, 1.0f, 1.0f};

	ScriptComponent& bulletScriptComponent = registry.emplace<ScriptComponent>(bullet);
	bulletScriptComponent.scripts.push_back({"MissileBulletScript", "", nullptr});

	SetVar(bullet, scene, "HasTarget", 1.0f);
	SetVar(bullet, scene, "TargetEntity", static_cast<float>(static_cast<uint32_t>(currentTarget_)));
	SetVar(bullet, scene, "Damage", damage_);
	SetVar(bullet, scene, "ExplosionRadius", explosionRadius_);

	attackTimer_ = currentAttackInterval;
}

void MissileCanonScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void MissileCanonScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Damage", &damage_, 1.0f, 1.0f, 500.0f);
	ImGui::DragFloat("Explosion Radius", &explosionRadius_, 0.1f, 0.1f, 50.0f);

	ImGui::Separator();
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
	ImGui::Text("Connected Canons: %d", connectedCanonCount);

	if (isConnectedToTank_) {
		ImGui::Text("Connected to Tank: YES");
	} else {
		ImGui::Text("Connected to Tank: NO");
	}
#endif
}

void MissileCanonScript::UpdateConnection(entt::entity entity, GameScene* scene) {
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

void MissileCanonScript::Debug(bool /*connected*/) {}

REGISTER_SCRIPT(MissileCanonScript);

} // namespace Game