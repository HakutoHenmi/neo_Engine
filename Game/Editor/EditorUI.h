// EditorUI.h
// ゲーム開発用エディタ (ImGui) の公開窓口。
// メニュー・ツールバー・Hierarchy・Inspector・Scene/Game ビュー・Project・Console などの描画と、
// シーンの保存・読み込み、Undo/Redo、ゲーム固有の拡張 (EditorExtension) の登録を扱う。
#pragma once
#include "Renderer.h"
#include "Audio.h"
#include "IScene.h"
#include "../ObjectTypes.h"
#include "../SceneComponents.h"
#include <vector>
#include <functional>
#include <string>
#include <deque>
#include <DirectXMath.h>

// ImGui の 2 次元ベクトル (このヘッダーでは参照だけなので前方宣言)
struct ImVec2;

namespace Game {

// ゲームシーン (Scenes/GameScene.h)
class GameScene;

// Undo/Redo用のコマンド
struct UndoCommand {
	std::string description;
	std::function<void()> undo;
	std::function<void()> redo;
};

// ギズモ操作モード
enum class GizmoMode {
	Translate,
	Rotate,
	Scale,
};

// Console のログ1件 (種類・内容・時刻)
enum class LogLevel { Info, Warning, Error };
struct LogEntry {
	LogLevel level;
	std::string message;
	float time;
};

// ゲーム固有のエディタ拡張。エディタ本体はこの型だけを知り、ゲーム側が登録する。
// (例: Game/Editor/SlimeDevTools.cpp。ファイルをプロジェクトから外せば拡張ごと消える)
struct EditorExtension {
	std::string windowName;                         // Tools メニューとウィンドウのタイトル
	bool open = true;
	std::function<void(GameScene*)> drawWindow;     // ImGui::Begin/End の内側を描画する
	// Hierarchy の Category 表示で使う分類名。空文字なら標準の分類 (コンポーネント構成) を使う
	std::function<std::string(SceneComponents&, entt::entity)> classify;
	// Game ビューポート上のオーバーレイ (画像の左上/右下のスクリーン座標)
	std::function<void(GameScene*, const ImVec2&, const ImVec2&)> drawViewportOverlay;
};

// エディタ本体 (状態はすべて静的に持つ)
class EditorUI {
public:
	// ゲーム固有の拡張を登録する
	static void RegisterExtension(EditorExtension extension);

	// GameScene のエディタを描く (毎フレーム)
	static void Show(Engine::Renderer* renderer, GameScene* gameScene);
	// App から毎フレーム呼ばれる。GameScene 以外 (タイトル・デモ) もエディタの中に表示する
	static void PresentScene(Engine::IScene& scene, const std::string& sceneName);

	// 取り消しとやり直しの関数
	static void PushUndo(const UndoCommand& cmd);
	static void Undo();
	static void Redo();

	// レンダリングサポート用
	static void ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir);

	// Console へのログ出力
	static void Log(const std::string& msg);
	static void LogWarning(const std::string& msg);
	static void LogError(const std::string& msg);
	
	static ImVec2 GetGameImageMin();
	static ImVec2 GetGameImageMax();

	// 現在のシーンファイルのパスと、変形ツールの状態
	static const std::string& GetCurrentScenePath() { return currentScenePath; }
    static GizmoMode GetGizmoMode() { return gizmo_.Mode(); }
    static bool IsGizmoDragging() { return gizmo_.Dragging(); }
    static int GetGizmoDragAxis() { return gizmo_.Axis(); }
	// シーンの保存 (ファイル / 文字列) と読み込み (ファイル / 文字列)
	static void SaveScene(GameScene* scene, const std::string& path = "");
	static std::string SaveToMemory(GameScene* scene);
	static void LoadScene(GameScene* scene, const std::string& path);
	static void LoadFromMemory(GameScene* scene, const std::string& data);
	
