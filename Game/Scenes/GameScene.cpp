#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../ObjectTypes.h"
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // 笘・ｿｽ蜉

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // 笘・ｿｽ蜉
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/PlayerActionSystem.h" // 笘・ｿｽ蜉: 繝励Ξ繧､繝､繝ｼ繧｢繧ｯ繧ｷ繝ｧ繝ｳ
#include "../Systems/CombatSystem.h"       // 笘・ｿｽ蜉: 謌ｦ髣伜愛螳・
#include "../Systems/EnemyAISystem.h"      // 笘・ｿｽ蜉: 謨ｵAI
#include "../Systems/BossActionSystem.h"    // 笘・ｿｽ蜉: 繝懊せAI
#include "../Systems/WaveSystem.h"         // 笘・ｿｽ蜉: 繧ｦ繧ｧ繝ｼ繝也ｮ｡逅・

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // 笘・ｿｽ蜉
#include "imgui.h"
#include <Windows.h> // OutputDebugStringA
#include <algorithm>
#include <cmath>
#include <filesystem> // 笘・ｿｽ蜉: Skybox DDS讀懃ｴ｢逕ｨ

namespace Game {

namespace {
	// CPU蛛ｴ縺ｧ縺ｮ縺ｷ繧医・繧域─險育ｮ礼畑縺ｮ邁｡譏・D繝弱う繧ｺ髢｢謨ｰ (SimplexNoise莉｣逕ｨ)
	float Noise3D(float x, float y, float z) {
		float n = std::sin(x) * std::cos(y) * std::sin(z);
		n += std::sin(x * 2.1f + y * 1.3f) * 0.5f;
		n += std::cos(y * 1.7f + z * 2.5f) * 0.5f;
		return n * 0.5f;
	}
}

GameScene::~GameScene() {
	// 笘・ｿｽ蜉: 遐ｴ譽・凾縺ｫ繧ｷ繧ｰ繝翫Ν繧定ｧ｣髯､縺励∝ｮ牙・縺ｫ繝ｬ繧ｸ繧ｹ繝医Μ繧偵け繝ｪ繧｢縺吶ｋ
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.clear();
}

void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	eventSystem_.Clear(); // 笘・ｿｽ蜉: 繧､繝吶Φ繝医Μ繧ｹ繝翫・繧偵け繝ｪ繧｢
	playTime_ = 0.0f;
	camera_.Initialize();
	// 笘・ｿｽ蜉: 譏守､ｺ逧・↓繝励Ο繧ｸ繧ｧ繧ｯ繧ｷ繝ｧ繝ｳ繧定ｨｭ螳・(1920x1080縺ｮ繧｢繧ｹ繝壹け繝域ｯ・
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
	renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});

	bool loaded = false;
	// 笘・繝ｪ繝ｪ繝ｼ繧ｹ讒区・遲峨〒縺ｮ閾ｪ蜍輔Ο繝ｼ繝・
	try {
		std::string scenePath = params.stagePath.empty() ? EditorUI::GetUnifiedProjectPath("Resources/Scenes/scene.json") : params.stagePath;
		// 笘・ｿｮ豁｣: UTF-8譁・ｭ怜・繧巽romUTF8邨檎罰縺ｧfs::path縺ｫ螟画鋤縺励∵律譛ｬ隱槭ヱ繧ｹ縺ｫ蟇ｾ蠢・
		if (std::filesystem::exists(Engine::PathUtils::FromUTF8(scenePath))) {
			OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
			EditorUI::LoadScene(this, scenePath);
			isPlaying_ = true; // 繝ｪ繝ｪ繝ｼ繧ｹ/襍ｷ蜍墓凾縺ｯ繝励Ξ繧､迥ｶ諷九°繧蛾幕蟋九☆繧・
			loaded = true;
		} else {
			OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
		}
	} catch (const std::exception& e) {
		std::string msg = "[GameScene] EXCEPTION during scene load: " + std::string(e.what()) + "\n";
		OutputDebugStringA(msg.c_str());
		MessageBoxA(NULL, msg.c_str(), "Scene Load Error", MB_OK | MB_ICONERROR);
	}

	// 譌｢縺ｫ繧ｪ繝悶ず繧ｧ繧ｯ繝医′蟄伜惠縺吶ｋ蝣ｴ蜷茨ｼ医Μ繧ｹ繧ｿ繝ｼ繝域凾・峨ｄ繝ｭ繝ｼ繝牙､ｱ謨玲凾縺ｯ譛菴朱剞縺ｮ蜀・ｮｹ繧剃ｽ懈・
	if (registry_.storage<entt::entity>().empty() || !loaded) {
		auto sun = registry_.create();
		registry_.emplace<NameComponent>(sun, "Sun");
		registry_.emplace<TransformComponent>(
		    sun, DirectX::XMFLOAT3{0, 10, 0}, DirectX::XMFLOAT3{DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(30.0f), 0}, DirectX::XMFLOAT3{1, 1, 1});
		registry_.emplace<DirectionalLightComponent>(sun);

		auto plane = registry_.create();
		registry_.emplace<NameComponent>(plane, "Plane");

		auto& mesh = registry_.emplace<MeshRendererComponent>(plane);
		mesh.modelHandle = renderer_->LoadObjMesh("Resources/Models/plane.obj");
		mesh.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		mesh.modelPath = "Resources/Models/plane.obj";
		mesh.texturePath = "Resources/Textures/white1x1.png";

		registry_.emplace<TransformComponent>(plane, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{20, 1, 20});

		// 笘・ｿｽ蜉: 迚ｩ逅・愛螳夂畑縺ｫGpuMeshCollider繧剃ｻ倅ｸ・
		auto& gmc = registry_.emplace<GpuMeshColliderComponent>(plane);
		gmc.meshHandle = mesh.modelHandle;
		gmc.enabled = true;

	}

	// 笘・ｿｽ蜉: Ring 繝｡繝・す繝･縺ｮ逕滓・縺ｨ陦ｨ遉ｺ (CG4_01_01 Ring隱ｲ鬘・
	// 繧ｷ繝ｼ繝ｳ繝ｭ繝ｼ繝峨・譛臥┌縺ｫ髢｢繧上ｉ縺壼ｸｸ縺ｫRing繧堤函謌舌☆繧・
	{
		auto ring = registry_.create();
		registry_.emplace<NameComponent>(ring, "Ring");
		auto& ringMesh = registry_.emplace<MeshRendererComponent>(ring);
		ringMesh.modelHandle = renderer_->CreateRingMesh(2.0f, 1.0f, 32); // 螟門濠蠕・.0, 蜀・濠蠕・.0, 蛻・牡謨ｰ32
		ringMesh.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		ringMesh.texturePath = "Resources/Textures/white1x1.png";
		registry_.emplace<TransformComponent>(ring,
			DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f},   // 菴咲ｽｮ: 蝨ｰ髱｢繧医ｊ蟆代＠荳・
			DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f},   // 蝗櫁ｻ｢: 縺ｪ縺・
			DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f});   // 繧ｹ繧ｱ繝ｼ繝ｫ: 遲牙・
		registry_.emplace<ColorComponent>(ring, DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f});
	}

	Game::FluidSystem::GetInstance()->Initialize(renderer_->GetDevice());

	// 繧ｨ繝・ぅ繧ｿ繝ｼUI縺ｮ蛻晄悄蛹・
	EditorUI::Initialize(renderer_);

	// 笘・ｿｽ蜉: Skybox逕ｨ繧ｭ繝･繝ｼ繝悶・繝・・縺ｮ隱ｭ縺ｿ霎ｼ縺ｿ (00. 迺ｰ蠅・・繝・・)
	// DDS繝輔ぃ繧､繝ｫ縺・Resources/Textures/ 縺ｫ驟咲ｽｮ縺輔ｌ縺ｦ縺・ｌ縺ｰ隱ｭ縺ｿ霎ｼ繧
	// 豕ｨ諢・ rostock_laage_airport_4k.dds 縺ｯ菴ｿ逕ｨ遖∵ｭ｢
	{
		namespace fs = std::filesystem;
		std::string texDir = EditorUI::GetUnifiedProjectPath("Resources/Textures");
		try {
			for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(texDir))) {
				if (entry.is_regular_file() && entry.path().extension() == L".dds") {
					std::string filename = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
					// 菴ｿ逕ｨ遖∵ｭ｢縺ｮ繝輔ぃ繧､繝ｫ繧偵せ繧ｭ繝・・
					if (filename.find("rostock_laage_airport") != std::string::npos) continue;
					
					std::string ddsPath = Engine::PathUtils::ToUTF8(entry.path().wstring());
					auto cubeHandle = renderer_->LoadCubeMap(ddsPath);
					if (cubeHandle > 0) {
						renderer_->SetSkyboxTexture(cubeHandle);
						OutputDebugStringA(("[GameScene] Skybox loaded: " + filename + "\n").c_str());
					}
					break; // 譛蛻昴↓隕九▽縺九▲縺櫂DS繧剃ｽｿ逕ｨ
				}
			}
		} catch (...) {
			OutputDebugStringA("[GameScene] Skybox DDS search failed, using default\n");
		}
	}

	// 繝代・繝・ぅ繧ｯ繝ｫ繧ｨ繝・ぅ繧ｿ繝ｼ縺ｮ蛻晄悄蛹・
	particleEditor_.Initialize();

	// 繧ｹ繧ｯ繝ｪ繝励ヨ繧ｨ繝ｳ繧ｸ繝ｳ縺ｮ蛻晄悄蛹・
	ScriptEngine::GetInstance()->Initialize();

	// 笘・System縺ｮ逋ｻ骭ｲ・磯・ｺ上′驥崎ｦ・ｼ・
	systems_.clear();
	systems_.push_back(std::make_unique<PlayerInputSystem>());
	systems_.push_back(std::make_unique<PlayerActionSystem>());  // 笘・ｿｽ蜉: 謾ｻ謦・・繝代Μ繧｣繝ｻ蝗樣∩
	systems_.push_back(std::make_unique<WeaponSystem>());        // 笘・ｿｽ蜉: 豁ｦ蝎ｨ縺ｮ邂｡逅・・繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ
	systems_.push_back(std::make_unique<CharacterMovementSystem>());
	systems_.push_back(std::make_unique<PhysicsSystem>());
	systems_.push_back(std::make_unique<EnemyAISystem>());      // 笘・ｿｽ蜉: 謨ｵAI・・ombatSystem縺ｮ蜑搾ｼ・
	systems_.push_back(std::make_unique<BossActionSystem>());   // 笘・ｿｽ蜉: 繝懊せAI・・ombatSystem縺ｮ蜑搾ｼ・
	systems_.push_back(std::make_unique<CombatSystem>());         // 笘・ｿｽ蜉: Hitbox vs Hurtbox 蛻､螳・
	systems_.push_back(std::make_unique<CameraFollowSystem>());
	systems_.push_back(std::make_unique<HealthSystem>());
	systems_.push_back(std::make_unique<WaveSystem>());           // 笘・ｿｽ蜉: 繧ｦ繧ｧ繝ｼ繝也ｮ｡逅・

	auto scriptSys = std::make_unique<ScriptSystem>();
	scriptSys->SetScene(this);
	systems_.push_back(std::move(scriptSys));


	systems_.push_back(std::make_unique<AudioSystem>());
	systems_.push_back(std::make_unique<UISystem>());
	systems_.push_back(std::make_unique<MotionSystem>());
	systems_.push_back(std::make_unique<PostProcessSystem>()); // 笘・ｿｽ蜉
	systems_.push_back(std::make_unique<CleanupSystem>());

	// 笘・ｿｽ蜉: 謫堺ｽ懆ｪｬ譏弱ユ繧ｭ繧ｹ繝・I縺ｮ逕滓・
	{
		std::vector<std::string> controls = {
			"【操作説明】",
			"W, A, S, D : 移動",
			"Space : ジャンプ",
			"Shift : 回避",
			"左クリック : 攻撃(大剣は長押しで溜め)",
			"右クリック : パリィ",
			"中クリック : ロックオン",
			"マウス : 視点移動"
		};
		float startX = 20.0f;
		float startY = 120.0f; // 繝励Ξ繧､繝､繝ｼHUD(蟾ｦ荳・縺ｫ陲ｫ繧峨↑縺・ｈ縺・↓荳九￡繧・
		float gapY = 35.0f;
		for (size_t i = 0; i < controls.size(); ++i) {
			auto textEntity = registry_.create();
			registry_.emplace<NameComponent>(textEntity, "ControlText_" + std::to_string(i));
			auto& tc = registry_.emplace<TransformComponent>(textEntity);
			tc.translate.x = startX;
			tc.translate.y = startY + (i * gapY);
			tc.translate.z = 0.0f;
			
			auto& txt = registry_.emplace<UITextComponent>(textEntity);
			txt.text = controls[i];
			txt.fontSize = (i == 0) ? 28.0f : 24.0f;
			txt.color = (i == 0) ? DirectX::XMFLOAT4{1.0f, 0.9f, 0.5f, 1.0f} : DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f}; // 隕句・縺励・鮟・牡
		}
	}

	// 笘・ｿｽ蜉: 襍ｷ蜍慕峩蠕後・迥ｶ諷九ｒ蛻晄悄繧ｹ繝翫ャ繝励す繝ｧ繝・ヨ縺ｨ縺励※菫晏ｭ・
	initialSceneSnapshot_ = EditorUI::SaveToMemory(this);



	// 蜷Тystem縺ｮ繝ｪ繧ｻ繝・ヨ
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}



	// 笘・ｿｽ蜉: 繧ｿ繧ｰ繧ｷ繧ｹ繝・Β縺ｮ蛻晄悄蛹・
	tagCache_.clear();
	pendingTagSync_.clear();
	pendingTagRemoved_.clear();
	auto tagInitView = registry_.view<TagComponent>();
	for (auto entity : tagInitView) {
		const auto tag = tagInitView.get<TagComponent>(entity).tag;
		tagCache_[tag].push_back(entity);
	}
	// 繝ｪ繧ｹ繝翫・逋ｻ骭ｲ
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);

}

