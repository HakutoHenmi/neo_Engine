// GameSceneDraw.cpp
// GameScene の描画: 流体 (スライム・水) の設定、メッシュの描画、ポーズ画面、各システムの描画。
// Draw は手順ごとの関数 (GatherPlayerDrawState・UploadFluidColliders・UpdatePlayerSlimeFluid・DrawSceneMeshes など) を順に呼ぶ。
#include <cfloat>
// シーンの BGM
#include "../UI/SceneMusic.h"
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
// プレイヤー向けの UI (共通部品・強化カード・グラフィック設定)
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../UI/GraphicsUI.h"
// 流体の放出と時間の管理 (エンジン側)
#include "../../Engine/FluidEmission.h"
#include "../../Engine/Time/TimeManager.h"
// 缶の装備 (旧アクションゲーム)
#include "../CanLoadout.h"
#include "../ObjectTypes.h"
// 音・パス・シーン管理 (エンジン側)
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
// エディタとスクリプト
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
// ゲームの処理を分担するシステム群
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // 武器の管理
#include "../../Engine/Input.h"      // キーボード・マウス入力

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // キーフレームのモーション
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/PlayerActionSystem.h" // プレイヤーアクション
#include "../Systems/CombatSystem.h"       // 戦闘の当たり判定
#include "../Systems/CanAbilitySystem.h"
#include "../Systems/EnemyAISystem.h"      // 敵の AI
#include "../Systems/BossActionSystem.h"    // ボスの AI
#include "../Systems/WaveSystem.h"         // ウェーブ管理
#include "../Scripts/BossTestScript.h"      // ボスの生成に使う

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // ポストエフェクト
// Web プロファイラ (外部ツールから値を見たり変えたりする)
#include "../../Engine/NetworkProfiler.h"
// JSON の読み書き
#include "../../externals/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h" // ImGui (エディタの画面)
#include <Windows.h> // デバッグ出力 (OutputDebugStringA) を使うため
#include <algorithm>
#include <cmath>
#include <filesystem> // 空のテクスチャ検索などのファイル操作

namespace Game {

// このファイルの中だけで使う関数
namespace {
	// スライムのぷよぷよした揺れを CPU で計算するための簡易 3D ノイズ (Simplex ノイズの代わり)
	float Noise3D(float x, float y, float z) {
		// sin・cos を組み合わせた滑らかな値
		float n = std::sin(x) * std::cos(y) * std::sin(z);
		n += std::sin(x * 2.1f + y * 1.3f) * 0.5f;
		n += std::cos(y * 1.7f + z * 2.5f) * 0.5f;
		return n * 0.5f;
	}
}

namespace {
// Scene ビュー表示中は、描画の間だけ camera_ にエディタカメラを入れる。
// 描画処理 (ChronoSystem など) は ctx.camera = &camera_ を参照するため、全体がエディタ視点で描かれる。
// 描画中にカメラへ書き込まれても、終了時にゲームカメラを書き戻すので更新処理には影響しない
class ViewCameraScope {
public:
	ViewCameraScope(Engine::Camera& gameCamera, const Engine::Camera& editorCamera, bool active)
		: camera_(gameCamera), active_(active) {
		// 有効なときだけ、ゲームのカメラを覚えてエディタのカメラに差し替える
		if (!active_) return;
		saved_ = gameCamera;
		gameCamera = editorCamera;
	}
	// 終わったらゲームのカメラに戻す
	~ViewCameraScope() { if (active_) camera_ = saved_; }
	// コピーはできない
	ViewCameraScope(const ViewCameraScope&) = delete;
	ViewCameraScope& operator=(const ViewCameraScope&) = delete;
private:
	// 差し替えたカメラ・元のカメラ・有効か
	Engine::Camera& camera_;
	Engine::Camera saved_;
	bool active_;
};
} // namespace

// 描画: 流体の設定・メッシュ・パーティクル・ギズモ・ゲーム内 UI
void GameScene::Draw() {
	// 描画の窓口が無ければ何もしない
	if (!renderer_)
		return;
	// Scene ビューなら描画の間だけエディタのカメラにする
	ViewCameraScope viewCamera(camera_, editorCamera_, editorView_ == EditorView::Scene);
    // スライムの調合室を開いているときは、調合室の画面だけを描く
    if(isPlaying_&&!isPaused_&&chronoSystem_&&chronoSystem_->SlimeLabOpen()){
        chronoSystem_->DrawSlimeLab(registry_,ctx_);return;
    }

	// プレイヤーの位置・大きさ・液状化の状態 (流体の描画に使う)
	PlayerDrawState player = GatherPlayerDrawState();
	auto& isLiquidated = player.isLiquidated;
	auto& hasPlayerSlime = player.hasPlayerSlime;
	auto& playerEntity = player.playerEntity;

	// 流体がぶつかる箱 (壁と既定のタグ) を渡す
	UploadFluidColliders();

	// プレイヤーがいないときの流体の中心 (水のプレビュー用)
	UpdateFluidPreviewCore(player);

	// 流体エミッターから粒子を出す
	UpdateFluidEmitters(player);

	// プレイヤーのスライム (GPU 流体・分身・弾のメッシュ)
	UpdatePlayerSlimeFluid(player);

	// 液状化の開始・終了時の流体リセット (現在は使っていない)
	if (hasPlayerSlime) {
		if (isLiquidated && !wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Puddle, blobRadii);
		} else if (!isLiquidated && wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Blob, blobRadii);
		}
	}
	// 今の液状化の状態を覚える
	wasLiquidated_ = isLiquidated;

	// 弾の当たり判定を持つエンティティに星形メッシュとスライム用シェーダーを使う
	auto projView = registry_.view<MeshRendererComponent>();
	for (auto entity : projView) {
		// 攻撃判定を持つもののうち
		if (registry_.all_of<HitboxComponent>(entity)) {
			auto& hb = registry_.get<HitboxComponent>(entity);
			// 弾で、星形メッシュが準備できているもの
			if (hb.isProjectile && projectileCpuLogic_.initialized) {
				auto& pmr = registry_.get<MeshRendererComponent>(entity);
				pmr.modelHandle = projectileCpuLogic_.dynamicMeshHandle;
				// プレイヤーの弾は深度ありで描き、それ以外は深度なしで描く (手前に見せる)
				if (registry_.all_of<NameComponent>(entity) && registry_.get<NameComponent>(entity).name == "PlayerProjectile") {
					pmr.shaderName = "SlimeNoFace";
				} else {
					pmr.shaderName = "SlimeNoFaceNoDepth";
				}
			}
		}
	}

	// 描画に使うカメラを渡す
	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	// ギズモは Unity と同様に Scene ビューだけに描く (Game ビューは実際のゲーム画面)
	if (editorView_ == EditorView::Scene) {
		DrawEditorGizmos();
	}
