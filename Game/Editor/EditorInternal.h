// EditorInternal.h
// エディタの実装ファイル (Editor*.cpp) どうしで共有する、内部向けの宣言。
// エディタの外のコードからは読み込まない (公開する機能は EditorUI.h にある)。
//
// エディタの実装は役割ごとにファイルを分けている:
//   EditorSceneIO.cpp   シーンの JSON 読み書き・保存・読み込み・プレハブ
//   EditorCommands.cpp  Undo / Redo・Console のログ・コピー / 貼り付け / 複製
//   EditorShell.cpp     メインメニュー・ツールバー・ステータスバー・ショートカット・ドック配置・
//                       シーンランチャー・GameScene 以外のシーンを包む表示
//   EditorViewport.cpp  Scene / Game ビュー (エディタカメラ・ギズモ・クリック選択)
//   EditorHierarchy.cpp Hierarchy ウィンドウと生成メニュー
//   EditorInspector.cpp Inspector ウィンドウ・Add Component・アセットの入力欄
//   EditorPanels.cpp    Project・Lighting・Console・Animation ウィンドウ
//   FluidSandbox.cpp    Fluid Sandbox ウィンドウ
#pragma once
#include "EditorUI.h"
// UTF-8 とワイド文字のパス変換、ImGui (内部 API はドック配置などに使う)
#include "../../Engine/PathUtils.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scripts/IScript.h" // 循環参照を避けるため先に読み込む
#include "../Scripts/ScriptLifecycle.h"
#include "../Scripts/ScriptEdits.h"
#include <optional>
// エディタから参照するゲーム側のシーンとシステム
#include "../Scenes/GameScene.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/RiverSystem.h"
#include "../Systems/PlayerActionSystem.h"
#include "../Systems/EnemyAISystem.h"
#include "../Systems/UISystem.h"
#include "../Scripts/ScriptEngine.h"
// MSVC の警告 4865 (列挙型の基になる型の変更) を抑止する
#ifdef _MSC_VER
#pragma warning(disable: 4865)
#endif

#include "Audio.h"
#include <tuple>
#include "../../Engine/FluidTank.h"
#include "SceneManager.h"
#include "Input.h"
#include "WindowDX.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <string>
// ファイル選択ダイアログ (GetOpenFileName) と JSON ライブラリ
#include <Windows.h>
#include <commdlg.h>
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