// =====================================================
// 笘・Update: 蜷Тystem縺ｫ蜃ｦ逅・ｒ蟋碑ｭｲ
// =====================================================
void GameScene::Update() {
	if (!renderer_) return;

	// 笘・ｿｽ蜉: 陦悟・繧ｭ繝｣繝・す繝･繧呈ｯ弱ヵ繝ｬ繝ｼ繝繧ｯ繝ｪ繧｢
	ClearMatrixCache();

	// 笘・ｿｽ蜉: 繧ｿ繧ｰ縺ｮ驕・ｻｶ蜷梧悄縺翫ｈ縺ｳ蜑企勁・育函謌千峩蠕後ｄ遐ｴ譽・凾縺ｮ蜷梧悄蠕・■繧貞・逅・ｼ・
	// 繝ｪ繧ｹ繝医′遨ｺ縺ｧ縺ｪ縺・ｴ蜷医・縺ｿ蜃ｦ逅・
	if (!pendingTagRemoved_.empty() || !pendingTagSync_.empty()) {
		// 1. 蜑企勁繝ｻ螟画峩莠亥ｮ壹・繧ｨ繝ｳ繝・ぅ繝・ぅ繧貞・繧ｭ繝｣繝・す繝･縺九ｉ蜿悶ｊ髯､縺・
		std::vector<entt::entity> toRemove = std::move(pendingTagRemoved_);
		// pendingTagSync 縺ｫ蜈･縺｣縺ｦ縺・ｋ繧ゅ・縺ｯ縲後ち繧ｰ縺悟､峨ｏ繧九榊庄閭ｽ諤ｧ縺後≠繧九・縺ｧ縲∽ｸ譌ｦ蜿､縺・く繝｣繝・す繝･縺九ｉ豸医＠縺ｦ縺翫￥
		for (auto e : pendingTagSync_) {
			toRemove.push_back(e);
		}

		for (auto e : toRemove) {
			for (auto& pair : tagCache_) {
				auto& vec = pair.second;
				// 縺吶∋縺ｦ縺ｮ繧ｿ繧ｰ繝ｪ繧ｹ繝医°繧峨√◎縺ｮ繧ｨ繝ｳ繝・ぅ繝・ぅ繧貞炎髯､
				vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
			}
		}

		// 2. 譛譁ｰ縺ｮ繧ｿ繧ｰ縺ｧ蜷梧悄
		std::vector<entt::entity> toSync = std::move(pendingTagSync_);
		for (auto e : toSync) {
			if (registry_.valid(e)) {
				SyncTag(e);
			}
		}
	}

	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	if (dt > 1.0f / 10.0f)
		dt = 1.0f / 60.0f; // 讌ｵ遶ｯ縺ｪ繝ｩ繧ｰ蟇ｾ遲・

	// 繧ｳ繝ｳ繝・く繧ｹ繝医ｒ譖ｴ譁ｰ
	ctx_.dt = dt;

	if (isPlaying_) {
		playTime_ += dt;
		// 笘・ｿｽ蜉: Play荳ｭ縺ｯ繝槭え繧ｹ繧ｫ繝ｼ繧ｽ繝ｫ繧堤判髱｢荳ｭ螟ｮ縺ｫ蝗ｺ螳・
		if (dx_ && dx_->GetHwnd()) {
			POINT center = { (LONG)Engine::WindowDX::kW / 2, (LONG)Engine::WindowDX::kH / 2 };
			ClientToScreen(dx_->GetHwnd(), &center);
			SetCursorPos(center.x, center.y);
		}
	}




	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = isPlaying_;
	ctx_.scene = this;
	ctx_.eventSystem = &eventSystem_;
	ctx_.pendingSpawns = &pendingSpawns_;

	// 笘・ｿｽ蜉: 繝薙Η繝ｼ繝昴・繝域ュ蝣ｱ繧偵ョ繝輔か繝ｫ繝医・繧ｦ繧｣繝ｳ繝峨え繧ｵ繧､繧ｺ縺ｧ蛻晄悄險ｭ螳・(繧ｨ繝・ぅ繧ｿ髱槫ｮ溯｡梧凾縺ｮ繝ｬ繧､繧｢繧ｦ繝亥ｴｩ繧碁亟豁｢)
	ctx_.viewportOffset = { 0.0f, 0.0f };
	ctx_.viewportSize = { (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };

	// GPU Collision Dispatch・医お繝ｳ繧ｸ繝ｳ縺ｮ豎守畑 PhysicsSystem.h 縺ｫ遘ｻ陦後＠縺溘◆繧√√％縺薙〒縺ｯ菴輔ｂ縺励↑縺・ｼ・

	// Animation・医お繝ｳ繧ｸ繝ｳ蝗ｺ譛牙・逅・・縺溘ａ谿狗蕗・・
	auto animView = registry_.view<AnimatorComponent, MeshRendererComponent>();
	if (isPlaying_) {
		std::vector<entt::entity> animEntities;
		animView.each([&](entt::entity entity, auto&, auto&) { animEntities.push_back(entity); });

		if (!animEntities.empty()) {
			Engine::JobSystem::Dispatch((uint32_t)animEntities.size(), 64, [&](uint32_t i) {
				auto entity = animEntities[i];
				auto& anim = registry_.get<AnimatorComponent>(entity);
				auto& meshWrapper = registry_.get<MeshRendererComponent>(entity);

				if (anim.enabled && anim.isPlaying) {
					anim.time += dt * 60.0f * anim.speed;
					auto* m = renderer_->GetModel(meshWrapper.modelHandle);
					if (m) {
						const auto& data = m->GetData();
						for (const auto& a : data.animations) {
							if (a.name == anim.currentAnimation) {
								if (anim.time > a.duration) {
									if (anim.loop)
										anim.time = std::fmod(anim.time, a.duration);
									else {
										anim.time = a.duration;
										anim.isPlaying = false;
									}
								}
								break;
							}
						}
					}
				}
			});
			Engine::JobSystem::Wait();
		}
	}

	// 繝代・繝・ぅ繧ｯ繝ｫ繧ｨ繝・ぅ繧ｿ繝ｼ
	particleEditor_.Update(dt);

	// 笘・蜈ｨSystem繧帝・↓螳溯｡・
	for (auto& system : systems_) {
		// 繝ｪ繧ｶ繝ｫ繝磯・遘ｻ荳ｭ縺ｪ縺ｩ縺ｯ繧ｷ繧ｹ繝・Β繧貞虚縺九＆縺ｪ縺・(繧ｨ繝ｳ繝・ぅ繝・ぅ縺悟炎髯､縺輔ｌ縺ｦ縺・ｋ蜿ｯ閭ｽ諤ｧ縺後≠繧九◆繧・
		if (!isPlaying_)
			break;
		system->Update(registry_, ctx_);
	}

	// 笘・霑ｽ蜉: 蛛懈ｭ｢荳ｭ縺ｮ縺ｿ繝・ヰ繝・げ繧ｫ繝｡繝ｩ繧呈怏蜉ｹ蛹・
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
	camera_.Tick(dt);







	// 笘・繝壹Φ繝・ぅ繝ｳ繧ｰ繧ｪ繝悶ず繧ｧ繧ｯ繝茨ｼ亥ｼｾ縺ｪ縺ｩ・峨ｒflush縺励∫ｴ譽・ｦ∵ｱゅｒ蜃ｦ逅・
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 荳譌ｦ縲｝endingSpawns_ 繧偵ム繝溘・縺ｨ縺励※驕狗畑縺吶ｋ縺九∫峩謗･ `registry_.create()` 縺吶ｋ縺ｮ縺ｧ縺薙％縺ｯ螳溯ｳｪ遨ｺ縺ｫ縺ｪ繧・
			pendingSpawns_.clear();
		}

		if (!pendingDestroys_.empty()) {
			for (auto id : pendingDestroys_) {
				if (registry_.valid(id)) {
					registry_.destroy(id);
				}
			}
			pendingDestroys_.clear();
		}
	}

	// Light System・医Ξ繝ｳ繝繝ｪ繝ｳ繧ｰ險ｭ螳壹・縺溘ａ谿狗蕗・・
	if (renderer_) {
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		auto dirLightView = registry_.view<DirectionalLightComponent, TransformComponent>();
		dirLightView.each([&](auto, const DirectionalLightComponent& dl, const TransformComponent& tc) {
			if (dl.enabled && !hasDirLight) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
				renderer_->SetDirectionalLight(dir, color, true);
				hasDirLight = true;
			}
		});

		auto plView = registry_.view<PointLightComponent, TransformComponent>();
		plView.each([&](auto, const PointLightComponent& pl, const TransformComponent& tc) {
			if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
				Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
				renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
				plCount++;
			}
		});

		auto slView = registry_.view<SpotLightComponent, TransformComponent>();
		slView.each([&](auto, const SpotLightComponent& sl, const TransformComponent& tc) {
			if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
				Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
				renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
				slCount++;
			}
		});

		if (!hasDirLight) {
			renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
		}
		for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
			renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
		}
		for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
			renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
		}
	}

	// 繝代・繝・ぅ繧ｯ繝ｫ繧ｨ繝溘ャ繧ｿ繝ｼ繧ｳ繝ｳ繝昴・繝阪Φ繝・
	auto peView = registry_.view<ParticleEmitterComponent, TransformComponent, NameComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe, const TransformComponent& tc, const NameComponent& nc) {
		if (!pe.enabled)
			return;

		if (!pe.isInitialized && renderer_) {
			pe.emitter.Initialize(*renderer_, nc.name + "_Emitter");
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}
			pe.isInitialized = true;
		}

		pe.emitter.params.position = {tc.translate.x, tc.translate.y, tc.translate.z};
		pe.emitter.Update(dt);
	});
}

