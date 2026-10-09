#pragma once
#include "ScriptLifecycle.h"
#include <cstddef>

namespace Game {

enum class ScriptEditKind { Replace, Remove, Add, RemoveComponent };

struct ScriptEdit {
    ScriptEditKind kind;
    entt::entity entity;
    size_t index = 0;
    std::string expectedPath;
    std::shared_ptr<IScript> expectedInstance;
    std::string replacementPath;
};

// Apply after drawing the inspector. Complete structural changes before user
// cleanup callbacks, and never use a component/entry reference after a callback.
template<class Components>
bool ApplyScriptEdit(Components& components, const ScriptEdit& edit, GameScene* scene) {
    if (!components.valid(edit.entity)) return false;
    auto* component = components.template try_get<ScriptComponent>(edit.entity);
    if (!component) return false;
    if (edit.kind == ScriptEditKind::Add) {
        component->scripts.emplace_back();
        return true;
    }
    if (edit.kind == ScriptEditKind::RemoveComponent) {
        ScriptComponent detached;
        detached.scripts.swap(component->scripts);
        components.template remove<ScriptComponent>(edit.entity);
        StopScripts(detached, edit.entity, scene);
        return true;
    }
    if (edit.index >= component->scripts.size()) return false;
    auto& entry = component->scripts[edit.index];
    if (entry.scriptPath != edit.expectedPath || entry.instance != edit.expectedInstance) return false;
    ScriptEntry detached = std::move(entry);
    if (edit.kind == ScriptEditKind::Replace) {
        entry = ScriptEntry{edit.replacementPath, "{}", nullptr, false};
    } else {
        component->scripts.erase(component->scripts.begin() + static_cast<std::ptrdiff_t>(edit.index));
    }
    StopScript(detached, edit.entity, scene);
    return true;
}

} // namespace Game
