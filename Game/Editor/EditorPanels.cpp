// EditorPanels.cpp
// 小さなウィンドウ: Project (アセットの一覧)、Lighting (ポストエフェクトと背景)、Console (ログ)、
// Animation (シーン内のアニメーションの一覧)。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// Project ウィンドウ: Resources 以下をアイコンで表示する
void EditorUI::ShowProject([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) {
	// ウィンドウを開く
	ImGui::Begin(kProjectWindow, &s_windows.project);
	// currentDir はプロジェクト相対の UTF-8 パス ("Resources/Models")。
	// 以前は作業ディレクトリ基準で列挙しており、exe の場所によってはフォルダが無く例外で落ちていた
	static std::string currentDir = "Resources";
	static char searchBuf[128] = "";
	static std::map<std::string, uint32_t> s_thumbnails; // 読み込んだサムネイルのテクスチャ (パス → ハンドル)

	// 上部のバー (戻る・現在のフォルダ・検索)
	ImGui::BeginDisabled(currentDir == "Resources");
	// Back: 1 つ上のフォルダーへ (Resources より上へは行かない)
	if (ImGui::Button("Back")) {
		size_t slash = currentDir.find_last_of('/');
		currentDir = slash == std::string::npos ? "Resources" : currentDir.substr(0, slash);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	// 今のフォルダー
	ImGui::TextUnformatted(currentDir.c_str());
	// 右端に検索欄と Clear ボタンを置く
	const float searchWidth = 180.0f;
	const float clearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - searchWidth - clearWidth - ImGui::GetStyle().ItemSpacing.x));
	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##Search", "Search Assets...", searchBuf, sizeof(searchBuf));
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter files in current directory");
	ImGui::SameLine();
	if (ImGui::Button("Clear##ProjectSearch")) searchBuf[0] = '\0';

	ImGui::Separator();

	// アイコンを並べる格子の設定
	// セルの大きさと、1 行に並べる数
	float padding = 16.0f;
	float thumbnailSize = 64.0f;
	float cellSize = thumbnailSize + padding;
	float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = (int)(panelWidth / cellSize);
	if (columnCount < 1) columnCount = 1;

	// フォルダーの中身 (名前とフォルダーか)
	struct Entry { std::string name; bool directory; };
	std::vector<Entry> entries;
	std::error_code ec;
	// 今のフォルダーの中を列挙する (例外を出さない版)
	const std::wstring absDir = Engine::PathUtils::FromUTF8(GetUnifiedProjectPath(currentDir));
	for (fs::directory_iterator it(absDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
		std::error_code typeError;
		entries.push_back({Engine::PathUtils::ToUTF8(it->path().filename().wstring()), it->is_directory(typeError)});
	}
	// 開けなければ、その旨と Resources へ戻るボタンを出す
	if (ec && entries.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Cannot open %s", currentDir.c_str());
		if (ImGui::Button("Return to Resources")) currentDir = "Resources";
	}
	// フォルダを先に、名前順に並べる
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if (a.directory != b.directory) return a.directory;
		return ToLower(a.name) < ToLower(b.name);
	});

	// スクロールする領域に列で並べる
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::Columns(columnCount, 0, false);

	// 検索文字 (小文字) と、次に開くフォルダー
	const std::string searchStr = ToLower(searchBuf);
	std::string nextDir;
	int i = 0; // 同じ名前のファイルがあっても ID が衝突しないよう、番号で PushID する
	for (const auto& entry : entries) {
		// 検索に合わないものは飛ばす
		if (!searchStr.empty() && ToLower(entry.name).find(searchStr) == std::string::npos)
			continue;

		ImGui::PushID(i++);
		// プロジェクト相対のパスと拡張子
		const std::string relPath = currentDir + "/" + entry.name;
		const std::string ext = entry.directory ? std::string() : ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(entry.name)).extension().wstring()));

		// アイコンを種類で選ぶ (フォルダー・モデル・プレハブ・音・スクリプト)
		uint32_t icon = s_icons.file;
		if (entry.directory) {
			icon = s_icons.folder;
		} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") icon = s_icons.model;
		else if (ext == ".prefab") icon = s_icons.prefab;
		else if (ext == ".wav" || ext == ".mp3") icon = s_icons.audio;
		else if (ext == ".h" || ext == ".cpp" || ext == ".json") icon = s_icons.script;
		else if (ext == ".png" || ext == ".jpg") {
			// 画像はサムネイルを表示する
			auto found = s_thumbnails.find(relPath);
			if (found == s_thumbnails.end()) found = s_thumbnails.emplace(relPath, Engine::Renderer::GetInstance()->LoadTexture2D(relPath, false)).first;
			icon = found->second;
		}

		// 背景の無いアイコンのボタン
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		const bool clicked = ImGui::ImageButton("##asset", (ImTextureID)renderer->GetTextureSrvGpu(icon).ptr, ImVec2(thumbnailSize, thumbnailSize));
		ImGui::PopStyleColor();
		// フォルダーをクリックしたら、描き終わってから開く
		if (clicked && entry.directory) nextDir = relPath;
		// パスと操作の案内をツールチップで出す
		if (ImGui::IsItemHovered()) {
			const char* hint = (ext == ".json" || ext == ".prefab") ? "\n(double-click to open / instantiate)" : "";
			ImGui::SetTooltip("%s%s", relPath.c_str(), hint);
		}
		// ダブルクリック: フォルダを開く / シーンを読み込む / プレハブを配置する
		if (!entry.directory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (ext == ".json" && currentDir.rfind("Resources/Scenes", 0) == 0 && scene && !scene->GetIsPlaying()) {
				LoadScene(scene, relPath);
			} else if (ext == ".prefab" && scene && !scene->GetIsPlaying()) {
				auto created = LoadPrefab(scene, relPath);
				if (!created.empty()) scene->SelectEntity(created.front());
			}
		}

		// ドラッグ元: プロジェクト相対のパスを運ぶ
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("PROJECT_ASSET", relPath.c_str(), relPath.size() + 1);
			ImGui::Text("%s", entry.name.c_str());
			ImGui::EndDragDropSource();
		}

		// 名前
		ImGui::TextWrapped("%s", entry.name.c_str());
		ImGui::NextColumn();
		ImGui::PopID();
	}

	ImGui::Columns(1);
	ImGui::EndChild();
	// 次のフォルダーへ移る
	if (!nextDir.empty()) currentDir = nextDir;
	ImGui::End();
}