// 笘・豎守畑繧ｹ繝昴・繝ｳ
entt::entity GameScene::CreateEntity(const std::string& name) {
	std::lock_guard<std::mutex> lock(spawnMutex_);
	auto entity = registry_.create();
	registry_.emplace<NameComponent>(entity, name);
	registry_.emplace<TransformComponent>(entity);
	return entity;
}

// 笘・ｿｽ蜉: ID縺ｧ繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ讀懃ｴ｢縺励∫ｴ譽・ヵ繝ｩ繧ｰ繧堤ｫ九※繧・
void GameScene::DestroyObject(uint32_t id) {
	std::lock_guard<std::mutex> lock(spawnMutex_);
	// ID繧偵◎縺ｮ縺ｾ縺ｾentt::entity縺ｨ縺励※謇ｱ縺・ｼ医ム繧ｦ繝ｳ繧ｭ繝｣繧ｹ繝茨ｼ・
	pendingDestroys_.push_back(static_cast<entt::entity>(id));
}


// 笘・ｿｽ蜉: 蜷榊燕縺ｧ繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ讀懃ｴ｢
entt::entity GameScene::FindObjectByName(const std::string& name) {
	auto view = registry_.view<NameComponent>();
	for (auto entity : view) {
		if (view.get<NameComponent>(entity).name == name) {
			return entity;
		}
	}
	return entt::null;
}

