#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "EventSystem.h" // 笘・ｿｽ蜉: 繧､繝吶Φ繝医す繧ｹ繝・Β
#include "../ObjectTypes.h"
#include "../Systems/ISystem.h"
#include <mutex>
#include <unordered_map>
#include <string>
#include "../../externals/entt/entt.hpp"
#include "../../Engine/ParticleEmitter.h"
#include "../../Engine/ParticleEditor.h"


namespace Game {

class GameScene : public Engine::IScene {
public:
    ~GameScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override; // 笘・ｿｽ蜉: 繝ｯ繝ｼ繝ｫ繝臥ｩｺ髢填I逕ｨ
    void DrawEditor() override;

    void DrawEditorGizmos();
    void DrawSelectionHighlight();
    void DrawLightGizmos();

	// 笘・豎守畑繧ｹ繝昴・繝ｳ・・egistry繧堤峩謗･謫堺ｽ懊☆繧九％縺ｨ繧呈耳螂ｨ・・
	entt::entity CreateEntity(const std::string& name = "New Object");
	// 笘・ｿｽ蜉: 繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒID縺ｧ遐ｴ譽・ｿ晉蕗縺ｫ縺吶ｋ
	void DestroyObject(uint32_t id);
	void ClearScene(); // ★追加: シーンの完全クリアとキューのリセット

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

	// 笘・ｿｽ蜉: 繧ｳ繝ｳ繝・く繧ｹ繝医∈縺ｮ繧｢繧ｯ繧ｻ繧ｹ
	GameContext& GetContext() { return ctx_; }

	// 笘・ｿｽ蜉: 蜷榊燕縺ｧ繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ讀懃ｴ｢縺吶ｋ繝倥Ν繝代・
	entt::entity FindObjectByName(const std::string& name);
	// 笘・ｿｽ蜉: 謖・ｮ壼ｺｧ讓吶・繝｡繝・す繝･陦ｨ髱｢縺ｮ鬮倥＆繧貞叙蠕・(startY 莉倩ｿ代°繧我ｸ九ｒ謗｢邏｢)
	float GetHeightAt(float x, float z, float startY = 1000.0f, uint32_t excludeId = 0);
	// 笘・ｿｽ蜉: 豎守畑繝ｬ繧､繧ｭ繝｣繧ｹ繝・(螢∝愛螳壹↑縺ｩ縺ｫ菴ｿ逕ｨ)
	bool RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist);
	
	// 笘・ｿｽ蜉: 鬮倬溘ち繧ｰ讀懃ｴ｢繧ｷ繧ｹ繝・Β
	const std::vector<entt::entity>& GetEntitiesByTag(const std::string& tag);
	const std::vector<entt::entity>& GetEntitiesByTag(TagType tag); // 笘・ｿｽ蜉
	void SetTag(entt::entity entity, const std::string& tag);
	void SetTag(entt::entity entity, TagType tag); // 笘・ｿｽ蜉
	void SyncTag(entt::entity entity); // 笘・ｿｽ蜉: 謇句虚蜷梧悄逕ｨ



private:
	// CPU蛛ｴ縺ｧ縺ｮ繝励Ξ繧､繝､繝ｼ繧ｹ繝ｩ繧､繝螟牙ｽ｢繝ｻ繝代・繝・ぅ繧ｯ繝ｫ邂｡逅・畑繝ｭ繧ｸ繝・け
	struct PlayerSlimeCpuLogic {
		uint32_t dynamicMeshHandle = 0;
		std::vector<Engine::VertexData> baseVertices;
		std::vector<Engine::VertexData> baseNormals;
		std::vector<uint32_t> indices;
		std::vector<Engine::VertexData> dynamicVerts;
		float time = 0.0f;
		float groundY = 0.0f;
		bool initialized = false;

		struct Particle {
			Engine::Vector3 basePos;
			Engine::Vector3 currentPos;
			Engine::Vector4 color;
		};
		std::vector<Particle> particles;
	};
	PlayerSlimeCpuLogic slimeCpuLogic_;
	PlayerSlimeCpuLogic projectileCpuLogic_; // Projectile mesh logic

    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;
    Engine::EventSystem eventSystem_; // 笘・ｿｽ蜉: 繧ｹ繧ｯ繝ｪ繝励ヨ髢馴壻ｿ｡逕ｨ
    entt::registry registry_;
    std::set<entt::entity> selectedEntities_;
    entt::entity selectedEntity_ = entt::null;

    bool isPlaying_ = false;
    entt::registry pendingSpawns_;
    std::vector<entt::entity> pendingDestroys_;
    std::mutex spawnMutex_; // 笘・ｿｽ蜉: 繝槭Ν繝√せ繝ｬ繝・ラ縺九ｉ螳牙・縺ｫ繧ｹ繝昴・繝ｳ繝ｻ遐ｴ譽・匳骭ｲ繧定｡後∴繧九ｈ縺・↓縺吶ｋ
	
	// 繧ｿ繧ｰ讀懃ｴ｢繧ｭ繝｣繝・す繝･
	std::unordered_map<TagType, std::vector<entt::entity>> tagCache_;
	std::vector<entt::entity> pendingTagSync_; // 笘・ｿｽ蜉: 逕滓・逶ｴ蠕後・蜷梧悄蠕・■
	std::vector<entt::entity> pendingTagRemoved_; // 笘・ｿｽ蜉: 遐ｴ譽・凾縺ｮ繧ｭ繝｣繝・す繝･蜑企勁蠕・■
	void OnTagAdded(entt::registry& reg, entt::entity entity);
	void OnTagRemoved(entt::registry& reg, entt::entity entity);

	// スクリプト破棄用コールバック
	void OnScriptDestroyed(entt::registry& registry, entt::entity entity);

	// 陦悟・險育ｮ励く繝｣繝・す繝･ (FPS蜷台ｸ顔畑)
	mutable std::unordered_map<entt::entity, Engine::Matrix4x4> matrixCache_;
	mutable uint64_t matrixFrameCount_ = 0;
	void ClearMatrixCache() const { matrixCache_.clear(); matrixFrameCount_++; }

	std::string sceneSnapshot_; // 笘・ｿｽ蜉: Play髢句ｧ区凾縺ｮ繧ｷ繝ｪ繧｢繝ｩ繧､繧ｺ譁・ｭ怜・
	std::string initialSceneSnapshot_; // 笘・ｿｽ蜉: 襍ｷ蜍包ｼ・SON繝ｭ繝ｼ繝会ｼ臥峩蠕後・迥ｶ諷・

    // 笘・ECS鬚ｨSystem繝ｪ繧ｹ繝・
    std::vector<std::unique_ptr<ISystem>> systems_;
    GameContext ctx_;

    // 繝代・繝・ぅ繧ｯ繝ｫ繧ｨ繝・ぅ繧ｿ繝ｼ
    Engine::ParticleEditor particleEditor_;

    float playTime_ = 0.0f; // 繧ｯ繝ｪ繧｢繧ｿ繧､繝險域ｸｬ逕ｨ
    bool wasLiquidated_ = false;
    bool gpuSlimeEmitted_ = false; // ★追加: GPUスライムの初回放出フラグ

    friend class EditorUI;



};

} // namespace Game
