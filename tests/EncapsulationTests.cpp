#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Engine/System.h"
#include "../Engine/CollisionSystem.h"
#include "../Game/Scripts/ScriptLifecycle.h"
#include "../Game/Scripts/ScriptEdits.h"
#include "../Game/CanHitEffects.h"
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

void TestCanHitEffects() {
    entt::registry registry;
    auto entity = registry.create();
    auto& transform = registry.emplace<Game::TransformComponent>(entity);
    transform.translate = {3, 7, 4};
    Game::CanAttackEffectComponent effect;
    effect.strength = 10;
    const auto* magnet = Game::FindCanHitEffect(Game::CanType::Magnet);
    magnet->Apply(registry, entity, {0, 0, 0}, effect);
    assert(transform.translate.x == 9 && transform.translate.z == 12);
    assert(!registry.all_of<Game::CanStatusComponent>(entity));
    registry.emplace<Game::BossActionComponent>(entity);
    transform.translate = {3, 7, 4};
    magnet->Apply(registry, entity, {0, 0, 0}, effect);
    assert(std::abs(transform.translate.x - 5.1f) < .0001f);
    assert(std::abs(transform.translate.z - 6.8f) < .0001f);
    transform.translate = {0, 7, 0};
    magnet->Apply(registry, entity, {0, 0, 0}, effect);
    assert(transform.translate.x == 0 && transform.translate.z == 0);

    effect.duration = 10;
    Game::FindCanHitEffect(Game::CanType::Ice)->Apply(registry, entity, {}, effect);
    auto& status = registry.get<Game::CanStatusComponent>(entity);
    assert(status.freezeTimer == 5.5f);
    effect.duration = 2;
    Game::FindCanHitEffect(Game::CanType::Ice)->Apply(registry, entity, {}, effect);
    assert(status.freezeTimer == 5.5f); // Reapplying does not shorten a status.
    status.acidTickTimer = 1;
    Game::FindCanHitEffect(Game::CanType::Acid)->Apply(registry, entity, {}, effect);
    assert(status.acidTimer == 2 && status.acidTickTimer == 0);
    Game::FindCanHitEffect(Game::CanType::Bubble)->Apply(registry, entity, {}, effect);
    assert(status.bubbleTimer == effect.duration * .55f && status.bubbleBaseY == 7);
    transform.translate.y = 12;
    Game::FindCanHitEffect(Game::CanType::Bubble)->Apply(registry, entity, {}, effect);
    assert(status.bubbleBaseY == 7 && status.bubblePositionSaved);
    registry.remove<Game::BossActionComponent>(entity);
    effect.duration = 10;
    Game::FindCanHitEffect(Game::CanType::Ice)->Apply(registry, entity, {}, effect);
    Game::FindCanHitEffect(Game::CanType::Bubble)->Apply(registry, entity, {}, effect);
    assert(status.freezeTimer == 10 && status.bubbleTimer == 10);
    assert(Game::FindCanHitEffect(Game::CanType::None) == nullptr);
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
    TestCanHitEffects();
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

    // Compare the combo/time-scale implementation with the original equations.
    Game::CombatFlowState flow;
    int combo = 0; float remaining = 0, scale = 1;
    for (int frame = 0; frame < 600; ++frame) {
        if (frame < 80 && frame % 10 == 0) {
            bool defeated = frame % 20 == 0;
            flow.RegisterHit(defeated);
            combo = (std::min)(12, combo + 1 + (defeated ? 1 : 0)); remaining = 1.8f;
        }
        constexpr float dt = 1.f / 60.f;
        if (remaining > 0) { remaining = (std::max)(0.f, remaining - dt); if (remaining == 0) combo = 0; }
        float target = combo >= 4 ? .45f : combo >= 3 ? .58f : combo >= 2 ? .75f : 1.f;
        scale += (target - scale) * (std::min)(1.f, dt * 9.f);
        flow.Tick(dt);
        assert(flow.Combo() == combo && flow.EnemyScale() == scale);
    }
    flow.SetChronoEnemyScale(0);
    assert(flow.EnemyScale() == 0);
    flow.Reset(); assert(flow.Combo() == 0 && flow.EnemyScale() == 1);

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