// 笘・ｿｽ蜉: 謖・ｮ壼ｺｧ讓吶・繝｡繝・す繝･陦ｨ髱｢逧・ｫ倥＆繧貞叙蠕・
float GameScene::GetHeightAt(float x, float z, float startY, uint32_t excludeId) {
	float maxHeight = -1000.0f;
	bool hitAny = false;

	// 謖・ｮ壹＆繧後◆ startY (縺ｾ縺溘・繝・ヵ繧ｩ繝ｫ繝・1000) 縺九ｉ荳句髄縺阪↓繝ｬ繧､繧帝｣帙・縺・
	DirectX::XMVECTOR rayPos = DirectX::XMVectorSet(x, startY, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		bool isEnemyOrBullet = false;
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX) {
				isEnemyOrBullet = true;
			}
		}
		if (isEnemyOrBullet)
			continue;

		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}

		if (modelHandle == 0)
			continue;

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = this->GetWorldMatrix(static_cast<int>(entity));

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (hitPoint.y > maxHeight) {
				maxHeight = hitPoint.y;
				hitAny = true;
			}
		}
	}

	return hitAny ? maxHeight : -10000.0f;
}

bool GameScene::RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist) {
	bool hitAny = false;
	float minDist = maxDist;

	DirectX::XMVECTOR rayPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&origin));
	DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&direction));

	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// 繧ｿ繧ｰ縺ｫ繧医ｋ繝輔ぅ繝ｫ繧ｿ繝ｪ繝ｳ繧ｰ
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX)
				continue;
		}

		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}
		if (modelHandle == 0)
			continue;

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = GetWorldMatrix(static_cast<int>(entity));

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (dist < minDist) {
				minDist = dist;
				hitAny = true;
			}
		}
	}

	if (hitAny) {
		outDist = minDist;
		return true;
	}
	return false;
}

Engine::Matrix4x4 GameScene::GetWorldMatrix(int entityId) const { return GetWorldMatrixRecursive(static_cast<entt::entity>(entityId), 0); }

Engine::Matrix4x4 GameScene::GetWorldMatrixRecursive(entt::entity e, int depth) const {
	if (depth > 32)
		return Engine::Matrix4x4::Identity();

	auto it = matrixCache_.find(e);
	if (it != matrixCache_.end())
		return it->second;

	if (!registry_.valid(e) || !registry_.all_of<TransformComponent>(e))
		return Engine::Matrix4x4::Identity();
	const auto& tc = registry_.get<TransformComponent>(e);
	Engine::Matrix4x4 local = tc.ToMatrix();

	Engine::Matrix4x4 world = local;
	if (registry_.all_of<HierarchyComponent>(e)) {
		const auto& hc = registry_.get<HierarchyComponent>(e);
		if (hc.parentId != entt::null && registry_.valid(hc.parentId)) {
			world = Engine::Matrix4x4::Multiply(local, GetWorldMatrixRecursive(hc.parentId, depth + 1));
		}
	}
	matrixCache_[e] = world;
	return world;
}

