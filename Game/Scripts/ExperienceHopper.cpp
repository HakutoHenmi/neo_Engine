#include "ExperienceHopper.h"
#include "ScriptEngine.h"
#include "Scenes/GameScene.h"
#include "ObjectTypes.h"
#include "../imgui/imgui.h"
#include <cmath>

namespace Game {

// タグ走査 (entt版)
static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

static int CountExperienceOrbs(entt::registry& registry) {
	int orbCount = 0;
	auto view = registry.view<TagComponent>();
	for (auto entity : view) {
		if (view.get<TagComponent>(entity).tag == TagType::ExperienceOrb) orbCount++;
	}
	return orbCount;
}

static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.all_of<TransformComponent>(a) || !registry.all_of<TransformComponent>(b)) return false;
	auto& posA = registry.get<TransformComponent>(a).translate;
	auto& posB = registry.get<TransformComponent>(b).translate;
	float dx = posB.x - posA.x, dy = posB.y - posA.y, dz = posB.z - posA.z;
	return std::sqrt(dx*dx + dy*dy + dz*dz) <= connectRange;
}

static bool IsAlreadyVisited(const std::vector<entt::entity>& visited, entt::entity e) {
	for (auto v : visited) if (v == e) return true;
	return false;
}

static bool IsPipeConnectedToExperienceMinerRecursive(entt::registry& registry, entt::entity currentPipe, std::vector<entt::entity>& visited, float connectRange) {
	visited.push_back(currentPipe);
	auto view = registry.view<TransformComponent>();
	for (auto other : view) {
		if (other == currentPipe) continue;
		if (!IsConnectedSphere(registry, currentPipe, other, connectRange)) continue;
		if (HasTag(registry, other, TagType::ExperienceMiner)) return true;
		if (HasTag(registry, other, TagType::Pipe)) {
			if (IsAlreadyVisited(visited, other)) continue;
			if (IsPipeConnectedToExperienceMinerRecursive(registry, other, visited, connectRange)) return true;
		}
	}
	return false;
}

static bool IsHopperConnectedToMiner(entt::registry& registry, entt::entity hopperEntity) {
	const float connectRange = 2.5f;
	auto view = registry.view<TransformComponent>();
	for (auto other : view) {
		if (!HasTag(registry, other, TagType::Pipe)) continue;
		if (!IsConnectedSphere(registry, hopperEntity, other, connectRange)) continue;
		std::vector<entt::entity> visited;
		if (IsPipeConnectedToExperienceMinerRecursive(registry, other, visited, connectRange)) return true;
	}
	return false;
}

void ExperienceHopper::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
}

void ExperienceHopper::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;
	auto& tc = registry.get<TransformComponent>(entity);
	tc.rotate.y += 1.0f * dt;

	if (spawnTimer_ > 0.0f) spawnTimer_ -= dt;

	bool connected = IsHopperConnectedToMiner(registry, entity);
	if (!connected) return;

	int orbCount = CountExperienceOrbs(registry);
	if (orbCount >= 10) return;
	if (spawnTimer_ > 0.0f) return;

	// ExperienceOrb を生成 (enTT)
	entt::entity orb = registry.create();
	auto& oTag = registry.emplace<TagComponent>(orb);
	oTag.tag = TagType::ExperienceOrb;
	auto& oTc = registry.emplace<TransformComponent>(orb);
	oTc.translate = tc.translate;
	oTc.translate.y += 0.5f;
	oTc.scale = {0.2f, 0.2f, 0.2f};

	auto& oHc = registry.emplace<HealthComponent>(orb);
	oHc.hp = 1.0f;
	oHc.maxHp = 1.0f;

	auto& oHitbox = registry.emplace<HitboxComponent>(orb);
	oHitbox.isActive = true;
	oHitbox.damage = 0.0f;
	oHitbox.tag = TagType::ExperienceOrb;
	oHitbox.size = {1.0f, 1.0f, 1.0f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& oMr = registry.emplace<MeshRendererComponent>(orb);
		oMr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		oMr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		
		auto& oGmc = registry.emplace<GpuMeshColliderComponent>(orb);
		oGmc.meshHandle = oMr.modelHandle;
	}

	auto& oScript = registry.emplace<ScriptComponent>(orb);
	oScript.scripts.push_back({ "ExperienceOrbScript", "", nullptr });

	spawnTimer_ = 1.0f;
}

void ExperienceHopper::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

REGISTER_SCRIPT(ExperienceHopper);
} // namespace Game
