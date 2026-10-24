// EditorHierarchy.cpp
// Hierarchy ウィンドウ (分類・名前・一列での整理、検索、親子付け、名前の変更、削除) と、
// エディタの拡張 (EditorExtension) の登録、生成メニュー。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// ====== ヒエラルキー ======
// 1000件を超えるオブジェクトを一列に並べると探せないため、次の3通りで整理して表示する。
//   Category : コンポーネント構成 (またはゲーム拡張の分類) ごとのフォルダ
//   Name     : 名前の単語を区切りにした自動フォルダ ("Chrono City prop xxx_01" → Chrono City / prop / xxx)
//   Flat     : 登録順 (従来表示)
// いずれも HierarchyComponent の親子関係は子ノードとして表示する。
namespace {

// Hierarchy の整理の仕方 (分類・名前・一列)
enum class HierarchyGrouping { Category, Name, Flat };

// Hierarchy の状態 (検索文字・整理の仕方・選択・削除・名前の変更・親の付け替えの予約)
struct HierarchyState {
	char search[128] = "";
	HierarchyGrouping grouping = HierarchyGrouping::Category;
	entt::entity lastSelected = entt::null;
	bool revealSelected = false;   // ビューポート等で選択が変わったらフォルダを開いてスクロールする
	bool collapseAll = false;
	entt::entity pendingDelete = entt::null;
	entt::entity renameRequest = entt::null;
	entt::entity renaming = entt::null;
	char renameBuffer[256] = "";
	entt::entity reparentChild = entt::null;
	entt::entity reparentTo = entt::null;
	bool reparentRequested = false;
};
HierarchyState s_hierarchy;

// フォルダーの木 (表示名・子フォルダー・中の物・数・選択中の物を含むか)
struct HierarchyFolder {
	std::string label;
	std::map<std::string, HierarchyFolder> folders;
	std::vector<entt::entity> leaves;
	int count = 0;
	bool containsSelected = false;
};

// 名前の末尾の連番 ("_01", " (Copy)") を除いてグループ化キーにする
std::string NameStem(std::string token) {
	// 末尾の " (Copy)" をすべて外す
	while (token.size() > 7 && token.compare(token.size() - 7, 7, " (Copy)") == 0) token.resize(token.size() - 7);
	// 末尾の数字を外し、その前の区切り (_ - .) も外す
	size_t end = token.size();
	while (end > 0 && std::isdigit(static_cast<unsigned char>(token[end - 1]))) --end;
	if (end < token.size() && end > 0 && (token[end - 1] == '_' || token[end - 1] == '-' || token[end - 1] == '.')) --end;
	// 全部数字なら元のまま
	if (end == 0) return token;
	token.resize(end);
	return token;
}

// 名前を空白で単語に分け、最後の単語から連番を外す (フォルダーの道筋に使う)
std::vector<std::string> NameTokens(const std::string& name) {
	std::vector<std::string> tokens;
	std::string current;
	// 空白ごとに区切る
	for (char c : name) {
		if (c == ' ') {
			if (!current.empty()) tokens.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}
	// 最後の単語を足す (空なら Object)
	if (!current.empty()) tokens.push_back(current);
	if (tokens.empty()) tokens.push_back("Object");
	tokens.back() = NameStem(tokens.back());
	return tokens;
}

// 標準の分類: 持っているコンポーネントから UI・ライト・プレイヤー・敵・エフェクトなどに振り分ける
const char* DefaultCategory(SceneComponents& reg, entt::entity e) {
	if (reg.any_of<RectTransformComponent, UIImageComponent, UITextComponent, UIButtonComponent, WorldSpaceUIComponent>(e)) return "UI";
	if (reg.any_of<DirectionalLightComponent, PointLightComponent, SpotLightComponent>(e)) return "Lights";
	if (reg.any_of<PlayerInputComponent, CharacterMovementComponent, CameraTargetComponent>(e)) return "Player";
	if (reg.any_of<EnemyAIComponent>(e)) return "Enemies";
	if (reg.any_of<ParticleEmitterComponent, FluidEmitterComponent>(e)) return "Effects";
	if (reg.any_of<AudioSourceComponent, AudioListenerComponent>(e)) return "Audio";
	if (reg.any_of<ScriptComponent>(e)) return "Scripts";
	if (reg.any_of<MeshRendererComponent>(e)) return "Meshes";
	if (reg.any_of<BoxColliderComponent, GpuMeshColliderComponent>(e)) return "Colliders";
	return "Other";
}

// ancestor が e 自身か、e の親をたどった先にあるか (深さ 256 で打ち切り)
bool IsAncestor(SceneComponents& reg, entt::entity ancestor, entt::entity e) {
	// 親をたどって ancestor を探す
	for (int depth = 0; depth < 256 && reg.valid(e); ++depth) {
		if (e == ancestor) return true;
		auto* hc = reg.try_get<HierarchyComponent>(e);
		if (!hc) return false;
		e = hc->parentId;
	}
	return false;
}

// フォルダ内の件数と、選択中の物を含むかを子フォルダまで集計する
void FinalizeFolder(HierarchyFolder& folder, const std::function<bool(entt::entity)>& isSelectedOrAncestor) {
	// このフォルダーの中の物の数と、選択中の物を含むか
	folder.count = static_cast<int>(folder.leaves.size());
	for (auto e : folder.leaves) folder.containsSelected |= isSelectedOrAncestor(e);
	// 子フォルダーの分も足す
	for (auto& [key, child] : folder.folders) {
		FinalizeFolder(child, isSelectedOrAncestor);
		folder.count += child.count;
		folder.containsSelected |= child.containsSelected;
	}
}

} // namespace

// 拡張の一覧 (関数内の static にして、静的初期化の順序に依存しないようにする)
std::vector<EditorExtension>& EditorUI::Extensions() {
	static std::vector<EditorExtension> extensions;
	return extensions;
}

void EditorUI::RegisterExtension(EditorExtension extension) {
	Extensions().push_back(std::move(extension));
}

// 生成メニュー (空・立方体・球・リング・円柱)
void EditorInternal::DrawCreateEntityMenu(GameScene* scene) {
	// このシーンの ECS と描画の窓口
	auto& registry = scene->GetComponents();
	auto* renderer = Engine::Renderer::GetInstance();
	// 名前を付けて作り、メッシュと白いテクスチャを付けて選ぶ
	auto createMesh = [&](const char* name, uint32_t handle, const std::string& modelPath) {
		auto e = scene->CreateEntity(name);
		auto& mr = registry.emplace<MeshRendererComponent>(e);
		mr.modelPath = modelPath;
		mr.modelHandle = handle;
		mr.texturePath = "Resources/Textures/white1x1.png";
		mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
		scene->SelectEntity(e);
	};
	// 空・立方体・球・リング・円柱
	if (ImGui::MenuItem("Create Empty")) scene->SelectEntity(scene->CreateEntity("Empty"));
	if (ImGui::MenuItem("Create Cube")) createMesh("Cube", renderer->LoadObjMesh("Resources/Models/cube/cube.obj"), "Resources/Models/cube/cube.obj");
	if (ImGui::MenuItem("Create Sphere")) createMesh("Sphere", renderer->LoadObjMesh("Resources/Models/player_ball/ball.obj"), "Resources/Models/player_ball/ball.obj");
	if (ImGui::MenuItem("Create Ring")) createMesh("Ring", renderer->CreateRingMesh(2.0f, 1.0f, 32), "");
	if (ImGui::MenuItem("Create Cylinder")) createMesh("Cylinder", renderer->CreateCylinderMesh(1.0f, 2.0f, 32), "");
}

// Hierarchy ウィンドウ
void EditorUI::ShowHierarchy(GameScene* scene) {
	// ウィンドウを開く (シーンが無ければすぐ閉じる)
	ImGui::Begin(kHierarchyWindow, &s_windows.hierarchy);
	if (!scene) {
		ImGui::End();
		return;
	}
	// このシーンの ECS と Hierarchy の状態
	auto& registry = scene->GetComponents();
	auto& st = s_hierarchy;
	// 選択が変わったら、その物が見えるようにフォルダーを開く
	const entt::entity selected = scene->GetSelectedEntity();
	if (selected != st.lastSelected) {
		st.revealSelected = selected != entt::null;
		st.lastSelected = selected;
	}

	// ツールバー (表示方法・すべて閉じる・生成・検索)
	// 整理の仕方の選択
	static const char* groupingNames[] = {"Category", "Name", "Flat"};
	int grouping = static_cast<int>(st.grouping);
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Category").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::Combo("##Grouping", &grouping, groupingNames, IM_ARRAYSIZE(groupingNames))) st.grouping = static_cast<HierarchyGrouping>(grouping);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Category: by components / game rules\nName: folders from shared name words\nFlat: registration order");
	ImGui::SameLine();
	// すべて閉じる
	if (ImGui::Button("-")) st.collapseAll = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse all folders");
	ImGui::SameLine();
	// 生成メニュー
	if (ImGui::Button("+")) ImGui::OpenPopup("HierarchyCreate");
	if (ImGui::BeginPopup("HierarchyCreate")) {
		DrawCreateEntityMenu(scene);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	// 検索欄 (小文字で比べる)
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##HierarchySearch", "Search...", st.search, sizeof(st.search));
	const std::string search = ToLower(st.search);

	// 名前を持つエンティティを集め、親子関係を整理する
	std::vector<entt::entity> all;
	std::unordered_map<entt::entity, std::vector<entt::entity>> children;
	for (auto e : registry.view<NameComponent>()) all.push_back(e);
	std::vector<entt::entity> roots;
	roots.reserve(all.size());
	// 親が有効ならその子に、そうでなければ一番上に入れる
	for (auto e : all) {
		auto* hc = registry.try_get<HierarchyComponent>(e);
		entt::entity parent = hc ? hc->parentId : entt::null;
		if (parent != entt::null && parent != e && registry.valid(parent) && registry.all_of<NameComponent>(parent)) children[parent].push_back(e);
		else roots.push_back(e);
	}
	// 物の数
	ImGui::TextDisabled("%d objects", static_cast<int>(all.size()));
	ImGui::Separator();

	// スクロールする木の領域
	ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), ImGuiChildFlags_None);