void GameScene::Draw() {
	if (!renderer_)
		return;

	DirectX::XMFLOAT3 corePos = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 blobRadii = { 1.2f, 0.85f, 1.2f };
	DirectX::XMFLOAT3 inputForce = { 0.0f, 0.0f, 0.0f };
	bool isLiquidated = false;
	bool hasPlayerSlime = false;

	const auto& playersForCore = GetEntitiesByTag(TagType::Player);
	if (!playersForCore.empty() && registry_.valid(playersForCore[0])) {
		entt::entity playerEntity = playersForCore[0];
		if (registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			corePos = { tc.translate.x, tc.translate.y, tc.translate.z };
			blobRadii = { tc.scale.x, tc.scale.y, tc.scale.z };
		}
		if (registry_.all_of<RigidbodyComponent>(playerEntity)) {
			auto& rb = registry_.get<RigidbodyComponent>(playerEntity);
			inputForce = rb.velocity; // 騾溷ｺｦ繧呈ｵ∽ｽ薙∈縺ｮ螟門鴨縺ｨ縺励※驕ｩ逕ｨ
		}
		if (registry_.all_of<PlayerActionComponent>(playerEntity)) {
			isLiquidated = registry_.get<PlayerActionComponent>(playerEntity).state == PlayerActionState::Liquefy;
		}
		if (registry_.all_of<MeshRendererComponent>(playerEntity)) {
			auto& mr = registry_.get<MeshRendererComponent>(playerEntity);
			
			// --- CPU蛛ｴ繧ｹ繝ｩ繧､繝繝｡繝・す繝･譖ｴ譁ｰ繝ｭ繧ｸ繝・け ---
			if (!slimeCpuLogic_.initialized) {
				auto* model = renderer_->GetModel(mr.modelHandle);
				if (model) {
					slimeCpuLogic_.baseVertices = model->GetData().vertices;
					// 豕慕ｷ壹・蛻晄悄迥ｶ諷九ｒ菫晏ｭ・
					slimeCpuLogic_.baseNormals = slimeCpuLogic_.baseVertices; 
					slimeCpuLogic_.indices = model->GetData().indices;
					slimeCpuLogic_.dynamicVerts = slimeCpuLogic_.baseVertices;
					
					// 蜍慕噪繝｡繝・す繝･繧堤函謌・
					slimeCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(slimeCpuLogic_.baseVertices, slimeCpuLogic_.indices);
					
					// 繝代・繝・ぅ繧ｯ繝ｫ縺ｮ蛻晄悄蛹・
					slimeCpuLogic_.particles.resize(50);
					for (auto& p : slimeCpuLogic_.particles) {
						Engine::Vector3 randPos = { (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f };
						p.basePos = Engine::Normalize(randPos);
						p.basePos.x *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.y *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.z *= ((rand()%100)/100.0f) * 1.5f;
						p.color = {0.2f, 0.8f, 1.0f, 1.0f}; // 髱堤區縺・・
					}
					slimeCpuLogic_.initialized = true;
				}
			}

			if (slimeCpuLogic_.initialized && registry_.all_of<TransformComponent>(playerEntity)) {
				// 蜍慕噪繝｡繝・す繝･縺ｫ蟾ｮ縺玲崛縺・
				mr.modelHandle = slimeCpuLogic_.dynamicMeshHandle;
				// 繧ｷ繧ｧ繝ｼ繝繝ｼ縺ｯ縺ｲ縺ｨ縺ｾ縺售lime縺ｮ縺ｾ縺ｾ縺ｫ縺吶ｋ・亥ｱ域釜繝槭ユ繝ｪ繧｢繝ｫ繧堤函縺九☆縺溘ａ・・
				mr.shaderName = "Slime";
				
				auto& tc = registry_.get<TransformComponent>(playerEntity);
				slimeCpuLogic_.time += ctx_.dt;
				
				const float noiseScale = 0.5f;
				const float noiseSpeed = 1.5f;

				for (size_t i = 0; i < slimeCpuLogic_.baseVertices.size(); ++i) {
					const auto& baseV = slimeCpuLogic_.baseVertices[i];
					const auto& baseN = slimeCpuLogic_.baseNormals[i];
					auto& dynV = slimeCpuLogic_.dynamicVerts[i];

					// 元の球体頂点からスライムのデフォルト形状を計算
					float bx = baseV.position.x;
					float by = baseV.position.y;
					float bz = baseV.position.z;

					// t は 0.0 (底: -0.5) から 1.0 (頂点: 0.5)
					float t = by + 0.5f;
					t = std::max(0.0f, std::min(1.0f, t));

					// スライムの裾野を広げ、てっぺんをすぼめるスケール
					float invT = 1.0f - t;
					// てっぺんの丸みを維持するため、頂点付近(invT=0)では sFactor を 1.0 に近づけ、下部にかけて広げる
					float sFactor = 1.0f + 1.4f * (invT * invT);

					// y方向の歪み（底に向かって少し持ち上げることで、肉厚な丸みを作る）
					float newY = by;
					if (t < 0.4f) {
						float k = (0.4f - t) / 0.4f;
						newY += 0.08f * std::sin(k * 1.57079f); // 90度 (PI/2)
					}

					// スライム形状のベース位置 (全体のスケールを大きくする)
					float scaleUp = 2.6f;
					float sx = bx * sFactor * scaleUp;
					float sz = bz * sFactor * scaleUp;
					float sy = newY * scaleUp;

					// 1. 繝弱う繧ｺ縺ｫ繧医ｋ鬆らせ縺ｮ謠ｺ繧峨℃
					float noise = Noise3D(
						sx * noiseScale + slimeCpuLogic_.time * noiseSpeed,
						sy * noiseScale,
						sz * noiseScale + slimeCpuLogic_.time * noiseSpeed
					);

					// 元の法線方向にノイズを乗せる
					Engine::Vector3 v = {
						sx + baseN.normal.x * (noise * 0.25f),
						sy + baseN.normal.y * (noise * 0.25f),
						sz + baseN.normal.z * (noise * 0.25f)
					};

					// 繝ｯ繝ｼ繝ｫ繝臥ｩｺ髢薙〒縺ｮY蠎ｧ讓吶ｒ險育ｮ・
					float worldY = tc.translate.y + (v.y * tc.scale.y);

					// 2. 蝨ｰ髱｢縺ｨ縺ｮ陦晉ｪ∝愛螳壹→螟牙ｽ｢
					if (false && worldY < slimeCpuLogic_.groundY) {
						float diff = slimeCpuLogic_.groundY - worldY;
						
						// Y繧偵け繝ｪ繝・・
						v.y = (slimeCpuLogic_.groundY - tc.translate.y) / tc.scale.y;

						// 貎ｰ繧後◆蛻・□縺第ｨｪ縺ｫ蠎・￡繧・
						float flattenSpread = 1.0f + (diff * 0.5f);
						v.x *= flattenSpread;
						v.z *= flattenSpread;
					}

					dynV.position = { v.x, v.y, v.z, 1.0f };
					dynV.normal = baseN.normal; 
				}

				// GPU縺ｸ鬆らせ繝舌ャ繝輔ぃ繧定ｻ｢騾・
				renderer_->UpdateDynamicMesh(slimeCpuLogic_.dynamicMeshHandle, slimeCpuLogic_.dynamicVerts);

				// 蜀・Κ繝代・繝・ぅ繧ｯ繝ｫ譖ｴ譁ｰ
				for (auto& p : slimeCpuLogic_.particles) {
					float pNoise = Noise3D(
						p.basePos.x * 1.2f + slimeCpuLogic_.time * 0.8f,
						p.basePos.y * 1.2f - slimeCpuLogic_.time * 0.5f,
						p.basePos.z * 1.2f
					);
					Engine::Vector3 dir = Engine::Normalize(p.basePos);
					p.currentPos.x = p.basePos.x + dir.x * (pNoise * 0.3f);
					p.currentPos.y = p.basePos.y + dir.y * (pNoise * 0.3f);
					p.currentPos.z = p.basePos.z + dir.z * (pNoise * 0.3f);
				}
			}
			// ----------------------------------------
			
			// --- 星形弾用メッシュの更新 ---
			if (!projectileCpuLogic_.initialized && slimeCpuLogic_.initialized && !slimeCpuLogic_.baseVertices.empty()) {
				// プレイヤーの元の球体頂点をコピーして確実なベースメッシュとして使う
				projectileCpuLogic_.baseVertices = slimeCpuLogic_.baseVertices;
				projectileCpuLogic_.baseNormals = slimeCpuLogic_.baseNormals; 
				projectileCpuLogic_.indices = slimeCpuLogic_.indices;
				projectileCpuLogic_.dynamicVerts = projectileCpuLogic_.baseVertices;
				projectileCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(projectileCpuLogic_.baseVertices, projectileCpuLogic_.indices);
				projectileCpuLogic_.initialized = true;
			}

			if (projectileCpuLogic_.initialized) {
				projectileCpuLogic_.time += ctx_.dt;
				const float pNoiseScale = 4.0f;  // 細かくうねらせる
				const float pNoiseSpeed = 8.0f;  // 速くうねらせる
				
				for (size_t i = 0; i < projectileCpuLogic_.baseVertices.size(); ++i) {
					const auto& baseV = projectileCpuLogic_.baseVertices[i];
					const auto& baseN = projectileCpuLogic_.baseNormals[i];
					auto& dynV = projectileCpuLogic_.dynamicVerts[i];

					float bx = baseV.position.x;
					float by = baseV.position.y;
					float bz = baseV.position.z;

					// 球体を少し大きめにベーススケール
					float scaleUp = 1.8f;
					float sx = bx * scaleUp;
					float sy = by * scaleUp;
					float sz = bz * scaleUp;

					float noise = Noise3D(
						sx * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sy * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sz * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed
					);
					
					// ノイズを鋭くして星形の突起を作る
					float spike = std::abs(noise) * 1.5f;

					Engine::Vector3 v = {
						sx + baseN.normal.x * spike,
						sy + baseN.normal.y * spike,
						sz + baseN.normal.z * spike
					};

					dynV.position = { v.x, v.y, v.z, 1.0f };
					dynV.normal = baseN.normal; 
				}
				renderer_->UpdateDynamicMesh(projectileCpuLogic_.dynamicMeshHandle, projectileCpuLogic_.dynamicVerts);
			}
			// ----------------------------------------

			hasPlayerSlime = mr.shaderName == "Slime";
		}
	}

	if (hasPlayerSlime) {
		if (isLiquidated && !wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Puddle, blobRadii);
		} else if (!isLiquidated && wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Blob, blobRadii);
		}
	}
	wasLiquidated_ = isLiquidated;

	// 弾エンティティに対して、星形メッシュとSlimeシェーダーを適用
	auto projView = registry_.view<MeshRendererComponent>();
	for (auto entity : projView) {
		if (registry_.all_of<HitboxComponent>(entity)) {
			auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.isProjectile && projectileCpuLogic_.initialized) {
				auto& pmr = registry_.get<MeshRendererComponent>(entity);
				pmr.modelHandle = projectileCpuLogic_.dynamicMeshHandle;
				if (registry_.all_of<NameComponent>(entity) && registry_.get<NameComponent>(entity).name == "PlayerProjectile") {
					pmr.shaderName = "SlimeNoFace";
				} else {
					pmr.shaderName = "SlimeNoFaceNoDepth";
				}
			}
		}
	}

	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	if (!isPlaying_) {
		DrawEditorGizmos();
	}