#endif

	// プレイヤーの位置をレンダラーへ渡す (タグのキャッシュで素早く探す)
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		playerEntity = players[0];
		if (registry_.valid(playerEntity) && registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			renderer_->SetPlayerPos(Engine::Vector3{tc.translate.x, tc.translate.y, tc.translate.z});
		}
	}

	// メッシュを描く (不透明な物の後に半透明の物)
	DrawSceneMeshes();

#if defined(USE_IMGUI) && !defined(NDEBUG)
	// Scene ビューのときだけ、選択中の強調とライトの印を描く
	if (editorView_ == EditorView::Scene) {
		DrawSelectionHighlight();
		DrawLightGizmos();
	}
#endif
	// パーティクルの描画
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		// 有効なものだけ描く
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});

	// 旧: CPU 計算のスライム描画 (軽量なスライム用シェーダーに置き換えたので無効)
	renderer_->SetCustomDrawJob(nullptr);

    // Scene ビューは Unity と同様にゲーム内 UI (ポーズ画面・HUD) を重ねず、空間だけを見せる
    const bool sceneView = editorView_ == EditorView::Scene;
    // ポーズ中: グラフィック設定画面またはポーズメニューを描く
    if (isPlaying_ && isPaused_) {
        // Scene ビューでは何も重ねない
        if (sceneView) return;
        // UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
        UI::Canvas ui(renderer_,ctx_.viewportSize.x,ctx_.viewportSize.y);if(ctx_.useOverrideMouse)ui.SetPointer(ctx_.overrideMouseX,ctx_.overrideMouseY);
        // グラフィック設定画面 (保存に失敗したら注意を出す)
        if(graphicsOpen_){UI::DrawGraphics(ui,*renderer_);if(graphicsSaveFailed_)ui.Text(UI::Str("pause.saveFailed"),110,689,17,UI::Gold,UI::JapaneseFont);return;}
        // スライム本編は強化パレット、旧アクションゲームはポーズメニュー
        if(chronoMode_&&!registry_.view<Chrono::InkPlayer>().empty()){auto view=registry_.view<Chrono::InkPlayer>();UI::RoguePalette(ui,view.get<Chrono::InkPlayer>(*view.begin()).rogue);}else ui.Pause();
        // 右上のグラフィック設定ボタン
        ui.Button(UI::GraphicsButton,UI::Str(UI::UsingPad()?"pause.graphics":"pause.graphicsKey"),false,"xbox_button_view");
        return;
    }
    // 各システムの描画
    for (auto& system : systems_) {
        system->Draw(registry_, ctx_);
		// スライム本編の HUD は Renderer::EndFrame でまとめて描かれる前に登録する
		if (chronoMode_ && isPlaying_ && !sceneView) system->DrawUI(registry_, ctx_);
	}
}

// ゲーム内 UI の描画 (旧アクションゲームのシステムが使う)
void GameScene::DrawUI() {
	// 描画の窓口が無ければ何もしない
	if (!renderer_) return;

	// Scene ビューではゲームの HUD を描かない (Draw と同じ扱い)
	if (!isPlaying_ || editorView_ == EditorView::Scene)
		return;
	// 旧アクションゲームのときだけ、各システムの UI を描く
	for (auto& sys : systems_) {
		if (!chronoMode_) sys->DrawUI(registry_, ctx_);
	}
}

