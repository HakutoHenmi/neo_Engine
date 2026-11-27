// GameSceneSetup.cpp
// GameScene::Initialize の各手順: ポストエフェクトとカメラの初期化、シーンファイルの読み込み、
// システムの登録、コールバックの接続、タグのキャッシュ、Web プロファイラとの通信。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../../Engine/Core/LoadProfile.h"
#include "../../Engine/Core/NetworkProfiler.h"
#include "../../Engine/Core/PathUtils.h"
#include "../../externals/nlohmann/json.hpp"
#include "../Editor/EditorUI.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/MotionSystem.h"
#include "../Systems/PhysicsSystem.h"
#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include <Windows.h>
#include <filesystem>

namespace Game {

using namespace GameSceneConstants;

// 前のシーンやテストで変えたポストエフェクトを初期状態に戻す
void GameScene::ResetPostEffects() {
	if (!renderer_) {
		return;
	}
	// ノイズ・歪み・色ずれ・周辺減光・走査線・SAN 値の効果をすべて 0 にする
	auto ppParams = renderer_->GetPostProcessParams();
	ppParams.noiseStrength = 0.0f;
	ppParams.distortion = 0.0f;
	ppParams.chromaShift = 0.0f;
	ppParams.vignette = 0.0f;
	ppParams.scanline = 0.0f;
	ppParams.san = 0.0f;
	renderer_->SetPostProcessParams(ppParams);
	renderer_->SetPostEffect("Default");
}

// ゲームとエディタのカメラを初期位置に置き、環境光を設定する
void GameScene::InitializeCameras() {
	eventSystem_.Clear();
	playTime_ = 0.0f;
	// ゲームのカメラ (視野角 45 度・画面の縦横比・描画範囲 0.1〜10000)
	const float aspect = static_cast<float>(Engine::WindowDX::kW) / static_cast<float>(Engine::WindowDX::kH);
	camera_.Initialize();
	camera_.SetProjection(CameraDefaults::kFovY, aspect, CameraDefaults::kNearZ, CameraDefaults::kFarZ);
	camera_.SetPosition(CameraDefaults::kStartX, CameraDefaults::kStartY, CameraDefaults::kStartZ);
	camera_.SetRotation(CameraDefaults::kStartPitch, 0, 0);

	// エディタのカメラも同じ初期位置にする
	editorCameraPos_ = {CameraDefaults::kStartX, CameraDefaults::kStartY, CameraDefaults::kStartZ};
	editorCameraRot_ = {CameraDefaults::kStartPitch, 0, 0};
	editorCamera_.Initialize();
	editorCamera_.SetProjection(CameraDefaults::kFovY, aspect, CameraDefaults::kNearZ, CameraDefaults::kFarZ);
	editorCamera_.SetPosition(CameraDefaults::kStartX, CameraDefaults::kStartY, CameraDefaults::kStartZ);
	editorCamera_.SetRotation(CameraDefaults::kStartPitch, 0, 0);

	// 環境光
	renderer_->SetAmbientColor(kAmbientColor);
}

// シーンファイルを読み込む (指定が無ければ本編のシーン)。読めなければ予備のシーンを追加する
bool GameScene::LoadStageFile(const Engine::SceneParameters& params) {
	bool loaded = false;
	// シーンファイルを読み込む (指定が無ければ本編のシーン)
	try {
		// 読み込むパス
		std::string scenePath =
		    params.stagePath.empty() ? EditorUI::GetUnifiedProjectPath(Data::MainStagePath()) : params.stagePath;
		// UTF-8 のパスを FromUTF8 で変換して日本語のパスにも対応する
		if (std::filesystem::exists(Engine::PathUtils::FromUTF8(scenePath))) {
			// 見つかったことをデバッグ出力に残し、読み込む
			OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
			{
				Engine::LoadProfile::Scope loadSceneProfile("phase", "EditorUI::LoadScene");
				EditorUI::LoadScene(this, scenePath);
			}
			isPlaying_ = true; // 読み込めたら PLAY 状態から始める
			loaded = true;
		} else {
			// 見つからなかったことを残す
			OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
		}

	} catch (const std::exception& e) {
		// 読み込み中の例外は内容をデバッグ出力とメッセージボックスで知らせる
		std::string msg = "[GameScene] EXCEPTION during scene load: " + std::string(e.what()) + "\n";
		OutputDebugStringA(msg.c_str());
		MessageBoxA(NULL, msg.c_str(), "Scene Load Error", MB_OK | MB_ICONERROR);
	}

	// 読み込みに失敗した、またはエンティティが空のときは、予備のシーン (game.json の fallbackStage:
	// 太陽・地形・空・プレイヤー) を追加で読み込む
	if (registry_.storage<entt::entity>().empty() || !loaded) {
		const std::string fallback = EditorUI::GetUnifiedProjectPath(Data::Config().fallbackStage);
		OutputDebugStringA(("[GameScene] Loading fallback scene: " + fallback + "\n").c_str());
		EditorUI::AddScene(this, fallback);
	}
	return loaded;
}

// 動かすシステムを登録し直す
void GameScene::RegisterSystems() {
	// 前のシステムをすべて消す
	systems_.clear();
	chronoSystem_ = nullptr;
	// スライム本編: ChronoSystem と物理だけで動く
	if (chronoMode_) {
		// スライム本編のシステム
		auto chrono = std::make_unique<ChronoSystem>(this);
		chronoSystem_ = chrono.get();
		systems_.push_back(std::move(chrono));
		// エディタで追加した Rigidbody も本編シーンで物理演算する
		systems_.push_back(std::make_unique<PhysicsSystem>());
		// エディタで開いた本編以外のシーン: 物理・スクリプト・音・UI・モーション・後片付けだけを動かす
	} else {
		systems_.push_back(std::make_unique<PhysicsSystem>());
		auto scriptSys = std::make_unique<ScriptSystem>();
		scriptSys->SetScene(this);
		systems_.push_back(std::move(scriptSys));
		systems_.push_back(std::make_unique<AudioSystem>());
		systems_.push_back(std::make_unique<UISystem>());
		systems_.push_back(std::make_unique<MotionSystem>());
		// 破棄の予約を処理するシステム
		systems_.push_back(std::make_unique<CleanupSystem>());
	}
}

// タグ・スクリプトの追加/削除を検知するコールバックを登録する (二重登録しないよう一度外す)
void GameScene::ConnectRegistryCallbacks() {
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);
}