#endif

	// 笘・鬮倬溘ち繧ｰ讀懃ｴ｢繧堤畑縺・※繝励Ξ繧､繝､繝ｼ菴咲ｽｮ繧貞酔譛滂ｼ・(N) -> O(1)・・
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		entt::entity playerEntity = players[0];
		if (registry_.valid(playerEntity) && registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			renderer_->SetPlayerPos(Engine::Vector3{tc.translate.x, tc.translate.y, tc.translate.z});
		}
	}

	auto renderView = registry_.view<TransformComponent>();
	for (auto entity : renderView) {
		Engine::Vector4 color = {1, 1, 1, 1};
		if (registry_.all_of<ColorComponent>(entity)) {
			const auto& cc = registry_.get<ColorComponent>(entity);
			color = {cc.color.x, cc.color.y, cc.color.z, cc.color.w};
		}

		bool hasMeshRenderer = false;
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			const auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled && mr.modelHandle != 0) {
				hasMeshRenderer = true;
				bool hasAnim = false;
				std::vector<Engine::Matrix4x4> bonePalette;

				if (registry_.all_of<AnimatorComponent>(entity)) {
					const auto& anim = registry_.get<AnimatorComponent>(entity);
					if (anim.enabled && !anim.currentAnimation.empty()) {
						auto* m = renderer_->GetModel(mr.modelHandle);
						if (m) {
							const auto& data = m->GetData();
							const Engine::Animation* currAnim = nullptr;
							for (const auto& a : data.animations) {
								if (a.name == anim.currentAnimation) {
									currAnim = &a;
									break;
								}
							}
							if (currAnim) {
								bonePalette.resize(data.bones.size());
								for (auto& b : bonePalette)
									b = Engine::Matrix4x4::Identity();

								std::vector<std::pair<Engine::Vector3, Engine::Vector3>> debugLines;
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette, anim.drawSkeleton ? &debugLines : nullptr);
								hasAnim = true;

								if (anim.drawSkeleton) {
									Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
									DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
									for (const auto& line : debugLines) {
										DirectX::XMVECTOR p1 = DirectX::XMVectorSet(line.first.x, line.first.y, line.first.z, 1.0f);
										DirectX::XMVECTOR p2 = DirectX::XMVectorSet(line.second.x, line.second.y, line.second.z, 1.0f);
										p1 = DirectX::XMVector3TransformCoord(p1, w);
										p2 = DirectX::XMVector3TransformCoord(p2, w);
										Engine::Vector3 wp1, wp2;
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp2), p2);
										renderer_->DrawLine3D(wp1, wp2, {0.0f, 1.0f, 0.0f, 1.0f}, true); // X-Ray true
									}
								}
							}
						}
					}
				}

				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				if (hasAnim) {
					renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
				} else {
					if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth") {
						// 蜊企乗・繧ｪ繝悶ず繧ｧ繧ｯ繝医↑縺ｮ縺ｧ縲∽ｸ埼乗・繧ｪ繝悶ず繧ｧ繧ｯ繝医・閭悟ｾ後↓縺ｪ繧峨↑縺・ｈ縺・ｾ後〒謠冗判縺吶ｋ
						continue;
					}
					if (mr.shaderName == "Toon" || mr.shaderName == "ToonSkinning" || mr.shaderName == "Hologram" || mr.shaderName == "EmissiveGlow" || mr.shaderName == "ForceField" ||
					    mr.shaderName == "Dissolve" || mr.shaderName == "Distortion" || mr.shaderName == "Reflection") {
						renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.reflectivity, mr.useCubemap);
					} else {
						renderer_->DrawMeshInstanced(
						    mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.extraTextureHandles);
					}
				}
			}
		}

		// 譌ｧSceneObject縺ｮ莠呈鋤逕ｨ (繧ゅ＠ MeshRenderer 繧ｳ繝ｳ繝昴・繝阪Φ繝医′縺ｪ縺剰・霄ｫ縺ｮ modelHandle 遲峨′縺ゅ▲縺溷ｴ蜷・
		// Registry蛹悶〒蝓ｺ譛ｬ逧・↓縺ｯ MeshRendererComponent 縺ｫ邨ｱ蜷医☆繧九・縺梧悍縺ｾ縺励＞縺後∽ｸ譌ｦ谿狗蕗
		/*
		if (!hasMeshRenderer && obj.modelHandle != 0) { ... }
		*/

		// 笘・ｿｽ蜉: 蟾昴さ繝ｳ繝昴・繝阪Φ繝医・謠冗判 (繝｡繝・す繝･縺ｯ繝ｯ繝ｼ繝ｫ繝牙ｺｧ讓吶〒逕滓・貂医∩縺ｪ縺ｮ縺ｧIdentity螟画鋤)
		if (registry_.all_of<RiverComponent>(entity)) {
			const auto& rv = registry_.get<RiverComponent>(entity);
			if (rv.enabled && rv.meshHandle != 0) {
				auto tex = renderer_->LoadTexture2D(rv.texturePath);
				Engine::Transform identity;
				identity.translate = {0, 0, 0};
				identity.rotate = {0, 0, 0};
				identity.scale = {1, 1, 1};
				renderer_->DrawMesh(rv.meshHandle, tex, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
			}
		}
	}

	// === 蜊企乗・繧ｪ繝悶ず繧ｧ繧ｯ繝茨ｼ・lime遲会ｼ峨・驕・ｻｶ謠冗判 ===
	for (auto entity : renderView) {
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth") {
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				Engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (registry_.all_of<ColorComponent>(entity)) {
					const auto& cc = registry_.get<ColorComponent>(entity);
					color = { cc.color.x, cc.color.y, cc.color.z, cc.color.w };
				}
				renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world,
					{ color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w },
					mr.shaderName, mr.reflectivity > 0 ? mr.reflectivity : 1.0f, true);
			}
		}
	}

