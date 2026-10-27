// EditorShell.cpp
// エディタの外枠: メインメニュー・ツールバー・ステータスバー・ショートカット・ドック配置・
// シーンランチャー (Scenes メニュー)・流体のプロファイラ・GameScene 以外のシーンを包む表示と、毎フレームの描画 (Show)。
// 画面構成: メインメニュー / ツールバー (ツール・Scene⇔Game・Play/Pause/Step) / ドックスペース / ステータスバー
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// ユーティリティとファイルダイアログ

// Windows の「開く」ダイアログ (選ばれたパスを UTF-8 で返す。キャンセル時は空)
static std::string OpenFileDialog(const char* filter) {
	// ダイアログの設定 (パスを入れるバッファ・絞り込み・存在するファイルだけ)
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	// 選ばれたら UTF-8 のパスで返す
	if (GetOpenFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

// Windows の「名前を付けて保存」ダイアログ
static std::string SaveFileDialog(const char* filter, const char* defExt) {
	// ダイアログの設定 (パスを入れるバッファ・絞り込み・既定の拡張子・上書きの確認)
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	std::wstring wdefExt = Engine::PathUtils::FromUTF8(defExt);
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = wdefExt.c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	// 選ばれたら UTF-8 のパスで返す
	if (GetSaveFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

// 初期化: Project ウィンドウのアイコンを読み込む
void EditorUI::Initialize(Engine::Renderer* renderer) {
	// 描画の窓口が無ければ何もしない
	if (!renderer) return;
	// エディタのアイコンは色が変わらないよう sRGB 変換をせずに読み込む
	s_icons.folder = renderer->LoadTexture2D("Resources/Editor/Icons/folder.png", false);
	s_icons.file = renderer->LoadTexture2D("Resources/Editor/Icons/file.png", false);
	s_icons.model = renderer->LoadTexture2D("Resources/Editor/Icons/model.png", false);
	s_icons.prefab = renderer->LoadTexture2D("Resources/Editor/Icons/prefab.png", false);
	s_icons.audio = renderer->LoadTexture2D("Resources/Editor/Icons/audio.png", false);
	s_icons.script = renderer->LoadTexture2D("Resources/Editor/Icons/script.png", false);
}

// ====== Scene launcher (Unity の Build Settings / シーン切り替えに相当) ======
namespace {
// ランチャーの1項目 (グループ名・表示名・シーン名・シーンファイル・説明)。Resources/Data/editor.json の sceneLauncher から読む
struct SceneLaunch {
	std::string group;
	std::string label;
	std::string key;       // SceneManager に登録された名前
	std::string stagePath; // GameScene に読み込ませるシーンファイル (空なら本編のシーン)
	std::string note;
};
// editor.json の中身 (シーンランチャーの項目と、エディタで包んで表示するシーン)
struct EditorData { std::vector<SceneLaunch> launches; std::vector<std::string> hostedScenes; };
const EditorData& EditorConfig() {
	static const EditorData data = [] {
		const auto& json = Data::Load("editor.json");
		EditorData out;
		for (const auto& item : Data::Require(json, "sceneLauncher", "editor.json")) {
			out.launches.push_back({item.at("group").get<std::string>(), item.at("label").get<std::string>(), item.at("scene").get<std::string>(),
				item.at("stage").get<std::string>(), item.at("note").get<std::string>()});
		}
		out.hostedScenes = Data::ReadStrings(Data::Require(json, "hostedScenes", "editor.json"));
		return out;
	}();
	return data;
}

// 今開いているシーンがこの項目か (同じシーン名で、シーンファイルも一致するか)
bool IsCurrentLaunch(const SceneLaunch& launch) {
	// シーン名が違えば違う
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager || manager->CurrentName() != launch.key) return false;
	// シーンファイルを比べる (空の項目は本編のステージ)
	const std::string& stage = manager->CurrentParameters().stagePath;
	if (launch.stagePath.empty()) return Data::IsMainStage(stage);
	return stage.find(launch.stagePath) != std::string::npos;
}

// シーンを切り替える (切り替え中は受け付けない)
void LaunchScene(const std::string& key, const std::string& stagePath) {
	// 未登録のシーンや切り替え中は何もしない
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager || !manager->Has(key) || manager->InTransition()) return;
	// シーンファイルを渡して切り替え、ログに残す
	Engine::SceneParameters params;
	params.stagePath = stagePath;
	manager->RequestChange(key, params);
	EditorUI::Log("Open scene: " + key + (stagePath.empty() ? "" : " (" + stagePath + ")"));
}

// ツールバーに表示する現在のシーン名
std::string CurrentSceneLabel() {
	// ランチャーの項目にあればその表示名
	for (const auto& launch : EditorConfig().launches) {
		if (IsCurrentLaunch(launch)) return launch.label;
	}
	// 無ければシーン名と、シーンファイルの名前
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager) return "Scene";
	std::string label = manager->CurrentName();
	const std::string& stage = manager->CurrentParameters().stagePath;
	if (!stage.empty()) label += " (" + Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(stage)).filename().wstring()) + ")";
	return label;
}

// メニューとツールバーのドロップダウンで共通の中身
void DrawSceneLaunchItems() {
	// 直前のグループ名 (変わったら見出しを出す)
	const char* group = nullptr;
	for (const auto& launch : EditorConfig().launches) {
		if (!group || std::string(group) != launch.group) {
			if (group) ImGui::Spacing();
			group = launch.group.c_str();
			ImGui::TextDisabled("%s", group);
		}
		// 選んだらそのシーンを開く
		if (ImGui::Selectable(launch.label.c_str(), IsCurrentLaunch(launch))) LaunchScene(launch.key, launch.stagePath);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", launch.note.c_str());
	}
	// Resources/Scenes の JSON はゲームシーンとして開く
	ImGui::Spacing();
	ImGui::TextDisabled("Scene Files");
	// フォルダーが無くても例外を出さないためのエラーの受け取り先
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(EditorUI::GetUnifiedProjectPath("Resources/Scenes")), ec)) {
		// JSON 以外は飛ばす
		if (entry.path().extension() != L".json") continue;
		const std::string fileName = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
		const std::string path = "Resources/Scenes/" + fileName;
		auto* manager = Engine::SceneManager::GetInstance();
		// 今開いているシーンファイルか
		const bool current = manager && manager->CurrentName() == "Game" && manager->CurrentParameters().stagePath.find(fileName) != std::string::npos;
		if (ImGui::Selectable(fileName.c_str(), current)) LaunchScene("Game", path);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open %s in the game scene", path.c_str());
	}
}

