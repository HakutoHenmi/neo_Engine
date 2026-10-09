#pragma once
#include "IScript.h"
#include "../ObjectTypes.h"
#include <utility>
#include <vector>

namespace Game {

// Detach before calling user code so reentrant cleanup cannot stop it twice.
inline void StopScript(ScriptEntry& entry, entt::entity entity, GameScene* scene) {
    auto instance = std::move(entry.instance);
    const bool started = std::exchange(entry.isStarted, false);
    if (instance && started) instance->OnDestroy(entity, scene);
}

inline void StopScripts(ScriptComponent& component, entt::entity entity, GameScene* scene) {
    std::vector<std::shared_ptr<IScript>> startedScripts;
    for (auto& entry : component.scripts) {
        auto instance = std::move(entry.instance);
        if (std::exchange(entry.isStarted, false) && instance) {
            startedScripts.push_back(std::move(instance));
        }
    }
    // Callbacks may change the component; do not access it after detaching.
    for (const auto& instance : startedScripts) instance->OnDestroy(entity, scene);
}

inline void StopAllScripts(entt::registry& registry, GameScene* scene) {
    auto view = registry.view<ScriptComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    for (auto entity : entities) {
        if (!registry.valid(entity)) continue;
        if (auto* component = registry.try_get<ScriptComponent>(entity)) StopScripts(*component, entity, scene);
    }
}

} // namespace Game