#ifdef USE_IMGUI
	DrawSelectionHighlight();
	DrawLightGizmos();
#endif
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});

	// 繝励Ξ繧､繝､繝ｼ繧ｹ繝ｩ繧､繝: Screen-Space Fluid Rendering・亥盾閠・ UE5 Niagara / 豸ｲ迥ｶ繧ｹ繝ｩ繧､繝・峨・霆ｽ驥冗沿Slime繧ｷ繧ｧ繝ｼ繝繝ｼ縺ｫ鄂ｮ縺肴鋤縺医◆縺溘ａ辟｡蜉ｹ蛹・
	renderer_->SetCustomDrawJob(nullptr);

	for (auto& system : systems_) {
		system->Draw(registry_, ctx_);
	}
}

extern GizmoMode currentGizmoMode;
void GameScene::DrawUI() {
	if (!isPlaying_)
		return;
	for (auto& sys : systems_) {
		sys->DrawUI(registry_, ctx_);
	}
}

extern bool gizmoDragging;
extern int gizmoDragAxis;

void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	for (auto entity : selectedEntities_) {
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity))
			continue;

		auto& tc = registry_.get<TransformComponent>(entity);
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};

		Engine::Matrix4x4 mat = this->GetWorldMatrix(static_cast<int>(entity));
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
		Engine::Vector3 v[8] = {
		    {-1.0f, -1.0f, -1.0f},
            {1.0f,  -1.0f, -1.0f},
            {1.0f,  1.0f,  -1.0f},
            {-1.0f, 1.0f,  -1.0f},
            {-1.0f, -1.0f, 1.0f },
            {1.0f,  -1.0f, 1.0f },
            {1.0f,  1.0f,  1.0f },
            {-1.0f, 1.0f,  1.0f },
		};

		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		if (registry_.all_of<BoxColliderComponent>(entity)) {
			const auto& bc = registry_.get<BoxColliderComponent>(entity);
			if (bc.enabled) {
				float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
				// ... Draw lines ...
				Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
				Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f};
				Engine::Vector3 cv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
			}
		}

		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			const auto& gmc = registry_.get<GpuMeshColliderComponent>(entity);
			if (gmc.enabled) {
				Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
				float hs = 1.0f;
				Engine::Vector3 cv[8] = {
				    {-hs, -hs, -hs},
                    {hs,  -hs, -hs},
                    {hs,  hs,  -hs},
                    {-hs, hs,  -hs},
                    {-hs, -hs, hs },
                    {hs,  -hs, hs },
                    {hs,  hs,  hs },
                    {-hs, hs,  hs }
                };
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
			}
		}

		if (registry_.all_of<HitboxComponent>(entity)) {
			const auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.enabled) {
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		if (registry_.all_of<HurtboxComponent>(entity)) {
			const auto& hb = registry_.get<HurtboxComponent>(entity);
			if (hb.enabled) {
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) * DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		const float al = 2.0f, ar = 0.3f;
		int dAxis = (gizmoDragging && entity == selectedEntity_) ? gizmoDragAxis : -1;
		auto axCol = [](int axis, int drag) -> Engine::Vector4 {
			bool a = (drag == axis);
			switch (axis) {
			case 0:
				return a ? Engine::Vector4{1, .6f, .6f, 1} : Engine::Vector4{1, .2f, .2f, 1};
			case 1:
				return a ? Engine::Vector4{.6f, 1, .6f, 1} : Engine::Vector4{.2f, 1, .2f, 1};
			case 2:
				return a ? Engine::Vector4{.6f, .6f, 1, 1} : Engine::Vector4{.2f, .2f, 1, 1};
			default:
				return {1, 1, 1, 1};
			}
		};
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
			float e = 0.15f;
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
			drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
			drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
			drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		}
	}
}

