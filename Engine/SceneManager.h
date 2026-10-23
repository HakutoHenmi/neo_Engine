#pragma once
// ==================================================
// SceneManager.h
//   ・シーン登録（名前→Factory）
//   ・現在シーンの保持/切替/更新/描画
//   ・即時切替と「次フレーム切替（リクエスト）」に対応
//   ・デバッグ/自動起動用ユーティリティ追加
// ==================================================
#include "IScene.h"
#include "WindowDX.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

class SceneManager {
public:
	SceneManager() { instance_ = this; }
	~SceneManager() { if (instance_ == this) instance_ = nullptr; }

public:
	static SceneManager* GetInstance();

	using Factory = std::function<std::unique_ptr<IScene>()>;

	// Scene transition: RequestChange first covers the screen, swaps the scene
	// (showing the loading screen while it initializes) and then reveals it.
	struct Transition {
		enum class Phase { Cover, Reveal } phase = Phase::Cover;
		float progress = 0;        // 0..1 within the phase
		float seconds = 0;         // phase duration
		bool loadingShown = false; // the loading screen was on screen for the swap
		std::string from, to;
	};
	using TransitionDrawer = std::function<void(const Transition&)>;
	void SetTransitionDrawer(TransitionDrawer drawer) { transitionDrawer_ = std::move(drawer); }
	bool InTransition() const { return transitionActive_; }
	// UI 専用シーン (RequiresFullRenderer()==false) でも 3D パイプラインを先に用意するシーン。
	// エディタはゲーム画像を Game ビューに表示するため、通常のフレーム経路が必要になる
	void SetFullRendererScenes(std::unordered_set<std::string> names) { fullRendererScenes_ = std::move(names); }

public:
	void Register(const std::string& name, Factory factory);

	bool Change(const std::string& name, const SceneParameters& params = {});
	void RequestChange(const std::string& name, const SceneParameters& params = {});

	void ProcessPendingChange(); // ★追加: BeginFrame前にシーン切り替えを処理する
	void Update();
	void Draw();

	IScene* Current() const { return current_.get(); }
	const std::string& CurrentName() const { return currentName_; }
	const SceneParameters& CurrentParameters() const { return currentParams_; }

	void SetDX(WindowDX* dx) { dx_ = dx; }

	void Clear();

	// ---- 追加：原因調査＆自動起動用 ----
	bool Has(const std::string& name) const;
	std::string FirstRegisteredName() const; // 登録済みの先頭（無ければ空）
	std::vector<std::string> RegisteredNames() const;

private:
	std::unordered_map<std::string, Factory> factories_;
	std::unordered_set<std::string> fullRendererScenes_;
	std::unique_ptr<IScene> current_;
	std::string currentName_;
	SceneParameters currentParams_;
	std::string pendingNext_;
	SceneParameters pendingParams_;
	WindowDX* dx_ = nullptr;

	TransitionDrawer transitionDrawer_;
	Transition transition_;
	bool transitionActive_ = false;
	std::chrono::steady_clock::time_point transitionClock_{};
	static constexpr float kCoverSeconds = 0.6f, kRevealSeconds = 0.75f;
	void BeginReveal(const std::string& from, bool loadingShown);

	static SceneManager* instance_;
};

} // namespace Engine
