// EditorViewport.cpp
// Scene / Game のビュー: エディタカメラの操作、表示するビューの切り替え、画像の表示、
// 移動・回転・拡大のギズモ、UI 部品の編集ハンドル、クリックでの選択、画面からワールドへのレイ。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// Game ビューの画像の範囲を返す
ImVec2 EditorUI::GetGameImageMin() { return gameImageMin; }
ImVec2 EditorUI::GetGameImageMax() { return gameImageMax; }

// ====== Scene / Game のビュー ======
// 描画先は1枚だけなので、表示中のタブのカメラで描画する (Unity の Scene / Game タブ相当)。
// Scene : エディタカメラ。右ドラッグ + WASD/QE で飛行、ホイールで前後、中ボタンで平行移動、F で選択物へ
// Game  : ゲーム (プレイヤー) カメラ。実際のプレイ画面

// ビュー行列から位置と向きを取り出して、エディタカメラをゲームカメラの視点に合わせる
// (ゲーム側は LookAt で行列だけ設定することがあり、Rotation() が実際の向きと一致しないため)
static void AlignEditorCameraToGame(GameScene* scene) {
	// ゲームカメラのワールド行列 (ビュー行列の逆行列)
	DirectX::XMMATRIX world = DirectX::XMMatrixInverse(nullptr, scene->GetCamera().View());
	// 位置と前方向
	DirectX::XMFLOAT3 position, forward;
	DirectX::XMStoreFloat3(&position, world.r[3]);
	DirectX::XMStoreFloat3(&forward, DirectX::XMVector3Normalize(world.r[2]));
	// エディタのカメラに位置と向き (上下と左右の角度) を設定する
	auto& camera = scene->GetEditorCamera();
	camera.SetPosition(position);
	camera.SetRotation(-std::asin(std::clamp(forward.y, -1.0f, 1.0f)), std::atan2(forward.x, forward.z), 0.0f);
}

// Scene ビューのカメラ操作 (右ドラッグで飛行、ホイール、中ボタンで平行移動、F で選択物へ)
void EditorUI::UpdateSceneCamera(GameScene* scene, bool hovered) {
	ImGuiIO& io = ImGui::GetIO();
	// エディタのカメラ
	auto& camera = scene->GetEditorCamera();
	// 最初の 1 回だけゲームカメラの視点に合わせる
	if (!s_editorCameraPlaced) {
		AlignEditorCameraToGame(scene);
		s_editorCameraPlaced = true;
	}
	// 画像の上で右ボタンを押したら飛行を始め、離したら終わる
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) s_sceneCameraNavigating = true;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_sceneCameraNavigating = false;

	// 今の位置と回転
	DirectX::XMFLOAT3 position = camera.Position();
	DirectX::XMFLOAT3 rotation = camera.Rotation();
	// カメラの向きの回転行列
	const DirectX::XMMATRIX basis = DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, 0.0f);
	// カメラから見た向きで移動する
	auto move = [&](DirectX::XMFLOAT3 local) {
		DirectX::XMFLOAT3 world;
		DirectX::XMStoreFloat3(&world, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&local), basis));
		position = {position.x + world.x, position.y + world.y, position.z + world.z};
	};
	// 変わったか / Shift で 4 倍速
	bool changed = false;
	const float boost = io.KeyShift ? 4.0f : 1.0f;

	if (s_sceneCameraNavigating) {
		// マウスで向きを変える (上下は ±89 度くらいまで)
		rotation.y += io.MouseDelta.x * 0.004f;
		rotation.x = std::clamp(rotation.x + io.MouseDelta.y * 0.004f, -1.55f, 1.55f);
		// 飛行中のホイールは速度の変更 (Unity と同じ)
		if (io.MouseWheel != 0.0f) s_sceneCameraSpeed = std::clamp(s_sceneCameraSpeed * (io.MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, 2000.0f);
		// 1 フレームの移動量
		const float step = s_sceneCameraSpeed * boost * io.DeltaTime;
		DirectX::XMFLOAT3 local{0, 0, 0};
		// W/S で前後、D/A で左右、E/Q で上下
		if (ImGui::IsKeyDown(ImGuiKey_W)) local.z += step;
		if (ImGui::IsKeyDown(ImGuiKey_S)) local.z -= step;
		if (ImGui::IsKeyDown(ImGuiKey_D)) local.x += step;
		if (ImGui::IsKeyDown(ImGuiKey_A)) local.x -= step;
		if (ImGui::IsKeyDown(ImGuiKey_E)) local.y += step;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) local.y -= step;
		move(local);
		changed = true;
	} else if (hovered && io.MouseWheel != 0.0f) {
		// 飛行していないときのホイールは前後に動く
		move({0, 0, io.MouseWheel * s_sceneCameraSpeed * 0.25f * boost});
		changed = true;
	}
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
		// 中ボタンのドラッグで平行移動
		const float pan = s_sceneCameraSpeed * 0.01f * boost;
		move({-io.MouseDelta.x * pan, io.MouseDelta.y * pan, 0});
		changed = true;
	}
	// F: 選択中のオブジェクトを正面に捉える (Hierarchy のダブルクリックや Edit メニューからも要求される)
	const bool frameRequested = s_frameSelectedRequest || (hovered && !io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false));
	// 要求は 1 回で消す
	s_frameSelectedRequest = false;
	if (frameRequested) {
		// 選択中の物 (位置があるときだけ)
		auto selected = scene->GetSelectedEntity();
		if (selected != entt::null && scene->GetComponents().valid(selected) && scene->GetComponents().all_of<TransformComponent>(selected)) {
			// ワールド行列と大きさ
			const auto world = scene->GetWorldMatrix(static_cast<int>(selected));
			const auto& tc = scene->GetComponents().get<TransformComponent>(selected);
			const float size = (std::max)({std::abs(tc.scale.x), std::abs(tc.scale.y), std::abs(tc.scale.z), 1.0f});
			// カメラの前方向
			DirectX::XMFLOAT3 forward;
			DirectX::XMStoreFloat3(&forward, DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), basis));
			// 大きさに合わせた距離だけ後ろへ下がる
			const float distance = size * 4.0f + 4.0f;
			position = {world.m[3][0] - forward.x * distance, world.m[3][1] - forward.y * distance, world.m[3][2] - forward.z * distance};
			changed = true;
		}
	}
	// 変わったらカメラに反映する
	if (changed) {
		camera.SetRotation(rotation);
		camera.SetPosition(position);
	}
}