// メインメニューの「Scenes」
void DrawSceneLaunchMenu() {
	// メニューを開いていなければ何もしない
	if (!ImGui::BeginMenu("Scenes")) return;
	DrawSceneLaunchItems();
	ImGui::Separator();
	// 今のシーンを読み込み直す
	auto* manager = Engine::SceneManager::GetInstance();
	if (ImGui::MenuItem("Restart Current Scene", "Ctrl+R") && manager) LaunchScene(manager->CurrentName().c_str(), manager->CurrentParameters().stagePath);
	ImGui::EndMenu();
}

// ツールバーのシーン切り替えドロップダウン
void DrawSceneLaunchCombo() {
	ImGui::SetNextItemWidth(170.0f);
	// 今のシーン名を表示する
	const std::string label = CurrentSceneLabel();
	if (ImGui::BeginCombo("##SceneLauncher", label.c_str(), ImGuiComboFlags_HeightLarge)) {
		DrawSceneLaunchItems();
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open another scene (Scenes menu)");
}

// Ctrl+R: 今のシーンを読み込み直す
void HandleSceneShortcuts() {
	// 文字入力中・Ctrl が押されていない・R が押されていないなら何もしない
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput || !io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_R, false)) return;
	if (auto* manager = Engine::SceneManager::GetInstance()) LaunchScene(manager->CurrentName().c_str(), manager->CurrentParameters().stagePath);
}

// GameScene 以外 (タイトル・セレクト・デモ) で編集できない欄に出す説明
void DrawCodeSceneNotice(const char* what) {
	// この欄はゲームシーンでのみ使えることを説明する
	ImGui::TextWrapped("%s is available in game scenes.", what);
	ImGui::Spacing();
	ImGui::TextDisabled("This scene is built in code (UI / demo). Use the Game view to try it, or open a game scene:");
	// 街のステージを開くボタン
	if (ImGui::Button("Open City Stage")) LaunchScene("Game", "");
}
} // namespace

// Console のログを種類ごとに数える
int EditorInternal::CountLogs(LogLevel level) {
	int count = 0;
	// 指定の種類のものを数える
	for (const auto& log : consoleLog) count += log.level == level ? 1 : 0;
	return count;
}

// Scene / Game タブを前面に出す (ウィンドウの Begin より前に呼ぶ)
void EditorInternal::RequestView(GameScene::EditorView view) {
	// 要求を覚えておく
	s_requestedView = view;
	s_viewRequested = true;
}

namespace {

// ツールバーのアイコンの種類 (再生・停止・一時停止・1 フレーム送り)
enum class ToolbarIcon { Play, Stop, Pause, Step };

// フォントに依存しないよう、記号は図形で描く
bool IconButton(const char* id, ToolbarIcon icon, bool active, const char* tooltip) {
	// ボタンの大きさと位置
	const float size = ImGui::GetFrameHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	// 見えないボタンを置き、押されたか・乗っているかを調べる
	const bool pressed = ImGui::InvisibleButton(id, ImVec2(size * 1.5f, size));
	const bool hovered = ImGui::IsItemHovered();
	// 背景 (押されている・乗っている・通常で色を変える)
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
	draw->AddRectFilled(pos, ImVec2(pos.x + size * 1.5f, pos.y + size), bg, 3.0f);
	// 記号の色 (有効なら青)
	const ImU32 fg = active ? IM_COL32(120, 200, 255, 255) : IM_COL32(225, 225, 225, 255);
	// 記号の中心と大きさ
	const ImVec2 c(pos.x + size * 0.75f, pos.y + size * 0.5f);
	const float r = size * 0.28f;
	switch (icon) {
	case ToolbarIcon::Play:
		// 右向きの三角
		draw->AddTriangleFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.8f, c.y + r), ImVec2(c.x + r, c.y), fg);
		break;
	case ToolbarIcon::Stop:
		// 四角
		draw->AddRectFilled(ImVec2(c.x - r * 0.85f, c.y - r * 0.85f), ImVec2(c.x + r * 0.85f, c.y + r * 0.85f), fg);
		break;
	case ToolbarIcon::Pause:
		// 縦の 2 本線
		draw->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.25f, c.y + r), fg);
		draw->AddRectFilled(ImVec2(c.x + r * 0.25f, c.y - r), ImVec2(c.x + r * 0.8f, c.y + r), fg);
		break;
	case ToolbarIcon::Step:
		// 三角と縦線
		draw->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r), ImVec2(c.x + r * 0.45f, c.y), fg);
		draw->AddRectFilled(ImVec2(c.x + r * 0.5f, c.y - r), ImVec2(c.x + r * 0.9f, c.y + r), fg);
		break;
	}
	if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
	return pressed;
}

// 選択状態を持つテキストボタン (ツール切り替え・Scene/Game 切り替え用)
bool ToggleButton(const char* label, bool active, const char* tooltip = nullptr) {
	// 選択中は押された色にする
	if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool pressed = ImGui::Button(label);
	if (active) ImGui::PopStyleColor();
	if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
	return pressed;
}

// PLAY 中はエディタ全体を色付けして、編集が停止時に戻ることを分かりやすくする (Unity の Play Mode Tint)
int PushPlayModeTint(bool playing, bool paused) {
	if (!playing) return 0;
	// 一時停止中は黄色っぽく、再生中は赤っぽくする
	const ImVec4 tint = paused ? ImVec4(0.30f, 0.26f, 0.12f, 1.0f) : ImVec4(0.28f, 0.14f, 0.14f, 1.0f);
	// 色を変える部分 (背景・メニュー・タイトル・タブ)
	const ImGuiCol targets[] = {ImGuiCol_WindowBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_Tab, ImGuiCol_TabSelected};
	for (ImGuiCol col : targets) {
		ImVec4 c = ImGui::GetStyleColorVec4(col);
		// 元の色に 25% だけ混ぜる
		ImGui::PushStyleColor(col, ImVec4(c.x * 0.75f + tint.x * 0.25f, c.y * 0.75f + tint.y * 0.25f, c.z * 0.75f + tint.z * 0.25f, c.w));
	}
	// 変えた色の数 (後で同じ数だけ戻す)
	return IM_ARRAYSIZE(targets);
}

