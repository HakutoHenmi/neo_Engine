// EditorInspector.cpp
// Inspector ウィンドウ: 名前・トランスフォーム・各コンポーネントの編集欄、Add Component、
// アセットのパスの入力欄 (Project からのドラッグ&ドロップと一覧からの選択)。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// フォルダ以下から指定拡張子のファイルを探し、Resources/ からの相対パスで返す
std::vector<std::string> EditorUI::GetAssetsInDir(const std::string& root, const std::vector<std::string>& extensions) {
	std::vector<std::string> assets;
	// プロジェクトのルートを基準にした絶対パス (フォルダーで無ければ空)
	std::string absRoot = GetUnifiedProjectPath(root);
	std::error_code ec;
	if (!fs::is_directory(Engine::PathUtils::FromUTF8(absRoot), ec)) return assets;

	// アクセスできないフォルダや日本語のファイル名があっても例外で落ちないよう error_code 版を使う
	fs::recursive_directory_iterator it(Engine::PathUtils::FromUTF8(absRoot), fs::directory_options::skip_permission_denied, ec);
	for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		// ファイルだけを調べる
		const auto& entry = *it;
		std::error_code fileError;
		if (entry.is_regular_file(fileError)) {
			// 拡張子 (小文字)
			std::string ext = ToLower(Engine::PathUtils::ToUTF8(entry.path().extension().wstring()));
			
			// 探している拡張子か
			bool match = false;
			for (const auto& targetExt : extensions) {
				if (ext == targetExt) { match = true; break; }
			}
			
			if (match) {
				// UTF-8 のパスにして、区切りを / にそろえる
				std::string path = Engine::PathUtils::ToUTF8(entry.path().wstring());
				std::replace(path.begin(), path.end(), '\\', '/');
				
				// 保存の形をそろえるため Resources/ からの相対パスにする
				size_t pos = path.find("/Resources/");
				if (pos != std::string::npos) {
					path = path.substr(pos + 1); // 先頭のスラッシュを除く
				} else {
					pos = path.find("Resources/");
					if (pos != std::string::npos) path = path.substr(pos);
				}
				assets.push_back(path);
			}
		}
	}
	return assets;
}