	const auto& extensions = Extensions();
	// 分類: 拡張の分類があればそれを使い、無ければ標準の分類
	auto categoryOf = [&](entt::entity e) -> std::string {
		for (const auto& ext : extensions) {
			if (!ext.classify) continue;
			std::string category = ext.classify(registry, e);
			if (!category.empty()) return category;
		}
		return DefaultCategory(registry, e);
	};

	// エンティティの行 (子があれば再帰的に描く)
	std::function<void(entt::entity, int)> drawEntity = [&](entt::entity e, int depth) {
		// 消えていれば何もしない
		if (!registry.valid(e)) return;
		ImGui::PushID(static_cast<int>(entt::to_integral(e)));
		// ロックの切り替えボタン (ロック中は L、それ以外は .)
		auto& esc = registry.get_or_emplace<EditorStateComponent>(e);
		ImGui::PushStyleColor(ImGuiCol_Text, esc.locked ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
		if (ImGui::SmallButton(esc.locked ? "L" : ".")) esc.locked = !esc.locked;
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip(esc.locked ? "Locked: cannot be picked in the viewport (click to unlock)" : "Click to lock (disable viewport picking)");
		ImGui::SameLine();

		// 子があるか (深すぎる場合は打ち切る)
		auto childIt = children.find(e);
		const bool hasChildren = childIt != children.end() && depth < 64;
		// 矢印かダブルクリックで開く、子が無ければ葉にする、選択中なら強調する
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (selected == e) flags |= ImGuiTreeNodeFlags_Selected;
		// 選択中の物の祖先なら開いておく
		if (st.revealSelected && hasChildren && selected != e && IsAncestor(registry, e, selected)) ImGui::SetNextItemOpen(true);
		const std::string& name = registry.get<NameComponent>(e).name;
		// 名前の行
		const bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());
		// クリックで選ぶ (開閉の矢印は除く)
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			scene->SelectEntity(e);
			st.lastSelected = e; // Hierarchy 内のクリックではスクロールさせない
		}
		// Unity と同じくダブルクリックで Scene ビューの視点を合わせる
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hasChildren) FrameSelectedInScene();
		// 選択中の物ならそこまでスクロールする
		if (st.revealSelected && selected == e) {
			ImGui::SetScrollHereY(0.4f);
			st.revealSelected = false;
		}

		// ドラッグで親子付け (ドロップ先が自分の子孫になる循環は拒否)
		// ドラッグ元: この物を運ぶ
		if (ImGui::BeginDragDropSource()) {
			entt::entity payload = e;
			ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &payload, sizeof(payload));
			ImGui::Text("Parent '%s' to...", name.c_str());
			ImGui::EndDragDropSource();
		}
		// ドロップ先: 運ばれた物をこの物の子にする (後でまとめて反映する)
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
				entt::entity child = *static_cast<const entt::entity*>(payload->Data);
				if (child != e && !IsAncestor(registry, child, e)) {
					st.reparentChild = child;
					st.reparentTo = e;
					st.reparentRequested = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 右クリックのメニュー: 名前の変更・カメラを寄せる・削除・親子解除・コピー/貼り付け/複製
		if (ImGui::BeginPopupContextItem("EntityContext")) {
			scene->SelectEntity(e);
			if (ImGui::MenuItem("Rename", "F2")) st.renameRequest = e;
			if (ImGui::MenuItem("Frame in Scene", "F")) FrameSelectedInScene();
			if (ImGui::MenuItem("Delete", "Del")) st.pendingDelete = e;
			if (ImGui::MenuItem("Unparent", nullptr, false, registry.all_of<HierarchyComponent>(e) && registry.get<HierarchyComponent>(e).parentId != entt::null)) {
				st.reparentChild = e;
				st.reparentTo = entt::null;
				st.reparentRequested = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopy(scene);
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty())) ExecutePaste(scene);
			if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ExecuteDuplicate(scene);
			ImGui::EndPopup();
		}

		// 開いていれば子を描く
		if (open && hasChildren) {
			for (auto child : childIt->second) drawEntity(child, depth + 1);
			ImGui::TreePop();
		}
		ImGui::PopID();
	};

	// 選択中の物か、その祖先か
	auto isSelectedOrAncestor = [&](entt::entity e) { return selected != entt::null && IsAncestor(registry, e, selected); };

	std::function<void(HierarchyFolder&, const std::string&)> drawFolder = [&](HierarchyFolder& folder, const std::string& id) {
		// 1件だけのフォルダは作らない / 子フォルダ1つだけのフォルダは名前を連結する ("Chrono" + "City")
		// 子フォルダーを順に描く
		for (auto& [key, child] : folder.folders) {
			HierarchyFolder* node = &child;
			std::string label = key;
			while (node->leaves.empty() && node->folders.size() == 1) {
				auto& only = node->folders.begin()->second;
				label += " " + node->folders.begin()->first;
				node = &only;
			}
			// 1 件だけなら、フォルダーを作らずにその物を直接描く
			if (node->count == 1) {
				std::function<void(HierarchyFolder&)> drawSingle = [&](HierarchyFolder& f) {
					for (auto e : f.leaves) drawEntity(e, 0);
					for (auto& [k, c] : f.folders) drawSingle(c);
				};
				drawSingle(*node);
				continue;
			}
			// フォルダーの ID と、開閉の状態
			std::string nodeId = id + "/" + label;
			ImGui::PushID(nodeId.c_str());
			if (st.collapseAll) ImGui::SetNextItemOpen(false);
			else if (st.revealSelected && node->containsSelected) ImGui::SetNextItemOpen(true);
			// フォルダーは金色の文字で、名前と件数を出す
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.82f, 0.45f, 1.0f));
			const bool open = ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%d)", label.c_str(), node->count);
			ImGui::PopStyleColor();
			if (open) {
				drawFolder(*node, nodeId);
				// 子を持たない大量の葉は可視範囲だけ描画する
				bool uniformRows = true;
				for (auto e : node->leaves) {
					if (children.count(e)) { uniformRows = false; break; }
				}
				if (uniformRows && node->leaves.size() > 64 && !st.revealSelected) {
					ImGuiListClipper clipper;
					clipper.Begin(static_cast<int>(node->leaves.size()));
					while (clipper.Step()) {
						for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) drawEntity(node->leaves[i], 0);
					}
				} else {
					// 少なければすべて描く
					for (auto e : node->leaves) drawEntity(e, 0);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		// 一番上のフォルダーなら、そこの物も描く
		if (id.empty()) {
			for (auto e : folder.leaves) drawEntity(e, 0);
		}
	};

	if (!search.empty()) {
		// 検索中は一致したものを親子関係を問わず平坦に表示する
		// 名前に検索文字を含むものを集める
		std::vector<entt::entity> hits;
		for (auto e : all) {
			if (ToLower(registry.get<NameComponent>(e).name).find(search) != std::string::npos) hits.push_back(e);
		}
		ImGui::TextDisabled("%d match(es)", static_cast<int>(hits.size()));
		// 見えている行だけ描く
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(hits.size()));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				auto e = hits[i];
				ImGui::PushID(static_cast<int>(entt::to_integral(e)));
				const std::string& name = registry.get<NameComponent>(e).name;
				// 選んだら選択する (Hierarchy 内なのでスクロールしない)
				if (ImGui::Selectable(name.c_str(), selected == e)) {
					scene->SelectEntity(e);
					st.lastSelected = e;
				}
				// 分類をツールチップで出す
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", categoryOf(e).c_str());
				ImGui::PopID();
			}
		}
	} else if (st.grouping == HierarchyGrouping::Flat) {
		// 一列: 一番上の物を順に描く
		for (auto e : roots) drawEntity(e, 0);
	} else {
		// 分類・名前: フォルダーの木を作る
		HierarchyFolder root;
		for (auto e : roots) {
			const std::string& name = registry.get<NameComponent>(e).name;
			HierarchyFolder* folder = &root;
			std::vector<std::string> path;
			if (st.grouping == HierarchyGrouping::Category) {
				path.push_back(categoryOf(e));
				// カテゴリ内でも 16 件を超える同名系統はさらに名前でまとめる
				for (auto& token : NameTokens(name)) path.push_back(token);
			} else {
				path = NameTokens(name);
			}
			// 道筋をたどってフォルダーを作り、物を入れる
			for (const auto& key : path) folder = &folder->folders[key];
			folder->leaves.push_back(e);
		}
		// 件数と選択中の物を集計する
		FinalizeFolder(root, isSelectedOrAncestor);
		if (st.grouping == HierarchyGrouping::Category) {
			// カテゴリは必ずフォルダとして表示し、中身だけ名前で畳む
			for (auto& [category, folder] : root.folders) {
				ImGui::PushID(category.c_str());
				if (st.collapseAll) ImGui::SetNextItemOpen(false);
				else if (st.revealSelected && folder.containsSelected) ImGui::SetNextItemOpen(true);
				// 分類の見出しは水色の枠付き
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
				const bool open = ImGui::TreeNodeEx("##category", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed, "%s  (%d)", category.c_str(), folder.count);
				ImGui::PopStyleColor();
				if (open) {
					// カテゴリ内の全員が同じ名前の系統 ("Chrono City prop ...") なら、その段は省いて中身を直接並べる
					HierarchyFolder* content = &folder;
					while (content->leaves.empty() && content->folders.size() == 1) content = &content->folders.begin()->second;
					drawFolder(*content, category);
					for (auto e : content->leaves) drawEntity(e, 0);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		} else {
			drawFolder(root, "");
		}
	}
	// すべて閉じる指定を消す
	st.collapseAll = false;
	// 展開とスクロールは選択が変わった1フレームだけ行う (以降はユーザーが自由に閉じられる)
	st.revealSelected = false;

	// 余白: 右クリックで生成メニュー / ドロップで親子解除
	// 残りの領域に見えないボタンを置く
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("HierarchyBackground", ImVec2((std::max)(avail.x, 1.0f), (std::max)(avail.y, 40.0f)));
	// ここにドロップしたら親子関係を外す
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
			st.reparentChild = *static_cast<const entt::entity*>(payload->Data);
			st.reparentTo = entt::null;
			st.reparentRequested = true;
		}
		ImGui::EndDragDropTarget();
	}
	// 右クリックで生成メニュー
	if (ImGui::BeginPopupContextItem("HierarchyBackgroundContext")) {
		DrawCreateEntityMenu(scene);
		ImGui::EndPopup();
	}
	ImGui::EndChild();

	// Delete / F2 / F キー (Hierarchy にフォーカスがあるときのみ)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && selected != entt::null) {
		// Delete: 削除 / F2: 名前の変更 / F: カメラを寄せる
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !scene->GetIsPlaying()) st.pendingDelete = selected;
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) st.renameRequest = selected;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().KeyCtrl) FrameSelectedInScene();
	}
	// 名前の変更の要求があれば、今の名前を入れて窓を開く
	if (st.renameRequest != entt::null) {
		st.renaming = st.renameRequest;
		st.renameRequest = entt::null;
		if (registry.valid(st.renaming) && registry.all_of<NameComponent>(st.renaming)) {
			CopyToBuffer(st.renameBuffer, registry.get<NameComponent>(st.renaming).name);
			ImGui::OpenPopup("Rename Object");
		}
	}
	// 名前の変更の窓
	if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(320.0f);
		const bool enter = ImGui::InputText("##RenameInput", st.renameBuffer, sizeof(st.renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		// Enter か OK で確定し、Undo を登録する
		if (enter || ImGui::Button("OK")) {
			if (registry.valid(st.renaming) && registry.all_of<NameComponent>(st.renaming)) {
				entt::entity target = st.renaming;
				std::string before = registry.get<NameComponent>(target).name, after = st.renameBuffer;
				registry.get<NameComponent>(target).name = after;
				PushUndo({"Rename",
					[scene, target, before]() { auto& r = scene->GetComponents(); if (r.valid(target) && r.all_of<NameComponent>(target)) r.get<NameComponent>(target).name = before; },
					[scene, target, after]() { auto& r = scene->GetComponents(); if (r.valid(target) && r.all_of<NameComponent>(target)) r.get<NameComponent>(target).name = after; }});
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		// Cancel か Esc で閉じる
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// --- 反復処理の外で構造を変更する ---
	if (st.reparentRequested) {
		st.reparentRequested = false;
		// 子と新しい親 (null なら親子解除)
		entt::entity child = st.reparentChild, parent = st.reparentTo;
		if (registry.valid(child) && (parent == entt::null || registry.valid(parent))) {
			// 親を付け替え、ワールド行列を計算し直し、Undo を登録する
			auto& hc = registry.get_or_emplace<HierarchyComponent>(child);
			entt::entity before = hc.parentId;
			hc.parentId = parent;
			scene->InvalidateWorldMatrices();
			PushUndo({"Reparent",
				[scene, child, before]() { auto& reg = scene->GetComponents(); if (reg.valid(child)) { reg.get_or_emplace<HierarchyComponent>(child).parentId = before; scene->InvalidateWorldMatrices(); } },
				[scene, child, parent]() { auto& reg = scene->GetComponents(); if (reg.valid(child)) { reg.get_or_emplace<HierarchyComponent>(child).parentId = parent; scene->InvalidateWorldMatrices(); } }});
		}
	}
	if (st.pendingDelete != entt::null) {
		// 削除の予約を取り出し、Undo を登録してから破棄する
		entt::entity e = st.pendingDelete;
		st.pendingDelete = entt::null;
		if (registry.valid(e)) {
			PushUndo(MakeSpawnUndo(scene, "Delete Entity", SerializeEntity(registry, e), e, false));
			scene->DestroyObject(static_cast<uint32_t>(e));
			if (scene->GetSelectedEntity() == e) scene->SelectEntity(entt::null);
		}
	}
	ImGui::End();
}

} // namespace Game
