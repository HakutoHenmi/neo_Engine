// BaseScript.h
// 新しいスクリプトを作るときのひな形。
#pragma once
#include "IScript.h"

namespace Game {

// 編集して使う、処理と保存パラメータが空のひな形。
class BaseScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;
};

} // namespace Game