// 既定のレイアウト: 左 Hierarchy / 中央 Scene・Game / 下 Project・Console / 右 Inspector など
void BuildDefaultLayout(ImGuiID dockspace, const ImVec2& size, const std::vector<EditorExtension>& extensions) {
	// 今の配置を消して作り直す
	ImGui::DockBuilderRemoveNode(dockspace);
	ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspace, size);
	// 右・左・下に分ける (残りが中央)
	ImGuiID center = dockspace;
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
	// 各ウィンドウを置く
	ImGui::DockBuilderDockWindow(kSceneWindow, center);
	ImGui::DockBuilderDockWindow(kGameWindow, center);
	ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
	ImGui::DockBuilderDockWindow(kProjectWindow, bottom);
	ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
	ImGui::DockBuilderDockWindow(kAnimationWindow, bottom);
	ImGui::DockBuilderDockWindow(kInspectorWindow, right);
	ImGui::DockBuilderDockWindow(kLightingWindow, right);
	ImGui::DockBuilderDockWindow(kParticleWindow, right);
	ImGui::DockBuilderDockWindow(kFluidSandboxWindow, right);
	// 拡張のウィンドウも右側に置く
	for (const auto& ext : extensions) ImGui::DockBuilderDockWindow(ext.windowName.c_str(), right);
	// 配置を確定する
	ImGui::DockBuilderFinish(dockspace);
	// 右側は Inspector を前面にする (ウィンドウがまだ無いので次フレーム以降に適用)
	s_pendingFocusWindow = kInspectorWindow;
}

} // namespace

// Scene ビューに切り替えて選択物を映す (Hierarchy のダブルクリック・F キー・Edit メニュー)
void EditorUI::FrameSelectedInScene() {
	// Scene タブを前面に出し、次のフレームでカメラを寄せる
	RequestView(GameScene::EditorView::Scene);
	s_frameSelectedRequest = true;
}

