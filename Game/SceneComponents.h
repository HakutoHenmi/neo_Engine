// SceneComponents.h
// シーン内のコンポーネントを読み書きするための窓口クラス。
#pragma once
#include "../externals/entt/entt.hpp"
#include <utility>

namespace Game {

// コンポーネントの編集だけを許可する。エンティティの生成・破棄やレジストリの全消去、シグナルは
// GameScene が管理し、ここから書き換え可能なレジストリを取り出す手段は用意しない
class SceneComponents {
public:
    // GameScene が持つレジストリを参照で受け取る (コピー不可)
    explicit SceneComponents(entt::registry& registry) : registry_(registry) {}
    SceneComponents(const SceneComponents&) = delete;
    SceneComponents& operator=(const SceneComponents&) = delete;

    // エンティティが有効か / 指定コンポーネントを全部・どれか持っているか
    bool valid(entt::entity entity) const { return registry_.valid(entity); }
    template<class... T> bool all_of(entt::entity entity) const { return registry_.all_of<T...>(entity); }
    template<class... T> bool any_of(entt::entity entity) const { return registry_.any_of<T...>(entity); }
    // コンポーネントの取得 (get は必須、try_get は無ければ nullptr)
    template<class... T> decltype(auto) get(entt::entity entity) { return registry_.get<T...>(entity); }
    template<class... T> auto try_get(entt::entity entity) { return registry_.try_get<T...>(entity); }
    // 指定コンポーネントを持つエンティティの一覧
    template<class... T> auto view() { return registry_.view<T...>(); }
    // コンポーネントの追加 (emplace は新規、emplace_or_replace は上書き、get_or_emplace は無ければ追加)
    template<class T, class... Args> decltype(auto) emplace(entt::entity entity, Args&&... args) {
        return registry_.emplace<T>(entity, std::forward<Args>(args)...);
    }
    template<class T, class... Args> decltype(auto) emplace_or_replace(entt::entity entity, Args&&... args) {
        return registry_.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
    }
    template<class T, class... Args> decltype(auto) get_or_emplace(entt::entity entity, Args&&... args) {
        return registry_.get_or_emplace<T>(entity, std::forward<Args>(args)...);
    }
    // コンポーネントの削除
    template<class... T> auto remove(entt::entity entity) { return registry_.remove<T...>(entity); }
private:
    // 実体は GameScene のレジストリ
    entt::registry& registry_;
};

} // namespace Game