// Scene ビュー左上のツールバー (ゲームカメラに合わせる・移動速度・操作の案内)
void EditorUI::DrawSceneToolbar(GameScene* scene, const ImVec2& imageMin) {
	// 画像の左上に置く
	ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 6.0f, imageMin.y + 6.0f));
	// ボタンを半透明にする
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.12f, 0.75f));
	if (ImGui::SmallButton("Align to Game Camera")) AlignEditorCameraToGame(scene);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(110.0f);
	// 移動速度 (対数のスライダー)
	ImGui::SliderFloat("##CameraSpeed", &s_sceneCameraSpeed, 1.0f, 2000.0f, "speed %.0f", ImGuiSliderFlags_Logarithmic);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	// 操作の案内
	ImGui::TextDisabled("RMB+WASD/QE  Wheel  MMB  F");
}

// もう一方のビューが描画中であることの案内と、こちらへ切り替えるボタン
static void DrawInactiveViewNotice(const char* active, bool& switchHere) {
	// 左上に案内を書く
	const ImVec2 contentStart = ImGui::GetCursorPos();
	ImGui::SetCursorPos(ImVec2(contentStart.x + 16.0f, contentStart.y + 16.0f));
	ImGui::TextWrapped("The %s view is being rendered. Only one view is rendered per frame.", active);
	ImGui::SetCursorPosX(contentStart.x + 16.0f);
	// 押されたらこちらへ切り替える
	switchHere = ImGui::Button("Render this view");
}