// メインメニュー (File / Edit / Scenes / GameObject / Window)
void EditorUI::DrawMainMenu(Engine::Renderer* renderer, GameScene* gameScene) {
	// ファイル選択の絞り込み (JSON とすべてのファイル)
	static const char* kSceneFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	if (ImGui::BeginMenu("File")) {
		// PLAY 中は保存しない: ゲームが生成した一時オブジェクトまでシーンに書き込まれてしまうため
		ImGui::BeginDisabled(playing);
		// 新しいシーン: 空にして選択を外し、保存先を既定に戻す
		if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
			gameScene->ClearScene();
			gameScene->SelectEntity(entt::null);
			currentScenePath = Data::MainStagePath();
		}
		ImGui::Separator();
		// シーンを開く / 追加で読み込む
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) LoadScene(gameScene, path);
		}
		if (ImGui::MenuItem("Append Scene...", "Ctrl+Shift+O")) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) AddScene(gameScene, path);
		}
		// Resources/Scenes の JSON を一覧にして開く
		if (ImGui::BeginMenu("Scenes")) {
			std::string sceneDir = "Resources/Scenes";
			std::error_code ec;
			bool any = false;
			// 日本語名のファイルで path::string() が例外を投げるため、UTF-8 へ明示変換する
			for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(GetUnifiedProjectPath(sceneDir)), ec)) {
				// JSON 以外は飛ばす
				if (entry.path().extension() != L".json") continue;
				std::string fileName = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
				std::string fullPath = sceneDir + "/" + fileName;
				if (ImGui::MenuItem(fileName.c_str(), nullptr, currentScenePath == fullPath)) LoadScene(gameScene, fullPath);
				any = true;
			}
			// 見つからなければその旨を出す
			if (!any) ImGui::TextDisabled("No scenes in %s", sceneDir.c_str());
			ImGui::EndMenu();
		}
		ImGui::Separator();
		// 上書き保存 / 名前を付けて保存
		if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene(gameScene);
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
			std::string path = SaveFileDialog(kSceneFilter, "json");
			if (!path.empty()) SaveScene(gameScene, path);
		}
		ImGui::EndDisabled();
		if (playing) ImGui::TextDisabled("Stop play mode to save or load scenes");
		ImGui::EndMenu();
	}
	// Edit: Undo・コピー/貼り付け・Frame Selected・Play/Pause/Step
	if (ImGui::BeginMenu("Edit")) {
		// Undo / Redo (PLAY 中は無効)
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !playing)) Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !playing)) Redo();
		ImGui::Separator();
		// コピー・貼り付け・複製
		if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopy(gameScene);
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty() && !playing)) ExecutePaste(gameScene);
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !playing)) ExecuteDuplicate(gameScene);
		ImGui::Separator();
		// 選択中の物へカメラを寄せる
		if (ImGui::MenuItem("Frame Selected", "F")) FrameSelectedInScene();
		ImGui::Separator();
		// Play / Stop・Pause・Step
		if (ImGui::MenuItem(playing ? "Stop" : "Play", "Ctrl+P")) gameScene->SetIsPlaying(!playing);
		if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", gameScene->IsEditorPaused(), playing)) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, playing)) {
			gameScene->SetEditorPaused(true);
			gameScene->StepEditorFrame();
		}
		ImGui::EndMenu();
	}
	DrawSceneLaunchMenu();
	// GameObject: 生成と Move To View (PLAY 中は無効)
	if (ImGui::BeginMenu("GameObject")) {
		ImGui::BeginDisabled(playing);
		DrawCreateEntityMenu(gameScene);
		ImGui::Separator();
		// 選択中の物が位置を持っていれば、カメラの前へ移せる
		const auto selected = gameScene->GetSelectedEntity();
		const bool hasTransform = selected != entt::null && gameScene->GetComponents().valid(selected) && gameScene->GetComponents().all_of<TransformComponent>(selected);
		if (ImGui::MenuItem("Move To View", "Ctrl+Alt+F", false, hasTransform)) MoveSelectedToView(gameScene);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
	// Window: Scene/Game の切り替え、各ウィンドウの開閉、分析ツール、レイアウトのリセット
	if (ImGui::BeginMenu("Window")) {
		// Scene / Game タブの切り替え
		if (ImGui::MenuItem("Scene", "Ctrl+1")) RequestView(GameScene::EditorView::Scene);
		if (ImGui::MenuItem("Game", "Ctrl+2")) RequestView(GameScene::EditorView::Game);
		ImGui::Separator();
		// 各ウィンドウの開閉
		ImGui::MenuItem(kHierarchyWindow, nullptr, &s_windows.hierarchy);
		ImGui::MenuItem(kInspectorWindow, nullptr, &s_windows.inspector);
		ImGui::MenuItem(kProjectWindow, nullptr, &s_windows.project);
		ImGui::MenuItem(kConsoleWindow, nullptr, &s_windows.console);
		ImGui::MenuItem(kAnimationWindow, nullptr, &s_windows.animation);
		ImGui::MenuItem(kLightingWindow, nullptr, &s_windows.lighting);
		ImGui::MenuItem(kParticleWindow, nullptr, &s_windows.particleEditor);
		ImGui::MenuItem(kFluidSandboxWindow, nullptr, &s_windows.fluidSandbox);
		// 拡張のウィンドウがあれば、その開閉も並べる
		if (!Extensions().empty()) {
			ImGui::Separator();
			for (auto& ext : Extensions()) ImGui::MenuItem(ext.windowName.c_str(), nullptr, &ext.open);
		}
		ImGui::Separator();
		// 分析ツール: 流体のプロファイラと、流体の表示モードの切り替え
		if (ImGui::BeginMenu("Analysis")) {
			ImGui::MenuItem("Fluid Profiler", nullptr, &s_windows.fluidProfiler);
			if (ImGui::BeginMenu("Fluid Debug View")) {
				if (renderer) {
					// 流体のボリュームが使えるか
					ImGui::TextDisabled("%s", renderer->IsFluidVolumeReady() ? "3D volume active" : "Fallback (volume init failed)");
					// 表示モード (材質・材質の段階・法線・厚さ・計算範囲)
					const char* modes[] = {"Material", "Material phases", "3D normals", "Thickness", "Domain / ray steps"};
					for (uint32_t mode = 0; mode < 5; ++mode) {
						if (ImGui::MenuItem(modes[mode], nullptr, renderer->GetFluidVolumeDebugMode() == mode)) renderer->SetFluidVolumeDebugMode(mode);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		// レイアウトを既定に戻す
		if (ImGui::MenuItem("Reset Layout")) s_resetLayoutRequested = true;
		ImGui::EndMenu();
	}
}

// 画面上部のツールバー
void EditorUI::DrawToolbar(GameScene* gameScene, float height) {
	// メインのビューポートの上端に、スクロールしない帯として置く
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, height, flags)) {
		// PLAY 中か
		const bool playing = gameScene->GetIsPlaying();
		// 左: 変形ツール (W/E/R) と Scene / Game の切り替え
		if (ToggleButton("Move", gizmo_.Mode() == GizmoMode::Translate, "Move tool (W)")) gizmo_.SetMode(GizmoMode::Translate);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Rotate", gizmo_.Mode() == GizmoMode::Rotate, "Rotate tool (E)")) gizmo_.SetMode(GizmoMode::Rotate);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Scale", gizmo_.Mode() == GizmoMode::Scale, "Scale tool (R)")) gizmo_.SetMode(GizmoMode::Scale);
		ImGui::SameLine(0, 16);
		// Scene ビューを表示しているか
		const bool sceneActive = gameScene->GetEditorView() == GameScene::EditorView::Scene;
		if (ToggleButton("Scene", sceneActive, "Editor camera view (Ctrl+1)")) RequestView(GameScene::EditorView::Scene);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Game", !sceneActive, "Player camera view (Ctrl+2)")) RequestView(GameScene::EditorView::Game);
		ImGui::SameLine(0, 16);
		// シーン切り替えのドロップダウン
		DrawSceneLaunchCombo();

		// 中央: Play / Pause / Step
		// ボタン 3 つ分の幅を求めて中央に置く
		const float buttonWidth = ImGui::GetFrameHeight() * 1.5f;
		const float groupWidth = buttonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));
		// 再生 / 停止
		if (IconButton("##Play", playing ? ToolbarIcon::Stop : ToolbarIcon::Play, playing, playing ? "Stop (Ctrl+P)" : "Play (Ctrl+P)")) {
			gameScene->SetIsPlaying(!playing);
		}
		ImGui::SameLine();
		// 一時停止と 1 フレーム送りは PLAY 中だけ
		ImGui::BeginDisabled(!playing);
		if (IconButton("##Pause", ToolbarIcon::Pause, gameScene->IsEditorPaused(), "Pause (Ctrl+Shift+P)")) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		ImGui::SameLine();
		if (IconButton("##Step", ToolbarIcon::Step, false, "Step one frame (Ctrl+Alt+P)")) {
			gameScene->SetEditorPaused(true);
			gameScene->StepEditorFrame();
		}
		ImGui::EndDisabled();

		// 右: Maximize On Play とレイアウト
		// 右端に置くための幅を求める
		const char* layoutLabel = "Reset Layout";
		const float rightWidth = ImGui::CalcTextSize("Maximize On Play").x + ImGui::CalcTextSize(layoutLabel).x + ImGui::GetFrameHeight() + 48.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - rightWidth));
		ImGui::Checkbox("Maximize On Play", &s_maximizeOnPlay);
		ImGui::SameLine();
		if (ImGui::Button(layoutLabel)) s_resetLayoutRequested = true;
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// 画面下部のステータスバー
void EditorUI::DrawStatusBar(GameScene* gameScene, float height) {
	// メインのビューポートの下端に、スクロールしない帯として置く
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags)) {
		// 左: 最新のログ (クリックで Console を開く)
		if (!consoleLog.empty()) {
			// 最新のログ (エラーは赤、警告は黄、それ以外は薄く)
			const auto& last = consoleLog.back();
			const ImVec4 color = last.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) :
				last.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (ImGui::Selectable(last.message.c_str(), false, 0, ImVec2(ImGui::GetWindowWidth() * 0.55f, 0))) {
				s_windows.console = true;
				ImGui::SetWindowFocus(kConsoleWindow);
			}
			ImGui::PopStyleColor();
		}
		// 右: エラー/警告数・状態・FPS
		// 右側の文字 (エラー数・警告数・状態・物の数・FPS)
		char right[160];
		const char* state = !gameScene->GetIsPlaying() ? "Edit" : gameScene->IsEditorPaused() ? "Paused" : "Playing";
		snprintf(right, sizeof(right), "%d errors  %d warnings   |   %s   |   %d objects   |   %.0f FPS",
			CountLogs(LogLevel::Error), CountLogs(LogLevel::Warning), state,
			static_cast<int>(gameScene->GetComponents().view<NameComponent>().size()), ImGui::GetIO().Framerate);
		// 右寄せで書く
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - 12.0f));
		ImGui::TextDisabled("%s", right);
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// 選択中のエンティティを Scene カメラの前方 10 の位置へ移す (Undo 対応)
void EditorUI::MoveSelectedToView(GameScene* scene) {
	// 選択中の物 (無いか位置が無ければ何もしない)
	auto selected = scene->GetSelectedEntity();
	auto& reg = scene->GetComponents();
	if (selected == entt::null || !reg.valid(selected) || !reg.all_of<TransformComponent>(selected)) return;
	// エディタのカメラのワールド行列 (ビュー行列の逆行列)
	const auto& camera = scene->GetEditorCamera();
	DirectX::XMMATRIX world = DirectX::XMMatrixInverse(nullptr, camera.View());
	// カメラの位置と前方向
	DirectX::XMFLOAT3 position, forward;
	DirectX::XMStoreFloat3(&position, world.r[3]);
	DirectX::XMStoreFloat3(&forward, DirectX::XMVector3Normalize(world.r[2]));
	// 移す前の位置と、移す先
	auto& tc = reg.get<TransformComponent>(selected);
	const DirectX::XMFLOAT3 before = tc.translate;
	const DirectX::XMFLOAT3 after{position.x + forward.x * 10.0f, position.y + forward.y * 10.0f, position.z + forward.z * 10.0f};
	tc.translate = after;
	// Undo / Redo を登録する (前の位置に戻す / 移した位置にする)
	PushUndo({"Move To View",
		[scene, selected, before]() { auto& r = scene->GetComponents(); if (r.valid(selected) && r.all_of<TransformComponent>(selected)) r.get<TransformComponent>(selected).translate = before; },
		[scene, selected, after]() { auto& r = scene->GetComponents(); if (r.valid(selected) && r.all_of<TransformComponent>(selected)) r.get<TransformComponent>(selected).translate = after; }});
}

