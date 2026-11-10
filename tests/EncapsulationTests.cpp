#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Engine/System.h"
#include "../Engine/CollisionSystem.h"
#include "../Game/Scripts/ScriptLifecycle.h"
#include "../Game/Scripts/ScriptEdits.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>
#include <iostream>

class CleanupScript final : public Game::IScript {
public:
    int& calls;
    std::function<void()> cleanup;
    explicit CleanupScript(int& count) : calls(count) {}
    void OnDestroy(entt::entity, Game::GameScene*) override {
        ++calls;
        if (cleanup) cleanup();
    }
};

void TestScriptLifecycle() {
    int calls = 0;
    Game::ScriptEntry entry;
    auto script = std::make_shared<CleanupScript>(calls);
    entry.instance = script;
    entry.isStarted = true;
    script->cleanup = [&] { Game::StopScript(entry, entt::null, nullptr); };
    Game::StopScript(entry, entt::null, nullptr);
    assert(calls == 1 && !entry.instance && !entry.isStarted);
    entry.instance = std::make_shared<CleanupScript>(calls);
    Game::StopScript(entry, entt::null, nullptr);
    assert(calls == 1); // Editor-only instances have not run Start.

    Game::ScriptComponent component;
    auto first = std::make_shared<CleanupScript>(calls);
    first->cleanup = [&] {
        Game::StopScripts(component, entt::null, nullptr);
        component.scripts.clear();
    };
    component.scripts.push_back({"first", "", first, true});
    component.scripts.push_back({"second", "", std::make_shared<CleanupScript>(calls), true});
    Game::StopScripts(component, entt::null, nullptr);
    assert(calls == 3 && component.scripts.empty());

    entt::registry registry;
    auto entity = registry.create();
    auto destroyComponent = std::make_shared<CleanupScript>(calls);
    destroyComponent->cleanup = [&] { registry.remove<Game::ScriptComponent>(entity); };
    registry.emplace<Game::ScriptComponent>(entity).scripts.push_back({"remove", "", destroyComponent, true});
    Game::StopAllScripts(registry, nullptr);
    Game::StopAllScripts(registry, nullptr);
    assert(calls == 4 && !registry.all_of<Game::ScriptComponent>(entity));
}

void TestScriptEdits() {
    entt::registry registry;
    auto entity = registry.create();
    int calls = 0;
    auto attach = [&](const std::string& name) {
        auto script = std::make_shared<CleanupScript>(calls);
        registry.get_or_emplace<Game::ScriptComponent>(entity).scripts.push_back({name, "old params", script, true});
        return script;
    };
    auto original = attach("old");
    Game::ScriptEdit replace{Game::ScriptEditKind::Replace, entity, 0, "old", original, "new"};
    assert(Game::ApplyScriptEdit(registry, replace, nullptr));
    auto& updated = registry.get<Game::ScriptComponent>(entity).scripts[0];
    assert(calls == 1 && updated.scriptPath == "new" && updated.parameterData == "{}");
    assert(!updated.instance && !updated.isStarted);
    assert(!Game::ApplyScriptEdit(registry, replace, nullptr)); // Stale edit must not affect the replacement.
    registry.remove<Game::ScriptComponent>(entity);

    original = attach("remove-own-component");
    original->cleanup = [&] { registry.remove<Game::ScriptComponent>(entity); };
    replace = {Game::ScriptEditKind::Replace, entity, 0, "remove-own-component", original, "new"};
    assert(Game::ApplyScriptEdit(registry, replace, nullptr));
    assert(calls == 2 && !registry.all_of<Game::ScriptComponent>(entity));

    original = attach("mutate-list");
    original->cleanup = [&] { registry.get<Game::ScriptComponent>(entity).scripts.push_back({"added-by-cleanup", "", {}, false}); };
    assert(Game::ApplyScriptEdit(registry, {Game::ScriptEditKind::Remove, entity, 0, "mutate-list", original, {}}, nullptr));
    assert(calls == 3 && registry.get<Game::ScriptComponent>(entity).scripts[0].scriptPath == "added-by-cleanup");
    original = attach("remove-component");
    original->cleanup = [&] { Game::StopAllScripts(registry, nullptr); };
    assert(Game::ApplyScriptEdit(registry, {Game::ScriptEditKind::RemoveComponent, entity, 0, {}, {}, {}}, nullptr));
    assert(calls == 4 && !registry.all_of<Game::ScriptComponent>(entity));
    assert(!Game::ApplyScriptEdit(registry, {Game::ScriptEditKind::Add, entity, 0, {}, {}, {}}, nullptr));
}

template<class T> concept CanClear = requires(T& value) { value.clear(); };
template<class T> concept CanCreate = requires(T& value) { value.create(); };
template<class T> concept HasMutableContext = requires(T& value) { value.GetContext(); };
template<class T> concept PublicHp = requires(T& value) { value.hp = 0.f; };
template<class T> concept PublicMembership = requires(T& value) { value.m_entities.clear(); };
static_assert(!CanClear<Game::SceneComponents> && !CanCreate<Game::SceneComponents>);
static_assert(!HasMutableContext<Game::GameScene>);
static_assert(!PublicHp<Game::HealthComponent>);
static_assert(!PublicMembership<TDEngine::ECS::System>);
static_assert(std::is_same_v<decltype(std::declval<Game::GameScene&>().GetRegistry()), const entt::registry&>);
static_assert(std::is_same_v<decltype(std::declval<Game::GameScene&>().GetSelectedEntities()), const std::set<entt::entity>&>);
static_assert(std::is_same_v<decltype(std::declval<Engine::Renderer&>().GetGraphicsSettings()), const Engine::Renderer::GraphicsSettings&>);

int main() {
    TestScriptLifecycle();
    TestScriptEdits();
    // Existing HP rules, including the delayed death flag and sandbag values.
    Game::HealthComponent health;
    assert(health.Hp() == 100 && health.MaxHp() == 100 && !health.IsDead());
    health.ApplyDamage(140);
    assert(health.Hp() == 0 && !health.IsDead());
    health.SetDead(true);
    health.RecoverHp(10);
    assert(health.Hp() == 10 && health.IsDead());
    health.RecoverHp(200);
    assert(health.Hp() == 100);
    assert(!health.TryConsumeHp(101) && health.Hp() == 100);
    assert(health.TryConsumeHp(100) && health.Hp() == 0);
    health.RestoreHpIfDepleted();
    assert(health.Hp() == 100 && health.IsDead());
    health.SetHp(9999); health.SetMaxHp(9999);
    assert(health.Hp() == 9999 && health.MaxHp() == 9999);
    health.SetHp(std::numeric_limits<float>::quiet_NaN());
    assert(!health.TryConsumeHp(5));

    entt::registry registry;
    Game::SceneComponents components(registry);
    auto entity = registry.create();
    components.emplace<Game::HealthComponent>(entity).ApplyDamage(25);
    assert(components.get<Game::HealthComponent>(entity).Hp() == 75);
    assert(components.view<Game::HealthComponent>().size() == 1);
    components.remove<Game::HealthComponent>(entity);
    assert(components.valid(entity) && !components.all_of<Game::HealthComponent>(entity));
    std::cout << "PASS: encapsulation API and gameplay rules\n";
}
