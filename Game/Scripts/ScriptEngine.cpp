#include "ScriptEngine.h"
#include "Scenes/GameScene.h"
#include <iostream>
#include <Windows.h> // OutputDebugStringA
#include "PhaseSystemScript.h"
#include "PreparationCamera.h"
#include "BaseScript.h"
#include "PlayerScript.h"
#include "EnemyAIScript.h"
#include "EnemyBehavior.h"
#include "EnemySpawnerScript.h"
#include "Canon.h"
// ★追加: リリースビルドでリンク漏れ防止のため全スクリプトをinclude
#include "BulletScript.h"
#include "BulletTank.h"
#include "TowerScript.h"
#include "PipeScript.h"
#include "KamikazeEnemyScript.h"
#include "ExperienceOrbScript.h"
#include "ExperienceMiner.h"
#include "ExperienceHopper.h"
#include "CommunicationTestScript.h"
#include "HitDistortionScript.h" // ★追加
#include "InstallationButton.h"
#include "PhaseTransition.h"
#include "WaveManagement.h"

namespace Game {

ScriptEngine* ScriptEngine::instance_ = nullptr;

ScriptEngine* ScriptEngine::GetInstance() {
	if (!instance_) {
		instance_ = new ScriptEngine();
	}
	return instance_;
}

void ScriptEngine::Initialize() {
	// ★変更: マクロによる自動登録に加え、リリースビルドでのリンク漏れ防止のため明示的に登録
	RegisterScript("PhaseSystemScript", []() { return std::make_shared<PhaseSystemScript>(); });
	RegisterScript("PreparationCamera", []() { return std::make_shared<PreparationCamera>(); });
	RegisterScript("BaseScript", []() { return std::make_shared<BaseScript>(); });
	RegisterScript("PlayerScript", []() { return std::make_shared<PlayerScript>(); });
	RegisterScript("EnemyAIScript", []() { return std::make_shared<EnemyAIScript>(); });
	RegisterScript("EnemyBehavior", []() { return std::make_shared<EnemyBehavior>(); });
	RegisterScript("EnemySpawnerScript", []() { return std::make_shared<EnemySpawnerScript>(); });
	RegisterScript("Canon", []() { return std::make_shared<Canon>(); });
	// ★追加: 不足していたスクリプトの明示登録
	RegisterScript("BulletScript", []() { return std::make_shared<BulletScript>(); });
	RegisterScript("BulletTank", []() { return std::make_shared<BulletTank>(); });
	RegisterScript("TowerScript", []() { return std::make_shared<TowerScript>(); });
	RegisterScript("PipeScript", []() { return std::make_shared<PipeScript>(); });
	RegisterScript("KamikazeEnemyScript", []() { return std::make_shared<KamikazeEnemyScript>(); });
	RegisterScript("ExperienceOrbScript", []() { return std::make_shared<ExperienceOrbScript>(); });
	RegisterScript("ExperienceMiner", []() { return std::make_shared<ExperienceMiner>(); });
	RegisterScript("ExperienceHopper", []() { return std::make_shared<ExperienceHopper>(); });
	RegisterScript("CommunicationTestScript", []() { return std::make_shared<CommunicationTestScript>(); });
	RegisterScript("HitDistortionScript", []() { return std::make_shared<HitDistortionScript>(); }); // ★追加
	RegisterScript("InstallationButton", []() { return std::make_shared<InstallationButton>(); });
	RegisterScript("PhaseTransition", []() { return std::make_shared<PhaseTransition>(); });
	RegisterScript("WaveManagement", []() { return std::make_shared<WaveManagement>(); });
}

void ScriptEngine::Shutdown() {
	scriptFactory_.clear();
	if (instance_) {
		delete instance_;
		instance_ = nullptr;
	}
}

void ScriptEngine::RegisterScript(const std::string& className, ScriptCreator creator) { scriptFactory_[className] = creator; }

std::shared_ptr<IScript> ScriptEngine::CreateScript(const std::string& className) {
	auto it = scriptFactory_.find(className);
	if (it != scriptFactory_.end()) {
		return it->second();
	}
	// ★ エラーログ強化: クラスが見つからない場合
	std::string msg = "[ScriptEngine] CRITICAL ERROR: Script class '" + className + "' is NOT registered!\n";
	msg += "  -> Did you write REGISTER_SCRIPT(" + className + "); in your .cpp file?\n";
	msg += "  -> Is the .cpp file included in your Visual Studio project?\n";
	OutputDebugStringA(msg.c_str());
	return nullptr;
}

void ScriptEngine::Execute(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) return;
	auto& registry = scene->GetRegistry();
	if (!registry.valid(entity) || !registry.all_of<ScriptComponent>(entity)) return;

	auto& comp = registry.get<ScriptComponent>(entity);
	if (!comp.enabled) return;

	for (auto& entry : comp.scripts) {
		if (entry.scriptPath.empty()) continue;

		if (entry.instance) {
			// すでにインスタンスがある場合は、そのパラメータを尊重する
			// 復元時（RestoreSceneFromJson）に作成・デシリアライズ済みのはず
			// 必要に応じてここで追加の同期を行えるが、基本はデシリアライズ済み
		} else {
			entry.instance = CreateScript(entry.scriptPath);
			if (entry.instance) {
				if (!entry.parameterData.empty()) {
					entry.instance->DeserializeParameters(entry.parameterData);
				}
			} else {
				continue;
			}
		}

		if (entry.instance && !entry.isStarted) {
			entry.instance->Start(entity, scene);
			entry.isStarted = true;
		}

		if (entry.instance) {
			entry.instance->Update(entity, scene, dt);
		}
	}
}

} // namespace Game