#pragma once
#include "../Scenes/GameScene.h"
#include "../../Engine/Renderer.h"
#include "../../externals/imgui/imgui.h"
#include <memory>
#include <vector>

namespace Game {

class EditorUI; // Forward declaration

// パイプの中身の挙動や外見を定義するインターフェース
// 他の開発者はこれを継承して独自のパイプ（例：水が流れる、電力が伝わる）を実装可能
class IPipeBehavior {
public:
    virtual ~IPipeBehavior() = default;

    // パイプ配置時に、モデルや特定のコンポーネントをSceneObjectに設定するためのフック
    virtual void OnGeneratePipe(entt::registry& registry, entt::entity outPipe, const Engine::Vector3& start, const Engine::Vector3& end, float length, Engine::Renderer* renderer) = 0;
    
    // ジョイント配置時に、モデルや特定のコンポーネントをSceneObjectに設定するためのフック
    virtual void OnGenerateJoint(entt::registry& registry, entt::entity outJoint, const Engine::Vector3& position, Engine::Renderer* renderer) = 0;
    
    // ジョイントとパイプが接続された時など、配置確定時の処理
    virtual void OnPlacementComplete(GameScene* scene, const Engine::Vector3& start, const Engine::Vector3& end) = 0;
    
    // （オプション）カスタムスナップ規則を実装したい場合にオーバーライド
    virtual Engine::Vector3 ApplyCustomSnapping(GameScene* /*scene*/, const Engine::Vector3& rawPos) {
        return rawPos; // デフォルトでは何もしない
    }
};

// デフォルトのパイプ挙動（単なる見た目のみの配置）
class DefaultPipeBehavior : public IPipeBehavior {
public:
    void OnGeneratePipe(entt::registry& registry, entt::entity outPipe, const Engine::Vector3& start, const Engine::Vector3& end, float length, Engine::Renderer* renderer) override;
    void OnGenerateJoint(entt::registry& registry, entt::entity outJoint, const Engine::Vector3& position, Engine::Renderer* renderer) override;
    void OnPlacementComplete(GameScene* scene, const Engine::Vector3& start, const Engine::Vector3& end) override;
};

// パイプの「設置」操作全体を管理するエディタ専用クラス
class PipeEditor {
public:
    PipeEditor();

    // 毎フレームの更新（レイキャスト、プレビュー表示、クリックによる配置確定などを処理）
    void UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer, const ImVec2& gameImageMin, const ImVec2& gameImageMax, float tW, float tH);
    
    // UI側のメニューやプロパティの描画（スナップOn/Offの切り替えなど）
    void DrawUI();

    // 挙動（パイプの中身）を設定する
    void SetBehavior(std::shared_ptr<IPipeBehavior> behavior) { behavior_ = behavior; }

    bool IsPipeMode() const { return pipeMode_; }
    void SetPipeMode(bool mode) {
        pipeMode_ = mode;
        if (!pipeMode_) hasPipeStart_ = false;
    }

private:
    std::shared_ptr<IPipeBehavior> behavior_;

    bool pipeMode_ = false;
    bool hasPipeStart_ = false;
    Engine::Vector3 pipeStartNode_ = {0, 0, 0};

    entt::entity previewPipeId_ = entt::null;
    entt::entity previewJointId_ = entt::null;

    bool useAngleSnap_ = false;
    float snapAngleStep_ = 15.0f;
    bool useNodeSnap_ = false;
    float nodeSnapThreshold_ = 1.0f;

    // ヘルパー：プレビュー用オブジェクトの削除
    void ClearPreview(GameScene* scene);
};

} // namespace Game
