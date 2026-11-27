// BaseScript.cpp
// 新しいスクリプトのひな形。既定の処理と保存パラメータは空。
#include "BaseScript.h"
#include "ScriptEngine.h"

namespace Game {

void BaseScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}
void BaseScript::Update(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {}
void BaseScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}
void BaseScript::OnEditorUI() {}
std::string BaseScript::SerializeParameters() {
	return {};
}
void BaseScript::DeserializeParameters(const std::string& /*data*/) {}

REGISTER_SCRIPT(BaseScript);

} // namespace Game