// キーボードショートカット (文字入力中は無効)
void EditorUI::HandleShortcuts(GameScene* gameScene) {
	ImGuiIO& io = ImGui::GetIO();
	// 文字入力中は何もしない
	if (io.WantTextInput) return;
	// ファイル選択の絞り込み
	static const char* kSceneFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	// Play / Pause / Step は Unity と同じく Ctrl+P 系 (P 単体はゲームの操作キーと衝突するため廃止)
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
		// Ctrl+Alt+P: 1 フレーム送り / Ctrl+Shift+P: 一時停止 / Ctrl+P: 再生と停止
		if (io.KeyAlt) {
			if (playing) { gameScene->SetEditorPaused(true); gameScene->StepEditorFrame(); }
		} else if (io.KeyShift) {
			if (playing) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		} else {
			gameScene->SetIsPlaying(!playing);
		}
		return;
	}
	// Ctrl+1 / Ctrl+2 で Scene / Game を切り替える
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1, false)) RequestView(GameScene::EditorView::Scene);
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_2, false)) RequestView(GameScene::EditorView::Game);
	if (playing) return; // ここから下は編集操作 (PLAY 中はゲームの入力と衝突する)

	// 編集用 (PLAY 中は効かない): 保存・Undo・新規・開く・コピー/貼り付け/複製・Move To View、W/E/R のツール切り替え
	if (io.KeyCtrl) {
		// Ctrl+S: 上書き保存 (Shift 付きで名前を付けて保存)
		if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			if (io.KeyShift) {
				std::string path = SaveFileDialog(kSceneFilter, "json");
				if (!path.empty()) SaveScene(gameScene, path);
			} else {
				SaveScene(gameScene, currentScenePath);
			}
		}
		// Ctrl+Z / Ctrl+Y: Undo / Redo
		if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
		if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
		// Ctrl+N: 新しいシーン
		if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
			gameScene->ClearScene();
			gameScene->SelectEntity(entt::null);
			currentScenePath = Data::MainStagePath();
		}
		// Ctrl+O: 開く (Shift 付きで追加)
		if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) {
				if (io.KeyShift) AddScene(gameScene, path);
				else LoadScene(gameScene, path);
			}
		}
		// Ctrl+C / V / D: コピー・貼り付け・複製、Ctrl+Alt+F: カメラの前へ移す
		if (ImGui::IsKeyPressed(ImGuiKey_C, false)) ExecuteCopy(gameScene);
		if (ImGui::IsKeyPressed(ImGuiKey_V, false)) ExecutePaste(gameScene);
		if (ImGui::IsKeyPressed(ImGuiKey_D, false)) ExecuteDuplicate(gameScene);
		if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false)) MoveSelectedToView(gameScene);
	} else if (!s_sceneCameraNavigating) {
		// W / E / R: 移動・回転・拡大のツール (Scene カメラの操作中は除く)
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmo_.SetMode(GizmoMode::Translate);
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmo_.SetMode(GizmoMode::Rotate);
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmo_.SetMode(GizmoMode::Scale);
	}
}

