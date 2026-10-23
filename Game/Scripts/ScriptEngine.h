// ScriptEngine.h
// C++ スクリプトの登録と生成を行うシングルトン。
// クラス名 (文字列) からスクリプトの実体を作れるようにし、シーンファイルに名前で保存できるようにする。
#pragma once
#include "../Engine/Input.h"
#include "IScript.h"
#include "ObjectTypes.h"
#include "../../externals/entt/entt.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Game {

class GameScene; // 前方宣言

// スクリプトの登録・生成・実行
class ScriptEngine {
public:
	// 唯一のインスタンスを取得する
	static ScriptEngine* GetInstance();

	// 初期化 (スクリプトの登録) / 終了
	void Initialize();
	void Shutdown();

	// エンティティのスクリプトを実行する (未生成なら作り、初回は Start、毎フレーム Update)
	void Execute(entt::entity entity, GameScene* scene, float dt);

	// クラス名とスクリプトを作る関数を登録する / クラス名から作る / 登録済みの名前の一覧
	using ScriptCreator = std::function<std::shared_ptr<IScript>()>;
	void RegisterScript(const std::string& className, ScriptCreator creator);
	std::shared_ptr<IScript> CreateScript(const std::string& className);
	std::vector<std::string> GetRegisteredScriptNames() const {
		std::vector<std::string> names;
		for (auto const& [name, _] : scriptFactory_) names.push_back(name);
		return names;
	}

private:
	// 外から作れないようにする (シングルトン)
	ScriptEngine() {}
	~ScriptEngine() {}

	static ScriptEngine* instance_;
	// クラス名 → 作成関数
	std::map<std::string, ScriptCreator> scriptFactory_;
};

} // namespace Game

// ==============================================================
// C++ スクリプトを自動登録するマクロ
// スクリプトの .cpp の最後に REGISTER_SCRIPT(クラス名); と書くだけで登録される
// ==============================================================
#define REGISTER_SCRIPT(ClassName)                                                                                                                                                                     \
	namespace {                                                                                                                                                                                        \
	struct ClassName##_Register {                                                                                                                                                                      \
		ClassName##_Register() {                                                                                                                                                                       \
			Game::ScriptEngine::GetInstance()->RegisterScript(#ClassName, []() { return std::make_shared<ClassName>(); });                                                                             \
		}                                                                                                                                                                                              \
	} ClassName##_register_instance;                                                                                                                                                                   \
	}