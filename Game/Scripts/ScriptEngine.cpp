// ScriptEngine.cpp
// C++ スクリプトの登録・生成・実行の実装。
#include "ScriptEngine.h"
// シーンと、登録するスクリプトの定義
#include "Scenes/GameScene.h"
#include "BossTestScript.h"
#include <iostream>
#include <Windows.h> // OutputDebugStringA を使うため

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// 唯一のインスタンス (最初は無し)
ScriptEngine* ScriptEngine::instance_ = nullptr;

// 初回に作成する
ScriptEngine* ScriptEngine::GetInstance() {
	if (!instance_) {
		instance_ = new ScriptEngine();
	}
	return instance_;
}

// 起動時に呼ぶ: 手動で登録するスクリプトを登録する
void ScriptEngine::Initialize() {
	// 新しいスクリプトを作ったらここに登録する
	RegisterScript("BossTestScript", []() { return std::make_shared<BossTestScript>(); });
}

// 登録を消してインスタンスを破棄する
void ScriptEngine::Shutdown() {
	scriptFactory_.clear();
	if (instance_) {
		delete instance_;
		instance_ = nullptr;
	}
}

// クラス名と作成関数を登録する (同名は上書き)
void ScriptEngine::RegisterScript(const std::string& className, ScriptCreator creator) { scriptFactory_[className] = creator; }

// クラス名からスクリプトを作る。未登録なら原因の候補をデバッグ出力に出して nullptr を返す
std::shared_ptr<IScript> ScriptEngine::CreateScript(const std::string& className) {
	// 登録簿からクラス名を探す
	auto it = scriptFactory_.find(className);
	if (it != scriptFactory_.end()) {
		// 見つかれば作成関数を呼んで新しいスクリプトを返す
		return it->second();
	}
	// クラスが見つからないときは、よくある原因と合わせて分かりやすく出力する
	std::string msg = "[ScriptEngine] CRITICAL ERROR: Script class '" + className + "' is NOT registered!\n";
	msg += "  -> Did you write REGISTER_SCRIPT(" + className + "); in your .cpp file?\n";
	msg += "  -> Is the .cpp file included in your Visual Studio project?\n";
	OutputDebugStringA(msg.c_str());
	return nullptr;
}

// エンティティに付いたスクリプトを順に実行する
void ScriptEngine::Execute(entt::entity entity, GameScene* scene, float dt) {
	// シーンが無ければ何もしない
	if (!scene) return;
	auto& registry = scene->GetComponents();
	// エンティティが消えているか、スクリプトのコンポーネントが無ければ何もしない
	if (!registry.valid(entity) || !registry.all_of<ScriptComponent>(entity)) return;

	auto& comp = registry.get<ScriptComponent>(entity);
	// コンポーネントが無効なら実行しない
	if (!comp.enabled) return;

	// 付いているスクリプトを順に処理する
	for (auto& entry : comp.scripts) {
		// クラス名が空の欄は飛ばす
		if (entry.scriptPath.empty()) continue;

		if (entry.instance) {
			// 既にインスタンスがあれば、そのパラメータをそのまま使う
			// (シーン復元時 (RestoreSceneFromJson) に作成とパラメータの読み込みが済んでいる)
		} else {
			// まだ実体が無ければクラス名から作り、保存されたパラメータを読み込む
			entry.instance = CreateScript(entry.scriptPath);
			if (entry.instance) {
				if (!entry.parameterData.empty()) {
					entry.instance->DeserializeParameters(entry.parameterData);
				}
			} else {
				// 作れなかったら (未登録) この欄は飛ばす
				continue;
			}
		}

		// 初回だけ Start を呼ぶ
		if (entry.instance && !entry.isStarted) {
			entry.instance->Start(entity, scene);
			entry.isStarted = true;
		}

		// 毎フレームの更新
		if (entry.instance) {
			entry.instance->Update(entity, scene, dt);
		}
	}
}

} // namespace Game