// 毎フレームのエディタ描画 (GameScene 用)
void EditorUI::Show(Engine::Renderer* renderer, GameScene* gameScene) {
	// エディタの経過時間を進める
	globalTime += ImGui::GetIO().DeltaTime;
	// PLAY 中か、PLAY 中ならエディタ全体を色付けする
	const bool playing = gameScene->GetIsPlaying();
	const int tintColors = PushPlayModeTint(playing, gameScene->IsEditorPaused());

	// PLAY 中 (Game ビュー表示時) は ImGui のカーソルを消す
	// スライムの調合室を開いているか
	const bool slimeLabOpen = gameScene->GetChronoSystem() && gameScene->GetChronoSystem()->SlimeLabOpen();
	if (playing && !gameScene->IsPaused() && !gameScene->IsEditorPaused() && !gameScene->IsResultShowing() && !slimeLabOpen &&
		gameScene->GetEditorView() == GameScene::EditorView::Game) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
	} else {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	}

	// ショートカットキーの処理
	HandleShortcuts(gameScene);
	HandleSceneShortcuts();
	// GameScene は自前でマウス座標を Game ビューへ換算する (ctx の override) ため、Input の換算は使わない
	if (auto* input = Engine::Input::GetInstance()) input->ClearPointerViewport();

	// メインメニュー・ツールバー・ステータスバー
	if (ImGui::BeginMainMenuBar()) {
		DrawMainMenu(renderer, gameScene);
		ImGui::EndMainMenuBar();
	}
	DrawToolbar(gameScene, ImGui::GetFrameHeight() + 8.0f);
	DrawStatusBar(gameScene, ImGui::GetFrameHeight() + 2.0f);

	// 作業領域いっぱいの、枠もタイトルも無い親ウィンドウを作る
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	const ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::Begin("EditorMain", nullptr, mainFlags);
	ImGui::PopStyleVar(3);

	// ドックスペースの ID
	const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
	// 初回 (新しいウィンドウ構成の設定がまだ無い) か、Reset Layout が押されたら既定のレイアウトを組む
	if (s_resetLayoutRequested || ImGui::FindWindowSettingsByID(ImHashStr(kInspectorWindow)) == nullptr) {
		// ウィンドウの表示状態を初期値に戻し、既定の配置を組む
		s_windows = EditorWindows{};
		for (auto& ext : Extensions()) ext.open = true;
		BuildDefaultLayout(dockspaceId, viewport->WorkSize, Extensions());
		s_resetLayoutRequested = false;
	}
	// ドックスペースを置く
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	// 前面に出す予約があり、ウィンドウができていれば前面に出す
	if (s_pendingFocusWindow && ImGui::FindWindowByName(s_pendingFocusWindow)) {
		ImGui::SetWindowFocus(s_pendingFocusWindow);
		s_pendingFocusWindow = nullptr;
	}

	// Maximize On Play: PLAY 中は Game ビューだけを作業領域いっぱいに表示する
	const bool maximized = playing && s_maximizeOnPlay;
	// 最大化していないときだけ各ウィンドウを描く
	if (!maximized) {
		// Hierarchy と Console
		if (s_windows.hierarchy) ShowHierarchy(gameScene);
		if (s_windows.console) ShowConsole();
		// Inspector (PLAY 中は、停止すると元に戻ることを知らせる)
		if (s_windows.inspector) {
			if (ImGui::Begin(kInspectorWindow, &s_windows.inspector)) {
				if (playing) {
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Play mode: changes are reverted on Stop");
					ImGui::Separator();
				}
				ShowInspector(gameScene);
			}
			ImGui::End();
		}
		// Animation・Lighting・Particle Editor
		if (s_windows.animation) {
			if (ImGui::Begin(kAnimationWindow, &s_windows.animation)) ShowAnimationWindow(renderer, gameScene);
			ImGui::End();
		}
		if (s_windows.lighting) {
			if (ImGui::Begin(kLightingWindow, &s_windows.lighting)) ShowSceneSettings(renderer);
			ImGui::End();
		}
		if (s_windows.particleEditor) {
			if (ImGui::Begin(kParticleWindow, &s_windows.particleEditor)) gameScene->GetParticleEditor().DrawUI();
			ImGui::End();
		}
		// Fluid Sandbox を開く要求があれば開いて前面に出す
		if (fluidSandboxOpen) {
			s_windows.fluidSandbox = true;
			ImGui::SetNextWindowFocus();
			fluidSandboxOpen = false;
		}
		if (s_windows.fluidSandbox) {
			if (ImGui::Begin(kFluidSandboxWindow, &s_windows.fluidSandbox)) ShowFluidSandbox(renderer, gameScene);
			ImGui::End();
		}
		// Project
		if (s_windows.project) ShowProject(renderer, gameScene);
		// 拡張のウィンドウ
		for (auto& ext : Extensions()) {
			if (!ext.open || !ext.drawWindow) continue;
			if (ImGui::Begin(ext.windowName.c_str(), &ext.open)) ext.drawWindow(gameScene);
			ImGui::End();
		}
	}
	// Scene / Game のビュー
	ShowViewports(renderer, gameScene, maximized);

	ImGui::End(); // EditorMain

	// 流体のプロファイラ (開いていて一時停止していなければ計測する)
	if (s_windows.fluidProfiler) ShowFluidProfiler(renderer);
	if (renderer) renderer->SetFluidProfilerEnabled(s_windows.fluidProfiler && !s_fluidProfilerPaused);
	// PLAY 中の色付けを戻す
	ImGui::PopStyleColor(tintColors);
}