// ステージクリアしたか (本編は ChronoSystem、旧モードは WaveSystem で判定する)
bool GameScene::IsStageClear() const {
	// 本編は ChronoSystem の終了判定
	if (chronoSystem_) return chronoSystem_->Finished();
	// 旧モードはウェーブがクリアの状態か
	return waveSystem_ && waveSystem_->state == WaveSystem::State::Clear;
}

// プレイヤーを探し、流体の描画に使う位置・大きさ・液状化・死亡の状態をまとめて返す
GameScene::PlayerDrawState GameScene::GatherPlayerDrawState() {
	// プレイヤーの位置・大きさ・液状化の状態 (流体の描画に使う)
	DirectX::XMFLOAT3 corePos = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 blobRadii = { 1.2f, 0.85f, 1.2f };
	bool isLiquidated = false;
	float liquefyInitialFlowSpeed = 0.0f;
	Engine::Vector3 liquefyFlowForward = {0.0f, 0.0f, 1.0f};
	bool hasPlayerSlime = false;
	bool isPlayerDead = false;

	// プレイヤーを探し、位置・液状化・死亡の状態を読む
	const auto& playersForCore = GetEntitiesByTag(TagType::Player);
	// プレイヤーのエンティティ
	entt::entity playerEntity = entt::null;
	if (!playersForCore.empty() && registry_.valid(playersForCore[0])) {
		playerEntity = playersForCore[0];
		// 位置と大きさ
		if (registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			corePos = { tc.translate.x, tc.translate.y, tc.translate.z };
			blobRadii = { tc.scale.x, tc.scale.y, tc.scale.z };
		}
		// 液状化中か・流れの速さと向き
		if (registry_.all_of<PlayerActionComponent>(playerEntity)) {
			const auto& pa = registry_.get<PlayerActionComponent>(playerEntity);
			isLiquidated = pa.state == PlayerActionState::Liquefy;
			liquefyInitialFlowSpeed = pa.CurrentLiquefyFlowSpeed();
			liquefyFlowForward = {pa.liquefyFlowDir.x, pa.liquefyFlowDir.y, pa.liquefyFlowDir.z};
		}
		// 死んでいるか
		if (registry_.all_of<HealthComponent>(playerEntity)) {
			const auto& hc = registry_.get<HealthComponent>(playerEntity);
			isPlayerDead = hc.IsDead() || hc.Hp() <= 0.0f;
		}
	}
	return {corePos, blobRadii, isLiquidated, liquefyInitialFlowSpeed, liquefyFlowForward, hasPlayerSlime, isPlayerDead, playerEntity};
}

// 流体がぶつかる箱をレンダラーへ渡す
void GameScene::UploadFluidColliders() {
	// 流体エミッターは当たり判定用の箱が必要 (プレイヤーがいないプレビューも含む)。壁と既定タグの箱を AABB にして渡す
	std::vector<Engine::Renderer::FluidAABB> fluidAABBs;
	// 箱の当たり判定を持つエンティティを調べる
	auto aabbView = registry_.view<TransformComponent, BoxColliderComponent>();
	aabbView.each([&](entt::entity entity, TransformComponent&, BoxColliderComponent& bc) {
		// 無効なものとすり抜けるものは除く
		if (!bc.enabled || bc.isTrigger) return;
		// 壁と既定のタグだけ
		const auto* tag = registry_.try_get<TagComponent>(entity);
		if (!tag || (tag->tag != TagType::Wall && tag->tag != TagType::Default)) return;
		// ワールド行列
		const auto matrix = GetWorldMatrix(static_cast<int>(entity));
		const auto world = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&matrix));
		// 最小・最大の点
		DirectX::XMVECTOR lo = DirectX::XMVectorReplicate(FLT_MAX);
		DirectX::XMVECTOR hi = DirectX::XMVectorReplicate(-FLT_MAX);
		// 箱の 8 つの角をワールド座標にして、最小・最大を広げる
		for (int corner = 0; corner < 8; ++corner) {
			const auto local = DirectX::XMVectorSet(
				bc.center.x + ((corner & 1) ? .5f : -.5f) * bc.size.x,
				bc.center.y + ((corner & 2) ? .5f : -.5f) * bc.size.y,
				bc.center.z + ((corner & 4) ? .5f : -.5f) * bc.size.z, 1);
			const auto point = DirectX::XMVector3TransformCoord(local, world);
			lo = DirectX::XMVectorMin(lo, point); hi = DirectX::XMVectorMax(hi, point);
		}
		// AABB にして一覧へ追加する
		Engine::Renderer::FluidAABB box{};
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&box.min), lo);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&box.max), hi);
		fluidAABBs.push_back(box);
	});
	// レンダラーへ渡す
	renderer_->SetFluidAABBs(fluidAABBs);
}

