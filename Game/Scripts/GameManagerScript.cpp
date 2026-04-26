#include "GameManagerScript.h"
#include "ScriptEngine.h"
#include <nlohmann/json.hpp>
#include <iostream>

namespace Game {

GameManagerScript* GameManagerScript::instance_ = nullptr;

void GameManagerScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
    instance_ = this;
}

void GameManagerScript::Update(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {
    // 毎フレームの処理は不要
}

void GameManagerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

GameManagerScript::~GameManagerScript() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}


std::string GameManagerScript::SerializeParameters() {
    nlohmann::json j;
    j["defeatText"] = defeatText;
    j["clearText"] = clearText;
    j["retryText"] = retryText;
    j["defeatColor"] = {defeatColor[0], defeatColor[1], defeatColor[2], defeatColor[3]};
    j["clearColor"] = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
    j["textScale"] = textScale;
    j["clearTextScale"] = clearTextScale;
    return j.dump();
}

void GameManagerScript::DeserializeParameters(const std::string& data) {
    if (data.empty()) return;
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        if (j.contains("defeatText")) defeatText = j["defeatText"].get<std::string>();
        if (j.contains("clearText")) clearText = j["clearText"].get<std::string>();
        if (j.contains("retryText")) retryText = j["retryText"].get<std::string>();
        
        if (j.contains("defeatColor") && j["defeatColor"].is_array() && j["defeatColor"].size() == 4) {
            auto color = j["defeatColor"];
            for(int i = 0; i < 4; ++i) defeatColor[i] = color[i].get<float>();
        }
        
        if (j.contains("clearColor") && j["clearColor"].is_array() && j["clearColor"].size() == 4) {
            auto color = j["clearColor"];
            for(int i = 0; i < 4; ++i) clearColor[i] = color[i].get<float>();
        }

        if (j.contains("textScale")) textScale = j["textScale"].get<float>();
        if (j.contains("clearTextScale")) clearTextScale = j["clearTextScale"].get<float>();
    } catch (...) {
        std::cerr << "[GameManagerScript] JSON Parse Error in DeserializeParameters\n";
    }
}

REGISTER_SCRIPT(GameManagerScript);

} // namespace Game
