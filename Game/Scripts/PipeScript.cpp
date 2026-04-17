#include "PipeScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

namespace Game {

static bool HasTag(GameScene* scene, entt::entity entity, TagType tagName) {
	if (!scene->GetRegistry().all_of<TagComponent>(entity)) return false;
	return scene->GetRegistry().get<TagComponent>(entity).tag == tagName;
}

static bool IsConnectedSphere(GameScene* scene, entt::entity a, entt::entity b, float connectRange) {
	if (!scene->GetRegistry().all_of<TransformComponent>(a) || !scene->GetRegistry().all_of<TransformComponent>(b)) return false;
	
	auto& aTc = scene->GetRegistry().get<TransformComponent>(a);
	auto& bTc = scene->GetRegistry().get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
	float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > connectRange) {
		return false;
	}

	return true;
}

static bool IsAlreadyVisited(const std::vector<entt::entity>& visitedObjects, entt::entity obj) {
	for (size_t i = 0; i < visitedObjects.size(); ++i) {
		if (visitedObjects[i] == obj) {
			return true;
		}
	}
	return false;
}

static bool IsConnectedToBulletTankRecursive(GameScene* scene, entt::entity currentPipe, std::vector<entt::entity>& visitedObjects, float connectRange) {
	visitedObjects.push_back(currentPipe);

	auto view = scene->GetRegistry().view<TransformComponent>();
	for (auto other : view) {

		if (other == currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(scene, currentPipe, other, connectRange)) {
			continue;
		}

		// 隣に弾倉があれば到達成功
		if (HasTag(scene, other, TagType::BulletTank)) {
			return true;
		}

		// 隣がパイプならさらに先を調べる
		if (HasTag(scene, other, TagType::Pipe)) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

	return false;
}

static bool IsConnectedToBulletTank(GameScene* scene, entt::entity selfObj) {
	const float connectRange = 2.5f;

	std::vector<entt::entity> visitedObjects;

	return IsConnectedToBulletTankRecursive(scene, selfObj, visitedObjects, connectRange);
}

void PipeScript::Start(entt::entity obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void PipeScript::Update(entt::entity obj, GameScene* scene, float dt) {
	// bool connectedToTank = IsConnectedToBulletTank(scene, obj);
	(void)scene;
	float speed = rotationSpeed_;

	// if (connectedToTank) {
	// speed = rotationSpeed_ * 3.0f;
	//}

	if (scene->GetRegistry().all_of<TransformComponent>(obj)) {
		scene->GetRegistry().get<TransformComponent>(obj).rotate.z += speed * dt;
	}
}

void PipeScript::OnDestroy(entt::entity obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(PipeScript);

} // namespace Game