void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		if (std::fabs(i) < 0.01f)
			continue;
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	auto dlView = registry_.view<DirectionalLightComponent, TransformComponent>();
	dlView.each([&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		if (!dl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});

	auto plView = registry_.view<PointLightComponent, TransformComponent>();
	plView.each([&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		if (!pl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
	});

	auto slView = registry_.view<SpotLightComponent, TransformComponent>();
	slView.each([&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		if (!sl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});
}

void GameScene::SetIsPlaying(bool play) {
	if (isPlaying_ == play)
		return;

	wasLiquidated_ = false;

	if (play) {
		// 繝励Ξ繧､髢句ｧ区凾: 繧ｹ繧ｯ繝ｪ繝励ヨ縺ｮ迴ｾ蝨ｨ縺ｮ險ｭ螳夲ｼ医う繝ｳ繧ｹ繝壹け繧ｿ繝ｼ縺ｧ縺ｮ螟画峩・峨ｒ繧ｳ繝ｳ繝昴・繝阪Φ繝医↓遒ｺ螳溘↓蜿肴丐 (Flush)
		auto scView = registry_.view<ScriptComponent>();
		for (auto entity : scView) {
			auto& sc = scView.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
				if (entry.instance) {
					std::string oldParam = entry.parameterData;
					entry.parameterData = entry.instance->SerializeParameters();
					if (entry.parameterData != oldParam) {
						char logBuf[2048];
						sprintf_s(logBuf, "[GameScene] Script synced: %s from %s to %s\n", entry.scriptPath.c_str(), oldParam.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					} else {
						char logBuf[1024];
						sprintf_s(logBuf, "[GameScene] Script already in sync: %s (%s)\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					}
				} else {
					char logBuf[1024];
					sprintf_s(logBuf, "[GameScene] Script instance NULL, skipping sync: %s (current param: %s)\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
					OutputDebugStringA(logBuf);
				}
			}
		}

		// 蜷Тystem縺ｮ繝ｪ繧ｻ繝・ヨ・医せ繧ｯ繝ｪ繝励ヨ縺ｮ蜀榊・譛溷喧縲√う繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ縺ｮ繧ｯ繝ｪ繧｢縺ｪ縺ｩ・峨ｒ蜈医↓螳溯｡・
		for (auto& sys : systems_) {
			sys->Reset(registry_);
		}

		sceneSnapshot_ = EditorUI::SaveToMemory(this);
		{
			char logBuf[128];
			sprintf_s(logBuf, "[GameScene] Saved snapshot for PLAY mode (size: %zu)\n", sceneSnapshot_.size());
			OutputDebugStringA(logBuf);
		}

		isPlaying_ = true;
		ShowCursor(FALSE); // 笘・ｿｽ蜉: 繧ｫ繝ｼ繧ｽ繝ｫ繧帝撼陦ｨ遉ｺ
	} else {
		// 繝励Ξ繧､蛛懈ｭ｢譎・ Play 繝懊ち繝ｳ繧呈款縺励◆逶ｴ蜑阪・迥ｶ諷・(`sceneSnapshot_`) 縺ｫ謌ｻ縺・
		// 驕ｸ謚樒憾諷九・繧ｨ繝ｳ繝・ぅ繝・ぅ蜷阪ｒ荳譎ゆｿ晏ｭ・
		std::vector<std::string> selectedNames;
		auto& reg = GetRegistry();
		for (auto entity : selectedEntities_) {
			if (reg.valid(entity) && reg.all_of<NameComponent>(entity)) {
				selectedNames.push_back(reg.get<NameComponent>(entity).name);
			}
		}

		isPlaying_ = false;
		ShowCursor(TRUE); // 笘・ｿｽ蜉: 繧ｫ繝ｼ繧ｽ繝ｫ繧定｡ｨ遉ｺ
		
		if (!sceneSnapshot_.empty()) {
			OutputDebugStringA(("[GameScene] Restoring from memory snapshot (size: " + std::to_string(sceneSnapshot_.size()) + ")...\n").c_str());
			EditorUI::LoadFromMemory(this, sceneSnapshot_);

			// 菫晏ｭ倥＠縺ｦ縺翫＞縺溷錐蜑阪ｒ蜈・↓驕ｸ謚樒憾諷九ｒ蠕ｩ蜈・
			selectedEntities_.clear();
			selectedEntity_ = entt::null;
			auto view = reg.view<NameComponent>();
			for (const auto& name : selectedNames) {
				for (auto entity : view) {
					if (view.get<NameComponent>(entity).name == name) {
						selectedEntities_.insert(entity);
						if (selectedEntity_ == entt::null)
							selectedEntity_ = entity;
						break;
					}
				}
			}
			if (!selectedNames.empty()) {
				OutputDebugStringA(("[GameScene] Restored selection for " + std::to_string(selectedEntities_.size()) + " entities.\n").c_str());
			}
		} else {
			OutputDebugStringA("[GameScene] ERROR: Memory snapshot is empty on STOP! Falling back to initial state.\n");
			if (!initialSceneSnapshot_.empty()) {
				EditorUI::LoadFromMemory(this, initialSceneSnapshot_);
			}
		}
		// sceneSnapshot_ = ""; // 縺薙ｌ繧呈ｶ医☆縺ｨ縲∝・髢区凾縺ｫ谿九▲縺ｦ縺励∪縺・庄閭ｽ諤ｧ縺後≠繧九′縲∝ｿｵ縺ｮ縺溘ａ谿九☆縺具ｼ・
		// 荳譌ｦ縲∵ｯ主屓菫晏ｭ倥☆繧九ｈ縺・↓縺吶ｋ縺ｮ縺ｧ繧ｯ繝ｪ繧｢縺励※繧り憶縺・・縺・
		sceneSnapshot_ = "";




		// 繝壹Φ繝・ぅ繝ｳ繧ｰ繝・・繧ｿ縺ｮ繧ｯ繝ｪ繧｢
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
	}
}

// =====================================================
// 笘・鬮倬溘ち繧ｰ繧｢繧ｯ繧ｻ繧ｹ螳溯｣・
// =====================================================

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(TagType tag) {
	static const std::vector<entt::entity> emptyResult;
	auto it = tagCache_.find(tag);
	if (it != tagCache_.end()) {
		return it->second;
	}
	return emptyResult;
}

void GameScene::SetTag(entt::entity entity, TagType tag) {
	if (!registry_.valid(entity)) {
		return;
	}
	auto& tc = registry_.get_or_emplace<TagComponent>(entity);
	tc.tag = tag;
	// 逶ｴ謗･ SyncTag 縺帙★縲・≦蟒ｶ譖ｴ譁ｰ繝ｪ繧ｹ繝医↓霑ｽ蜉
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagAdded(entt::registry& /*registry*/, entt::entity entity) {
	// 蜊ｳ蠎ｧ縺ｫ蜷梧悄縺帙★縲∵ｬ｡繝輔Ξ繝ｼ繝遲峨・驕ｩ蛻・↑繧ｿ繧､繝溘Φ繧ｰ縺ｧ蜷梧悄
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagRemoved(entt::registry& /*registry*/, entt::entity entity) {
	// 蜊ｳ蠎ｧ縺ｫ蜑企勁縺帙★縲・≦蟒ｶ繝ｪ繧ｹ繝医↓霑ｽ蜉縺励※谺｡繝輔Ξ繝ｼ繝髢句ｧ区凾縺ｫ蜑企勁繧定｡後≧
	// 縺薙ｌ縺ｫ繧医ｊ縲√う繝・Ξ繝ｼ繧ｷ繝ｧ繝ｳ荳ｭ縺ｮ繧ｳ繝ｳ繝・リ螟画峩縺ｫ繧医ｋ萓句､悶ｒ髦ｲ豁｢縺吶ｋ
	pendingTagRemoved_.push_back(entity);
}

void GameScene::SyncTag(entt::entity entity) {
	if (!registry_.valid(entity) || !registry_.all_of<TagComponent>(entity)) {
		return;
	}

	// 譁ｰ縺励＞繧ｭ繝｣繝・す繝･縺ｫ霑ｽ蜉・亥炎髯､縺ｯ Update 縺ｮ髢句ｧ区凾縺ｫ荳諡ｬ縺励※陦後ｏ繧後ｋ蜑肴署・・
	const TagType tag = registry_.get<TagComponent>(entity).tag;
	
	// 驥崎､・メ繧ｧ繝・け繧剃ｸ莉ｶ縺壹▽陦後≧縺ｨ驕・＞縺溘ａ縲∝渕譛ｬ逧・↓縺ｯ Update 蛛ｴ縺ｮ蜈ｨ蜑企勁繧剃ｿ｡鬆ｼ縺吶ｋ
	tagCache_[tag].push_back(entity);
}

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(const std::string& tag) {
	return GetEntitiesByTag(StringToTag(tag));
}

void GameScene::SetTag(entt::entity entity, const std::string& tagStr) {
	SetTag(entity, StringToTag(tagStr));
}

} // namespace Game


// Dummy line to trigger rebuild



