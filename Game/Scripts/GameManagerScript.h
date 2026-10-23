// GameManagerScript.h
// 旧アクションゲームの結果表示 (敗北・クリア・リトライ) の文言と色を持つスクリプト。
// シーンに1つだけ置き、GetInstance で他のシステムから参照する。
#pragma once
#include "IScript.h"
#include <string>

namespace Game {

class GameManagerScript : public IScript {
public:
    // 表示する文言・色・文字の大きさ (Inspector で編集し、シーンに保存する)
    std::string defeatText = "YOU DIED";
    std::string clearText = "STAGE CLEAR!";
    std::string retryText = "Press [ R ] to Retry";
    float defeatColor[4] = {1.0f, 0.1f, 0.1f, 1.0f};
    float clearColor[4] = {1.0f, 0.8f, 0.2f, 1.0f};
    float textScale = 6.0f;
    float clearTextScale = 3.0f;

    // シーンに置かれているインスタンス (無ければ nullptr)
    static GameManagerScript* GetInstance() { return instance_; }

    ~GameManagerScript();

    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void OnDestroy(entt::entity entity, GameScene* scene) override;

    std::string SerializeParameters() override;
    void DeserializeParameters(const std::string& data) override;

private:
    static GameManagerScript* instance_;
};

} // namespace Game
