#pragma once
#include "../externals/entt/entt.hpp"
#include <utility>

namespace Game {

// Component editing capability. Entity lifetime, registry clearing and signals
// remain under GameScene's control; there is no mutable registry conversion.
class SceneComponents {
public:
    explicit SceneComponents(entt::registry& registry) : registry_(registry) {}
    SceneComponents(const SceneComponents&) = delete;
    SceneComponents& operator=(const SceneComponents&) = delete;

    bool valid(entt::entity entity) const { return registry_.valid(entity); }
    template<class... T> bool all_of(entt::entity entity) const { return registry_.all_of<T...>(entity); }
    template<class... T> bool any_of(entt::entity entity) const { return registry_.any_of<T...>(entity); }
    template<class... T> decltype(auto) get(entt::entity entity) { return registry_.get<T...>(entity); }
    template<class... T> auto try_get(entt::entity entity) { return registry_.try_get<T...>(entity); }
    template<class... T> auto view() { return registry_.view<T...>(); }
    template<class T, class... Args> decltype(auto) emplace(entt::entity entity, Args&&... args) {
        return registry_.emplace<T>(entity, std::forward<Args>(args)...);
    }
    template<class T, class... Args> decltype(auto) emplace_or_replace(entt::entity entity, Args&&... args) {
        return registry_.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
    }
    template<class T, class... Args> decltype(auto) get_or_emplace(entt::entity entity, Args&&... args) {
        return registry_.get_or_emplace<T>(entity, std::forward<Args>(args)...);
    }
    template<class... T> auto remove(entt::entity entity) { return registry_.remove<T...>(entity); }
private:
    entt::registry& registry_;
};

} // namespace Game