// 流体の GPU 処理時間を表示するプロファイラ
void EditorUI::ShowFluidProfiler(Engine::Renderer* renderer) {
	// 最初の大きさ
	ImGui::SetNextWindowSize(ImVec2(540, 420), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Fluid Profiler##Development", &s_windows.fluidProfiler)) {
		// 計測を止めるか
		ImGui::Checkbox("Pause capture", &s_fluidProfilerPaused);
		// 流体の計測結果
		const auto& profile = renderer->GetFluidProfileStats();
		if (s_fluidProfilerPaused) ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.32f, 1.0f),
			"Capture paused: the values below are from an earlier frame.");
		// 計測できない / まだ結果が無い / 結果の表示
		if (!profile.available) {
			ImGui::TextUnformatted("GPU timestamp queries are unavailable on this device.");
		} else if (!profile.valid) {
			ImGui::TextUnformatted("Waiting for a completed GPU frame...");
		} else {
			// 流体の合計時間
			ImGui::Text("Fluid GPU total: %.3f ms", profile.totalMs);
			ImGui::SameLine();
			ImGui::TextDisabled("(%u frames old)", profile.framesSinceSample);
			// ゲームの描画全体と、それ以外の時間
			if (profile.sampledStages[Engine::Renderer::SceneRender]) {
				const float sceneMs = profile.lastMs[Engine::Renderer::SceneRender];
				ImGui::Text("Game render GPU: %.3f ms   Other game passes: %.3f ms",
					sceneMs, (std::max)(0.0f, sceneMs - profile.totalMs));
			}
			// 粒子数・分割数・時間刻み・格子の作り直しの回数・ボリュームの大きさ・FPS・CPU の時間
			ImGui::Text("Particles: %u   Simulation substeps: %u   Simulated dt: %.2f ms",
				profile.particleSlots, profile.substeps, profile.simulatedDtMs);
			ImGui::Text("Spatial grid rebuilds: %u", profile.simulatedDtMs > 0.0f ? 1 + 4 * profile.substeps : 1);
			ImGui::Text("Volume: 176 x 64 x 176 at 0.56 units   LOD: 46.0 - 48.5 units");
			ImGui::Text("Frame rate: %.1f FPS (whole editor)", ImGui::GetIO().Framerate);
			ImGui::Text("CPU command recording: simulation %.3f ms, volume %.3f ms",
				profile.cpuSimulationMs, profile.cpuVolumeMs);
			ImGui::Separator();
			// 処理ごとの直近・平均・最大の時間
			ImGui::TextUnformatted("GPU pass                                Last       Mean       Peak (ms)");
			static const char* names[Engine::Renderer::kFluidProfileStageCount] = {
				"Particle simulation", "Shape reconstruction", "Density splat + resolve",
				"Filtering + occupancy", "Refraction + raymarch", "Distant impostors", "Fluid shadow", "Whole game render",
				"Scenery shadow", "RT acceleration", "RT lighting rays", "RT radiance filter",
				"DLSS depth + motion inputs", "DLSS super resolution", "RT HDR composite"
			};
			// 計測していない処理は inactive と出す
			for (uint32_t i = 0; i < Engine::Renderer::kFluidProfileStageCount; ++i) {
				if (!profile.sampledStages[i]) {
					ImGui::Text("%-30s    (inactive)", names[i]);
					continue;
				}
				ImGui::Text("%-30s %8.3f   %8.3f   %8.3f", names[i],
					profile.lastMs[i], profile.averageMs[i], profile.peakMs[i]);
			}
			ImGui::Separator();
			// 合計時間の折れ線グラフ
			ImGui::PlotLines("Fluid GPU total", profile.totalHistory.data(),
				static_cast<int>(profile.historyCount), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 100));
			ImGui::TextDisabled("Mean / peak: last %u completed samples. GPU time excludes CPU waits and UI.", profile.historyCount);
			ImGui::TextDisabled("Game render includes fluid; Other game passes is the difference. Editor UI and Present are excluded.");
		}
	}
	ImGui::End();
}

// ====== エディタ内で表示するシーン (タイトル / セレクト / デモ) ======
// App から呼ばれる: シーンに応じて、エディタで包むか各シーンの DrawEditor をそのまま呼ぶかを決める
void EditorUI::PresentScene(Engine::IScene& scene, const std::string& sceneName) {
	// プレイヤーが通るシーンとデモだけをエディタで包む。検証用シーンは独自の処理をそのまま使う
	// 包む対象のシーンなら、エディタで包んで表示する
	for (const auto& hosted : EditorConfig().hostedScenes) {
		if (sceneName == hosted) {
			ShowSceneHost(scene, sceneName);
			return;
		}
	}
	// それ以外は入力の換算を外し、シーン自身のエディタ描画を呼ぶ
	if (auto* input = Engine::Input::GetInstance()) input->ClearPointerViewport();
	scene.DrawEditor();
}

