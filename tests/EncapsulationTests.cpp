#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Engine/System.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>
#include <iostream>

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
