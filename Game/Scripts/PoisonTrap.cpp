#include "PoisonTrap.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
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
	entt::registry& registry,
	entt::entity currentPipe,
	std::unordered_set<entt::entity>& visitedPipes,
	std::unordered_set<entt::entity>& foundTanks,
	const std::vector<entt::entity>& allPipes,
	const std::vector<entt::entity>& allTanks,
	float connectRange) {
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

void PoisonTrap::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	poisonTimer_ = 0.0f;
}

void PoisonTrap::Update(entt::entity entity, GameScene* scene, float dt) {
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

	// Debug(isConnectedToTank_); // ★削除: Update 内での ImGui 呼び出しは例外の原因となる可能性があるため

	if (!isConnectedToTank_) {
		return;
	}

	if (!IsEnemyInRange(entity, scene, poisonRange_)) {
		return;
	}



	CreatePoisonAttackArea(entity, scene);
	poisonTimer_ = poisonInterval_;
}

void PoisonTrap::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

void PoisonTrap::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Poison Damage", &poisonDamage_, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("Poison Range", &poisonRange_, 0.1f, 0.1f, 20.0f);
	ImGui::DragFloat("Poison Interval", &poisonInterval_, 0.01f, 0.05f, 10.0f);

	ImGui::Separator();
	ImGui::Text("Status (Debug)");
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
	ImGui::Text("Connected to Tank: %s", isConnectedToTank_ ? "YES" : "NO");
	ImGui::Text("Poison Range: %.2f", poisonRange_);
	ImGui::Text("Poison Interval: %.2f", poisonInterval_);
#endif
}

void PoisonTrap::UpdateConnection(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();
	float connectRange = 2.5f;

	const std::vector<entt::entity>& allPipes = scene->GetEntitiesByTag(TagType::Pipe);
	const std::vector<entt::entity>& allTanks = scene->GetEntitiesByTag(TagType::BulletTank);

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
}

bool PoisonTrap::IsEnemyInRange(entt::entity entity, GameScene* scene, float range) {
	if (!scene) {
		return false;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return false;
	}

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);
	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - trapTransform.translate.x;
		float diffY = enemyTransform.translate.y - trapTransform.translate.y;
		float diffZ = enemyTransform.translate.z - trapTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance <= range) {
			return true;
		}
	}

	return false;
}

void PoisonTrap::CreatePoisonAttackArea(entt::entity entity, GameScene* scene) {
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

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);

	entt::entity poisonAttackArea = registry.create();
	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& poisonMeshRenderer = registry.emplace<MeshRendererComponent>(poisonAttackArea);
		poisonMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		poisonMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}
	TagComponent& poisonTag = registry.emplace<TagComponent>(poisonAttackArea);
	poisonTag.tag = TagType::Poison;

	TransformComponent& poisonTransform = registry.emplace<TransformComponent>(poisonAttackArea);
	poisonTransform.translate = trapTransform.translate;
	poisonTransform.rotate = trapTransform.rotate;
	poisonTransform.scale = {poisonRange_/2.0f, poisonRange_/2.0f, poisonRange_/2.0f};

	HitboxComponent& poisonHitbox = registry.emplace<HitboxComponent>(poisonAttackArea);
	poisonHitbox.isActive = true;
	poisonHitbox.damage = poisonDamage_;
	poisonHitbox.tag = TagType::Poison;
	poisonHitbox.size = {poisonRange_, poisonRange_, poisonRange_};

	ScriptComponent& poisonScript = registry.emplace<ScriptComponent>(poisonAttackArea);
	poisonScript.scripts.push_back({"PoisonAttackArea", "", nullptr});


}

void PoisonTrap::Debug(bool /*connected*/) {
	// 以前はここで ImGui::Begin を呼んでいたが、Update からの呼び出しは危険なため廃止。
	// 代わりに OnEditorUI を使用する。
}

REGISTER_SCRIPT(PoisonTrap);

} // namespace Game