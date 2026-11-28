// GameScenePlay.cpp
// PLAY の開始と停止、ポーズ、停止中のカメラ。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../../Engine/Core/Audio.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/IScript.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/GameRumble.h"
#include "../UI/GameUI.h"
#include <algorithm>
#include <cmath>

namespace Game {

using namespace GameSceneConstants;

// ゲーム内のポーズの切り替え。BGM を小さくし、カーソルを出し、流体などを止めてメニューを出す
void GameScene::SetPaused(bool paused) {
	if (paused && !isPaused_) {
		pauseOpenedAt_ = UI::Seconds();
		pauseMenu_ = 0;
	}
	isPaused_ = paused;
	// BGM の音量を下げる
	if (auto* audio = Engine::Audio::GetInstance()) {
		audio->SetBGMDucked(paused);
		audio->SetSEPaused(paused);
	}
	// カーソルを出す
	Engine::WindowDX::SetCursorVisible(paused);
	// 流体などの世界を止める
	if (renderer_)
		renderer_->SetWorldFrozen(paused);
	// ポーズメニューを作る / 消す
	if (paused)
		CreatePauseMenu();
	else
		DestroyPauseMenu();
}

// エディタの Pause: ゲームの更新だけを止める。止める前の流体の停止状態を覚えておき、再開時に戻す
void GameScene::SetEditorPaused(bool paused) {
	// 変わらなければ何もしない
	if (editorPaused_ == paused)
		return;
	if (renderer_) {
		if (paused)
			worldFrozenBeforeEditorPause_ = renderer_->GetWorldFrozen();
		else
			renderer_->SetWorldFrozen(worldFrozenBeforeEditorPause_);
	}
	editorPaused_ = paused;
	// Step で進める残りのフレームを 0 に戻す
	editorStepFrames_ = 0;
}

// 追従カメラはシーンファイルに Camera エンティティとして保存されていないため、
// 停止中は戻したプレイヤーを映すだけにする (ゲーム・物理・入力は進めない。シーン内の移動とは独立)
void GameScene::FrameStoppedGameCamera() {
	if (!chronoMode_)
		return;
	for (auto e : registry_.view<TransformComponent, NameComponent>()) {
		if (registry_.get<NameComponent>(e).name != "Player")
			continue;
		const auto& t = registry_.get<TransformComponent>(e);
		using namespace StoppedPreviewCamera;
		const float distance = (std::max)(kMinDistance, kDistancePerScale * t.scale.x);
		camera_.StopShake();
		camera_.SetHandheld(0);
		camera_.SetRotation(kPitch, 0, 0);
		camera_.SetPosition(
		    t.translate.x, t.translate.y + kLookHeight + std::sin(kPitch) * distance,
		    t.translate.z - std::cos(kPitch) * distance);
		break;
	}
}

// PLAY の開始と停止
void GameScene::SetIsPlaying(bool play) {
	// 変わらなければ何もしない
	if (isPlaying_ == play)
		return;
	// エディタの Pause は解く
	SetEditorPaused(false);

	// 流体のプレビューとスライム本編のポストエフェクトを初期状態にする
	if (play || chronoMode_)
		ResetFluidPreview();
	if (chronoMode_) {
		gpuSlimeEmitted_ = false;
		renderer_->SetPostEffect("Default");
		renderer_->SetPostProcessParams({});
	}

	if (play) {
		// PLAY 開始時: インスペクターで変えたスクリプトの設定をコンポーネントへ確実に反映する
		auto scView = registry_.view<ScriptComponent>();
		// スクリプトを持つエンティティを順に処理する
		for (auto entity : scView) {
			auto& sc = scView.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
				// 実体があれば、今のパラメータを書き出して保存し、変わったかをログに残す
				if (entry.instance) {
					std::string oldParam = entry.parameterData;
					entry.parameterData = entry.instance->SerializeParameters();
					if (entry.parameterData != oldParam) {
						char logBuf[2048];
						sprintf_s(
						    logBuf, "[GameScene] Script synced: %s from %s to %s\n", entry.scriptPath.c_str(),
						    oldParam.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					} else {
						char logBuf[1024];
						sprintf_s(
						    logBuf, "[GameScene] Script already in sync: %s (%s)\n", entry.scriptPath.c_str(),
						    entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					}
				} else {
					// 実体が無ければ反映できないことをログに残す
					char logBuf[1024];
					sprintf_s(
					    logBuf, "[GameScene] Script instance NULL, skipping sync: %s (current param: %s)\n",
					    entry.scriptPath.c_str(), entry.parameterData.c_str());
					OutputDebugStringA(logBuf);
				}
			}
		}

		const std::string editSnapshot = EditorUI::SavePlaySnapshot(this);
		// 各システムをリセットする (スクリプトの再初期化やインスタンスの破棄など) のを先に行う
		for (auto& sys : systems_) {
			sys->Reset(registry_);
		}

		// 停止時に戻すため、PLAY 開始時のシーンを保存する
		sceneSnapshot_ = editSnapshot;
		{
			char logBuf[128];
			sprintf_s(logBuf, "[GameScene] Saved snapshot for PLAY mode (size: %zu)\n", sceneSnapshot_.size());
			OutputDebugStringA(logBuf);
		}

		// PLAY 前のゲームカメラの位置と回転を保存する
		DirectX::XMFLOAT3 pos = camera_.Position();
		editorCameraPos_ = {pos.x, pos.y, pos.z};
		DirectX::XMFLOAT3 rot = camera_.Rotation();
		editorCameraRot_ = {rot.x, rot.y, rot.z};

		// PLAY 中にして、ポーズを解く
		isPlaying_ = true;
		isPaused_ = false;
		Engine::WindowDX::SetCursorVisible(false); // カーソルを隠す
	} else {
		// PLAY 停止時: PLAY を押す直前の状態 (sceneSnapshot_) に戻す
		// 選択中のエンティティの名前を覚えておく (復元後に名前で選び直す)
		std::vector<std::string> selectedNames;
		auto& reg = registry_;
		for (auto entity : selectedEntities_) {
			if (reg.valid(entity) && reg.all_of<NameComponent>(entity)) {
				selectedNames.push_back(reg.get<NameComponent>(entity).name);
			}
		}

		isPlaying_ = false;
		Rumble::Stop();
		gpuSlimeEmitted_ = false;                 // GPU スライムの放出フラグを戻す
		Engine::WindowDX::SetCursorVisible(true); // カーソルを表示する

		// ゲームカメラを PLAY 前の位置に戻す
		camera_.SetPosition(editorCameraPos_.x, editorCameraPos_.y, editorCameraPos_.z);
		camera_.SetRotation(editorCameraRot_.x, editorCameraRot_.y, editorCameraRot_.z);

		// 保存したシーンがあれば戻す
		if (!sceneSnapshot_.empty()) {
			OutputDebugStringA(("[GameScene] Restoring from memory snapshot (size: " +
			                    std::to_string(sceneSnapshot_.size()) + ")...\n")
			                       .c_str());
			EditorUI::LoadFromMemory(this, sceneSnapshot_);

			// 覚えておいた名前で選択を復元する
			selectedEntities_.clear();
			selectedEntity_ = entt::null;
			// 名前が同じエンティティを選び直す (最初のものを主な選択にする)
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
				OutputDebugStringA(
				    ("[GameScene] Restored selection for " + std::to_string(selectedEntities_.size()) + " entities.\n")
				        .c_str());
			}
		} else {
			OutputDebugStringA("[GameScene] ERROR: Memory snapshot is empty on STOP! Falling back to initial state.\n");
			// 保存が無ければ最初の状態に戻す
			if (!initialSceneSnapshot_.empty()) {
				EditorUI::LoadFromMemory(this, initialSceneSnapshot_);
			}
		}
		FrameStoppedGameCamera();
		// 保存内容は PLAY 開始のたびに取り直すので、停止したら空にする
		sceneSnapshot_ = "";

		// 残っている生成・破棄の予約を捨てる
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
	}
}

} // namespace Game