// Scene / Game ウィンドウの管理 (表示中のビューを決め、そのビューだけ描く)
void EditorUI::ShowViewports(Engine::Renderer* renderer, GameScene* gameScene, bool maximized) {
	// ビューの種類
	using View = GameScene::EditorView;
	// 表示中のビュー / PLAY 前のビュー / 前のフレームで Game が見えていたか / 前のフレームで PLAY 中だったか / 初回か / 固定する残りフレーム
	static View activeView = View::Game;
	static View viewBeforePlay = View::Scene;
	static bool gameVisibleLastFrame = true;
	static bool wasPlaying = false;
	static bool firstFrame = true;
	static int pinFrames = 0;

	// Unity と同様に PLAY 開始で Game タブへ、停止で元のタブへ戻す (起動時に既に PLAY 中でも Game から始める)
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	if (playing != wasPlaying || firstFrame) {
		if (playing) {
			// PLAY 開始: 今のビューを覚えて Game へ
			if (!firstFrame) viewBeforePlay = activeView;
			RequestView(View::Game);
		// PLAY 停止: 元が Scene なら Scene へ戻す
		} else if (!firstFrame && viewBeforePlay == View::Scene) {
			RequestView(View::Scene);
		}
		wasPlaying = playing;
		firstFrame = false;
	}
	// ウィンドウがまだ作られていない (初回・レイアウト再構築直後) 間は要求を保持し、
	// タブの切り替えが反映されるまでの数フレームは表示中のビューを固定する
	if (s_viewRequested) {
		// 要求されたビューのウィンドウ名
		const char* name = s_requestedView == View::Scene ? kSceneWindow : kGameWindow;
		activeView = s_requestedView;
		pinFrames = 3;
		// ウィンドウがあれば前面に出し、要求を消す
		if (ImGui::FindWindowByName(name)) {
			ImGui::SetWindowFocus(name);
			s_viewRequested = false;
		}
	}
	// 固定中か
	const bool pinned = pinFrames > 0;
	if (pinFrames > 0) --pinFrames;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (maximized) {
		// Maximize On Play: ドックを使わず作業領域全体に Game ビューを出す
		// メインのビューポート
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		if (ImGui::Begin("##MaximizedGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
			activeView = View::Game;
			DrawViewport(renderer, gameScene, false);
		}
		ImGui::End();
		ImGui::PopStyleVar();
		// 表示中のビューを伝え、カーソルの強制表示を外す
		gameScene->SetEditorView(activeView);
		Engine::WindowDX::SetCursorForcedVisible(false);
		return;
	}
	// 初回は Game ウィンドウと同じ場所にタブとして並べる
	if (ImGuiWindow* game = ImGui::FindWindowByName("Game"); game && game->DockId != 0) {
		ImGui::SetNextWindowDockID(game->DockId, ImGuiCond_FirstUseEver);
	}
	// Scene ウィンドウ
	const bool sceneVisible = ImGui::Begin("Scene");
	if (sceneVisible) {
		bool switchHere = false;
		// Scene が表示中か、Game が隠れていれば Scene を描く
		if (activeView == View::Scene || (!gameVisibleLastFrame && !pinned)) {
			activeView = View::Scene;
			DrawViewport(renderer, gameScene, true);
		// そうでなければ案内を出す
		} else {
			DrawInactiveViewNotice("Game", switchHere);
			if (switchHere) activeView = View::Scene;
		}
	}
	ImGui::End();

	// Game ウィンドウ
	const bool gameVisible = ImGui::Begin("Game");
	if (gameVisible) {
		bool switchHere = false;
		// Game が表示中か、Scene が隠れていれば Game を描く
		if (activeView == View::Game || (!sceneVisible && !pinned)) {
			activeView = View::Game;
			DrawViewport(renderer, gameScene, false);
		// そうでなければ案内を出す
		} else {
			DrawInactiveViewNotice("Scene", switchHere);
			if (switchHere) activeView = View::Game;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	gameVisibleLastFrame = gameVisible;

	gameScene->SetEditorView(activeView);
	// PLAY 中はゲームがカーソルを隠すが、Scene ビューを操作する間は表示する
	Engine::WindowDX::SetCursorForcedVisible(playing && activeView == View::Scene);
}

// ビューの中身: 画像・ツールバー・Stats・ドラッグ&ドロップ・ギズモ・UI の編集・クリック選択
void EditorUI::DrawViewport(Engine::Renderer* renderer, GameScene* gameScene, bool sceneView) {
	if (!sceneView) {
		// Game ビューのツールバー (Unity と同じく縦横比と Stats はここで切り替える)
		// 縦横比の選択
		// Begin() places the cursor below the title/docking tabs. Keep that
		// content origin instead of positioning relative to the window border.
		const ImVec2 contentStart = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(contentStart.x + 6.0f, contentStart.y + 4.0f));
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::BeginCombo("##Aspect", aspects[currentAspect])) {
			for (int n = 0; n < IM_ARRAYSIZE(aspects); n++) {
				if (ImGui::Selectable(aspects[n], currentAspect == n)) currentAspect = n;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		// Stats と Maximize On Play
		ImGui::Checkbox("Stats", &s_showStats);
		ImGui::SameLine();
		ImGui::Checkbox("Maximize On Play", &s_maximizeOnPlay);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
	}
	
	// 使える領域
	ImVec2 av = ImGui::GetContentRegionAvail();
	// ドッキング直後やウィンドウを畳んだ時は 0 になり、以降の除算が NaN/Inf になる
	float tW = (std::max)(av.x, 1.0f), tH = (std::max)(av.y, 1.0f);
	
	// 縦横比を決めてカメラの投影を更新する
	// 内部解像度の縦横比
	float nativeAspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
	float targetAspect = nativeAspect; // 既定は内部解像度の比 (2D や 3D が潰れないように)
	
	// Game ビューで縦横比が指定されていればそれを使う
	if (!sceneView && currentAspect > 0 && aspectValues[currentAspect] > 0.0f) {
		targetAspect = aspectValues[currentAspect];
	}

	// 表示する大きさと、使える領域の縦横比
	float renderW = tW, renderH = tH;
	float currentAvailAspect = tW / tH;
	
	// 描画先 (固定サイズ) が伸び縮みしないよう、常に縦横比を保って余白を付ける
	if (currentAvailAspect > targetAspect) {
		renderW = tH * targetAspect;
		renderH = tH;
	} else {
		renderW = tW;
		renderH = tW / targetAspect;
	}
	// Scene ビューはエディタカメラ、Game ビューはゲームカメラ。Scene ビューの縦横比は常に描画先と同じにする
	Engine::Camera& viewCamera = sceneView ? gameScene->GetEditorCamera() : gameScene->GetCamera();
	viewCamera.SetProjection(0.7854f, targetAspect, 0.1f, sceneView ? 10000.0f : 1000.0f);
	
	// 画像を中央に置く
	ImVec2 cursorPadding((tW - renderW) * 0.5f, (tH - renderH) * 0.5f);
	ImVec2 currentCursorPos = ImGui::GetCursorPos();
	float targetIdxX = currentCursorPos.x + cursorPadding.x;
	float targetIdxY = currentCursorPos.y + cursorPadding.y;
	ImGui::SetCursorPos(ImVec2(targetIdxX, targetIdxY));
	
	// 描画した画像を表示する
	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(renderW, renderH));
	
	// 実際に画像を描いた画面上の範囲を取得する
	gameImageMin = ImGui::GetItemRectMin();
	gameImageMax = ImGui::GetItemRectMax();
	if (!sceneView) renderer->SetEditorLoadingViewport(gameImageMin.x, gameImageMin.y, renderW, renderH);
	const bool imageHovered = ImGui::IsItemHovered();
	// Scene ビュー左上のツールバー。ここをクリックしてもピッキングしない
	const ImVec2 toolbarMax(gameImageMin.x + 420.0f, gameImageMin.y + ImGui::GetFrameHeightWithSpacing() + 8.0f);
	const bool overToolbar = sceneView && ImGui::IsMouseHoveringRect(gameImageMin, toolbarMax);
	if (sceneView) UpdateSceneCamera(gameScene, imageHovered && !overToolbar);
	
	// UISystem などが座標を換算できるよう、ビューの情報をシーンへ渡す
	// 画像の上のマウスの位置を、内部解像度の座標に換算する
	// 既存のピクセル中心の座標の対応関係を保つ
	ImVec2 mPos_img = ImGui::GetMousePos();
	const float internalMouseX = (mPos_img.x - gameImageMin.x) * (float)Engine::WindowDX::kW / renderW;
	const float internalMouseY = (mPos_img.y - gameImageMin.y) * (float)Engine::WindowDX::kH / renderH;
	// Scene ビューではゲーム内 UI (調合室のボタンなど) がマウスに反応しないよう、画面外の座標を渡す
	gameScene->SetEditorViewport({gameImageMin.x, gameImageMin.y}, {renderW, renderH},
		sceneView ? -100000.0f : internalMouseX, sceneView ? -100000.0f : internalMouseY);

	// ツールのオーバーレイを画像の上に描く (Stats・拡張のオーバーレイ)
	// Stats: FPS・物の数・表示と描画の大きさを左上に重ねる
	if (!sceneView && s_showStats) {
		const ImVec2 at(gameImageMin.x + 8.0f, gameImageMin.y + 8.0f);
		char stats[256];
		snprintf(stats, sizeof(stats), "%.1f FPS (%.2f ms)\nObjects: %d\nView: %.0f x %.0f (render %u x %u)",
			ImGui::GetIO().Framerate, 1000.0f / (std::max)(ImGui::GetIO().Framerate, 1.0f),
			static_cast<int>(gameScene->GetComponents().view<NameComponent>().size()), renderW, renderH,
			Engine::WindowDX::kW, Engine::WindowDX::kH);
		const ImVec2 textSize = ImGui::CalcTextSize(stats);
		ImDrawList* statsDraw = ImGui::GetWindowDrawList();
		statsDraw->AddRectFilled(at, ImVec2(at.x + textSize.x + 16.0f, at.y + textSize.y + 12.0f), IM_COL32(0, 0, 0, 170), 4.0f);
		statsDraw->AddText(ImVec2(at.x + 8.0f, at.y + 6.0f), IM_COL32(230, 230, 230, 255), stats);
	}
	// 拡張のオーバーレイ
	for (auto& ext : Extensions()) {
		if (ext.open && ext.drawViewportOverlay) ext.drawViewportOverlay(gameScene, gameImageMin, gameImageMax);
	}
	
	// Project からのドラッグ&ドロップ: プレハブは配置、モデルはメッシュ付きエンティティを作る
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			// 渡されたパスと、その拡張子 (小文字)
			std::string sPath = (const char*)payload->Data;
			std::replace(sPath.begin(), sPath.end(), '\\', '/'); // 正規化
			const std::string ext = ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(sPath)).extension().wstring()));
			// プレハブなら読み込んで、最初のものを選ぶ
			if (ext == ".prefab") {
				auto created = LoadPrefab(gameScene, sPath);
				if (!created.empty()) gameScene->SelectEntity(created.front());
			} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
				// モデルなら、ファイル名を名前にして作り、メッシュと白いテクスチャを付ける
				auto e = gameScene->CreateEntity(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(sPath)).stem().wstring()));
				auto& mr = gameScene->GetComponents().emplace<MeshRendererComponent>(e);
				mr.modelPath = sPath;
				mr.modelHandle = renderer->LoadObjMesh(sPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				gameScene->SelectEntity(e);
			}
		}
		ImGui::EndDragDropTarget();
	}
	// クリック選択とギズモ
	// ギズモに触れているか・PLAY 中か・選択中の物
	bool gizmoHovered = false;
	bool isPlaying = gameScene->GetIsPlaying();
	auto selectedEnt = gameScene->GetSelectedEntity();

	// TransformComponent を持たないエンティティ (UI だけの物など) で get<> が assert しないよう先に確認する
	// 3D の移動ギズモは Scene ビューだけ。Game ビューでは UI の配置だけ編集できる
	if (!isPlaying && selectedEnt != entt::null && gameScene->GetComponents().valid(selectedEnt) &&
		gameScene->GetComponents().all_of<TransformComponent>(selectedEnt)) {
		// このシーンの ECS と、選択中の物のモーションと位置
		auto& reg = gameScene->GetComponents();
		auto* mc = reg.try_get<MotionComponent>(selectedEnt);
		auto& tc = reg.get<TransformComponent>(selectedEnt);
		
		DirectX::XMFLOAT3 currentPos;
		// モーションのキーフレームを選択中なら、そのキーフレームの位置にギズモを出す
		if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
			auto& kfs = mc->clips.at(mc->activeClip).keyframes;
			if (mc->selectedKeyframe < (int)kfs.size()) {
				currentPos = kfs[mc->selectedKeyframe].translate;
			} else {
				currentPos = tc.translate;
			}
		} else {
			currentPos = tc.translate;
		}

		// 位置とカメラの行列
		DirectX::XMVECTOR objPos3D = DirectX::XMLoadFloat3(&currentPos);
		DirectX::XMMATRIX view = viewCamera.View();
		DirectX::XMMATRIX proj = viewCamera.Proj();
		
		// オブジェクトの位置を画面座標へ投影する
		DirectX::XMVECTOR projP = DirectX::XMVector3Project(objPos3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
		// 画面上の位置と奥行き
		float px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
		float py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
		float pz = DirectX::XMVectorGetZ(projP);

		if (sceneView && pz > 0.0f && pz < 1.0f) { // カメラの後ろや遠すぎる場合は描かない
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// ギズモの原点・軸の長さ・当たりの半径・マウスの位置
			ImVec2 origin(px, py);
			float gizmoLen = 60.0f;
			float hitRadius = 15.0f;
			ImVec2 mousePos = ImGui::GetMousePos();
			
			// X・Y・Z 軸の単位ベクトルを画面へ投影する
			DirectX::XMVECTOR pX = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(1,0,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pY = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,1,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pZ = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,0,1,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			
			// 原点から各軸の先への画面上の向き
			ImVec2 dirX(DirectX::XMVectorGetX(pX) + gameImageMin.x - px, DirectX::XMVectorGetY(pX) + gameImageMin.y - py);
			ImVec2 dirY(DirectX::XMVectorGetX(pY) + gameImageMin.x - px, DirectX::XMVectorGetY(pY) + gameImageMin.y - py);
			ImVec2 dirZ(DirectX::XMVectorGetX(pZ) + gameImageMin.x - px, DirectX::XMVectorGetY(pZ) + gameImageMin.y - py);
			
			// 画面上の向きとして正規化する
			auto normalize = [](ImVec2 v) { float len = std::sqrt(v.x*v.x + v.y*v.y); if (len>0) { v.x/=len; v.y/=len; } return v; };
			dirX = normalize(dirX); dirY = normalize(dirY); dirZ = normalize(dirZ);

			// 各軸の先端
			ImVec2 endX(origin.x + dirX.x * gizmoLen, origin.y + dirX.y * gizmoLen);
			ImVec2 endY(origin.x + dirY.x * gizmoLen, origin.y + dirY.y * gizmoLen);
			ImVec2 endZ(origin.x + dirZ.x * gizmoLen, origin.y + dirZ.y * gizmoLen);

			// 点から線分までの距離
			auto distToSegment = [](ImVec2 p, ImVec2 a, ImVec2 b) {
				ImVec2 pa(p.x - a.x, p.y - a.y), ba(b.x - a.x, b.y - a.y);
				float lenSq = ba.x*ba.x + ba.y*ba.y;
				float h = lenSq > 1e-6f ? std::clamp((pa.x*ba.x + pa.y*ba.y) / lenSq, 0.0f, 1.0f) : 0.0f;
				float dx = pa.x - ba.x * h, dy = pa.y - ba.y * h;
				return std::sqrt(dx*dx + dy*dy);
			};

			// 触れている軸 (ドラッグ中はその軸)
			int hoveredAxis = -1;
			if (!gizmo_.Dragging()) {
				if (distToSegment(mousePos, origin, endX) < hitRadius) hoveredAxis = 0;
				else if (distToSegment(mousePos, origin, endY) < hitRadius) hoveredAxis = 1;
				else if (distToSegment(mousePos, origin, endZ) < hitRadius) hoveredAxis = 2;
			} else {
				hoveredAxis = gizmo_.Axis();
			}

			// どれかの軸に触れていれば、背面のクリック選択をしない
			if (hoveredAxis != -1) gizmoHovered = true;

			// 入力: 軸の上で押したらドラッグ開始
			if (gizmoHovered && ImGui::IsMouseClicked(0)) {
				gizmo_.BeginDrag(hoveredAxis);
				gizmoDragStartMouse = mousePos;
				gizmoStartTranslate = currentPos;
				gizmoStartRotate = {0,0,0}; // キーフレームでは回転を使わない
				gizmoStartScale = {1,1,1};
			}

			if (gizmo_.Dragging()) {
				if (ImGui::IsMouseReleased(0)) {
					// Undo の記録
					// 今の値と、ドラッグを始めたときの値
					DirectX::XMFLOAT3 endT = tc.translate;
					DirectX::XMFLOAT3 endR = tc.rotate;
					DirectX::XMFLOAT3 endS = tc.scale;
					DirectX::XMFLOAT3 startT = gizmoStartTranslate;
					DirectX::XMFLOAT3 startR = gizmoStartRotate;
					DirectX::XMFLOAT3 startS = gizmoStartScale;
					entt::entity e = selectedEnt;

					// Undo / Redo を登録する (始めの値に戻す / 今の値にする)
					PushUndo({
						"Transform",
						[=, &reg = gameScene->GetComponents()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = startT; t.rotate = startR; t.scale = startS;
							}
						},
						[=, &reg = gameScene->GetComponents()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = endT; t.rotate = endR; t.scale = endS;
							}
						}
					});

					// ドラッグを終える
					gizmo_.EndDrag();
				} else if (ImGui::IsMouseDragging(0)) {
					// 前のフレームからのマウスの移動量
					ImVec2 delta = ImGui::GetMouseDragDelta(0);
					ImGui::ResetMouseDragDelta(0);
					
					// 軸の向きへの移動量 (画面上)
					ImVec2 axisDir = (gizmo_.Axis() == 0) ? dirX : ((gizmo_.Axis() == 1) ? dirY : dirZ);
					float projMovement = delta.x * axisDir.x + delta.y * axisDir.y;
					
					// ツールごとの簡易的な感度
					float sensitivity = (gizmo_.Mode() == GizmoMode::Rotate) ? 0.02f : 0.05f;
					if (gizmo_.Mode() == GizmoMode::Scale) sensitivity = 0.01f;

					// 動かす対象 (キーフレームを選択中ならその位置)
					DirectX::XMFLOAT3* targetPos = &reg.get<TransformComponent>(selectedEnt).translate;
					if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						if (mc->selectedKeyframe < (int)kfs.size()) {
							targetPos = &kfs[mc->selectedKeyframe].translate;
						}
					}
					// 回転と大きさ (キーフレームでは使わない)
					auto* targetRot = &reg.get<TransformComponent>(selectedEnt).rotate;
					auto* targetScale = &reg.get<TransformComponent>(selectedEnt).scale;

					// 移動: 軸の方向へ動かす
					if (gizmo_.Mode() == GizmoMode::Translate) {
						if (gizmo_.Axis() == 0) targetPos->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetPos->y -= projMovement * sensitivity; // 画面の Y は上下が逆
						if (gizmo_.Axis() == 2) targetPos->z += projMovement * sensitivity;
					// 回転: 軸のまわりに回す
					} else if (gizmo_.Mode() == GizmoMode::Rotate && !mc) {
						if (gizmo_.Axis() == 0) targetRot->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetRot->y -= projMovement * sensitivity;
						if (gizmo_.Axis() == 2) targetRot->z += projMovement * sensitivity;
					// 拡大: 軸の方向へ大きくする
					} else if (gizmo_.Mode() == GizmoMode::Scale && !mc) {
						if (gizmo_.Axis() == 0) targetScale->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetScale->y -= projMovement * sensitivity;
						if (gizmo_.Axis() == 2) targetScale->z += projMovement * sensitivity;
					}
				}
			}

			// 軸を描く (触れている軸は黄色)
			// 各軸の色 (X 赤・Y 緑・Z 青、触れている軸は黄色)
			ImU32 colX = (hoveredAxis == 0) ? IM_COL32(255,255,0,255) : IM_COL32(255,50,50,255);
			ImU32 colY = (hoveredAxis == 1) ? IM_COL32(255,255,0,255) : IM_COL32(50,255,50,255);
			ImU32 colZ = (hoveredAxis == 2) ? IM_COL32(255,255,0,255) : IM_COL32(50,50,255,255);

			// 軸の線
			drawList->AddLine(origin, endX, colX, 3.0f);
			drawList->AddLine(origin, endY, colY, 3.0f);
			drawList->AddLine(origin, endZ, colZ, 3.0f);
			
			// 移動は先端に三角、拡大は先端に四角
			if (gizmo_.Mode() == GizmoMode::Translate) {
				drawList->AddTriangleFilled(endX, ImVec2(endX.x - dirX.x*10 - dirX.y*5, endX.y - dirX.y*10 + dirX.x*5), ImVec2(endX.x - dirX.x*10 + dirX.y*5, endX.y - dirX.y*10 - dirX.x*5), colX);
				drawList->AddTriangleFilled(endY, ImVec2(endY.x - dirY.x*10 - dirY.y*5, endY.y - dirY.y*10 + dirY.x*5), ImVec2(endY.x - dirY.x*10 + dirY.y*5, endY.y - dirY.y*10 - dirY.x*5), colY);
				drawList->AddTriangleFilled(endZ, ImVec2(endZ.x - dirZ.x*10 - dirZ.y*5, endZ.y - dirZ.y*10 + dirZ.x*5), ImVec2(endZ.x - dirZ.x*10 + dirZ.y*5, endZ.y - dirZ.y*10 - dirZ.x*5), colZ);
			} else if (gizmo_.Mode() == GizmoMode::Scale) {
				drawList->AddRectFilled(ImVec2(endX.x-4, endX.y-4), ImVec2(endX.x+4, endX.y+4), colX);
				drawList->AddRectFilled(ImVec2(endY.x-4, endY.y-4), ImVec2(endY.x+4, endY.y+4), colY);
				drawList->AddRectFilled(ImVec2(endZ.x-4, endZ.y-4), ImVec2(endZ.x+4, endZ.y+4), colZ);
			} else { // 回転
			}

			// モーションのキーフレームの点をクリックして選ぶ
			if (mc && !gizmoHovered) {
				if (ImGui::IsMouseClicked(0)) {
					// クリックした位置
					mousePos = ImGui::GetMousePos();
					float sx = mousePos.x, sy = mousePos.y;
					
					// カメラの行列を取り直す
					view = viewCamera.View();
					proj = viewCamera.Proj();

					if (mc->clips.count(mc->activeClip)) {
						// キーフレームを順に画面へ投影し、15 ピクセル以内なら選ぶ
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						for (int i = 0; i < (int)kfs.size(); ++i) {
							DirectX::XMVECTOR p3D = DirectX::XMLoadFloat3(&kfs[i].translate);
							projP = DirectX::XMVector3Project(p3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
							px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
							py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
							pz = DirectX::XMVectorGetZ(projP);

							if (pz > 0.0f && pz < 1.0f) {
								float dx = sx - px, dy = sy - py;
								if (std::sqrt(dx*dx + dy*dy) < 15.0f) {
									mc->selectedKeyframe = i;
									gizmoHovered = true; // オブジェクトの選択より優先する
									break;
								}
							}
						}
					}
				}
			}
		}

		// UI 部品のギズモ
		// UI 部品 (RectTransform) の編集: 枠と、移動・拡縮・判定範囲のハンドル
		if (auto* rt = reg.try_get<RectTransformComponent>(selectedEnt)) {
			// 描画先とマウスの位置
			ImDrawList* uiDrawList = ImGui::GetWindowDrawList();
			ImVec2 mPos = ImGui::GetMousePos();
				// UI の画面上の矩形 (内部解像度の座標)
				UISystem::WorldRect wr = UISystem::CalculateWorldRect(selectedEnt, gameScene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
				
				// 内部解像度 (1920x1080) から表示上のピクセルへの倍率
				float scaleX = renderW / (float)Engine::WindowDX::kW;
				float scaleY = renderH / (float)Engine::WindowDX::kH;

				// 表示上の矩形
				ImVec2 pMin(gameImageMin.x + wr.x * scaleX, gameImageMin.y + wr.y * scaleY);
				ImVec2 pMax(pMin.x + wr.w * scaleX, pMin.y + wr.h * scaleY);
				
				// UI 本体の枠 (橙)
				uiDrawList->AddRect(pMin, pMax, IM_COL32(255, 140, 0, 255), 0.0f, 0, 2.0f);
				
				// UIButton があればクリック判定の範囲も表示する (水色)
				if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
					// 判定範囲の大きさと中心 (倍率とずれから求める)
					float hw = wr.w * btn->hitboxScale.x;
					float hh = wr.h * btn->hitboxScale.y;
					float cx = wr.x + wr.w * 0.5f + btn->hitboxOffset.x;
					float cy = wr.y + wr.h * 0.5f + btn->hitboxOffset.y;
					float hx = cx - hw * 0.5f;
					float hy = cy - hh * 0.5f;

					// 表示上の判定範囲
					ImVec2 hpMin(gameImageMin.x + hx * scaleX, gameImageMin.y + hy * scaleY);
					ImVec2 hpMax(hpMin.x + hw * scaleX, hpMin.y + hh * scaleY);
					uiDrawList->AddRect(hpMin, hpMax, IM_COL32(0, 255, 255, 255), 0.0f, 0, 1.0f);

					// 判定範囲の移動ハンドル (中央の丸)
					ImVec2 hCenter((hpMin.x + hpMax.x) * 0.5f, (hpMin.y + hpMax.y) * 0.5f);
					uiDrawList->AddCircleFilled(hCenter, 6.0f, IM_COL32(0, 255, 255, 255));
					
					mPos = ImGui::GetMousePos();
					// マウスとの距離
					float dx_h = mPos.x - hCenter.x;
					float dy_h = mPos.y - hCenter.y;
					float dToHCenter = std::sqrt(dx_h * dx_h + dy_h * dy_h);

					if (dToHCenter < 10.0f) {
						gizmoHovered = true; // ハンドルに触れている間は背面のクリック選択をしない
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 10; // 10 = 判定範囲の位置
							uiDragStartHitOffset = btn->hitboxOffset;
						}
					}
					
					// 判定範囲の拡縮ハンドル (右下の丸)
					// 右下の角
					ImVec2 hScaleHandle = hpMax;
					uiDrawList->AddCircleFilled(hScaleHandle, 6.0f, IM_COL32(0, 255, 255, 255));
					// マウスとの距離
					float dx_s = mPos.x - hScaleHandle.x;
					float dy_s = mPos.y - hScaleHandle.y;
					float dToHScale = std::sqrt(dx_s * dx_s + dy_s * dy_s);
					if (dToHScale < 10.0f) {
						gizmoHovered = true;
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 11; // 11 = 判定範囲の大きさ
							uiDragStartHitScale = btn->hitboxScale;
						}
					}
				}

				// UI 本体の移動ハンドル (左上)
				uiDrawList->AddCircleFilled(pMin, 8.0f, IM_COL32(255, 140, 0, 255));
				
				mPos = ImGui::GetMousePos();
				// マウスとの距離
				float dx_p = mPos.x - pMin.x;
				float dy_p = mPos.y - pMin.y;
				float dToPMin = std::sqrt(dx_p * dx_p + dy_p * dy_p);
				if (dToPMin < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 0; // 0 = UI の位置
						uiDragStartPos = rt->pos;
					}
				}

				// UI 本体の拡縮ハンドル (右下)
				uiDrawList->AddCircleFilled(pMax, 8.0f, IM_COL32(255, 140, 0, 255));
				// マウスとの距離
				float dx_m = mPos.x - pMax.x;
				float dy_m = mPos.y - pMax.y;
				float dToPMax = std::sqrt(dx_m * dx_m + dy_m * dy_m);
				if (dToPMax < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 1; // 1 = UI の大きさ
						uiDragStartSize = rt->size;
					}
				}

				if (uiDragging) {
					gizmoHovered = true;
					// ドラッグの移動量を内部解像度の座標にする
					ImVec2 delta = ImGui::GetMouseDragDelta(0);
					float dx = delta.x / scaleX;
					float dy = delta.y / scaleY;

					// ドラッグ中:ハンドルの種類に応じて位置・大きさ・判定範囲を変える
					if (uiDragHandle == 0) { // UI の位置
						rt->pos.x = uiDragStartPos.x + dx;
						rt->pos.y = uiDragStartPos.y + dy;
					} else if (uiDragHandle == 1) { // UI の大きさ
						rt->size.x = (std::max)(1.0f, uiDragStartSize.x + dx);
						rt->size.y = (std::max)(1.0f, uiDragStartSize.y + dy);
					} else if (uiDragHandle == 10) { // 判定範囲の位置
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxOffset.x = uiDragStartHitOffset.x + dx;
							btn->hitboxOffset.y = uiDragStartHitOffset.y + dy;
						}
					} else if (uiDragHandle == 11) { // 判定範囲の大きさ
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxScale.x = (std::max)(0.01f, uiDragStartHitScale.x + dx / wr.w);
							btn->hitboxScale.y = (std::max)(0.01f, uiDragStartHitScale.y + dy / wr.h);
						}
					}

					// 離したらドラッグを終える
					if (ImGui::IsMouseReleased(0)) {
						uiDragging = false;
						uiDragHandle = -1;
				}
			}
		}
	}

	// クリック選択 (PLAY 中は無効)
	if (!isPlaying && imageHovered && !overToolbar && !gizmoHovered && !s_sceneCameraNavigating) {
		// クリック選択: UI を優先し、無ければ 3D のメッシュにレイを当てて一番近いものを選ぶ
		if (ImGui::IsMouseClicked(0)) {
			// クリックした位置 (画像の左上が原点)
			ImVec2 mousePos = ImGui::GetMousePos();
			float sx = mousePos.x - gameImageMin.x;
			float sy = mousePos.y - gameImageMin.y;
			
			// 画像の中なら判定する
			if (sx >= 0 && sx <= renderW && sy >= 0 && sy <= renderH) {

				// 当たったもの
				entt::entity hitE = entt::null;
				
				// 1. UI を優先して判定する
				int maxLayer = -10000;
				// UI の矩形にマウスが入っているものを探す
				gameScene->GetComponents().view<RectTransformComponent>().each([&](entt::entity e, const RectTransformComponent&) {
					UISystem::WorldRect wr = UISystem::CalculateWorldRect(e, gameScene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
					if (internalMouseX >= wr.x && internalMouseX <= wr.x + wr.w &&
						internalMouseY >= wr.y && internalMouseY <= wr.y + wr.h) {
						
						// 重なり順 (画像があればその値)
						int layer = 0;
						if (auto* img = gameScene->GetComponents().try_get<UIImageComponent>(e)) {
							layer = img->layer;
						}
						// より手前（レイヤーが大きい）のUIを優先
						if (hitE == entt::null || layer >= maxLayer) {
							maxLayer = layer;
							hitE = e;
						}
					}
				});

				// 2. UI に当たらなければ 3D のメッシュを判定する
				if (hitE == entt::null && sceneView) {
					// クリックした位置からのレイ
					DirectX::XMVECTOR rayOrig, rayDir;
					ScreenToWorldRay(sx, sy, (float)renderW, (float)renderH, viewCamera.View(), viewCamera.Proj(), rayOrig, rayDir);
					
					// 一番近い距離
					float minD = FLT_MAX;
					gameScene->GetComponents().view<MeshRendererComponent, TransformComponent>().each([&](entt::entity e, const MeshRendererComponent& mr, [[maybe_unused]] const TransformComponent& tc) {
						// モデルが無ければ飛ばす
						if (mr.modelHandle == 0) return;
						auto* m = renderer->GetModel(mr.modelHandle);
						if (!m) return;

						float d; Engine::Vector3 p;
						// ロックされているオブジェクトはピッキング（クリック選択）させない
						if (auto* esc = gameScene->GetComponents().try_get<EditorStateComponent>(e)) {
							if (esc->locked) return;
						}

						// 当たったら、一番近いものを覚える
						if (m->RayCast(rayOrig, rayDir, gameScene->GetWorldMatrix(static_cast<int>(e)), d, p)) {
							if (d < minD) { minD = d; hitE = e; }
						}
					});
				}

				// 当たったものを選ぶ (無ければ選択を外す)
				if (hitE != entt::null) {
					gameScene->SelectEntity(hitE);
				} else {
					gameScene->SelectEntity(entt::null);
				}
			}
		}
	}

	// Scene ビューならツールバーを描く
	if (sceneView) DrawSceneToolbar(gameScene, gameImageMin);
}

// 画面上の点からワールド空間のレイ (原点と向き) を求める
void EditorUI::ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir) {
	// ビュー行列と射影行列の逆行列
	DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);

	// 画面の位置を -1〜1 の正規化デバイス座標にする
	float vx = (2.0f * screenX / imageW - 1.0f);
	float vy = (1.0f - 2.0f * screenY / imageH);

	// 奥の面の点をビュー空間、ワールド空間へ戻す
	DirectX::XMVECTOR rayEndNDC = DirectX::XMVectorSet(vx, vy, 1.0f, 1.0f);
	DirectX::XMVECTOR rayEndView = DirectX::XMVector3TransformCoord(rayEndNDC, invProj);
	DirectX::XMVECTOR rayEndWorld = DirectX::XMVector3TransformCoord(rayEndView, invView);

	// 原点はカメラの位置、向きはそこから奥の点へ
	outOrig = invView.r[3];
	outDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(rayEndWorld, outOrig));
}

} // namespace Game
