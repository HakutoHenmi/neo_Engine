// ScriptLifecycle.h
// スクリプトの停止 (OnDestroy の呼び出し) の共通処理。
#pragma once
#include "IScript.h"
#include "../ObjectTypes.h"
#include <utility>
#include <vector>

namespace Game {

// ユーザーのコードを呼ぶ前に切り離し、OnDestroy の中から再び停止されても二重に止めないようにする
inline void StopScript(ScriptEntry& entry, entt::entity entity, GameScene* scene) {
    auto instance = std::move(entry.instance);
    const bool started = std::exchange(entry.isStarted, false);
    if (instance && started) instance->OnDestroy(entity, scene);
}

// コンポーネント内の全スクリプトを停止する
inline void StopScripts(ScriptComponent& component, entt::entity entity, GameScene* scene) {
    std::vector<std::shared_ptr<IScript>> startedScripts;
    for (auto& entry : component.scripts) {
        auto instance = std::move(entry.instance);
        if (std::exchange(entry.isStarted, false) && instance) {
            startedScripts.push_back(std::move(instance));
        }
    }
    // コールバックがコンポーネントを書き換える可能性があるので、切り離した後は触らない
    for (const auto& instance : startedScripts) instance->OnDestroy(entity, scene);
}

// シーン内の全スクリプトを停止する (シーンを空にする前に呼ぶ)
inline void StopAllScripts(entt::registry& registry, GameScene* scene) {
    auto view = registry.view<ScriptComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    for (auto entity : entities) {
        if (!registry.valid(entity)) continue;
        if (auto* component = registry.try_get<ScriptComponent>(entity)) StopScripts(*component, entity, scene);
    }
}

} // namespace Game