// タグ検索のキャッシュを作り直す
void GameScene::RebuildTagCache() {
	tagCache_.clear();
	pendingTagSync_.clear();
	pendingTagRemoved_.clear();
	// タグを持つエンティティをタグごとの一覧に入れる
	auto tagInitView = registry_.view<TagComponent>();
	for (auto entity : tagInitView) {
		const auto tag = tagInitView.get<TagComponent>(entity).tag;
		tagCache_[tag].push_back(entity);
	}
}

// Web プロファイラとの双方向のパラメータ通信を登録する
void GameScene::ConnectNetworkProfiler() {
	// Web プロファイラから値を変更されたとき: 環境光の色
	Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback(
	    [this](const std::string& target, const std::string& prop, float val) {
		    // 環境光の R / G / B のどれかを変える
		    if (target == "AmbientColor") {
			    auto current = renderer_->GetAmbientColor();
			    if (prop == "R")
				    current.x = val;
			    else if (prop == "G")
				    current.y = val;
			    else if (prop == "B")
				    current.z = val;
			    renderer_->SetAmbientColor(current);
		    }
	    });

	// Web プロファイラへ現在の値を返す
	Engine::NetworkProfiler::GetInstance().SetParameterGetCallback([this]() -> std::string {
		// 環境光の色
		nlohmann::json j;
		auto amb = renderer_->GetAmbientColor();
		j["AmbientColor"]["R"] = amb.x;
		j["AmbientColor"]["G"] = amb.y;
		j["AmbientColor"]["B"] = amb.z;

		// JSON の文字列にして返す
		return j.dump();
	});
}

} // namespace Game