// GameScene 以外のシーン用のエディタ (メニュー・ツールバー・ステータスバー・Game ビュー)
void EditorUI::ShowSceneHost(Engine::IScene& scene, const std::string& sceneName) {
	// エディタの経過時間を進める
	globalTime += ImGui::GetIO().DeltaTime;
	// 描画の窓口
	auto* renderer = Engine::Renderer::GetInstance();
	// カーソルは常に矢印
	ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	// Ctrl+R で読み込み直す
	HandleSceneShortcuts();

	// メインメニュー: Scenes と Window (このシーンで使える欄だけ)
	if (ImGui::BeginMainMenuBar()) {
		DrawSceneLaunchMenu();
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem(kProjectWindow, nullptr, &s_windows.project);
			ImGui::MenuItem(kConsoleWindow, nullptr, &s_windows.console);
			ImGui::MenuItem(kLightingWindow, nullptr, &s_windows.lighting);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout")) s_resetLayoutRequested = true;
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	// ツールバー: シーン切り替え・再読み込み・最大化
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	// ツールバーの高さ
	const float toolbarHeight = ImGui::GetFrameHeight() + 8.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, toolbarHeight, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
		DrawSceneLaunchCombo();
		ImGui::SameLine();
		// 今のシーンを読み込み直すボタン
		if (ImGui::Button("Restart")) LaunchScene(sceneName.c_str(), Engine::SceneManager::GetInstance()->CurrentParameters().stagePath);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reload this scene (Ctrl+R)");
		ImGui::SameLine(0, 16);
		// 動いているシーン名
		ImGui::TextDisabled("Running: %s", sceneName.c_str());
		// 右端に Maximize のチェック
		const float rightWidth = ImGui::CalcTextSize("Maximize").x + ImGui::GetFrameHeight() + 24.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - rightWidth));
		ImGui::Checkbox("Maximize", &s_maximizeOnPlay);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// ステータスバー: 最新のログと、エラー数・警告数・シーン名・FPS
	const float statusHeight = ImGui::GetFrameHeight() + 2.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, statusHeight, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
		if (!consoleLog.empty()) ImGui::TextDisabled("%s", consoleLog.back().message.c_str());
		char right[128];
		snprintf(right, sizeof(right), "%d errors  %d warnings   |   %s   |   %.0f FPS",
			CountLogs(LogLevel::Error), CountLogs(LogLevel::Warning), sceneName.c_str(), ImGui::GetIO().Framerate);
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - 12.0f));
		ImGui::TextDisabled("%s", right);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// 作業領域いっぱいの、枠もタイトルも無い親ウィンドウを作る
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("EditorMain", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
	ImGui::PopStyleVar(3);
	// ドックスペース (初回か Reset Layout のときは既定の配置を組む)
	const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
	if (s_resetLayoutRequested || ImGui::FindWindowSettingsByID(ImHashStr(kInspectorWindow)) == nullptr) {
		s_windows = EditorWindows{};
		for (auto& ext : Extensions()) ext.open = true;
		BuildDefaultLayout(dockspaceId, viewport->WorkSize, Extensions());
		s_resetLayoutRequested = false;
	}
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	// シーンが変わったら Game タブを前に出す (Scene ビューはゲームシーン専用)
	static std::string lastScene;
	static bool focusGame = false;
	if (lastScene != sceneName) {
		lastScene = sceneName;
		focusGame = true;
	}
	if (focusGame && ImGui::FindWindowByName(kGameWindow)) {
		ImGui::SetWindowFocus(kGameWindow);
		focusGame = false;
	}

	// 最大化していないときだけ各ウィンドウを出す
	if (!s_maximizeOnPlay) {
		// 既定レイアウトのドックが空にならないよう、ゲームシーン専用の欄も説明付きで出しておく
		if (ImGui::Begin(kSceneWindow)) DrawCodeSceneNotice("The Scene view");
		ImGui::End();
		if (s_windows.hierarchy) {
			if (ImGui::Begin(kHierarchyWindow, &s_windows.hierarchy)) DrawCodeSceneNotice("The Hierarchy");
			ImGui::End();
		}
		if (s_windows.inspector) {
			if (ImGui::Begin(kInspectorWindow, &s_windows.inspector)) DrawCodeSceneNotice("The Inspector");
			ImGui::End();
		}
		if (s_windows.animation) {
			if (ImGui::Begin(kAnimationWindow, &s_windows.animation)) DrawCodeSceneNotice("The Animation list");
			ImGui::End();
		}
		if (s_windows.lighting && renderer) {
			if (ImGui::Begin(kLightingWindow, &s_windows.lighting)) ShowSceneSettings(renderer);
			ImGui::End();
		}
		// Console と Project (Project はシーン無しで使う)
		if (s_windows.console) ShowConsole();
		if (s_windows.project) ShowProject(renderer, nullptr);
		// 拡張のウィンドウ
		for (auto& ext : Extensions()) {
			if (!ext.open || !ext.drawWindow) continue;
			if (ImGui::Begin(ext.windowName.c_str(), &ext.open)) ext.drawWindow(nullptr);
			ImGui::End();
		}
	}

	// Game ビュー: シーンの最終画像を表示し、マウスをその範囲で換算してゲーム UI を操作できるようにする
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool gameVisible;
	// 最大化なら作業領域全体に、そうでなければ Game ウィンドウに出す
	if (s_maximizeOnPlay) {
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		gameVisible = ImGui::Begin("##MaximizedHostGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
	} else {
		gameVisible = ImGui::Begin(kGameWindow);
	}
	// 見えていれば、縦横比と Stats の切り替えを上に並べる
	if (gameVisible && renderer) {
		// Begin() places the cursor below the title/docking tabs. Keep that
		// content origin instead of positioning relative to the window border.
		const ImVec2 contentStart = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(contentStart.x + 6.0f, contentStart.y + 4.0f));
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::BeginCombo("##HostAspect", aspects[currentAspect])) {
			for (int n = 0; n < IM_ARRAYSIZE(aspects); n++) {
				if (ImGui::Selectable(aspects[n], currentAspect == n)) currentAspect = n;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Stats", &s_showStats);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

		// 使える領域
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float availW = (std::max)(avail.x, 1.0f), availH = (std::max)(avail.y, 1.0f);
		// 縦横比 (既定は内部解像度の比)
		float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
		if (currentAspect > 0 && aspectValues[currentAspect] > 0.0f) aspect = aspectValues[currentAspect];
		// 領域に収まる大きさ
		const float w = availW / availH > aspect ? availH * aspect : availW;
		const float h = availW / availH > aspect ? availH : availW / aspect;
		// 中央に置いて画像を表示する
		const ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(cursor.x + (availW - w) * 0.5f, cursor.y + (availH - h) * 0.5f));
		ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(w, h));
		// 画像の範囲を覚える
		gameImageMin = ImGui::GetItemRectMin();
		gameImageMax = ImGui::GetItemRectMax();
		renderer->SetEditorLoadingViewport(gameImageMin.x, gameImageMin.y, w, h);
		// ImGui の座標はクライアント座標なので、そのまま Input の換算範囲に使える
		if (auto* input = Engine::Input::GetInstance()) input->SetPointerViewport(gameImageMin.x, gameImageMin.y, w, h);
		// Stats: FPS と表示の大きさを左上に重ねる
		if (s_showStats) {
			char stats[128];
			snprintf(stats, sizeof(stats), "%.1f FPS (%.2f ms)\nView: %.0f x %.0f", ImGui::GetIO().Framerate,
				1000.0f / (std::max)(ImGui::GetIO().Framerate, 1.0f), w, h);
			// 半透明の黒い板の上に文字を書く
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 at(gameImageMin.x + 8.0f, gameImageMin.y + 8.0f), size = ImGui::CalcTextSize(stats);
			draw->AddRectFilled(at, ImVec2(at.x + size.x + 16.0f, at.y + size.y + 12.0f), IM_COL32(0, 0, 0, 170), 4.0f);
			draw->AddText(ImVec2(at.x + 8.0f, at.y + 6.0f), IM_COL32(230, 230, 230, 255), stats);
		}
	}
	// Game ウィンドウを閉じる
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::End(); // EditorMain

	// シーン固有のツール (Assignment の GPU Particle Editor など)
	scene.DrawEditor();
}

} // namespace Game