// アセットのパス入力欄 (直接入力・Project からのドラッグ&ドロップ・一覧からの選択)
bool EditorUI::AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions) {
	// 変更されたか / 欄ごとに ID を分ける
	bool modified = false;
	ImGui::PushID(label);
	
	// 入力欄の文字
	char buf[512];
	CopyToBuffer(buf, path);
	
	// 右の ... ボタンの分だけ欄を狭くする
	float buttonSize = ImGui::GetFrameHeight();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
	
	// 直接入力
	if (ImGui::InputText(label, buf, sizeof(buf))) {
		path = buf;
		modified = true;
	}

	// ドラッグ&ドロップ
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			// 落とされたパスと拡張子
			std::string droppedPath = (const char*)payload->Data;
			std::replace(droppedPath.begin(), droppedPath.end(), '\\', '/');
			
			std::string ext = ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(droppedPath)).extension().wstring()));
			
			// 受け付ける拡張子なら反映する
			bool valid = false;
			for (const auto& e : extensions) { if (ext == e) { valid = true; break; } }
			
			if (valid) {
				path = droppedPath;
				modified = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	// ... ボタンで一覧を開く
	if (ImGui::Button("...")) {
		ImGui::OpenPopup("AssetDropdown");
	}

	if (ImGui::BeginPopup("AssetDropdown")) {
		// 一覧は開いたときに作り直す
		static std::vector<std::string> assetList;
		if (ImGui::IsWindowAppearing()) {
			assetList = GetAssetsInDir("Resources", extensions);
		}

		// (None) で空にする
		if (ImGui::Selectable("(None)", path.empty())) {
			path = "";
			modified = true;
		}

		// 一覧から選ぶ
		for (const auto& a : assetList) {
			if (ImGui::Selectable(a.c_str(), path == a)) {
				path = a;
				modified = true;
			}
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
	return modified;
}

// Inspector の補助
// Inspector を描いている間、PLAY 中かどうか
static bool s_inspectorPlaying = false;

// コンポーネントの追加・削除など構造を変えるボタン。PLAY 中は値の調整だけを許可する (停止時に元へ戻るため)
static bool StructureButton(const char* label) {
	// PLAY 中は押せないようにする
	ImGui::BeginDisabled(s_inspectorPlaying);
	const bool pressed = ImGui::Button(label);
	ImGui::EndDisabled();
	return pressed;
}

// Add Component の項目 (カテゴリ・表示名・持っているかの判定・追加する関数)
namespace {
struct ComponentEntry {
	const char* category;
	const char* name;
	bool (*has)(SceneComponents&, entt::entity);
	void (*add)(SceneComponents&, entt::entity);
};
// 持っているか / 追加する (共通の関数)
template <class T> bool HasComponent(SceneComponents& r, entt::entity e) { return r.all_of<T>(e); }
template <class T> void AddComponent(SceneComponents& r, entt::entity e) { std::ignore = r.get_or_emplace<T>(e); }

// GPU メッシュコライダーを追加する (描画用のメッシュがあればそれを判定にも使う)
void AddGpuMeshCollider(SceneComponents& r, entt::entity e) {
	auto& gmc = r.get_or_emplace<GpuMeshColliderComponent>(e);
	if (auto* mr = r.try_get<MeshRendererComponent>(e)) {
		gmc.meshPath = mr->modelPath;
		gmc.meshHandle = mr->modelHandle;
	}
}

// Legacy は旧アクションゲーム (缶・ボス戦) 用。現行のスライム (Chrono) モードでは処理されない
const ComponentEntry kComponentEntries[] = {
	// 描画: メッシュ・アニメーター・モーション
	{"Rendering", "Mesh Renderer", &HasComponent<MeshRendererComponent>, &AddComponent<MeshRendererComponent>},
	{"Rendering", "Animator", &HasComponent<AnimatorComponent>, &AddComponent<AnimatorComponent>},
	{"Rendering", "Motion (Keyframes)", &HasComponent<MotionComponent>, &AddComponent<MotionComponent>},
	// エフェクト: パーティクル・流体・川
	{"Effects", "Particle Emitter", &HasComponent<ParticleEmitterComponent>, &AddComponent<ParticleEmitterComponent>},
	{"Effects", "Fluid Emitter", &HasComponent<FluidEmitterComponent>, &AddComponent<FluidEmitterComponent>},
	{"Effects", "River", &HasComponent<RiverComponent>, &AddComponent<RiverComponent>},
	// 物理: 箱・剛体・GPU メッシュ
	{"Physics", "Box Collider", &HasComponent<BoxColliderComponent>, &AddComponent<BoxColliderComponent>},
	{"Physics", "Rigidbody", &HasComponent<RigidbodyComponent>, &AddComponent<RigidbodyComponent>},
	{"Physics", "GPU Mesh Collider", &HasComponent<GpuMeshColliderComponent>, &AddGpuMeshCollider},
	// ライト
	{"Lighting", "Directional Light", &HasComponent<DirectionalLightComponent>, &AddComponent<DirectionalLightComponent>},
	{"Lighting", "Point Light", &HasComponent<PointLightComponent>, &AddComponent<PointLightComponent>},
	{"Lighting", "Spot Light", &HasComponent<SpotLightComponent>, &AddComponent<SpotLightComponent>},
	// 音
	{"Audio", "Audio Source", &HasComponent<AudioSourceComponent>, &AddComponent<AudioSourceComponent>},
	{"Audio", "Audio Listener", &HasComponent<AudioListenerComponent>, &AddComponent<AudioListenerComponent>},
	// UI
	{"UI", "Rect Transform", &HasComponent<RectTransformComponent>, &AddComponent<RectTransformComponent>},
	{"UI", "UI Image", &HasComponent<UIImageComponent>, &AddComponent<UIImageComponent>},
	{"UI", "UI Text", &HasComponent<UITextComponent>, &AddComponent<UITextComponent>},
	{"UI", "UI Button", &HasComponent<UIButtonComponent>, &AddComponent<UIButtonComponent>},
	// スクリプト・変数・タグ・体力
	{"Scripting", "Script", &HasComponent<ScriptComponent>, &AddComponent<ScriptComponent>},
	{"Scripting", "Variables", &HasComponent<VariableComponent>, &AddComponent<VariableComponent>},
	{"Scripting", "Tag", &HasComponent<TagComponent>, &AddComponent<TagComponent>},
	{"Scripting", "Health", &HasComponent<HealthComponent>, &AddComponent<HealthComponent>},
	// 旧アクションゲーム用
	{"Legacy (old action game)", "Player Input", &HasComponent<PlayerInputComponent>, &AddComponent<PlayerInputComponent>},
	{"Legacy (old action game)", "Character Movement", &HasComponent<CharacterMovementComponent>, &AddComponent<CharacterMovementComponent>},
	{"Legacy (old action game)", "Camera Target", &HasComponent<CameraTargetComponent>, &AddComponent<CameraTargetComponent>},
	{"Legacy (old action game)", "Hitbox", &HasComponent<HitboxComponent>, &AddComponent<HitboxComponent>},
	{"Legacy (old action game)", "Hurtbox", &HasComponent<HurtboxComponent>, &AddComponent<HurtboxComponent>},
	{"Legacy (old action game)", "World Space UI", &HasComponent<WorldSpaceUIComponent>, &AddComponent<WorldSpaceUIComponent>},
};
} // namespace

// Unity と同じく検索欄付きの Add Component。検索中はカテゴリを畳まず一覧にする
void EditorUI::DrawAddComponentButton(SceneComponents& registry, entt::entity entity, bool playing) {
	// 検索文字
	static char search[64] = "";
	// ボタンの幅 (最大 260) と中央寄せ
	const float width = (std::min)(ImGui::GetContentRegionAvail().x, 260.0f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
	ImGui::BeginDisabled(playing);
	// 押したら検索を空にして一覧を開く (PLAY 中は押せない)
	if (ImGui::Button("Add Component", ImVec2(width, 0))) {
		search[0] = '\0';
		ImGui::OpenPopup("AddComp");
	}
	ImGui::EndDisabled();
	if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop play mode to add components");
	// 一覧が開いていなければ終わる
	if (!ImGui::BeginPopup("AddComp")) return;
	// 開いたときは検索欄に入力できるようにする
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputTextWithHint("##ComponentSearch", "Search...", search, sizeof(search));
	ImGui::Separator();
	// 小文字で比べる
	const std::string needle = ToLower(search);
	// 1 項目: 持っていればチェック付きで押せない、押したら追加して閉じる
	auto drawEntry = [&](const ComponentEntry& entry) {
		const bool has = entry.has(registry, entity);
		if (ImGui::MenuItem(entry.name, nullptr, has, !has)) {
			entry.add(registry, entity);
			ImGui::CloseCurrentPopup();
		}
	};
	// 検索中: 名前に含むものだけを一列に並べる
	if (!needle.empty()) {
		int shown = 0;
		for (const auto& entry : kComponentEntries) {
			if (ToLower(entry.name).find(needle) == std::string::npos) continue;
			drawEntry(entry);
			++shown;
		}
		if (shown == 0) ImGui::TextDisabled("No component matches");
	} else {
		// 検索していないとき: カテゴリごとのサブメニューに分ける
		const char* current = nullptr;
		bool open = false;
		for (const auto& entry : kComponentEntries) {
			if (!current || std::string(current) != entry.category) {
				if (open) ImGui::EndMenu();
				current = entry.category;
				open = ImGui::BeginMenu(entry.category);
			}
			if (open) drawEntry(entry);
		}
		if (open) ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

// Inspector: 選択中のエンティティの名前・トランスフォーム・各コンポーネントを編集する
void EditorUI::ShowInspector(GameScene* scene) {
	// PLAY 中か (構造を変えるボタンを無効にする)
	s_inspectorPlaying = scene && scene->GetIsPlaying();
	// ウィンドウの Begin は呼び出し側で行う
	if (!scene) return;
	// スクリプトの変更の予約 (描画の後にまとめて反映する)
	std::optional<ScriptEdit> pendingScriptEdit;
	// 選択中の物
	auto selected = scene->GetSelectedEntity();
	if (scene && selected != entt::null && scene->GetComponents().valid(selected)) {
		// 物ごとに ID を分ける
		ImGui::PushID((int)selected);
		auto entity = selected;
		auto& registry = scene->GetComponents();

		// 名前とロック
		if (auto* nc = registry.try_get<NameComponent>(entity)) {
			// 名前の入力欄
			char buf[256]; CopyToBuffer(buf, nc->name);
			ImGui::SetNextItemWidth(-130);
			if (ImGui::InputText("##Name", buf, sizeof(buf))) nc->name = buf;
			ImGui::SameLine();
			// ロック
			auto& esc = registry.get_or_emplace<EditorStateComponent>(entity);
			ImGui::Checkbox("Locked", &esc.locked);
		}
		// ID
		ImGui::TextDisabled("ID: %d", (uint32_t)entity);
		ImGui::SameLine();
		// プレハブとして保存する (Resources/Prefabs/名前.prefab)
		if (ImGui::Button("Save Prefab")) {
			// 名前 (無ければ object)
			std::string name = (registry.all_of<NameComponent>(entity) ? registry.get<NameComponent>(entity).name : "object");
			// ファイル名に使えない文字 (\\ / : * ? " < > |) を置き換える
			for (char& c : name) {
				if (std::string("\\/:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
			}
			// 空なら object
			if (name.empty()) name = "object";
			// 保存先のパスと、フォルダーの作成
			std::string p = "Resources/Prefabs/" + name + ".prefab";
			std::string fullPath = GetUnifiedProjectPath(p);
			std::error_code dirError;
			fs::create_directories(fs::path(Engine::PathUtils::FromUTF8(fullPath)).parent_path(), dirError);
			// prefab キーの下にエンティティの JSON を書く
			std::ofstream f(Engine::PathUtils::FromUTF8(fullPath));
			if (f.is_open()) {
				f << "{\n  \"prefab\":\n";
				f << SerializeEntity(registry, entity);
				f << "\n}\n";
				f.close();
				Log("Prefab saved: " + p);
			} else {
				LogError("Failed to save prefab: " + fullPath);
			}
		}

		// トランスフォーム (編集の開始と終了を記録して Undo できるようにする)
		if (auto* tc = registry.try_get<TransformComponent>(entity)) {
			ImGui::Separator();
			ImGui::Text("Transform");
			// 位置: 動かし始めの値を覚え、離したら Undo を登録する
			if (ImGui::DragFloat3("Pos", &tc->translate.x, 0.1f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartTranslate = tc->translate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartTranslate;
				DirectX::XMFLOAT3 end = tc->translate;
				entt::entity e = entity;
				PushUndo({"Change Position",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = end; }
				});
			}

			// 回転: 同様
			if (ImGui::DragFloat3("Rot", &tc->rotate.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartRotate = tc->rotate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartRotate;
				DirectX::XMFLOAT3 end = tc->rotate;
				entt::entity e = entity;
				PushUndo({"Change Rotation",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = end; }
				});
			}

			// 大きさ: 同様
			if (ImGui::DragFloat3("Scale", &tc->scale.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartScale = tc->scale;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartScale;
				DirectX::XMFLOAT3 end = tc->scale;
				entt::entity e = entity;
				PushUndo({"Change Scale",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = end; }
				});
			}
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 各コンポーネントの編集欄 (コンポーネントごとに ID を分けて、同名の項目が干渉しないようにする)
			if (auto* cp = registry.try_get<MeshRendererComponent>(selected)) {
				ImGuiIdScope componentId("MeshRendererComponent");
				if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##MR", &cp->enabled);
					
					// モデル (変えたら読み込み直す)
					if (AssetField("Model Path", cp->modelPath, {".obj", ".fbx", ".gltf"})) {
						cp->modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(cp->modelPath);
					}

					// テクスチャ (変えたら読み込み直す)
					if (AssetField("Texture Path", cp->texturePath, {".png", ".jpg"})) {
						cp->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(cp->texturePath);
					}

					// シェーダーの選択
					// 登録されているシェーダーの一覧
					const auto& shaders = Engine::Renderer::GetInstance()->GetShaderNames();
					std::string currentShader = cp->shaderName.empty() ? "Default" : cp->shaderName;
					if (ImGui::BeginCombo("Shader", currentShader.c_str())) {
						for (size_t i = 0; i < shaders.size(); ++i) {
							const auto& sName = shaders[i];
							ImGui::PushID((int)i);
							bool isSelected = (currentShader == sName);
							if (ImGui::Selectable(sName.c_str(), isSelected)) {
								cp->shaderName = sName;
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}

					// 色・UV の繰り返しとずらし
					ImGui::ColorEdit4("Base Color", &cp->color.x);
					ImGui::DragFloat2("UV Tiling", &cp->uvTiling.x, 0.01f);
					ImGui::DragFloat2("UV Offset", &cp->uvOffset.x, 0.01f);

					// Reflection シェーダーのときは反射率の項目を出す
					if (cp->shaderName == "Reflection") {
						ImGui::SliderFloat("Reflectivity", &cp->reflectivity, 0.0f, 1.0f, "%.2f");
						ImGui::Checkbox("Use Cubemap", &cp->useCubemap);
					}

					// コンポーネントを外す (PLAY 中は押せない)
					if (StructureButton("Remove##MR")) registry.remove<MeshRendererComponent>(entity);
				}
			}
			// 箱の当たり判定
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				ImGuiIdScope componentId("BoxColliderComponent");
				if (ImGui::CollapsingHeader("BoxCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・すり抜けるか
					ImGui::Checkbox("Enabled##BC", &bc->enabled);
					ImGui::DragFloat3("Center", &bc->center.x, 0.1f);
					ImGui::DragFloat3("Size", &bc->size.x, 0.1f);
					ImGui::Checkbox("Is Trigger", &bc->isTrigger);
					if (StructureButton("Remove##BC")) registry.remove<BoxColliderComponent>(entity);
				}
			}
			// GPU メッシュコライダー
			if (auto* gmc = registry.try_get<GpuMeshColliderComponent>(entity)) {
				ImGuiIdScope componentId("GpuMeshColliderComponent");
				if (ImGui::CollapsingHeader("GpuMeshCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・メッシュ (変えたら読み込み直す)・すり抜けるか
					ImGui::Checkbox("Enabled##GMC", &gmc->enabled);
					if (AssetField("Mesh Path", gmc->meshPath, {".obj", ".fbx"})) {
						gmc->meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(gmc->meshPath);
					}
					ImGui::Checkbox("Is Trigger", &gmc->isTrigger);
					if (StructureButton("Remove##GMC")) registry.remove<GpuMeshColliderComponent>(entity);
				}
			}
			// 物理挙動
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				ImGuiIdScope componentId("RigidbodyComponent");
				if (ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・速度・重力・自分で動かすか
					ImGui::Checkbox("Enabled##RB", &rb->enabled);
					ImGui::DragFloat3("Velocity", &rb->velocity.x, 0.1f);
					ImGui::Checkbox("Use Gravity", &rb->useGravity);
					ImGui::Checkbox("Is Kinematic", &rb->isKinematic);
					if (StructureButton("Remove##RB")) registry.remove<RigidbodyComponent>(entity);
				}
			}
			// タグ (変更したらタグ検索のキャッシュも更新する)
			if (auto* tag = registry.try_get<TagComponent>(entity)) {
				ImGuiIdScope componentId("TagComponent");
				if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##Tag", &tag->enabled);
					
					// 今のタグと、タグの一覧
					const char* currentTagName = TagToString(tag->tag);
					if (ImGui::BeginCombo("Tag##TagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (tag->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) {
								tag->tag = t;
								scene->SyncTag(entity); // キャッシュ更新
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					if (StructureButton("Remove##Tag")) registry.remove<TagComponent>(entity);
				}
			}
			// 音の発信源
			if (auto* as = registry.try_get<AudioSourceComponent>(entity)) {
				ImGuiIdScope componentId("AudioSourceComponent");
				if (ImGui::CollapsingHeader("AudioSource", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・音のファイル (変えたら読み込み直す)
					ImGui::Checkbox("Enabled##AS", &as->enabled);
					if (AssetField("Sound File", as->soundPath, {".wav", ".mp3"})) {
						as->soundHandle = Engine::Audio::GetInstance()->Load(as->soundPath);
					}
					// 音量・ループ・開始時に鳴らすか・3D か
					ImGui::DragFloat("Volume", &as->volume, 0.01f, 0, 1);
					ImGui::Checkbox("Loop", &as->loop);
					ImGui::Checkbox("Play on Start", &as->playOnStart);
					ImGui::Checkbox("Is 3D", &as->is3D);
					if (StructureButton("Remove##AS")) registry.remove<AudioSourceComponent>(entity);
				}
			}
			// 体力
			if (auto* hp = registry.try_get<HealthComponent>(entity)) {
				ImGuiIdScope componentId("HealthComponent");
				if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##HP", &hp->enabled);
					// 体力・最大体力・死んでいるか
					float currentHp = hp->Hp(), maxHp = hp->MaxHp();
                    bool dead = hp->IsDead();
                    if (ImGui::DragFloat("HP", &currentHp, 1.0f, 0, maxHp)) hp->SetHp(currentHp);
                    if (ImGui::DragFloat("Max HP", &maxHp, 1.0f, 1, 10000)) hp->SetMaxHp(maxHp);
                    if (ImGui::Checkbox("Is Dead", &dead)) hp->SetDead(dead);
					if (StructureButton("Remove##HP")) registry.remove<HealthComponent>(entity);
				}
			}
			// モーション: クリップの選択・追加・削除、キーフレームの編集
			if (auto* mc = registry.try_get<MotionComponent>(entity)) {
				ImGuiIdScope componentId("MotionComponent");
				if (ImGui::CollapsingHeader("Motion Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##MC", &mc->enabled);
					
					// クリップの選択
					if (ImGui::BeginCombo("Active Clip", mc->activeClip.c_str())) {
						for (auto& [name, clip] : mc->clips) {
							bool isSelected = (mc->activeClip == name);
							if (ImGui::Selectable(name.c_str(), isSelected)) {
								mc->activeClip = name;
								mc->currentTime = 0.0f;
								mc->selectedKeyframe = -1;
							}
						}
						ImGui::EndCombo();
					}

					ImGui::SameLine();
					// 新しいクリップを作る窓を開く
					if (StructureButton("+##NewClip")) {
						ImGui::OpenPopup("NewClipPopup");
					}

					if (ImGui::BeginPopup("NewClipPopup")) {
						static char newClipBuf[64] = "NewClip";
						ImGui::InputText("Name", newClipBuf, sizeof(newClipBuf));
						// 最初と 1 秒後のキーフレームを入れて追加する
						if (ImGui::Button("Add")) {
							MotionComponent::MotionClip clip;
							clip.name = newClipBuf;
							clip.keyframes.push_back({0.00f, {0,0,0}, {0,0,0}, {1,1,1}});
							clip.keyframes.push_back({1.00f, {1,0,0}, {0,0,0}, {1,1,1}});
							mc->clips[newClipBuf] = clip;
							mc->activeClip = newClipBuf;
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					// クリップが 1 つしか無いときは削除できない
					ImGui::BeginDisabled(mc->clips.size() <= 1);
					// 今のクリップを消し、先頭のクリップを選ぶ
					if (StructureButton("Delete Clip") && mc->clips.size() > 1) {
						mc->clips.erase(mc->activeClip);
						mc->activeClip = mc->clips.begin()->first;
						mc->selectedKeyframe = -1;
						mc->currentTime = 0.0f;
					}
					ImGui::EndDisabled();

					ImGui::Separator();
					// 今のクリップの設定
					if (mc->clips.count(mc->activeClip)) {
						auto& clip = mc->clips[mc->activeClip];
						ImGui::Text("Clip: %s", clip.name.c_str());
						// 再生中か・現在の時刻・長さ・ループ
						ImGui::Checkbox("Playing", &mc->isPlaying);
						ImGui::DragFloat("Current Time", &mc->currentTime, 0.01f, 0, clip.totalDuration);
						ImGui::DragFloat("Duration", &clip.totalDuration, 0.1f, 0.1f, 100.0f);
						ImGui::Checkbox("Loop", &clip.loop);
						
						if (StructureButton("Add Keyframe")) {
							// 末尾の時刻に、最後のキーフレームと同じ値で追加する
							MotionComponent::Keyframe k;
							k.time = clip.totalDuration;
							if (!clip.keyframes.empty()) {
								k.translate = clip.keyframes.back().translate;
								k.rotate = clip.keyframes.back().rotate;
								k.scale = clip.keyframes.back().scale;
							} else {
								// キーフレームが無ければ原点・回転なし・大きさ 1
								k.translate = {0,0,0};
								k.rotate = {0,0,0};
								k.scale = {1,1,1};
							}
							// 末尾に追加する
							clip.keyframes.push_back(k);
						}
						
						ImGui::Separator();
						ImGui::Text("Keyframes:");
						// キーフレームの一覧 (選んだものだけ時刻・位置・回転・大きさを編集できる)
						for (int i = 0; i < (int)clip.keyframes.size(); ++i) {
							char label[32]; sprintf_s(label, "KF %d", i);
							if (ImGui::Selectable(label, mc->selectedKeyframe == i)) {
								mc->selectedKeyframe = i;
							}
							if (mc->selectedKeyframe == i) {
								ImGuiIdScope keyframeId(label);
								ImGui::Indent();
								ImGui::DragFloat("Time", &clip.keyframes[i].time, 0.01f, 0, clip.totalDuration);
								ImGui::DragFloat3("Pos", &clip.keyframes[i].translate.x, 0.1f);
								ImGui::DragFloat3("Rot", &clip.keyframes[i].rotate.x, 0.01f);
								ImGui::DragFloat3("Scale", &clip.keyframes[i].scale.x, 0.01f);
								// このキーフレームを消す
								if (StructureButton("Remove KF")) {
									clip.keyframes.erase(clip.keyframes.begin() + i);
									mc->selectedKeyframe = -1;
								}
								ImGui::Unindent();
							}
						}
					}

					// コンポーネントを外す
					if (StructureButton("Remove Component##MC")) registry.remove<MotionComponent>(entity);
				}
			}
			// スクリプト: クラスの選択・パラメータの編集・追加と削除 (変更は描画の後でまとめて反映する)
			if (auto* cp = registry.try_get<ScriptComponent>(selected)) {
				ImGuiIdScope componentId("ScriptComponent");
				if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##Scripts", &cp->enabled);
					
					// 登録されているスクリプトのクラス名
					auto scriptNames = ScriptEngine::GetInstance()->GetRegisteredScriptNames();

					// 付いているスクリプトを順に表示する
					for (int i = 0; i < (int)cp->scripts.size(); ++i) {
						auto& entry = cp->scripts[i];
						ImGui::PushID(i);
						std::string treeLabel = (entry.scriptPath.empty() ? "None" : entry.scriptPath) + "##Tree";
						if (ImGui::TreeNodeEx(treeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
							// 今のクラス名の番号
							int current = -1;
							for (int j = 0; j < (int)scriptNames.size(); ++j) {
								if (scriptNames[j] == entry.scriptPath) { current = j; break; }
							}

							std::string comboName = entry.scriptPath.empty() ? "None" : entry.scriptPath;
							// クラスの選択 (変えたら差し替えを予約する)
							if (ImGui::BeginCombo("Class", comboName.c_str())) {
								for (int j = 0; j < (int)scriptNames.size(); ++j) {
									bool isSelected = (current == j);
									if (ImGui::Selectable(scriptNames[j].c_str(), isSelected)) {
										if (entry.scriptPath != scriptNames[j]) {
											pendingScriptEdit = ScriptEdit{ScriptEditKind::Replace, selected,
												static_cast<size_t>(i), entry.scriptPath, entry.instance, scriptNames[j]};
										}
									}
								}
								ImGui::EndCombo();
							}

							// クラス名があるのに実体が無ければ作る
							if (!entry.instance && !entry.scriptPath.empty()) {
								entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
								if (entry.instance) {
									char logBuf[1024];
									sprintf_s(logBuf, "[EditorUI] Inspector: Created instance for %s, Restoring params: %s\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
									OutputDebugStringA(logBuf);
									
									// パラメータが空でなければデシリアライズ
									if (!entry.parameterData.empty()) {
										entry.instance->DeserializeParameters(entry.parameterData);
									}
								} else if (entry.instance) {
									// パラメータが空の場合は初期状態をセット
									entry.parameterData = entry.instance->SerializeParameters();
									char logBuf[1024];
									sprintf_s(logBuf, "[EditorUI] Inspector: Creating instance for %s, Param was empty, setting default: %s\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
									OutputDebugStringA(logBuf);
								}
							}

							// 実体があれば、スクリプト自身の編集欄を描く
							if (entry.instance) {
								ImGui::PushID("ScriptEditor");
								
								// 描く前と後のパラメータ
								std::string oldParams = entry.parameterData;
								entry.instance->OnEditorUI();
								std::string newParams = entry.instance->SerializeParameters();

								// 比較を少し賢くする
								// プレイ中以外で、かつ実際に中身が意味のある変更をされた場合のみ更新
								if (!scene->GetIsPlaying()) {
									bool shouldUpdate = false;
									if (oldParams != newParams) {
										// "{}" から具体的な値への変化、または値同士の変化
										if (oldParams == "{}" || oldParams.empty()) {
											// 初期状態からの設定
											shouldUpdate = true;
										} else if (newParams != "{}" && !newParams.empty()) {
											// 値が変更された
											shouldUpdate = true;
										}
									}

									if (shouldUpdate) {
										entry.parameterData = newParams;
										char logBuf[1024];
										sprintf_s(logBuf, "[EditorUI] Parameter Changed: %s -> %s\n", oldParams.c_str(), newParams.c_str());
										OutputDebugStringA(logBuf);
									}
								}
								ImGui::PopID();
							}

							// このスクリプトを外す (予約する)
							if (StructureButton("Remove Script")) {
								pendingScriptEdit = ScriptEdit{ScriptEditKind::Remove, selected,
									static_cast<size_t>(i), entry.scriptPath, entry.instance, {}};
								// 削除後のループの番号ずれを避けるため、ここで抜ける
								ImGui::TreePop();
								ImGui::PopID();
								break; 
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
						ImGui::Separator();
					}

					// スクリプトの欄を増やす / コンポーネントごと外す (予約する)
					if (StructureButton("Add Script")) {
						pendingScriptEdit = ScriptEdit{ScriptEditKind::Add, selected, 0, {}, {}, {}};
					}
					ImGui::SameLine();
					if (StructureButton("Remove Component##SC"))
						pendingScriptEdit = ScriptEdit{ScriptEditKind::RemoveComponent, selected, 0, {}, {}, {}};
				}
			}
			// 平行光源
			if (auto* dl = registry.try_get<DirectionalLightComponent>(entity)) {
				ImGuiIdScope componentId("DirectionalLightComponent");
				if (ImGui::CollapsingHeader("DirectionalLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ
					ImGui::Checkbox("Enabled##DL", &dl->enabled);
					ImGui::ColorEdit3("Light Color", &dl->color.x);
					ImGui::DragFloat("Intensity", &dl->intensity, 0.1f, 0, 100);
					if (StructureButton("Remove##DL")) registry.remove<DirectionalLightComponent>(entity);
				}
			}
			// 点光源
			if (auto* pl = registry.try_get<PointLightComponent>(entity)) {
				ImGuiIdScope componentId("PointLightComponent");
				if (ImGui::CollapsingHeader("PointLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ・届く範囲
					ImGui::Checkbox("Enabled##PL", &pl->enabled);
					ImGui::ColorEdit3("Light Color", &pl->color.x);
					ImGui::DragFloat("Intensity", &pl->intensity, 0.1f, 0, 100);
					ImGui::DragFloat("Range", &pl->range, 0.1f, 0, 1000);
					if (StructureButton("Remove##PL")) registry.remove<PointLightComponent>(entity);
				}
			}
			// パーティクルエミッター: シェーダーの種類 (煙を選ぶと煙用の設定を入れる)、設定ファイルへの保存
			if (auto* pe = registry.try_get<ParticleEmitterComponent>(entity)) {
				ImGuiIdScope componentId("ParticleEmitterComponent");
				if (ImGui::CollapsingHeader("ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・設定ファイル
					ImGui::Checkbox("Enabled##PE", &pe->enabled);
					AssetField("Asset", pe->assetPath, {".json"});

					// シェーダーの種類の選択
					// シェーダーの選択肢 (表示名・シェーダー名・加算合成か)
					struct ShaderOption { const char* label; const char* shaderName; bool isAdditive; };
					static const ShaderOption shaderOptions[] = {
						{ "(Default) Particle",              "",                        false },
						{ "Particle (Alpha Blend)",          "Particle",               false },
						{ "Particle (Additive)",             "ParticleAdditive",       true  },
						{ "Procedural Smoke",                "ProceduralSmoke",        false },
						{ "Procedural Smoke (Additive)",     "ProceduralSmokeAdditive",true  },
					};
					static const int shaderOptionCount = IM_ARRAYSIZE(shaderOptions);

					// 今のシェーダーの番号
					int currentIdx = 0;
					for (int si = 0; si < shaderOptionCount; ++si) {
						if (pe->emitter.params.shaderName == shaderOptions[si].shaderName) {
							currentIdx = si;
							break;
						}
					}

					// 選んだらシェーダー名と加算合成を設定する
					if (ImGui::Combo("Shader Type##PE", &currentIdx, [](void* data, int idx, const char** out) -> bool {
						*out = ((const ShaderOption*)data)[idx].label;
						return true;
					}, (void*)shaderOptions, shaderOptionCount)) {
						pe->emitter.params.shaderName = shaderOptions[currentIdx].shaderName;
						pe->emitter.params.isAdditive = shaderOptions[currentIdx].isAdditive;

						// 煙のシェーダーを選んだら、煙らしい設定 (上昇・広がり・色) を入れる
						std::string sn = shaderOptions[currentIdx].shaderName;
						if (sn == "ProceduralSmoke" || sn == "ProceduralSmokeAdditive") {
							pe->emitter.params.startVelocity = {0, 1.5f, 0};
							pe->emitter.params.velocityVariance = {0.3f, 0.3f, 0.3f};
							pe->emitter.params.acceleration = {0, 0.5f, 0}; // 上昇気流
							pe->emitter.params.damping = 1.0f;
							pe->emitter.params.lifeTime = 3.0f;
							pe->emitter.params.lifeTimeVariance = 0.5f;
							pe->emitter.params.startSize = {1.5f, 1.5f, 1.5f};
							pe->emitter.params.endSize = {4.0f, 4.0f, 4.0f};
							pe->emitter.params.startColor = {0.85f, 0.9f, 0.95f, 0.7f};  // 水蒸気らしい薄青白いグレー
							pe->emitter.params.endColor = {0.6f, 0.65f, 0.7f, 0.0f}; // 滑らかに空気色にフェード
							pe->emitter.params.emitRate = 8.0f;
						}
					}

					// 設定ファイルへ保存する (パスが空ならエラー)
					if (ImGui::Button("Save Asset##PE")) {
						if (!pe->assetPath.empty()) {
							if (pe->emitter.SaveToJson(pe->assetPath)) {
								Log("Saved Particle Asset: " + pe->assetPath);
							} else {
								LogError("Failed to save Particle Asset: " + pe->assetPath);
							}
						} else {
							LogError("Particle Asset Path is empty.");
						}
					}
					ImGui::SameLine();
					if (StructureButton("Remove##PE")) registry.remove<ParticleEmitterComponent>(entity);
				}
			}
			// 流体エミッター
			if (auto* fec = registry.try_get<FluidEmitterComponent>(entity)) {
				ImGuiIdScope componentId("FluidEmitterComponent");
				if (ImGui::CollapsingHeader("FluidEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・1 フレームの数 (1 秒あたりの数も表示)
					ImGui::Checkbox("Enabled##FEC", &fec->enabled);
					ImGui::DragInt("Emit Rate", &fec->emitCountPerFrame, 1, 0, 1000);
					ImGui::Text("%d particles / second", fec->emitCountPerFrame * 60);
					// 水を残すか・合計の上限 (0 は無制限)
					ImGui::Checkbox("Retain water", &fec->retainWater);
					ImGui::DragInt("Particle limit (0 = continuous)", &fec->particleLimit, 100, 0, 25000);
					// 色・初速・流体の種類
					ImGui::ColorEdit4("Color", &fec->color.x);
					ImGui::DragFloat3("Velocity", &fec->velocity.x, 0.1f);
					ImGui::DragFloat("Fluid Type", &fec->fluidType, 0.1f, 0.0f, 1.0f);
					if (StructureButton("Remove##FEC")) registry.remove<FluidEmitterComponent>(entity);
				}
			}
			// 旧アクションゲーム用: 入力・移動
			if (auto* pi = registry.try_get<PlayerInputComponent>(entity)) {
				ImGuiIdScope componentId("PlayerInputComponent");
				if (ImGui::CollapsingHeader("PlayerInput", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・入力の向き (表示だけ)
					ImGui::Checkbox("Enabled##PI", &pi->enabled);
					ImGui::Text("MoveDir: (%.2f, %.2f)", pi->moveDir.x, pi->moveDir.y);
					if (StructureButton("Remove##PI")) registry.remove<PlayerInputComponent>(entity);
				}
			}
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				ImGuiIdScope componentId("CharacterMovementComponent");
				if (ImGui::CollapsingHeader("CharacterMovement", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・速さ・ジャンプ力・重力
					ImGui::Checkbox("Enabled##CM", &cm->enabled);
					ImGui::DragFloat("Speed", &cm->speed, 0.1f);
					ImGui::DragFloat("Jump Power", &cm->jumpPower, 0.1f);
					ImGui::DragFloat("Gravity", &cm->gravity, 0.1f);
					if (StructureButton("Remove##CM")) registry.remove<CharacterMovementComponent>(entity);
				}
			}
			// スポットライト
			if (auto* sl = registry.try_get<SpotLightComponent>(entity)) {
				ImGuiIdScope componentId("SpotLightComponent");
				if (ImGui::CollapsingHeader("SpotLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ・届く範囲
					ImGui::Checkbox("Enabled##SL", &sl->enabled);
					ImGui::ColorEdit3("Color", &sl->color.x);
					ImGui::DragFloat("Intensity", &sl->intensity, 0.1f);
					ImGui::DragFloat("Range", &sl->range, 0.1f);
					if (StructureButton("Remove##SL")) registry.remove<SpotLightComponent>(entity);
				}
			}
			// UI: 配置・画像・文字・ボタン
			if (auto* rt = registry.try_get<RectTransformComponent>(entity)) {
				ImGuiIdScope componentId("RectTransformComponent");
				if (ImGui::CollapsingHeader("RectTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・位置・大きさ・基準点
					ImGui::Checkbox("Enabled##RT", &rt->enabled);
					ImGui::DragFloat2("Pos", &rt->pos.x);
					ImGui::DragFloat2("Size", &rt->size.x);
					ImGui::DragFloat2("Anchor", &rt->anchor.x, 0.01f, 0, 1);
					if (StructureButton("Remove##RT")) registry.remove<RectTransformComponent>(entity);
				}
			}
			if (auto* img = registry.try_get<UIImageComponent>(entity)) {
				ImGuiIdScope componentId("UIImageComponent");
				if (ImGui::CollapsingHeader("UIImage", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・テクスチャ (変えたら読み込み直す)
					ImGui::Checkbox("Enabled##IMG", &img->enabled);
					if (AssetField("Texture", img->texturePath, {".png", ".jpg"})) {
						img->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(img->texturePath, false);
					}
					// 色・重なり順
					ImGui::ColorEdit4("Color", &img->color.x);
					ImGui::DragInt("Layer", &img->layer, 1, -100, 100);
					// 9 分割の画像なら、上下左右の境界を設定する
					ImGui::Checkbox("9-Slice", &img->is9Slice);
					if (img->is9Slice) {
						ImGui::DragFloat("Border Top", &img->borderTop, 1.0f, 0, 500);
						ImGui::DragFloat("Border Bottom", &img->borderBottom, 1.0f, 0, 500);
						ImGui::DragFloat("Border Left", &img->borderLeft, 1.0f, 0, 500);
						ImGui::DragFloat("Border Right", &img->borderRight, 1.0f, 0, 500);
					}
					if (StructureButton("Remove##IMG")) registry.remove<UIImageComponent>(entity);
				}
			}
			if (auto* txt = registry.try_get<UITextComponent>(entity)) {
				ImGuiIdScope componentId("UITextComponent");
				if (ImGui::CollapsingHeader("UIText", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・文字 (複数行)
					ImGui::Checkbox("Enabled##TXT", &txt->enabled);
					char tbuf[1024]; CopyToBuffer(tbuf, txt->text);
					if (ImGui::InputTextMultiline("Text", tbuf, sizeof(tbuf), ImVec2(0, ImGui::GetTextLineHeight() * 3.5f))) txt->text = tbuf;
					// 大きさ・色
					ImGui::DragFloat("Font Size", &txt->fontSize, 1.0f, 1, 100);
					ImGui::ColorEdit4("Color##TXT", &txt->color.x);

					// フォント選択ドロップダウン
					std::vector<std::string> fonts = { "C:\\Windows\\Fonts\\msgothic.ttc" };
					// Resources/Fonts の中のフォントも一覧に入れる
					std::string fontDir = GetUnifiedProjectPath("Resources/Fonts");
					std::error_code fontError;
					for (auto& p : fs::directory_iterator(Engine::PathUtils::FromUTF8(fontDir), fontError)) {
						std::string ext = ToLower(Engine::PathUtils::ToUTF8(p.path().extension().wstring()));
						if (ext == ".ttf" || ext == ".ttc" || ext == ".otf") {
							fonts.push_back(GetUnifiedProjectPath("Resources/Fonts/" + Engine::PathUtils::ToUTF8(p.path().filename().wstring())));
						}
					}

					// フォントの選択
					if (ImGui::BeginCombo("Font Path", txt->fontPath.c_str())) {
						for (const auto& f : fonts) {
							bool isSelected = (txt->fontPath == f);
							if (ImGui::Selectable(f.c_str(), isSelected)) txt->fontPath = f;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					// 一覧に無いフォントを指定する
					AssetField("Custom Font", txt->fontPath, { ".ttf", ".ttc", ".otf" });

					// RectTransform が無ければ、付けるためのボタンを出す (橙)
					if (!registry.all_of<RectTransformComponent>(entity)) {
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
						if (ImGui::Button("Add RectTransform (Show UI Gizmo)")) {
							registry.emplace<RectTransformComponent>(entity);
						}
						ImGui::PopStyleColor();
					}

					if (StructureButton("Remove##TXT")) registry.remove<UITextComponent>(entity);
				}
			}
			if (auto* btn = registry.try_get<UIButtonComponent>(entity)) {
				ImGuiIdScope componentId("UIButtonComponent");
				if (ImGui::CollapsingHeader("UIButton", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・マウスが乗っているか (表示だけ)
					ImGui::Checkbox("Enabled##BTN", &btn->enabled);
					ImGui::Text("Hovered: %s", btn->isHovered ? "Yes" : "No");
					
					// クリック判定の範囲を個別に調整する
					ImGui::DragFloat2("Hitbox Offset", &btn->hitboxOffset.x, 1.0f);
					ImGui::DragFloat2("Hitbox Scale", &btn->hitboxScale.x, 0.01f, 0.0f, 10.0f);
					
					if (StructureButton("Remove##BTN")) registry.remove<UIButtonComponent>(entity);
				}
			}
			// 川
			if (auto* riv = registry.try_get<RiverComponent>(entity)) {
				ImGuiIdScope componentId("RiverComponent");
				if (ImGui::CollapsingHeader("River", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・幅・流れの速さ・テクスチャ
					ImGui::Checkbox("Enabled##RIV", &riv->enabled);
					ImGui::DragFloat("Width", &riv->width, 0.1f);
					ImGui::DragFloat("Speed", &riv->flowSpeed, 0.1f);
					AssetField("Texture", riv->texturePath, {".png", ".jpg"});
					if (StructureButton("Remove##RIV")) registry.remove<RiverComponent>(entity);
				}
			}
			// アニメーター: モデルに含まれるクリップから選ぶ
			if (auto* anim = registry.try_get<AnimatorComponent>(entity)) {
				ImGuiIdScope componentId("AnimatorComponent");
				if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##ANIM", &anim->enabled);
					// メッシュのモデルにアニメーションがあれば、その一覧から選ぶ
					if (auto* mr = registry.try_get<MeshRendererComponent>(entity)) {
						if (mr->modelHandle != 0) {
							auto* m = Engine::Renderer::GetInstance()->GetModel(mr->modelHandle);
							if (m && !m->GetData().animations.empty()) {
								if (ImGui::BeginCombo("Clips", anim->currentAnimation.c_str())) {
									for (const auto& a : m->GetData().animations) {
										if (ImGui::Selectable(a.name.c_str(), anim->currentAnimation == a.name)) {
											anim->currentAnimation = a.name;
											anim->time = 0.0f;
										}
									}
									ImGui::EndCombo();
								}
							} else {
								ImGui::Text("No animations in model.");
							}
						} else {
							ImGui::Text("No Model attached.");
						}
					} else {
						ImGui::Text("Require MeshRenderer.");
					}

					// 速さ・再生中か・ループ・骨の表示
					ImGui::DragFloat("Speed", &anim->speed, 0.1f);
					ImGui::Checkbox("Playing", &anim->isPlaying);
					ImGui::SameLine();
					ImGui::Checkbox("Loop", &anim->loop);
					ImGui::Checkbox("Draw Skeleton (Debug)", &anim->drawSkeleton);

					if (StructureButton("Remove##ANIM")) registry.remove<AnimatorComponent>(entity);
				}
			}
			// 旧アクションゲーム用: カメラ追従・攻撃判定・食らい判定・頭上の HP バー
			if (auto* ct = registry.try_get<CameraTargetComponent>(entity)) {
				ImGuiIdScope componentId("CameraTargetComponent");
				if (ImGui::CollapsingHeader("CameraTarget", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・距離・高さ・なめらかさ
					ImGui::Checkbox("Enabled##CT", &ct->enabled);
					ImGui::DragFloat("Dist", &ct->distance, 0.1f);
					ImGui::DragFloat("Height", &ct->height, 0.1f);
					ImGui::DragFloat("Smooth", &ct->smoothSpeed, 0.1f);
					if (StructureButton("Remove##CT")) registry.remove<CameraTargetComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HitboxComponent>(entity)) {
				ImGuiIdScope componentId("HitboxComponent");
				if (ImGui::CollapsingHeader("Hitbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・ダメージ
					ImGui::Checkbox("Enabled##HB", &hb->enabled);
					ImGui::DragFloat3("Ofs##HB", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size##HB", &hb->size.x, 0.1f);
					ImGui::DragFloat("DMG##HB", &hb->damage, 1.0f);
					
					// タグの選択
					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					// 有効か (攻撃中か)
					ImGui::Checkbox("Active##HB", &hb->isActive);
					if (StructureButton("Remove##HB")) registry.remove<HitboxComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HurtboxComponent>(entity)) {
				ImGuiIdScope componentId("HurtboxComponent");
				if (ImGui::CollapsingHeader("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・ダメージ倍率
					ImGui::Checkbox("Enabled##HurtB", &hb->enabled);
					ImGui::DragFloat3("Ofs##HurtB", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size##HurtB", &hb->size.x, 0.1f);
					ImGui::DragFloat("Mult##HurtB", &hb->damageMultiplier, 0.1f);

					// タグの選択
					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HurtBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					if (StructureButton("Remove##HurtB")) registry.remove<HurtboxComponent>(entity);
				}
			}
			if (auto* ws = registry.try_get<WorldSpaceUIComponent>(entity)) {
				ImGuiIdScope componentId("WorldSpaceUIComponent");
				if (ImGui::CollapsingHeader("WorldSpaceUI", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・HP バーを出すか・頭上のずれ
					ImGui::Checkbox("Enabled##WS", &ws->enabled);
					ImGui::Checkbox("HP Bar##WS", &ws->showHealthBar);
					ImGui::DragFloat3("Ofs##WS", &ws->offset.x, 0.1f);
					if (StructureButton("Remove##WS")) registry.remove<WorldSpaceUIComponent>(entity);
				}
			}
			// 音を聞く位置
			if (auto* al = registry.try_get<AudioListenerComponent>(entity)) {
				ImGuiIdScope componentId("AudioListenerComponent");
				if (ImGui::CollapsingHeader("AudioListener", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##AL", &al->enabled);
					ImGui::Text("Listening...");
					if (StructureButton("Remove##AL")) registry.remove<AudioListenerComponent>(entity);
				}
			}
			// 汎用の変数 (数値)
			if (auto* var = registry.try_get<VariableComponent>(entity)) {
				ImGuiIdScope componentId("VariableComponent");
				if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##VAR", &var->enabled);
					// 数値の変数を順に編集する
					for (auto& [k, v] : var->values) {
						ImGui::PushID(k.c_str());
						ImGui::DragFloat(k.c_str(), &v, 0.1f);
						ImGui::PopID();
					}
					if (StructureButton("Remove##VAR")) registry.remove<VariableComponent>(entity);
				}
			}
		}
		
		// コンポーネントの追加ボタン
		ImGui::Separator();
		DrawAddComponentButton(registry, entity, scene->GetIsPlaying());
		// スクリプトの変更の予約があれば、描画の後でここで反映する
		if (pendingScriptEdit) ApplyScriptEdit(registry, *pendingScriptEdit, scene);
		ImGui::PopID();
	} else {
		ImGui::Text("No Selection");
	}
	// ウィンドウの End は呼び出し側で行う
}

} // namespace Game
