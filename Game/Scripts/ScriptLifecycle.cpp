#include "ScriptLifecycle.h"
#include <algorithm>

namespace Game {

void NotifyScriptClick(entt::registry& registry, entt::entity entity, GameScene* scene, std::string callbackName) {
	if (!registry.valid(entity))
		return;
	const auto* component = registry.try_get<ScriptComponent>(entity);
	if (!component || !component->enabled)
		return;
	std::vector<std::shared_ptr<IScript>> targets;
	for (const auto& entry : component->scripts)
		if (entry.instance && entry.isStarted)
			targets.push_back(entry.instance);
	for (const auto& instance : targets) {
		if (!registry.valid(entity))
			break;
		component = registry.try_get<ScriptComponent>(entity);
		if (!component || !component->enabled)
			break;
		const bool attached = std::any_of(component->scripts.begin(), component->scripts.end(), [&](const auto& entry) {
			return entry.instance == instance && entry.isStarted;
		});
		if (attached)
			instance->OnClick(entity, scene, callbackName);
	}
}

} // namespace Game