// プレイヤーがいないとき (水のプレビューなど) の流体の中心を決める
void GameScene::UpdateFluidPreviewCore(PlayerDrawState& player) {
	auto& corePos = player.corePos;
	auto& playerEntity = player.playerEntity;
	// プレイヤーがいないとき (水のプレビューなど) は、最初の流体エミッター付近を高画質で描く
	if (!registry_.valid(playerEntity)) {
		// プレイヤーがいなくてもプレビューの周りは高画質で再構成する
		// 流体エミッターを探す
		auto sources = registry_.view<TransformComponent, FluidEmitterComponent>();
		if (sources.begin() != sources.end()) {
			// 最初のエミッターの少し下を中心にする
			const auto source = *sources.begin();
			const auto matrix = GetWorldMatrix(static_cast<int>(source));
			corePos = {matrix.m[3][0], matrix.m[3][1] - 3, matrix.m[3][2]};
		}
		// 中心だけ渡し、引き寄せ・分身・糸は無効にする
		renderer_->SetGPUFluidCore({corePos.x, corePos.y, corePos.z}, 0);
		renderer_->SetGPUFluidDecoy({0,-1000,0}, 0, {1,1,1}, {0,0,1});
		renderer_->SetGPUFluidTether({}, false);
		renderer_->SetChronoFluidPresentation(1, 0, false);
	}
}

// エディタで置いた流体エミッターから粒子を出す (水の真下にいるプレイヤーは回復する)
void GameScene::UpdateFluidEmitters(const PlayerDrawState& player) {
	const auto& corePos = player.corePos;
	const auto playerEntity = player.playerEntity;
	// エディタで自由に置ける流体エミッターの処理 (プレイヤーの有無や表示に関係なく動かす)
	{
		// 流体エミッターごとに、毎フレームの粒子数 (端数と上限を考慮) を GPU 流体へ追加する
		auto emitterView = registry_.view<TransformComponent, FluidEmitterComponent>();
		emitterView.each([&](entt::entity e, TransformComponent&, FluidEmitterComponent& fec) {
			// 無効か、出す数が 0 なら何もしない
			if (!fec.enabled || fec.emitCountPerFrame <= 0) return;

			// ワールド座標での位置・初速・色
			const auto world = GetWorldMatrix(static_cast<int>(e));
			Engine::Vector3 pos = {world.m[3][0], world.m[3][1], world.m[3][2]};
			Engine::Vector3 vel = { fec.velocity.x, fec.velocity.y, fec.velocity.z };
			Engine::Vector4 col = { fec.color.x, fec.color.y, fec.color.z, fec.color.w };

			// 毎フレーム粒子を出す
			const int emitCount=Engine::LimitFluidEmission(Engine::AccumulateFluidEmission(fec.emitCountPerFrame,
				Engine::TimeManager::GetInstance().GetDeltaTime(),fec.emissionRemainder), fec.particleLimit, fec.emittedParticles);
			// GPU 流体へ追加する
			renderer_->EmitGPUFluid(pos, vel, col, emitCount, fec.fluidType, fec.retainWater);

			// 水 (fluidType >= 0.5) の真下 (半径 1.5 以内) にプレイヤーがいれば HP を回復する
			if (isPlaying_ && !isPaused_ && fec.fluidType >= 0.5f && registry_.valid(playerEntity)) {
				// エミッターとプレイヤーの水平の差
				float dx = corePos.x - pos.x;
				float dz = corePos.z - pos.z;
				if (dx * dx + dz * dz < 1.5f * 1.5f) {
					if (registry_.all_of<HealthComponent>(playerEntity)) {
						auto& hc = registry_.get<HealthComponent>(playerEntity);
						// 回復の間隔を数える (関数をまたいで覚えておく)
						static float healTimer = 0.0f;
						healTimer += ctx_.dt;
						if (healTimer > 0.05f) { // 0.05 秒ごとに 1 回復 (1 秒で 20)
							healTimer = 0.0f;
							hc.RecoverHp(1);
						}
					}
				}
			}
		});
	}
}