namespace Game {
using json = nlohmann::json;

namespace EditorInternal {
// std::filesystem を短く fs と書く
namespace fs = std::filesystem;

// 英字を小文字にする (大文字小文字を区別しない検索用)
inline std::string ToLower(std::string s) {
	// 1 文字ずつ小文字にする
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// strcpy_s は入力がバッファより長いと invalid parameter で即終了するため、切り詰めてコピーする
template <size_t N>
inline void CopyToBuffer(char (&buffer)[N], const std::string& text) {
	// 長すぎれば切り詰めてコピーする
	strncpy_s(buffer, N, text.c_str(), _TRUNCATE);
}

// Inspector の各コンポーネント欄を ID で分離する。"Speed" "Loop" "Color" などの同名ウィジェットが
// 別コンポーネントにもあり、ID が衝突して片方しか操作できなかった
struct ImGuiIdScope {
	// 作ったときに ID を積み、消えるときに外す
	explicit ImGuiIdScope(const char* id) { ImGui::PushID(id); }
	~ImGuiIdScope() { ImGui::PopID(); }
	// コピーはできない
	ImGuiIdScope(const ImGuiIdScope&) = delete;
	ImGuiIdScope& operator=(const ImGuiIdScope&) = delete;
};

// ---- エディタ全体の状態 ----
// Undo / Redo の履歴 (最大 100 件)
inline std::deque<UndoCommand> undoStack;
inline std::deque<UndoCommand> redoStack;
inline constexpr size_t kMaxUndoDepth = 100;
// Game ビューの縦横比の選択肢 (0 は自由、-1 は自動)
inline int currentAspect = 0;
inline const float aspectValues[] = { 0.0f, 16.0f/9.0f, 4.0f/3.0f, 1.0f/1.0f, -1.0f };
inline const char* aspects[] = { "Free", "16:9", "4:3", "1:1", "Auto" };

// Console のログ (最大 500 行) と、エディタの経過時間
inline std::deque<LogEntry> consoleLog;
inline constexpr size_t kMaxConsoleLines = 500;
inline float globalTime = 0.0f;

// 変形ツールのドラッグ開始時の値 (Undo 用)
inline ImVec2 gizmoDragStartMouse = {};
inline DirectX::XMFLOAT3 gizmoStartTranslate;
inline DirectX::XMFLOAT3 gizmoStartRotate;
inline DirectX::XMFLOAT3 gizmoStartScale;
// Inspector で値を変え始めたときの値 (Undo 用)
inline DirectX::XMFLOAT3 s_inspectorStartTranslate;
inline DirectX::XMFLOAT3 s_inspectorStartRotate;
inline DirectX::XMFLOAT3 s_inspectorStartScale;

// UI 部品 (RectTransform) のドラッグ操作の状態
inline bool uiDragging = false;
inline int uiDragHandle = -1;
inline DirectX::XMFLOAT2 uiDragStartPos = {};
inline DirectX::XMFLOAT2 uiDragStartSize = {};
inline DirectX::XMFLOAT2 uiDragStartHitOffset = {};
inline DirectX::XMFLOAT2 uiDragStartHitScale = {};

// Game ビューに表示した画像の画面上の範囲
inline ImVec2 gameImageMin = {};
inline ImVec2 gameImageMax = {};

// コピーしたエンティティの JSON (Edit > Copy)
inline std::string s_clipboardJson;

// Scene ビューでカメラ操作中か・カメラの移動速度
inline bool s_sceneCameraNavigating = false;
inline float s_sceneCameraSpeed = 20.0f;
// エディタのカメラを初期位置に置いたか・選択中の物へカメラを寄せる要求
inline bool s_editorCameraPlaced = false;
inline bool s_frameSelectedRequest = false;

// Fluid Sandbox ウィンドウを開く要求 (検証シーンなどから呼ばれる)
inline bool fluidSandboxOpen = false;

// Window メニューで開閉できる各ウィンドウの表示状態
struct EditorWindows {
	bool hierarchy = true;
	bool inspector = true;
	bool project = true;
	bool console = true;
	bool animation = true;
	bool lighting = true;
	// Particle Editor と Fluid Sandbox はプレビュー描画でレンダラーのカメラや流体を使うため、必要な時だけ開く
	bool particleEditor = false;
	bool fluidSandbox = false;
	bool fluidProfiler = false;
};
inline EditorWindows s_windows;
// Maximize On Play / Stats / レイアウトの再構築 / プロファイラの一時停止 の状態
inline bool s_maximizeOnPlay = false;
inline bool s_showStats = false;
inline bool s_resetLayoutRequested = false;
inline bool s_fluidProfilerPaused = false;
// Scene / Game タブの切り替え要求
inline GameScene::EditorView s_requestedView = GameScene::EditorView::Game;
inline bool s_viewRequested = false;
inline const char* s_pendingFocusWindow = nullptr; // レイアウト再構築後、ウィンドウが作られてから前面に出す

// ウィンドウ名 (ドック配置と Window メニューで共通に使う)
inline constexpr const char* kSceneWindow = "Scene";
inline constexpr const char* kGameWindow = "Game";
inline constexpr const char* kHierarchyWindow = "Hierarchy";
inline constexpr const char* kInspectorWindow = "Inspector";
inline constexpr const char* kProjectWindow = "Project";
inline constexpr const char* kConsoleWindow = "Console";
inline constexpr const char* kAnimationWindow = "Animation";
inline constexpr const char* kLightingWindow = "Lighting";
inline constexpr const char* kParticleWindow = "Particle Editor";
inline constexpr const char* kFluidSandboxWindow = "Fluid Sandbox";

// ---- ファイルをまたいで使う関数 ----
// シーンの JSON から復元する (append が true なら今のシーンに追加する) [EditorSceneIO.cpp]
std::vector<entt::entity> RestoreSceneFromJson(GameScene* scene, const json& j, bool append);
// 1 つのエンティティを JSON の文字列にする [EditorSceneIO.cpp]
std::string SerializeEntity(SceneComponents& registry, entt::entity entity);
// 生成・削除の Undo を作る (existsAfterRedo: やり直した後に存在する操作か) [EditorCommands.cpp]
UndoCommand MakeSpawnUndo(GameScene* scene, const std::string& description, const std::string& snapshot, entt::entity current, bool existsAfterRedo);
// コピー / 貼り付け / 複製 [EditorCommands.cpp]
void ExecuteCopy(GameScene* scene);
void ExecutePaste(GameScene* scene);
void ExecuteDuplicate(GameScene* scene);
// 生成メニュー (空・立方体・球・リング・円柱) [EditorHierarchy.cpp]
void DrawCreateEntityMenu(GameScene* scene);
// Scene / Game タブを前面に出す (ウィンドウの Begin より前に呼ぶ) [EditorShell.cpp]
void RequestView(GameScene::EditorView view);
// Console のログを種類ごとに数える [EditorShell.cpp]
int CountLogs(LogLevel level);
} // namespace EditorInternal
} // namespace Game
