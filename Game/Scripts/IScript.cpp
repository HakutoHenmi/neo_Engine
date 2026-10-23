// IScript.cpp
// IScript の便利関数の実装 (イベントの発行・購読、共有変数の読み書き)。
#include "IScript.h"
// シーン (イベントシステムと ECS を持つ) と、共有変数のコンポーネントの定義
#include "Scenes/GameScene.h"
#include "../Engine/EventSystem.h"
#include "ObjectTypes.h"

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// イベントの発行 (数値付き): シーンのイベントシステムへそのまま渡す
void IScript::Emit(GameScene* scene, const std::string& eventName, float value) {
    // シーンが無い (破棄済み・未設定) ときは何もしない
    if (scene) scene->GetEventSystem().Emit(eventName, value);
}

// イベントの発行 (文字列付き)
void IScript::EmitString(GameScene* scene, const std::string& eventName, const std::string& value) {
    if (scene) scene->GetEventSystem().EmitString(eventName, value);
}

// イベントの発行 (値なし)
void IScript::EmitVoid(GameScene* scene, const std::string& eventName) {
    if (scene) scene->GetEventSystem().EmitVoid(eventName);
}

// イベントの購読 (数値付き): イベントが来たら callback が呼ばれる
void IScript::Subscribe(GameScene* scene, const std::string& eventName, std::function<void(float)> callback) {
    if (scene) scene->GetEventSystem().Subscribe(eventName, callback);
}

// イベントの購読 (文字列付き)
void IScript::SubscribeString(GameScene* scene, const std::string& eventName, std::function<void(const std::string&)> callback) {
    if (scene) scene->GetEventSystem().SubscribeString(eventName, callback);
}

// イベントの購読 (値なし)
void IScript::SubscribeVoid(GameScene* scene, const std::string& eventName, std::function<void()> callback) {
    if (scene) scene->GetEventSystem().SubscribeVoid(eventName, callback);
}

// 共有変数の設定 (数値): エンティティごとの VariableComponent に書き込む
void IScript::SetVar(entt::entity entity, GameScene* scene, const std::string& key, float value) {
    // シーンが無ければ何もしない
    if (!scene) return;
    // このシーンの ECS (エンティティとコンポーネントの登録簿)
    auto& registry = scene->GetComponents();
    // VariableComponent がまだ無ければ付ける
    if (!registry.all_of<VariableComponent>(entity)) {
        registry.emplace<VariableComponent>(entity);
    }
    // キーに数値を書き込む
    registry.get<VariableComponent>(entity).SetValue(key, value);
}

// 共有変数の設定 (文字列): 数値版と同じ手順で文字列を書き込む
void IScript::SetVarString(entt::entity entity, GameScene* scene, const std::string& key, const std::string& value) {
    if (!scene) return;
    auto& registry = scene->GetComponents();
    // VariableComponent がまだ無ければ付ける
    if (!registry.all_of<VariableComponent>(entity)) {
        registry.emplace<VariableComponent>(entity);
    }
    // キーに文字列を書き込む
    registry.get<VariableComponent>(entity).SetString(key, value);
}

// 共有変数の取得 (数値): 無ければ既定値を返す
float IScript::GetVar(entt::entity entity, GameScene* scene, const std::string& key, float defaultVal) {
    // シーンが無ければ既定値
    if (!scene) return defaultVal;
    auto& registry = scene->GetComponents();
    // VariableComponent があれば、キーの値 (無ければ既定値) を返す
    if (registry.all_of<VariableComponent>(entity)) {
        return registry.get<VariableComponent>(entity).GetValue(key, defaultVal);
    }
    // VariableComponent が無ければ既定値
    return defaultVal;
}

// 共有変数の取得 (文字列): 無ければ既定値を返す
std::string IScript::GetVarString(entt::entity entity, GameScene* scene, const std::string& key, const std::string& defaultVal) {
    // シーンが無ければ既定値
    if (!scene) return defaultVal;
    auto& registry = scene->GetComponents();
    // VariableComponent があれば、キーの文字列 (無ければ既定値) を返す
    if (registry.all_of<VariableComponent>(entity)) {
        return registry.get<VariableComponent>(entity).GetString(key, defaultVal);
    }
    // VariableComponent が無ければ既定値
    return defaultVal;
}

} // namespace Game
