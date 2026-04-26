#pragma once
#include "IScript.h"
#include <string>

namespace Game {

class GameManagerScript : public IScript {
public:
    std::string defeatText = "YOU DIED";
    std::string clearText = "STAGE CLEAR!";
    std::string retryText = "Press [ R ] to Retry";
    float defeatColor[4] = {1.0f, 0.1f, 0.1f, 1.0f};
    float clearColor[4] = {1.0f, 0.8f, 0.2f, 1.0f};
    float textScale = 6.0f;
    float clearTextScale = 3.0f;

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
