// IScript.h
// C++ スクリプトの基底クラス。エンティティの ScriptComponent に付けると、
// PLAY 中に Start / Update / OnDestroy が呼ばれる (Unity の MonoBehaviour に相当)。
#pragma once
#include <string>
#include <functional>

#include "../../externals/entt/entt.hpp"

namespace Game {

// 前方宣言 (SceneObject は旧形式の名残)
struct SceneObject;
class GameScene;

// C++ スクリプトの基底クラス (必要なものだけオーバーライドする)
class IScript {
public:
	virtual ~IScript() = default;

	// スクリプトの初期化（アタッチ時やシーン開始時に1回呼ばれる）
	virtual void Start(entt::entity /*entity*/, GameScene* /*scene*/) {}
	
	// 毎フレーム呼ばれる更新処理
	virtual void Update(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {}
	
	// UIクリック時に呼ばれる
	virtual void OnClick(entt::entity /*entity*/, GameScene* /*scene*/, const std::string& /*callbackName*/) {}
	
	// オブジェクト破棄時やスクリプトが外れた時に呼ばれる
	virtual void OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

	// エディターUI描画用
	virtual void OnEditorUI() {}

	// パラメーターの個別保存・読み込み用 (エディター用)
	virtual std::string SerializeParameters() { return ""; }
	virtual void DeserializeParameters(const std::string& /*data*/) {}

	// 便利機能 (よく使う処理のショートカット)

	// イベントの発行 (EventSystem と連携)
	void Emit(GameScene* scene, const std::string& eventName, float value = 0.0f);
	void EmitString(GameScene* scene, const std::string& eventName, const std::string& value = "");
	void EmitVoid(GameScene* scene, const std::string& eventName);

	// イベントの購読 (EventSystem と連携)
	// 登録したコールバックは、基本的にシーン切り替え時にまとめて解除される
	void Subscribe(GameScene* scene, const std::string& eventName, std::function<void(float)> callback);
	void SubscribeString(GameScene* scene, const std::string& eventName, std::function<void(const std::string&)> callback);
	void SubscribeVoid(GameScene* scene, const std::string& eventName, std::function<void()> callback);

	// 共有変数の取得・設定 (VariableComponent と連携)
	void SetVar(entt::entity entity, GameScene* scene, const std::string& key, float value);
	void SetVarString(entt::entity entity, GameScene* scene, const std::string& key, const std::string& value);
	float GetVar(entt::entity entity, GameScene* scene, const std::string& key, float defaultVal = 0.0f);
	std::string GetVarString(entt::entity entity, GameScene* scene, const std::string& key, const std::string& defaultVal = "");
};

} // namespace Game
