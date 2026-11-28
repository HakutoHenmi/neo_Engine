// IScript.cpp
// IScript の便利関数の実装 (イベントの発行・購読、共有変数の読み書き)。
#include "IScript.h"
// シーン (イベントシステムと ECS を持つ) と、共有変数のコンポーネントの定義
#include "Scenes/GameScene.h"
#include "Core/EventSystem.h"
#include "ObjectTypes.h"

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// イベントの発行 (数値付き): シーンのイベントシステムへそのまま渡す
void IScript::Emit(GameScene* scene, const std::string& eventName, float value) {
	// シーンが無い (破棄済み・未設定) ときは何もしない
	if (scene)
		scene->GetEventSystem().Emit(eventName, value);
}

// イベントの発行 (文字列付き)
void IScript::EmitString(GameScene* scene, const std::string& eventName, const std::string& value) {
	if (scene)
		scene->GetEventSystem().EmitString(eventName, value);
}

// イベントの発行 (値なし)
void IScript::EmitVoid(GameScene* scene, const std::string& eventName) {
	if (scene)
		scene->GetEventSystem().EmitVoid(eventName);
}

// イベントの購読 (数値付き): イベントが来たら callback が呼ばれる
void IScript::Subscribe(GameScene* scene, const std::string& eventName, std::function<void(float)> callback) {
	if (scene)
		scene->GetEventSystem().Subscribe(eventName, callback);
}

// イベントの購読 (文字列付き)
void IScript::SubscribeString(
    GameScene* scene, const std::string& eventName, std::function<void(const std::string&)> callback) {
	if (scene)
		scene->GetEventSystem().SubscribeString(eventName, callback);
}

// イベントの購読 (値なし)
void IScript::SubscribeVoid(GameScene* scene, const std::string& eventName, std::function<void()> callback) {
	if (scene)
		scene->GetEventSystem().SubscribeVoid(eventName, callback);
}

} // namespace Game