// プレイヤーのスライム: 見た目を GPU 流体で描くための設定、分身の流体、弾の星形メッシュ
void GameScene::UpdatePlayerSlimeFluid(PlayerDrawState& player) {
	auto& corePos = player.corePos;
	auto& blobRadii = player.blobRadii;
	auto& isLiquidated = player.isLiquidated;
	auto& liquefyInitialFlowSpeed = player.liquefyInitialFlowSpeed;
	auto& liquefyFlowForward = player.liquefyFlowForward;
	auto& hasPlayerSlime = player.hasPlayerSlime;
	auto& isPlayerDead = player.isPlayerDead;
	auto& playerEntity = player.playerEntity;
	// プレイヤーのスライム: 見た目は GPU 流体で描き、元のメッシュは非表示にする
	if (registry_.valid(playerEntity) && registry_.all_of<MeshRendererComponent>(playerEntity)) {
		// プレイヤーの見た目
		auto& mr = registry_.get<MeshRendererComponent>(playerEntity);
			
			// CPU 側のスライムメッシュの準備
			// 初回だけ、元の球メッシュから変形用の動的メッシュとパーティクルを作る
			if (!slimeCpuLogic_.initialized) {
				// 元のモデル
				auto* model = renderer_->GetModel(mr.modelHandle);
				if (model) {
					// 元の頂点と添え字を覚える
					slimeCpuLogic_.baseVertices = model->GetData().vertices;
					// 法線の初期状態を保存する
					slimeCpuLogic_.baseNormals = slimeCpuLogic_.baseVertices; 
					slimeCpuLogic_.indices = model->GetData().indices;
					// 変形後の頂点 (最初は元と同じ)
					slimeCpuLogic_.dynamicVerts = slimeCpuLogic_.baseVertices;
					
					// 変形用の動的メッシュを作る
					slimeCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(slimeCpuLogic_.baseVertices, slimeCpuLogic_.indices);
					
					// 体の中に漂うパーティクル (50個) の初期化
					slimeCpuLogic_.particles.resize(50);
					for (auto& p : slimeCpuLogic_.particles) {
						// ランダムな向きに、ランダムな距離 (最大 1.5) だけ離した位置
						Engine::Vector3 randPos = { (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f };
						p.basePos = Engine::Normalize(randPos);
						p.basePos.x *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.y *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.z *= ((rand()%100)/100.0f) * 1.5f;
						p.color = {0.4f, 0.8f, 0.1f, 1.0f}; // 濃い黄緑
					}
					slimeCpuLogic_.initialized = true;
				}
			}

			if (slimeCpuLogic_.initialized && registry_.all_of<TransformComponent>(playerEntity)) {
				// CPU で変形させたメッシュは描かず、GPU 流体のスライムで描く
				mr.enabled = false;
				
				if (!gpuSlimeEmitted_) {
					// 最初の1回だけ、大量の GPU 流体粒子を体の中心に放出する
					// スライムの色 (変形の見本があればその色)
					Engine::Vector4 pColor = {Chrono::SlimeRed, Chrono::SlimeGreen, Chrono::SlimeBlue, 1.0f};
                    if(!renderer_->fluidMorphTargets_.empty())pColor=renderer_->fluidMorphTargets_.front().color;
					// プレイヤーの粒子数だけ出す
					renderer_->EmitGPUFluid({corePos.x, corePos.y, corePos.z}, {0, -2, 0}, pColor, Engine::Renderer::kPlayerFluidParticles);
					gpuSlimeEmitted_ = true;
				}
				
				// 状態で引き寄せる力を変える (死亡中は 0、液状化中は弱く、通常は強く)
				float attraction = isPlayerDead ? 0.0f : (isLiquidated ? 1.0f : 32.0f);
				
				// 流体の中心 (圧力の源) を体の少し上に置く
				Engine::Vector3 targetCore = {corePos.x, corePos.y + 0.18f, corePos.z};
				// 向き (回転行列の Z 軸。液状化中は流れの向き)
				auto& playerTc = registry_.get<TransformComponent>(playerEntity);
				Engine::Matrix4x4 mat = playerTc.GetTransform().ToMatrix();
				Engine::Vector3 forward = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				if (isLiquidated) {
					forward = liquefyFlowForward;
				}
				// 大きさと、液状化の流れの速さ
				Engine::Vector3 scaleVec = {blobRadii.x, blobRadii.y, blobRadii.z};
				float liquidFlowSpeed = 0.0f;
				if (isLiquidated) {
					liquidFlowSpeed = liquefyInitialFlowSpeed;
				}
				// スライム本編: 地面の傾き・顔の向き・体の形 (伸び・空中・動き)・質量を流体へ渡す
				if (chronoMode_) {
					// 地面の傾き・高さを渡す
					if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity)){renderer_->SetSlimeGround({ink->groundSlope.x,ink->groundHeight,ink->groundSlope.z},registry_.get<Chrono::Player>(playerEntity).grounded);renderer_->SetSlimeFaceCamera(ink->rogue.MenuEase());scaleVec={ink->fluidAspect,ink->airBlend,ink->fluidMotion};forward={ink->fluidDirection.x,0,ink->fluidDirection.z};}
					// 質量 (体力)。変形の見本があれば最大にする
					float mass = registry_.get<HealthComponent>(playerEntity).Hp();
                    if(!renderer_->fluidMorphTargets_.empty())mass=Chrono::SlimeMaximumMass;
                    if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity);ink&&ink->perfectAge>=0&&ink->perfectAge<.16f)
                        mass*=.18f; // 一瞬だけ見た目を縮める (ゲーム上の質量は変えない)
					// 流体の大きさの基準 (100) に換算する
					if(registry_.all_of<Chrono::InkPlayer>(playerEntity))mass*=100.f/Chrono::SlimeMaximumMass;
					// 中心・引き寄せ・大きさ・向き・状態を渡す
					renderer_->SetGPUFluidCore(targetCore, attraction, scaleVec, forward, (registry_.all_of<Chrono::InkPlayer>(playerEntity) && registry_.get<Chrono::InkPlayer>(playerEntity).dodgeLiquid) ? 5.0f : (registry_.all_of<Chrono::InkPlayer>(playerEntity) ? 4.0f : 2.0f), mass);
					// 被弾時の点滅・不安定時の明滅、ヒットストップ中の停止、触手を伸ばしている間の糸
					if (auto* action = registry_.try_get<Chrono::Player>(playerEntity)) {
						// 強化の選択画面を開いているか
						bool upgradeOpenForFluid=false;if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity))upgradeOpenForFluid=ink->rogue.menu;
						// 被弾から 0.6 秒は激しく点滅する
						float flash=action->damageAge<.6f?(std::sin(action->damageAge*70)>0?.75f:0):0;
						// 不安定さが 75 を超えたらゆっくり明滅する
						if(action->instability>75)flash=std::max(flash,.35f*(.5f+.5f*std::sin(action->stats.seconds*10)));
						// カメラ用の透明度・点滅・止めるか (ポーズ中かヒットストップ中) を渡す
						renderer_->SetChronoFluidPresentation(action->cameraOpacity,flash,isPaused_||(action->hitStop>0&&!upgradeOpenForFluid));
						// 触手を伸ばしている・引き寄せている間は手まで糸を張る
						bool tether = action->action == Chrono::Action::Extending || action->action == Chrono::Action::Pulling || action->action == Chrono::Action::Retracting;
						renderer_->SetGPUFluidTether({action->hand.x, action->hand.y, action->hand.z}, tether);
					}
				// 旧アクションゲーム: 液状化の流れの速さを渡す
				} else {
					// 糸と点滅は無し
					renderer_->SetGPUFluidTether({}, false);
					renderer_->SetChronoFluidPresentation(1,0,false);
					// 中心・引き寄せ・大きさ・向き・液状化の状態と流れの速さを渡す
					renderer_->SetGPUFluidCore(targetCore, attraction, scaleVec, forward, isLiquidated ? 1.0f : 0.0f, liquidFlowSpeed);
				}

				// 分身 (デコイ) の流体の設定
				bool decoyFound = false;
				// タグ・位置・名前を持つエンティティから分身を探す
				auto decoyView = registry_.view<TagComponent, TransformComponent, NameComponent>();
				decoyView.each([&](entt::entity e, TagComponent& tagComp, TransformComponent& dtc, NameComponent& nc) {
					// 見つかったら残りは飛ばす
					if (decoyFound) return;
					// プレイヤーのタグで、名前が Decoy のもの
					if (tagComp.tag == TagType::Player && nc.name == "Decoy") {
						
						// 分身の呼吸のような伸び縮み
						float timeLived = 5.0f;
						if (registry_.all_of<AutoDestroyComponent>(e)) {
							timeLived = 5.0f - registry_.get<AutoDestroyComponent>(e).timer;
						}
						float breathe = std::sin(timeLived * 3.0f);
						
						// 幅は自機より少し小さく、厚みは自機と同じにしてぺたんこにならないようにする
						dtc.scale.x = 0.9f + breathe * 0.05f;
						dtc.scale.y = 0.8f - breathe * 0.05f; // 自機 (0.8) と同じ高さ
						dtc.scale.z = 0.9f + breathe * 0.05f;

						// 中心を持ち上げて地面に潰されないようにする
						Engine::Vector3 targetDecoyCore = {dtc.translate.x, dtc.translate.y + 0.8f, dtc.translate.z};
						// 向きと大きさ
						Engine::Matrix4x4 dMat = dtc.GetTransform().ToMatrix();
						Engine::Vector3 dForward = {dMat.m[2][0], dMat.m[2][1], dMat.m[2][2]};
						Engine::Vector3 dScale = {dtc.scale.x, dtc.scale.y, dtc.scale.z};
						// 分身はプレイヤーの状態 (液状化など) に関係なく常に形を保つ
						float decoyAttraction = 80.0f;
						// 分身の流体の設定を渡す
						renderer_->SetGPUFluidDecoy(targetDecoyCore, decoyAttraction, dScale, dForward);
						decoyFound = true;
					}
				});
				if (!decoyFound) {
					// 分身がいなければ引き寄せる力を 0 にしておく
					renderer_->SetGPUFluidDecoy({0,-1000,0}, 0.0f, {1,1,1}, {0,0,1});
				}

}
			// ----------------------------------------
			
			// 星形の弾のメッシュの準備
			if (!projectileCpuLogic_.initialized && slimeCpuLogic_.initialized && !slimeCpuLogic_.baseVertices.empty()) {
				// プレイヤーの元の球の頂点を複製して、弾の元のメッシュにする
				projectileCpuLogic_.baseVertices = slimeCpuLogic_.baseVertices;
				projectileCpuLogic_.baseNormals = slimeCpuLogic_.baseNormals; 
				projectileCpuLogic_.indices = slimeCpuLogic_.indices;
				projectileCpuLogic_.dynamicVerts = projectileCpuLogic_.baseVertices;
				projectileCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(projectileCpuLogic_.baseVertices, projectileCpuLogic_.indices);
				projectileCpuLogic_.initialized = true;
			}

			// 弾のメッシュをノイズで毎フレーム変形させ、トゲのある星形にする
			if (projectileCpuLogic_.initialized) {
				// 時間を進める
				projectileCpuLogic_.time += ctx_.dt;
				const float pNoiseScale = 4.0f;  // 細かくうねらせる
				const float pNoiseSpeed = 8.0f;  // 速くうねらせる
				
				// 頂点ごとに変形する
				for (size_t i = 0; i < projectileCpuLogic_.baseVertices.size(); ++i) {
					// 元の頂点と法線、書き込む頂点
					const auto& baseV = projectileCpuLogic_.baseVertices[i];
					const auto& baseN = projectileCpuLogic_.baseNormals[i];
					auto& dynV = projectileCpuLogic_.dynamicVerts[i];

					// 元の位置
					float bx = baseV.position.x;
					float by = baseV.position.y;
					float bz = baseV.position.z;

					// 球を少し大きくしてから変形する
					float scaleUp = 1.8f;
					float sx = bx * scaleUp;
					float sy = by * scaleUp;
					float sz = bz * scaleUp;

					// 時間で流れる 3D ノイズ
					float noise = Noise3D(
						sx * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sy * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sz * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed
					);
					
					// ノイズの絶対値を強めて、星形のトゲを作る
					float spike = std::abs(noise) * 1.5f;

					// 法線の向きへトゲの分だけ押し出す
					Engine::Vector3 v = {
						sx + baseN.normal.x * spike,
						sy + baseN.normal.y * spike,
						sz + baseN.normal.z * spike
					};

					// 変形後の位置と法線
					dynV.position = { v.x, v.y, v.z, 1.0f };
					dynV.normal = baseN.normal; 
				}
				// GPU のメッシュを更新する
				renderer_->UpdateDynamicMesh(projectileCpuLogic_.dynamicMeshHandle, projectileCpuLogic_.dynamicVerts);
			}
			// ----------------------------------------

			// プレイヤーがスライムのシェーダーで描かれているか
			hasPlayerSlime = mr.shaderName == "Slime";
		}
}

