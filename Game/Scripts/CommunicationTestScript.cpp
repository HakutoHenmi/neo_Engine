#include "CommunicationTestScript.h"
#include "ScriptUtils.h"
#include "PlayerScript.h"
#include "ScriptEngine.h"
#include <iostream>

namespace Game {

void CommunicationTestScript::Start(entt::entity entity, GameScene* scene) {
    // 1. EventSystemの購読テスト
    SubscribeVoid(scene, "TestEvent", [entity]() {
        OutputDebugStringA("[CommTest] TestEvent received!\n");
    });

    // 2. VariableComponentの設定テスト
    SetVar(entity, scene, "InitialValue", 100.0f);
    SetVarString(entity, scene, "Status", "Started");
}

void CommunicationTestScript::Update(entt::entity entity, GameScene* scene, float dt) {
    timer_ += dt;
    if (timer_ >= 5.0f) { // 5秒おきにテスト実行
        timer_ = 0.0f;
        testCount_++;

        // 3. ScriptUtilsのテスト (PlayerScriptを探す)
        auto player = ScriptUtils::FindScript<PlayerScript>(scene);
        if (player) {
            OutputDebugStringA("[CommTest] Found PlayerScript via ScriptUtils!\n");
        }

        // 4. VariableComponentの読み取りテスト
        float val = GetVar(entity, scene, "InitialValue");
        std::string status = GetVarString(entity, scene, "Status");
        
        char buf[256];
        sprintf_s(buf, "[CommTest] Run %d: Var=%.1f, Status=%s\n", testCount_, val, status.c_str());
        OutputDebugStringA(buf);

        // 5. EventSystemの発行テスト
        EmitVoid(scene, "TestEvent");
        
        // 値を更新してみる
        SetVar(entity, scene, "InitialValue", val + 1.0f);
    }
}

REGISTER_SCRIPT(CommunicationTestScript);

} // namespace Game