// Lighting ウィンドウ: ポストエフェクトの種類とパラメータ、背景のキューブマップ
void EditorUI::ShowSceneSettings(Engine::Renderer* renderer) {
	// ウィンドウの Begin は呼び出し側で行う
	// 今のパラメータと、変更されたか
	auto pp = renderer->GetPostProcessParams();
	bool ch = false;

	// ポストエフェクトの種類 (常に表示)
	static const char* ppTypes[] = { "None", "Default(CRT)", "Rich", "Anime", "Grayscale", "Smoothing", "GaussianFilter", "OutlinePost", "RadialBlur", "Random", "PaperFrame" };
	static int currentPP = 1; // デフォルトはCRT
	// シーン読み込みで無効化された場合などに表示が食い違わないよう、実際の状態へ合わせる
	if (!renderer->GetPostProcessEnabled()) currentPP = 0;
	else if (currentPP == 0) currentPP = 1;
	// 種類の選択 (None は無効、Default は CRT、それ以外は名前で設定する)
	if (ImGui::BeginCombo("PostEffect", ppTypes[currentPP])) {
		for (int n = 0; n < IM_ARRAYSIZE(ppTypes); n++) {
			bool isSelected = (currentPP == n);
			if (ImGui::Selectable(ppTypes[n], isSelected)) {
				currentPP = n;
				if (currentPP == 0) {
					renderer->SetPostProcessEnabled(false);
				} else if (currentPP == 1) {
					renderer->SetPostEffect("");
					renderer->SetPostProcessEnabled(true);
				} else {
					renderer->SetPostEffect(ppTypes[currentPP]);
				}
			}
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// パラメータ（ポストエフェクト有効時のみ表示）
	if (currentPP > 0) {
		ImGui::Separator();
		// 周辺減光・ノイズ・歪み・色ずれ・走査線・SAN 値
		ch |= ImGui::DragFloat("Vignette", &pp.vignette, 0.01f);
		ch |= ImGui::DragFloat("Noise", &pp.noiseStrength, 0.01f);
		ch |= ImGui::DragFloat("Distortion", &pp.distortion, 0.001f);
		ch |= ImGui::DragFloat("ChromaShift", &pp.chromaShift, 0.0001f);
		ch |= ImGui::DragFloat("Scanline", &pp.scanline, 0.01f);
		ch |= ImGui::DragFloat("SAN", &pp.san, 0.01f, 0.0f, 1.0f);
	}
	// 変わったら反映する
	if (ch) renderer->SetPostProcessParams(pp);

	// 背景にキューブマップを使うか
	bool useCubemapBg = renderer->GetUseCubemapBackground();
	if (ImGui::Checkbox("Use Cubemap Background", &useCubemapBg)) {
		renderer->SetUseCubemapBackground(useCubemapBg);
	}
}

// Console ウィンドウ
void EditorUI::ShowConsole() {
	// ウィンドウを開く
	ImGui::Begin(kConsoleWindow, &s_windows.console);
	// 表示する種類・自動スクロール・まとめ表示と、絞り込みの文字
	static bool showInfo = true, showWarning = true, showError = true, autoScroll = true, collapse = false;
	static char filter[128] = "";
	// Clear: ログを消す / Copy: すべてのログをクリップボードへ
	if (ImGui::Button("Clear##Console")) consoleLog.clear();
	ImGui::SameLine();
	if (ImGui::Button("Copy")) {
		std::string all;
		for (const auto& log : consoleLog) all += log.message + "\n";
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::SameLine();
	ImGui::Checkbox("Collapse", &collapse);
	ImGui::SameLine();
	// 種類ごとの表示の切り替え (件数付き)
	ImGui::Checkbox(("Info " + std::to_string(CountLogs(LogLevel::Info))).c_str(), &showInfo);
	ImGui::SameLine();
	ImGui::Checkbox(("Warn " + std::to_string(CountLogs(LogLevel::Warning))).c_str(), &showWarning);
	ImGui::SameLine();
	ImGui::Checkbox(("Error " + std::to_string(CountLogs(LogLevel::Error))).c_str(), &showError);
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &autoScroll);
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##ConsoleFilter", "Filter...", filter, sizeof(filter));
	ImGui::Separator();

	// 絞り込みの文字 (小文字)
	const std::string needle = ToLower(filter);
	ImGui::BeginChild("ConsoleLines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	// Collapse: 同じ内容のログを1行にまとめて件数を表示する (Unity と同じ)
	std::map<std::string, int> repeats;
	if (collapse) for (const auto& log : consoleLog) ++repeats[log.message];
	std::map<std::string, bool> shown;
	// ログを順に表示する
	for (const auto& log : consoleLog) {
		// 表示しない種類と、絞り込みに合わないものは飛ばす
		if ((log.level == LogLevel::Info && !showInfo) || (log.level == LogLevel::Warning && !showWarning) || (log.level == LogLevel::Error && !showError)) continue;
		if (!needle.empty() && ToLower(log.message).find(needle) == std::string::npos) continue;
		// まとめ表示なら、同じ内容は最初の 1 回だけ出す
		if (collapse) {
			if (shown[log.message]) continue;
			shown[log.message] = true;
		}
		// エラーは赤、警告は黄
		ImVec4 color = log.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) :
			log.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		// 時刻と内容
		ImGui::TextDisabled("%7.2f", log.time);
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(log.message.c_str());
		ImGui::PopStyleColor();
		if (collapse && repeats[log.message] > 1) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d)", repeats[log.message]);
		}
	}
	// 一番下にいれば、新しいログに合わせてスクロールする
	if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::End();
}