// メッシュの描画: 色の上書き・カメラを遮る物の透明化・アニメーション・シェーダーごとの描き分け、
// 最後に半透明の物 (スライムなど)
void GameScene::DrawSceneMeshes() {
	// メッシュの描画: 色の上書き・カメラを遮る物の透明化・アニメーション・シェーダーごとの描画方法
	auto renderView = registry_.view<TransformComponent>();
	for (auto entity : renderView) {
		// 色 (ColorComponent があればその色)
		Engine::Vector4 color = {1, 1, 1, 1};
		if (registry_.all_of<ColorComponent>(entity)) {
			const auto& cc = registry_.get<ColorComponent>(entity);
			color = {cc.color.x, cc.color.y, cc.color.z, cc.color.w};
		}

		// メッシュで描いたか
		bool hasMeshRenderer = false;
		if(chronoMode_&&isPlaying_)if(auto* occluder=registry_.try_get<Chrono::CameraOccluder>(entity))
			if(occluder->opacity<.995f)color.w=-occluder->opacity; // カメラを遮る物は網目状に透かす (影と当たり判定はそのまま)
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			// 見た目を持ち、有効でモデルがあるものを描く
			const auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled && mr.modelHandle != 0) {
				hasMeshRenderer = true;
				// アニメーションしているかと、骨の行列
				bool hasAnim = false;
				std::vector<Engine::Matrix4x4> bonePalette;

				// アニメーターがあれば骨の行列 (直前のクリップとのクロスフェード込み) を計算する
				if (registry_.all_of<AnimatorComponent>(entity)) {
					const auto& anim = registry_.get<AnimatorComponent>(entity);
					if (anim.enabled && !anim.currentAnimation.empty()) {
						auto* m = renderer_->GetModel(mr.modelHandle);
						if (m) {
							const auto& data = m->GetData();
							// 再生中のクリップを探す
							const Engine::Animation* currAnim = nullptr;
							for (const auto& a : data.animations) {
								if (a.name == anim.currentAnimation) {
									currAnim = &a;
									break;
								}
							}
							if (currAnim) {
								// 骨の数だけ単位行列で用意する
								bonePalette.resize(data.bones.size());
								for (auto& b : bonePalette)
									b = Engine::Matrix4x4::Identity();

								// クロスフェード中なら直前のクリップと、混ぜる割合 (0〜1) を求める
								const Engine::Animation* prevAnim = nullptr;
								float blendFactor = 0.0f;
								if (anim.crossfadeTimer > 0.0f && anim.crossfadeDuration > 0.0f) {
									for (const auto& a : data.animations) {
										if (a.name == anim.prevAnimation) {
											prevAnim = &a;
											break;
										}
									}
									blendFactor = 1.0f - (anim.crossfadeTimer / anim.crossfadeDuration);
								}

								// 骨の行列を計算する
								std::vector<Engine::Model::DebugBone> debugBones;
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, prevAnim, anim.prevTime, blendFactor, bonePalette, anim.drawSkeleton ? &debugBones : nullptr);
								hasAnim = true;

								// 骨の表示が有効なら、親の骨から各骨へ線を引く
								if (anim.drawSkeleton) {
									// このエンティティのワールド行列
									Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
									DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
									for (const auto& bone : debugBones) {
										// 親の骨の位置と、この骨の位置
										DirectX::XMVECTOR p1 = DirectX::XMVectorSet(bone.parentPos.x, bone.parentPos.y, bone.parentPos.z, 1.0f);
										Engine::Vector3 bPos = {bone.globalMatrix.m[3][0], bone.globalMatrix.m[3][1], bone.globalMatrix.m[3][2]};
										DirectX::XMVECTOR p2 = DirectX::XMVectorSet(bPos.x, bPos.y, bPos.z, 1.0f);
										// ワールド座標へ変換する
										p1 = DirectX::XMVector3TransformCoord(p1, w);
										p2 = DirectX::XMVector3TransformCoord(p2, w);
										// Engine の型へ取り出す
										Engine::Vector3 wp1, wp2;
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp2), p2);
										renderer_->DrawLine3D(wp1, wp2, {0.0f, 1.0f, 0.0f, 1.0f}, true); // 手前の物に隠れずに描く
									}
								}
							}
						}
					}
				}

				// このエンティティのワールド行列
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				// アニメーションありはスキニング描画、半透明は後回し、特殊シェーダーは個別描画、それ以外はインスタンシング描画
				if (hasAnim) {
					// 骨の行列を使って描く
					renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
				} else {
					// スライム系・ビームなどの半透明のシェーダー
					if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth" || mr.shaderName == "EnergyBeam" || mr.shaderName == "EnergyCylinder") {
						// 半透明なので、不透明な物の後でまとめて描く
						continue;
					}
					// 特殊なシェーダーは 1 つずつ描く
					if (mr.shaderName == "Toon" || mr.shaderName == "ToonSkinning" || mr.shaderName == "Hologram" || mr.shaderName == "EmissiveGlow" || mr.shaderName == "ForceField" ||
					    mr.shaderName == "Dissolve" || mr.shaderName == "Distortion" || mr.shaderName == "Reflection" || mr.shaderName == "Glass") {
						renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.reflectivity, mr.useCubemap);
					} else {
						// それ以外は同じモデルをまとめて描く
						renderer_->DrawMeshInstanced(
						    mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.extraTextureHandles,uint64_t(entt::to_integral(entity))+1);
					}
				}
			}
		}

		// 旧 SceneObject との互換用 (MeshRenderer を持たず自前の modelHandle を持つ場合)
		// 基本は MeshRendererComponent にまとめる方針なので、処理は無効化して残している
		/*
		if (!hasMeshRenderer && obj.modelHandle != 0) { ... }
		*/

		// 川の描画 (メッシュはワールド座標で作ってあるので単位行列で描く)
		// 川を持っていれば
		if (registry_.all_of<RiverComponent>(entity)) {
			const auto& rv = registry_.get<RiverComponent>(entity);
			if (rv.enabled && rv.meshHandle != 0) {
				// 川のテクスチャ
				auto tex = renderer_->LoadTexture2D(rv.texturePath);
				// 単位行列の代わりの、移動・回転なし・大きさ 1 のトランスフォーム
				Engine::Transform identity;
				identity.translate = {0, 0, 0};
				identity.rotate = {0, 0, 0};
				identity.scale = {1, 1, 1};
				// 流れの速さと UV の倍率を色の欄で渡して、川のシェーダーで描く
				renderer_->DrawMesh(rv.meshHandle, tex, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
			}
		}
	}

	// 半透明の物 (スライムなど) を後から描く
	for (auto entity : renderView) {
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			// 無効なら飛ばす
			if (!mr.enabled) continue;
			// 半透明のシェーダーのものだけ描く
			if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth" || mr.shaderName == "EnergyBeam" || mr.shaderName == "EnergyCylinder") {
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				// 色 (ColorComponent があればその色)
				Engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (registry_.all_of<ColorComponent>(entity)) {
					const auto& cc = registry_.get<ColorComponent>(entity);
					color = { cc.color.x, cc.color.y, cc.color.z, cc.color.w };
				}
				// 半透明として描く
				renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world,
					{ color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w },
					mr.shaderName, mr.reflectivity > 0 ? mr.reflectivity : 1.0f, true);
			}
		}
	}
}

} // namespace Game
