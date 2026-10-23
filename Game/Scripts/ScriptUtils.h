// ScriptUtils.h
// スクリプトを探すための便利関数。
#pragma once
// ECS (entt) と、コンポーネント・シーンの定義
#include "../../externals/entt/entt.hpp"
#include "../ObjectTypes.h"
#include "Scenes/GameScene.h"
#include <memory>
#include <vector>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

/**
 * @brief スクリプト関連の便利関数を提供するユーティリティクラス
 */
class ScriptUtils {
public:
    /**
     * @brief 指定したエンティティから特定の種類のスクリプトを取得します。
     * @tparam T 取得したいスクリプトのクラス型
     * @param entity 対象のエンティティ
     * @param scene 現在のシーン
     * @return std::shared_ptr<T> スクリプトのポインタ。見つからない場合は nullptr。
     */
    template <typename T>
    static std::shared_ptr<T> GetScript(entt::entity entity, GameScene* scene) {
        // シーンが無ければ見つからない扱い
        if (!scene) return nullptr;
        
        // このシーンの ECS
        auto& registry = scene->GetComponents();
        // スクリプトのコンポーネントを持っていれば、付いているスクリプトを順に調べる
        if (registry.all_of<ScriptComponent>(entity)) {
            auto& sc = registry.get<ScriptComponent>(entity);
            for (auto& entry : sc.scripts) {
                // 目的の型に変換できたものを返す
                if (auto script = std::dynamic_pointer_cast<T>(entry.instance)) {
                    return script;
                }
            }
        }
        // 見つからなければ nullptr
        return nullptr;
    }

    /**
     * @brief シーン内から最初に見つかった特定の種類のスクリプトを取得します。
     * @tparam T 取得したいスクリプトのクラス型
     * @param scene 現在のシーン
     * @return std::shared_ptr<T> スクリプトのポインタ。見つからない場合は nullptr。
     */
    template <typename T>
    static std::shared_ptr<T> FindScript(GameScene* scene) {
        // シーンが無ければ見つからない扱い
        if (!scene) return nullptr;

        auto& registry = scene->GetComponents();
        // スクリプトのコンポーネントを持つエンティティを順に調べる
        auto view = registry.view<ScriptComponent>();
        for (auto entity : view) {
            // 最初に見つかったものを返す
            if (auto script = GetScript<T>(entity, scene)) {
                return script;
            }
        }
        // どこにも無ければ nullptr
        return nullptr;
    }

    /**
     * @brief シーン内から特定の種類のスクリプトをすべて取得します。
     * @tparam T 取得したいスクリプトのクラス型
     * @param scene 現在のシーン
     * @return std::vector<std::shared_ptr<T>> スクリプトのポインタ配列
     */
    template <typename T>
    static std::vector<std::shared_ptr<T>> FindScripts(GameScene* scene) {
        // 見つかったスクリプトをためる配列
        std::vector<std::shared_ptr<T>> results;
        // シーンが無ければ空のまま返す
        if (!scene) return results;

        auto& registry = scene->GetComponents();
        // スクリプトのコンポーネントを持つエンティティを順に調べる
        auto view = registry.view<ScriptComponent>();
        for (auto entity : view) {
            auto& sc = registry.get<ScriptComponent>(entity);
            // 付いているスクリプトのうち、目的の型に変換できたものをすべて集める
            for (auto& entry : sc.scripts) {
                if (auto script = std::dynamic_pointer_cast<T>(entry.instance)) {
                    results.push_back(script);
                }
            }
        }
        return results;
    }
};

} // namespace Game
