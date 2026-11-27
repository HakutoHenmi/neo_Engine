#include "ScriptSystem.h"
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
#include <vector>

namespace Game {

void ScriptSystem::Update(entt::registry& registry, GameContext& ctx) {
	// PLAY 中でなければ実行しない
	if (!ctx.isPlaying)
		return;

	// スクリプトを動かすエンジン
	auto* scriptEngine = ScriptEngine::GetInstance();

	// スクリプトを持つエンティティの一覧を複製する
	auto view = registry.view<ScriptComponent>();
	std::vector<entt::entity> entities(view.begin(), view.end());
	if (entities.empty())
		return;

	// まだ有効で、スクリプトが有効なものだけを実行する
	for (auto entity : entities) {
		if (!registry.valid(entity))
			continue;
		auto& sc = registry.get<ScriptComponent>(entity);
		if (sc.enabled && !sc.scripts.empty()) {
			scriptEngine->Execute(entity, scene_, ctx.dt);
		}
	}
}

void ScriptSystem::Reset(entt::registry& registry) {
	StopAllScripts(registry, scene_);
}

} // namespace Game
