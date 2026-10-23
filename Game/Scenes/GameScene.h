// GameScene.h
// ゲーム本編のシーン。エンティティ (ECS のレジストリ) とシステム群を持ち、
// 毎フレームのゲーム処理・描画・エディタ表示を行う。
// スライムの街ステージ (chrono.json) では ChronoSystem が中心になり、
// それ以外のシーンファイルでは旧アクションゲーム用のシステム群が動く。
#pragma once
// ゲーム全体の設定 (本編のシーンファイルなど)
#include "../Data/GameConfig.h"
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "EventSystem.h" // スクリプト間のイベント通知
#include "../ObjectTypes.h"
#include "../Systems/ISystem.h"
#include "../SceneComponents.h"
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include "../../externals/entt/entt.hpp"
#include "../../Engine/ParticleEmitter.h"
#include "../../Engine/ParticleEditor.h"


namespace Game {

// システムの前方宣言 (本体は Systems/ 以下)
class ChronoSystem;
class WaveSystem;

// ゲーム本編のシーン
class GameScene : public Engine::IScene {
public:
    // IScene の実装
    ~GameScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override; // ワールド空間 UI (頭上の HP バーなど) の描画
    void DrawEditor() override;
#ifndef NDEBUG
    // システムごとの更新時間 (Debug / Development の計測用)
    struct UpdateTiming { const char* name; float ms; };
    const std::vector<UpdateTiming>& GetUpdateTimings() const { return updateTimings_; }
#endif

    // エディタ用のギズモ (移動ツールの軸・選択中の強調・ライトの位置) を描く
    void DrawEditorGizmos();
    void DrawSelectionHighlight();
    void DrawLightGizmos();

	// エンティティの生成・破棄はシーンが管理する
	entt::entity CreateEntity(const std::string& name = "New Object");
	// 流体のプレビュー (エディタでの水の表示) を初期状態に戻す
	void ResetFluidPreview();
	// ID を指定して破棄を予約する (実際の削除はフレームの区切り)
	void DestroyObject(uint32_t id);
	void ClearScene(); // シーンを完全に空にし、予約や各システムの状態もリセットする

    // コンポーネントの読み書き窓口と、ゲームの中心システム (スライム本編)
    SceneComponents& GetComponents() { return components_; }
    ChronoSystem* GetChronoSystem()const {return chronoSystem_;}
    // ワールド行列のキャッシュを捨てて次に使うときに計算し直させる
    void InvalidateWorldMatrices() const { ClearMatrixCache(); }
    // 名前もトランスフォームも持たない空のエンティティを作る / エディタ用の一時エンティティを即座に消す
    entt::entity CreateEmptyEntity();
    void DestroyEditorEntity(entt::entity entity);
    // レジストリの読み取り専用参照 (書き換えは GetComponents 経由)
    const entt::registry& GetRegistry() const { return registry_; }
	// スクリプト間のイベント通知と、パーティクルエディタ
	Engine::EventSystem& GetEventSystem() { return eventSystem_; }
	Engine::ParticleEditor& GetParticleEditor() { return particleEditor_; }
	// PLAY 中か (エディタの再生状態)
	bool GetIsPlaying() const { return isPlaying_; }
	bool IsPlaying() const { return isPlaying_; } // 古い名前のための別名
	// ゲーム内のポーズ / ステージクリアの結果画面を表示中か
	bool IsPaused() const { return isPaused_; }
	bool IsResultShowing() const { return IsStageClear(); }
	// PLAY の開始・停止 (停止時は PLAY 前の状態に戻す) / ポーズの切り替え
	void SetIsPlaying(bool play);
    void SetPaused(bool paused);

	void CreatePauseMenu();  // ポーズメニューの UI を作る
	void DestroyPauseMenu(); // ポーズメニューの UI を消す

	// 描画とワールド行列 (親子関係をたどって計算する)
	Engine::Renderer* GetRenderer() const { return renderer_; }
	Engine::Matrix4x4 GetWorldMatrix(int index) const; 
	Engine::Matrix4x4 GetWorldMatrixRecursive(entt::entity entity, int depth) const;
	Engine::Camera& GetCamera() { return camera_; } // ゲーム (プレイヤー) カメラ
	// エディタの Scene ビュー用カメラ。ゲームの処理からは触られない
	enum class EditorView { Game, Scene };
	Engine::Camera& GetEditorCamera() { return editorCamera_; }
	// 現在描画しているビューのカメラ (ピッキングやオーバーレイの投影に使う)
	Engine::Camera& GetViewCamera() { return editorView_ == EditorView::Scene ? editorCamera_ : camera_; }
	EditorView GetEditorView() const { return editorView_; }
	void SetEditorView(EditorView view) { editorView_ = view; }
	// エディタの Pause / Step (Unity と同じ)。ゲームのポーズ画面は出さず、更新だけを止める
	void SetEditorPaused(bool paused);
	bool IsEditorPaused() const { return editorPaused_; }
	void StepEditorFrame() { if (editorPaused_) ++editorStepFrames_; }
	// エディタでの選択 (単体と複数)
	entt::entity GetSelectedEntity() const { return selectedEntity_; }
	void SelectEntity(entt::entity entity);
	void ClearSelection();
	const std::set<entt::entity>& GetSelectedEntities() const { return selectedEntities_; }

	// エディタの Game ビューの位置・大きさとマウス座標をシステムへ伝える
    void SetEditorViewport(DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, float mouseX, float mouseY);