// シーン内のアニメーション (スキニング Animator / キーフレーム Motion) を一覧し、まとめて再生・停止・選択する
void EditorUI::ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// このシーンの ECS と、表示した数
	auto& reg = scene->GetComponents();
	int count = 0;
	// すべて再生 / すべて停止
	if (ImGui::Button("Play all")) {
		for (auto e : reg.view<AnimatorComponent>()) reg.get<AnimatorComponent>(e).isPlaying = true;
		for (auto e : reg.view<MotionComponent>()) reg.get<MotionComponent>(e).isPlaying = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop all")) {
		for (auto e : reg.view<AnimatorComponent>()) reg.get<AnimatorComponent>(e).isPlaying = false;
		for (auto e : reg.view<MotionComponent>()) reg.get<MotionComponent>(e).isPlaying = false;
	}
	ImGui::Separator();
	// 表の列: 物・クリップ・再生・速さ
	if (ImGui::BeginTable("AnimationList", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("Object");
		ImGui::TableSetupColumn("Clip");
		ImGui::TableSetupColumn("Play", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableHeadersRow();
		// 名前の列 (クリックで選ぶ)
		auto drawName = [&](entt::entity e) {
			ImGui::TableNextColumn();
			const char* name = reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name.c_str() : "Object";
			if (ImGui::Selectable(name, scene->GetSelectedEntity() == e, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) scene->SelectEntity(e);
		};
		// アニメーター: モデルのクリップから選ぶ
		for (auto e : reg.view<AnimatorComponent>()) {
			auto& anim = reg.get<AnimatorComponent>(e);
			ImGui::PushID(static_cast<int>(entt::to_integral(e)));
			ImGui::TableNextRow();
			drawName(e);
			ImGui::TableNextColumn();
			// メッシュのモデル
			const Engine::Model* model = nullptr;
			if (auto* mr = reg.try_get<MeshRendererComponent>(e); mr && mr->modelHandle != 0 && renderer) model = renderer->GetModel(mr->modelHandle);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (model && !model->GetData().animations.empty()) {
				if (ImGui::BeginCombo("##Clip", anim.currentAnimation.c_str())) {
					for (const auto& a : model->GetData().animations) {
						if (ImGui::Selectable(a.name.c_str(), anim.currentAnimation == a.name)) {
							anim.currentAnimation = a.name;
							anim.time = 0.0f;
						}
					}
					ImGui::EndCombo();
				}
			} else {
				ImGui::TextDisabled("%s", anim.currentAnimation.empty() ? "(no clips)" : anim.currentAnimation.c_str());
			}
			// 再生中か・速さ
			ImGui::TableNextColumn();
			ImGui::Checkbox("##Play", &anim.isPlaying);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::DragFloat("##Speed", &anim.speed, 0.05f, 0.0f, 10.0f, "%.2fx");
			ImGui::PopID();
			++count;
		}
		// モーション: クリップから選ぶ (ID がアニメーターと重ならないようにずらす)
		for (auto e : reg.view<MotionComponent>()) {
			auto& motion = reg.get<MotionComponent>(e);
			ImGui::PushID(static_cast<int>(entt::to_integral(e)) + 0x40000000);
			ImGui::TableNextRow();
			drawName(e);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##MotionClip", motion.activeClip.c_str())) {
				for (auto& [clipName, clip] : motion.clips) {
					if (ImGui::Selectable(clipName.c_str(), motion.activeClip == clipName)) {
						motion.activeClip = clipName;
						motion.currentTime = 0.0f;
						motion.selectedKeyframe = -1;
					}
				}
				ImGui::EndCombo();
			}
			// 再生中か
			ImGui::TableNextColumn();
			ImGui::Checkbox("##Play", &motion.isPlaying);
			ImGui::TableNextColumn();
			ImGui::TextDisabled("motion");
			ImGui::PopID();
			++count;
		}
		ImGui::EndTable();
	}
	if (count == 0) ImGui::TextDisabled("No Animator or Motion components in the scene.");
}

} // namespace Game
