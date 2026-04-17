#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "EventSystem.h" // ★追加: イベントシステム
#include "../ObjectTypes.h"
#include "../Systems/ISystem.h"
#include <mutex>
#include <unordered_map>
#include <string>
#include "../../externals/entt/entt.hpp"
#include "../../Engine/ParticleEmitter.h"
#include "../../Engine/ParticleEditor.h"
#include "../EnemySystem/NavigationManager.h"

namespace Game {

class GameScene : public Engine::IScene {
public:
    ~GameScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override; // ★追加: ワールド空間UI用
    void DrawEditor() override;

    void DrawEditorGizmos();
    void DrawSelectionHighlight();
    void DrawLightGizmos();

	// ★ 汎用スポーン（Registryを直接操作することを推奨）
	entt::entity CreateEntity(const std::string& name = "New Object");
	// ★追加: オブジェクトをIDで破棄保留にする
	void DestroyObject(uint32_t id);

    entt::registry& GetRegistry() { return registry_; }
    const entt::registry& GetRegistry() const { return registry_; }
	Engine::EventSystem& GetEventSystem() { return eventSystem_; }
	Engine::ParticleEditor& GetParticleEditor() { return particleEditor_; }
	bool GetIsPlaying() const { return isPlaying_; }
	bool IsPlaying() const { return isPlaying_; } // Alias for backward compatibility
	void SetIsPlaying(bool play);
	Engine::Renderer* GetRenderer() const { return renderer_; }
	Engine::Matrix4x4 GetWorldMatrix(int index) const; 
	Engine::Matrix4x4 GetWorldMatrixRecursive(entt::entity entity, int depth) const;
	Engine::Camera& GetCamera() { return camera_; }
	entt::entity GetSelectedEntity() const { return selectedEntity_; }
	void SetSelectedEntity(entt::entity entity) { selectedEntity_ = entity; }
	std::set<entt::entity>& GetSelectedEntities() { return selectedEntities_; }

	// ★追加: コンテキストへのアクセス
	GameContext& GetContext() { return ctx_; }

	// ★追加: 名前でオブジェクトを検索するヘルパー
	entt::entity FindObjectByName(const std::string& name);
	// ★追加: 指定座標のメッシュ表面の高さを取得 (startY 付近から下を探索)
	float GetHeightAt(float x, float z, float startY = 1000.0f, uint32_t excludeId = 0);
	// ★追加: 汎用レイキャスト (壁判定などに使用)
	bool RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist);
	
	// ★追加: 高速タグ検索システム
	const std::vector<entt::entity>& GetEntitiesByTag(const std::string& tag);
	const std::vector<entt::entity>& GetEntitiesByTag(TagType tag); // ★追加
	void SetTag(entt::entity entity, const std::string& tag);
	void SetTag(entt::entity entity, TagType tag); // ★追加
	void SyncTag(entt::entity entity); // ★追加: 手動同期用

	// flowField_取得
	NavigationManager& GetNavigationManager() { return *flowField_; }

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;
    Engine::EventSystem eventSystem_; // ★追加: スクリプト間通信用
    entt::registry registry_;
    std::set<entt::entity> selectedEntities_;
    entt::entity selectedEntity_ = entt::null;

    bool isPlaying_ = false;
    entt::registry pendingSpawns_;
    std::vector<entt::entity> pendingDestroys_;
    std::mutex spawnMutex_; // ★追加: マルチスレッドから安全にスポーン・破棄登録を行えるようにする
	
	// タグ検索キャッシュ
	std::unordered_map<TagType, std::vector<entt::entity>> tagCache_;
	std::vector<entt::entity> pendingTagSync_; // ★追加: 生成直後の同期待ち
	std::vector<entt::entity> pendingTagRemoved_; // ★追加: 破棄時のキャッシュ削除待ち
	void OnTagAdded(entt::registry& reg, entt::entity entity);
	void OnTagRemoved(entt::registry& reg, entt::entity entity);

	// 行列計算キャッシュ (FPS向上用)
	mutable std::unordered_map<entt::entity, Engine::Matrix4x4> matrixCache_;
	mutable uint64_t matrixFrameCount_ = 0;
	void ClearMatrixCache() const { matrixCache_.clear(); matrixFrameCount_++; }

	std::string sceneSnapshot_; // ★追加: Play開始時のシリアライズ文字列
	std::string initialSceneSnapshot_; // ★追加: 起動（JSONロード）直後の状態

    // ★ ECS風Systemリスト
    std::vector<std::unique_ptr<ISystem>> systems_;
    GameContext ctx_;

    // パーティクルエディター
    Engine::ParticleEditor particleEditor_;

    float playTime_ = 0.0f; // クリアタイム計測用

    friend class EditorUI;
    friend class PipeEditor;
    friend class EnemySpawnerEditor;


	//フローフィールド用
	std::unique_ptr<NavigationManager> flowField_ = nullptr;
};

} // namespace Game