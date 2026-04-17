#include "InstallationButton.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "../../Engine/Input.h"
#include "PhaseSystemScript.h"
#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#endif
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;



namespace Game {



bool InstallationButton::isButtonPressed_[ButtonTypesNum] = {};

InstallationButton::InstallationButton() {
	OutputDebugStringA("[InstallationButton] Constructor called\n");
}

void InstallationButton::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	OutputDebugStringA("[InstallationButton] Start called\n");
}

void InstallationButton::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;

	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

	if (buttonTypes_ >= 0 && buttonTypes_ < ButtonTypesNum) {
		isButtonPressed_[buttonTypes_] = scene->GetRegistry().all_of<UIButtonComponent>(entity) && scene->GetRegistry().get<UIButtonComponent>(entity).isPressed;
	}

	if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
		if (scene->GetRegistry().all_of<UIImageComponent>(entity))
			scene->GetRegistry().get<UIImageComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<UIButtonComponent>(entity))
			scene->GetRegistry().get<UIButtonComponent>(entity).enabled = true;
		// cameraTargets はECS化が必要だが、一旦省略
	} else {
		if (scene->GetRegistry().all_of<UIImageComponent>(entity))
			scene->GetRegistry().get<UIImageComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<UIButtonComponent>(entity))
			scene->GetRegistry().get<UIButtonComponent>(entity).enabled = false;
	}
}

void InstallationButton::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void InstallationButton::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Installation Button");

	const char* buttonTypeNames[] = {"Cannon", "Pipe", "Tank"};
	int currentType = static_cast<int>(buttonTypes_);
	if (ImGui::Combo("Button Type", &currentType, buttonTypeNames, IM_ARRAYSIZE(buttonTypeNames))) {
		buttonTypes_ = static_cast<ButtonTypes>(currentType);
	}

	ImGui::Text("isButtonPressed_: %s", isButtonPressed_[buttonTypes_] ? "true" : "false");
#endif
}

std::string InstallationButton::SerializeParameters() {
	json j;
	j["buttonTypes"] = static_cast<int>(buttonTypes_);
	// compact型で出力
	std::string data = j.dump();

	// ログ追加
	char logBuf[1024];
	sprintf_s(logBuf, "[InstallationButton] SerializeParameters (Type=%d): %s\n", (int)buttonTypes_, data.c_str());
	OutputDebugStringA(logBuf);

	return data;
}

void InstallationButton::DeserializeParameters(const std::string& data) {
	char logBuf[1024];
	sprintf_s(logBuf, "[InstallationButton] DeserializeParameters: %s\n", data.c_str());
	OutputDebugStringA(logBuf);

	if (data.empty() || data == "{}") {
		// 空データの場合は現在の値を維持する
		OutputDebugStringA("[InstallationButton] No data or empty JSON, keeping current values.\n");
		return;
	}
	try {
		json j = json::parse(data);
		if (j.contains("buttonTypes") && j["buttonTypes"].is_number()) {
			int typeVal = j["buttonTypes"].get<int>();
			if (typeVal >= 0 && typeVal < ButtonTypesNum) {
				buttonTypes_ = static_cast<ButtonTypes>(typeVal);
				sprintf_s(logBuf, "[InstallationButton] Successfully restored buttonTypes to: %d\n", (int)buttonTypes_);
				OutputDebugStringA(logBuf);
			} else {
				sprintf_s(logBuf, "[InstallationButton] Invalid buttonTypes value: %d\n", typeVal);
				OutputDebugStringA(logBuf);
			}
		} else {
			OutputDebugStringA("[InstallationButton] JSON does not contain valid 'buttonTypes'\n");
		}
	} catch (const std::exception& e) {
		sprintf_s(logBuf, "[InstallationButton] Deserialize error: %s (data: %s)\n", e.what(), data.c_str());
		OutputDebugStringA(logBuf);
	}
}

bool InstallationButton::IsButtonPressed(ButtonTypes type) { return isButtonPressed_[type]; }

REGISTER_SCRIPT(InstallationButton);

} // namespace Game