// BaseScript.cpp
// ひな形スクリプトの実装 (中身は空)。
#include "BaseScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

#include <sstream>

namespace Game {

void BaseScript::Start(entt::entity entity, GameScene* scene) {
	// 初期化処理をここに記述
	(void)entity;
	(void)scene;
}

void BaseScript::Update(entt::entity entity, GameScene* scene, float dt) {
	// 毎フレームの更新処理をここに記述
	(void)entity;
	(void)scene;
	(void)dt;
}

// 破棄時の処理 (ひな形では何もしない)
void BaseScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void BaseScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	// エディタ上のパラメータ表示
	// 例: ImGui::DragFloat("Speed", &speed_, 0.1f);
#endif
}

// 保存するパラメータを文字列にする / 文字列から読み込む
std::string BaseScript::SerializeParameters() {
	return "";
}

void BaseScript::DeserializeParameters(const std::string& data) {
	(void)data;
}

// ScriptEngine へ自動登録する
REGISTER_SCRIPT(BaseScript);

} // namespace Game