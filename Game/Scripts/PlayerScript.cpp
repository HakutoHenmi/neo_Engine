#include "PlayerScript.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

namespace Game {

void PlayerScript::Start(entt::entity entity, GameScene* scene) {
	// ★追加: プレイヤーを物理演算から切り離し、CharacterController方式（レイキャスト）で制御する
	if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
		scene->GetRegistry().get<RigidbodyComponent>(entity).isKinematic = true;
	}
	if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
		scene->GetRegistry().get<CharacterMovementComponent>(entity).heightOffset = 1.0f; // 2m立方体キャラの中心がy=1.0になるように
	}

	// プレイヤー自身のコライダーサイズを「見た目（2m立方体）」に合わせる
	if (scene->GetRegistry().all_of<BoxColliderComponent>(entity)) {
		scene->GetRegistry().get<BoxColliderComponent>(entity).size = { 2.0f, 2.0f, 2.0f };
	}
	if (scene->GetRegistry().all_of<HurtboxComponent>(entity)) {
		scene->GetRegistry().get<HurtboxComponent>(entity).size = { 2.0f, 2.0f, 2.0f };
	}

	// 剣がシーンに既にあるか確認
	entt::entity sword = entt::null;
	auto view = scene->GetRegistry().view<NameComponent>();
	for (auto e : view) {
		if (view.get<NameComponent>(e).name == swordName_) {
			sword = e;
			break;
		}
	}

	if (sword == entt::null) {
		// 剣がなければ生成
		sword = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(sword).name = swordName_;
		auto& tc = scene->GetRegistry().emplace<TransformComponent>(sword);
		tc.scale = { 0.15f, 0.15f, 1.8f };
		
		auto& cc = scene->GetRegistry().emplace<ColorComponent>(sword);
		cc.color = { 0.9f, 0.9f, 0.9f, 1.0f };

		auto& tcTag = scene->GetRegistry().emplace<TagComponent>(sword);
		tcTag.tag = TagType::PlayerSword;

		// ★追加: 親子関係の設定
		auto& hierarchy = scene->GetRegistry().emplace<HierarchyComponent>(sword);
		hierarchy.parentId = entity;

		// ★追加: モーションコンポーネントの追加
		auto& motion = scene->GetRegistry().emplace<MotionComponent>(sword);
		motion.isPlaying = false;
		motion.isPlaying = false;

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(sword);
			mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png"); // 必要ならテクスチャも指定可
			mr.color = { 0.9f, 0.9f, 0.9f, 1.0f };
			mr.enabled = true;
		}

		auto& hb = scene->GetRegistry().emplace<HitboxComponent>(sword);
		hb.isActive = false;
		hb.damage = 30.0f;
		hb.tag = TagType::Sword;
		hb.size = { 3.0f, 3.0f, 1.0f }; 
		hb.center = { 0.0f, 0.0f, 0.5f };
		hb.enabled = true;
	} else {
		// 既にタグがない場合は追加
		if (!scene->GetRegistry().all_of<TagComponent>(sword)) {
			scene->GetRegistry().emplace<TagComponent>(sword).tag = TagType::PlayerSword;
		}
		// ★追加: 親子関係の確認・設定
		if (!scene->GetRegistry().all_of<HierarchyComponent>(sword)) {
			auto& hc = scene->GetRegistry().emplace<HierarchyComponent>(sword);
			hc.parentId = entity;
		}
		// ★追加: モーションコンポーネントの確認・設定
		if (!scene->GetRegistry().all_of<MotionComponent>(sword)) {
			auto& motion = scene->GetRegistry().emplace<MotionComponent>(sword);
			motion.isPlaying = false;
			motion.isPlaying = false;
		}

		// 既にある場合は基本的なプロパティを維持
		scene->GetRegistry().get<TransformComponent>(sword).scale = { 0.15f, 0.15f, 1.8f };

		// ★最重要: Hitbox がなければ追加する
		if (!scene->GetRegistry().all_of<HitboxComponent>(sword)) {
			auto& hb = scene->GetRegistry().emplace<HitboxComponent>(sword);
			hb.isActive = false;
			hb.damage = 30.0f;
			hb.tag = TagType::Sword;
			hb.size = { 3.0f, 3.0f, 1.0f }; 
			hb.center = { 0.0f, 0.0f, 0.5f };
			hb.enabled = true;
		}

		if (scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
			auto& mr = scene->GetRegistry().get<MeshRendererComponent>(sword);
			auto* renderer = scene->GetRenderer();
			if (renderer) {
				mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
				mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
				mr.enabled = true;
			}
		}
	}

	if (scene->GetRegistry().all_of<NameComponent>(entity)) {
		std::cout << "PlayerScript Started: " << scene->GetRegistry().get<NameComponent>(entity).name << std::endl;
	}

	// ★追加: 剣にコンボモーションがない場合はデフォルトで作成
	sword = scene->FindObjectByName(swordName_);
	if (sword != entt::null) {
		if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
			auto setupDefaultCombo = [&](const std::string& name, int index) {
				if (motion->clips.find(name) == motion->clips.end()) {
					MotionComponent::MotionClip clip;
					clip.name = name;
					clip.totalDuration = 0.4f;
					clip.loop = false;
					
					if (index == 1) { // 横薙ぎ（より広く、伸縮追加）
						clip.keyframes.push_back({0.00f, { 2.5f, 0.8f, -1.0f }, { 0.0f, 1.57f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
						clip.keyframes.push_back({0.20f, { 0.0f, 0.8f, 4.0f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 3.5f }});
						clip.keyframes.push_back({0.40f, {-2.5f, 0.8f, -1.0f }, { 0.0f, -1.57f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
					} else if (index == 2) { // 斜め斬り（右上から左下）
						clip.keyframes.push_back({0.00f, { 2.2f, 4.0f, -0.8f }, { -0.7f, 0.0f, 0.7f }, { 0.15f, 0.15f, 2.0f }});
						clip.keyframes.push_back({0.20f, { 0.0f, 1.2f, 4.5f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 3.5f }});
						clip.keyframes.push_back({0.40f, { -2.2f, -1.0f, 0.2f }, { 0.7f, 0.0f, -0.7f }, { 0.15f, 0.15f, 1.8f }});
					} else { // 突き/回転（超推力・多回転）
						clip.keyframes.push_back({0.00f, { 0.0f, 0.8f, 0.5f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
						clip.keyframes.push_back({0.15f, { 0.0f, 0.8f, 6.5f }, { 0.0f, 0.0f, 6.28f }, { 0.15f, 0.15f, 5.0f }});
						clip.keyframes.push_back({0.40f, { 0.0f, 0.8f, 1.5f }, { 0.0f, 0.0f, 12.56f }, { 0.15f, 0.15f, 1.8f }});
					}
					motion->clips[name] = clip;
				}
			};
			setupDefaultCombo("Combo1", 1);
			setupDefaultCombo("Combo2", 2);
			setupDefaultCombo("Combo3", 3);
		}
	}
}

void PlayerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
		if (scene->GetRegistry().get<HealthComponent>(entity).isDead) return;
	}

	if (!isSubscribed_) {
		scene->GetEventSystem().Subscribe("GainGold", [this](float amount) {
			experience_ += amount;
			debugReceiveCount_ += 1;
			debugLastValue_ = amount;
		});
		debugSubscribeCount_ += 1;
		isSubscribed_ = true;
	}

	bool hasPhaseSystem = false;
	{
		auto scView = scene->GetRegistry().view<ScriptComponent>();
		for (auto e : scView) {
			auto& sc = scView.get<ScriptComponent>(e);
			for (auto& entry : sc.scripts) {
				if (entry.scriptPath == "PhaseSystemScript") {
					hasPhaseSystem = true;
					break;
				}
			}
			if (hasPhaseSystem) break;
		}
	}

	if (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;
	} else {
		UpdateMovement(entity, scene, dt);
		UpdateAttack(entity, scene, dt);
		UpdateSword(entity, scene, dt);
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
	}

	// Update 内での ImGui 呼び出しは例外の原因となる可能性があるため、OnEditorUI に移動しました。
}

void PlayerScript::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene->GetRegistry().all_of<PlayerInputComponent>(entity)) return;
	auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);
	float speedMul = 1.0f;
	if (isAttacking_) {
		speedMul = 0.2f;
	}
	input.moveDir.x *= speedMul;
	input.moveDir.y *= speedMul;
}

void PlayerScript::UpdateAttack(entt::entity /*entity*/, GameScene* scene, float /*dt*/) {
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (currentAttackKeyDown && !prevAttackKeyDown_) {
		attackQueued_ = true;
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) return;

	auto& motion = scene->GetRegistry().get<MotionComponent>(sword);

	if (isAttacking_) {
		sheatheTimer_ = 0.0f;
		isSheathed_ = false;

		if (!motion.isPlaying) {
			if (attackQueued_ && comboCount_ < 3) {
				comboCount_++;
				attackQueued_ = false;
				motion.PlayAnimation("Combo" + std::to_string(comboCount_));
			} else {
				isAttacking_ = false;
				comboCount_ = 0;
				attackQueued_ = false;
			}
		}
	} else {
		if (attackQueued_) {
			isAttacking_ = true;
			isSheathed_ = false;
			comboCount_ = 1;
			attackQueued_ = false;
			motion.PlayAnimation("Combo1");
		} else {
			if (!isSheathed_) {
				sheatheTimer_ += scene->GetContext().dt;
				if (sheatheTimer_ >= AUTO_SHEATHE_TIME) {
					isSheathed_ = true;
				}
			}
		}
	}
}

void PlayerScript::UpdateSword(entt::entity /*entity*/, GameScene* scene, float dt) {
	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) return;

	auto& swordTc = scene->GetRegistry().get<TransformComponent>(sword);
	
	if (!isAttacking_) {
		// 非攻撃時は常に背中に背負う配置にする
		swordTc.translate = { -0.55f, 1.3f, -0.5f };
		swordTc.rotate = { DirectX::XMConvertToRadians(35.0f), 0.0f, DirectX::XMConvertToRadians(25.0f) };
		isSheathed_ = true;
		sheatheTimer_ = AUTO_SHEATHE_TIME;
	}

	if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
		if (motion->isPlaying) {
			auto it = motion->clips.find(motion->activeClip);
			if (it != motion->clips.end()) {
				float duration = it->second.totalDuration;
				float t = motion->currentTime;
				float start = duration * 0.10f;
				float end = duration * 0.85f;
				bool active = (t > start && t < end);
				if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
					hb->isActive = active;
				}
			}
		} else {
			if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
				hb->isActive = false;
			}
		}
	}

	bool hitboxActive = false;
	if (scene->GetRegistry().all_of<HitboxComponent>(sword)) {
		hitboxActive = scene->GetRegistry().get<HitboxComponent>(sword).isActive;
	}

	if (isAttacking_ && hitboxActive) {
		Engine::Matrix4x4 worldMat = scene->GetWorldMatrix((int)sword);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&worldMat));
		float bladeLen = 1.6f;
		DirectX::XMVECTOR basePos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, -bladeLen*0.2f, 1), m);
		DirectX::XMVECTOR tipPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, bladeLen*0.8f, 1), m);

		TrailPoint tp;
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.base), basePos);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.tip), tipPos);
		tp.life = 0.35f;
		tp.maxLife = 0.35f;
		trailPoints_.push_back(tp);
		if (trailPoints_.size() > 60) trailPoints_.pop_front();
	}

	for (auto& tp : trailPoints_) tp.life -= dt;
	while (!trailPoints_.empty() && trailPoints_.front().life <= 0) trailPoints_.pop_front();

	auto* renderer = scene->GetRenderer();
	if (renderer && trailPoints_.size() >= 2) {
		for (size_t i = 1; i < trailPoints_.size(); ++i) {
			float alpha = trailPoints_[i].life / trailPoints_[i].maxLife;
			Engine::Vector4 col = {0.2f, 0.6f, 1.0f, alpha * 0.95f};
			// 少しずらした線を追加して「厚み」を出す
			Engine::Vector3 tip1 = trailPoints_[i-1].tip;
			Engine::Vector3 tip2 = trailPoints_[i].tip;
			Engine::Vector3 base1 = trailPoints_[i-1].base;
			Engine::Vector3 base2 = trailPoints_[i].base;

			renderer->DrawLine3D(tip1, tip2, col, true);
			renderer->DrawLine3D(base1, base2, col, true);
			renderer->DrawLine3D(tip1, base1, col, true);

			Engine::Vector3 off = { 0.0f, 0.03f, 0.0f };
			renderer->DrawLine3D(tip1 + off, tip2 + off, col, true);
			renderer->DrawLine3D(base1 + off, base2 + off, col, true);
			renderer->DrawLine3D(tip1 - off, tip2 - off, col, true);
			renderer->DrawLine3D(base1 - off, base2 - off, col, true);
		}
	}
}

void PlayerScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Player Debug");
	ImGui::Text("Experience: %.1f", experience_);
	ImGui::Text("Subscribe Count: %d", debugSubscribeCount_);
	ImGui::Text("Receive Count: %d", debugReceiveCount_);
	ImGui::Text("Last Value: %.2f", debugLastValue_);
#endif
}

void PlayerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PlayerScript);

} // namespace Game