	// 名前でエンティティを探す (最初に見つかった1つ)
	entt::entity FindObjectByName(const std::string& name);
	// 指定座標のメッシュ表面の高さを取得する (startY から下向きに探す)
	float GetHeightAt(float x, float z, float startY = 1000.0f, uint32_t excludeId = 0);
	// レイキャスト (壁の判定などに使う)。当たれば距離を返す
	bool RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist);
	
	// タグでエンティティを素早く探す (キャッシュを使う)
	const std::vector<entt::entity>& GetEntitiesByTag(const std::string& tag);
	const std::vector<entt::entity>& GetEntitiesByTag(TagType tag); // 列挙型のタグで探す
	void SetTag(entt::entity entity, const std::string& tag);
	void SetTag(entt::entity entity, TagType tag); // 列挙型のタグを設定する
	void SyncTag(entt::entity entity); // タグを書き換えた後にキャッシュを手動で同期する



private:
    // エンティティを即座に削除する (タグのキャッシュなども片付ける)
    void RemoveEntity(entt::entity entity);
#ifndef NDEBUG
    std::vector<UpdateTiming> updateTimings_;
#endif
	// CPU側でのプレイヤースライム変形・パーティクル管理用ロジック
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
	PlayerSlimeCpuLogic projectileCpuLogic_; // 弾 (飛び道具) のメッシュ変形用

    // エンジン側のオブジェクト
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    // ゲーム (プレイヤー) カメラと、エディタの Scene ビュー用カメラ
    Engine::Camera camera_;
    Engine::Camera editorCamera_;
    EditorView editorView_ = EditorView::Game; // エディタが無い構成では常に Game
    // エディタの Pause と Step の状態 (Step で進める残りフレーム数、Pause 前の流体停止状態)
    bool editorPaused_ = false;
    int editorStepFrames_ = 0;
    bool worldFrozenBeforeEditorPause_ = false;
    Engine::EventSystem eventSystem_; // スクリプト間のイベント通知
    // エンティティとコンポーネントを入れる ECS のレジストリと、その編集窓口
    entt::registry registry_;
    SceneComponents components_{registry_};
	// 旧アクションゲームの戦闘の流れ (開始演出など) の状態
	CombatFlowState combatFlow_;
    // エディタで選択中のエンティティ
    std::set<entt::entity> selectedEntities_;
    entt::entity selectedEntity_ = entt::null;

    // PLAY 中か / スライム本編モードか / ポーズ中か / グラフィック設定画面の状態
    bool isPlaying_ = false;
    bool chronoMode_ = false;
    bool isPaused_ = false;
    bool graphicsOpen_=false;
    bool graphicsSaveFailed_=false;
    // 生成・破棄の予約 (更新の途中で消すと反復処理が壊れるため、フレームの区切りでまとめて行う)
    entt::registry pendingSpawns_;
    std::vector<entt::entity> pendingDestroys_;
    std::mutex spawnMutex_; // 別スレッドからも安全に生成・破棄の予約ができるようにするロック
	
	// タグ検索のキャッシュ (タグ → エンティティ一覧)
	std::unordered_map<TagType, std::vector<entt::entity>> tagCache_;
	std::vector<entt::entity> pendingTagSync_; // 生成直後で、まだキャッシュに入っていないエンティティ
	std::vector<entt::entity> pendingTagRemoved_; // 破棄されたため、キャッシュから消す必要があるエンティティ
	// タグが付いた・外れたときに呼ばれるコールバック
	void OnTagAdded(entt::registry& reg, entt::entity entity);
	void OnTagRemoved(entt::registry& reg, entt::entity entity);

	// スクリプトコンポーネントが外れたときの後片付け
	void OnScriptDestroyed(entt::registry& registry, entt::entity entity);
	// ステージクリアしたか
	bool IsStageClear() const;

	// ワールド行列の計算結果のキャッシュ (同じフレームで何度も計算しないため)
	mutable std::unordered_map<entt::entity, Engine::Matrix4x4> matrixCache_;
	mutable uint64_t matrixFrameCount_ = 0;
	void ClearMatrixCache() const { matrixCache_.clear(); matrixFrameCount_++; }

	std::string sceneSnapshot_; // PLAY 開始時のシーンの保存内容 (停止時にここへ戻す)
	std::string initialSceneSnapshot_; // 起動時 (JSON 読み込み直後) の状態

	// PLAY 前のゲームカメラの位置と回転 (停止時に戻す)
	DirectX::XMFLOAT3 editorCameraPos_{0, 2, -5};
	DirectX::XMFLOAT3 editorCameraRot_{0.2f, 0, 0};

    // ECS のシステム一覧 (登録順に更新・描画する)
    std::vector<std::unique_ptr<ISystem>> systems_;
    ChronoSystem* chronoSystem_ = nullptr; // 所有は systems_。ここは参照するためだけのポインタ
    WaveSystem* waveSystem_ = nullptr;
    // システムへ渡す共有データ (時間・カメラ・入力・ビューポートなど)
    GameContext ctx_;

    // パーティクルエディタ
    Engine::ParticleEditor particleEditor_;

    float playTime_ = 0.0f; // クリアタイムの計測用
    // 前フレームでプレイヤーが液状化していたか
    bool wasLiquidated_ = false;
    bool gpuSlimeEmitted_ = false; // GPU スライムを最初に放出したか





};

} // namespace Game