	// 実行ファイルの場所に関係なく、プロジェクト内を指す絶対パスにする
	static std::string GetUnifiedProjectPath(const std::string& path);
	// シーンを追加で読み込む / プレハブを読み込んで配置する
	static void AddScene(GameScene* scene, const std::string& path);
	static std::vector<entt::entity> LoadPrefab(GameScene* scene, const std::string& path);

	// 初期化 (アイコンの読み込みなど)
	static void Initialize(Engine::Renderer* renderer);
	// Fluid Sandbox ウィンドウを開く
	static void OpenFluidSandbox();

private:
    // 変形ツールの状態 (移動・回転・拡大のどれか、ドラッグ中か、どの軸か)
    class GizmoState {
    public:
        GizmoMode Mode() const { return mode_; }
        bool Dragging() const { return dragging_; }
        int Axis() const { return axis_; }
        void SetMode(GizmoMode mode) { mode_ = mode; }
        void BeginDrag(int axis) { dragging_ = true; axis_ = axis; }
        void EndDrag() { dragging_ = false; axis_ = -1; }
    private:
        GizmoMode mode_ = GizmoMode::Translate;
        bool dragging_ = false;
        int axis_ = -1;
    };
    // 変形ツールの状態と、現在のシーンファイルのパス
    static GizmoState gizmo_;
    static std::string currentScenePath;
	// アイコン用テクスチャハンドル
	struct Icons {
		uint32_t folder = 0;
		uint32_t file = 0;
		uint32_t model = 0;
		uint32_t prefab = 0;
		uint32_t audio = 0;
		uint32_t script = 0;
	};
	static Icons s_icons;

private:
	// 登録された拡張の一覧
	static std::vector<EditorExtension>& Extensions();
	// 各ウィンドウの描画 (Hierarchy・Scene/Game ビュー・メニュー・ツールバー・ステータスバー・ショートカット)
	static void ShowHierarchy(GameScene* scene);
	static void ShowViewports(Engine::Renderer* renderer, GameScene* scene, bool maximized);
	static void DrawMainMenu(Engine::Renderer* renderer, GameScene* scene);
	static void DrawToolbar(GameScene* scene, float height);
	static void DrawStatusBar(GameScene* scene, float height);
	static void HandleShortcuts(GameScene* scene);
	// 流体のプロファイラ / 選択物を Scene カメラの前へ / Scene ビューで選択物を映す
	static void ShowFluidProfiler(Engine::Renderer* renderer);
	static void MoveSelectedToView(GameScene* scene);
	static void FrameSelectedInScene();
	// GameScene 以外のシーン (タイトルなど) を包むエディタ
	static void ShowSceneHost(Engine::IScene& scene, const std::string& sceneName);
	// 検索付きの Add Component ボタン
	static void DrawAddComponentButton(SceneComponents& registry, entt::entity entity, bool playing);
	// Scene / Game ビューの中身・Scene カメラの操作・Scene ビューのツールバー
	static void DrawViewport(Engine::Renderer* renderer, GameScene* scene, bool sceneView);
	static void UpdateSceneCamera(GameScene* scene, bool hovered);
	static void DrawSceneToolbar(GameScene* scene, const ImVec2& imageMin);
	// Inspector・Project・Lighting・Fluid Sandbox・Animation・Console の各ウィンドウ
	static void ShowInspector(GameScene* scene);
	static void ShowProject(Engine::Renderer* renderer, GameScene* scene);
	static void ShowSceneSettings(Engine::Renderer* renderer);
	static void ShowFluidSandbox(Engine::Renderer* renderer, GameScene* scene);
	static void ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene);
	static void ShowConsole();

	// アセット選択用のヘルパー (フォルダ内の一覧・ドラッグ&ドロップ対応の入力欄)
	static std::vector<std::string> GetAssetsInDir(const std::string& root, const std::vector<std::string>& extensions);
	static bool AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions);
};

} // namespace Game
