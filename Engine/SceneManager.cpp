#include "SceneManager.h"
#include "Renderer.h"
#include "LoadingScreen.h"
#include "LoadProfile.h"
#include <Windows.h> // OutputDebugStringA
#include <cstdio>

static void LogFileSM(const char* msg) {
#if defined(_DEBUG)
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
#else
	(void)msg;
#endif
}

namespace Engine {


SceneManager* SceneManager::instance_ = nullptr;

SceneManager* SceneManager::GetInstance() { return instance_; }

void SceneManager::Register(const std::string& name, Factory factory) { factories_[name] = std::move(factory); }

bool SceneManager::Has(const std::string& name) const { return factories_.find(name) != factories_.end(); }

std::string SceneManager::FirstRegisteredName() const {
	if (factories_.empty())
		return {};
	// unordered_map は順序不定だが「何も無いよりマシ」な自動起動用
	return factories_.begin()->first;
}

std::vector<std::string> SceneManager::RegisteredNames() const {
	std::vector<std::string> out;
	out.reserve(factories_.size());
	for (auto& kv : factories_)
		out.push_back(kv.first);
	return out;
}

bool SceneManager::Change(const std::string& name, const SceneParameters& params) {
	LoadProfile::Scope profile("scene", "Change " + name);
	auto it = factories_.find(name);
	if (it == factories_.end()) {
		OutputDebugStringA("[SceneManager] Change failed: not registered\n");
		return false;
	}

	std::string changeLog = "  SceneManager: Change to " + name;
	LogFileSM(changeLog.c_str());

	if (dx_) {
		// ★修正: dx_->WaitIdle() は WindowDX の fence_ を直接進めてしまい、描画フレームの同期を破壊するため、
		// 代わりに一時フェンスで安全に待機する Renderer::WaitGPU() を使用します。
		if (auto* renderer = Renderer::GetInstance()) {
			renderer->WaitGPU();
		}
	}
	const bool startup = !current_;
	const std::string from = currentName_;
	// Release the previous scene first so the loading screen never draws stale state.
	current_.reset();
	auto next = it->second();
	auto* renderer = Renderer::GetInstance();
	// The first world scene also finishes the renderer's deferred 3D setup.
	const bool needs3D = next && (next->RequiresFullRenderer() || fullRendererScenes_.count(name) != 0) && renderer && !renderer->Ready3D();
	const bool showLoading = !startup || needs3D;
	if (showLoading) LoadingScreen::Begin(needs3D ? name + "+Engine" : name, startup);
	if (needs3D) renderer->InitializeDeferred();
	current_ = std::move(next);
	currentName_ = name;
	currentParams_ = params;

	if (current_) {
		OutputDebugStringA("[SceneManager] Initialize scene\n");
		current_->Initialize(dx_, params);
	}
	if (showLoading) LoadingScreen::End();
	// The startup splash already covers initialization; show the first scene immediately.
	if (transitionDrawer_ && !startup) BeginReveal(from, LoadingScreen::Active() || LoadingScreen::Last().elapsed > 0.35f);

	pendingNext_.clear();
	pendingParams_ = {};
	return true;
}

void SceneManager::RequestChange(const std::string& name, const SceneParameters& params) {
	pendingNext_ = name;
	pendingParams_ = params;
	if (!transitionDrawer_) return;
	// A request during the cover only retargets it; one during the reveal re-covers from here.
	if (transitionActive_ && transition_.phase == Transition::Phase::Cover) {
		transition_.to = name;
		return;
	}
	const float start = transitionActive_ ? (1 - transition_.progress) : 0.f;
	transition_ = {};
	transition_.phase = Transition::Phase::Cover;
	transition_.seconds = kCoverSeconds;
	transition_.progress = start;
	transition_.from = currentName_;
	transition_.to = name;
	transitionActive_ = true;
	transitionClock_ = std::chrono::steady_clock::now();
}

void SceneManager::BeginReveal(const std::string& from, bool loadingShown) {
	transition_ = {};
	transition_.phase = Transition::Phase::Reveal;
	transition_.seconds = kRevealSeconds;
	transition_.loadingShown = loadingShown;
	transition_.from = from;
	transition_.to = currentName_;
	transitionActive_ = true;
	transitionClock_ = std::chrono::steady_clock::now();
}

void SceneManager::ProcessPendingChange() {
	if (transitionActive_) {
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::min(std::chrono::duration<float>(now - transitionClock_).count(), 0.05f);
		transitionClock_ = now;
		transition_.progress = std::min(1.f, transition_.progress + dt / transition_.seconds);
		if (transition_.phase == Transition::Phase::Cover) {
			// Swap only once the screen is fully covered.
			if (transition_.progress < 1 || pendingNext_.empty()) return;
		} else {
			if (transition_.progress >= 1) transitionActive_ = false;
			return;
		}
	}
	if (!pendingNext_.empty()) {
		LogFileSM("  SceneManager: Change pending scene (before BeginFrame)");
		Change(pendingNext_, pendingParams_);
	}
}

void SceneManager::Update() {
	// ★修正: シーン切り替えはProcessPendingChange()に移動済み（BeginFrame前に呼ばれる）

	if (current_) {
		LogFileSM("  SceneManager: Update current scene");
		current_->Update();
		LogFileSM("  SceneManager: Update done");

		if (current_->IsEnd()) {
			const std::string next = current_->Next();
			if (!next.empty()) {
				// ★修正: 即時切り替えではなくリクエストにして次フレームのProcessPendingChangeで処理する
				RequestChange(next);
			}
		}
	}
}

void SceneManager::Draw() {
	if (current_) {
		LogFileSM("  SceneManager: Draw current scene");
		current_->Draw();
		LogFileSM("  SceneManager: Draw done");
	}
	if (transitionActive_ && transitionDrawer_) {
		if (auto* renderer = Renderer::GetInstance()) {
			renderer->SetOverlayQueue(true);
			transitionDrawer_(transition_);
			renderer->SetOverlayQueue(false);
		}
	}
}

void SceneManager::Clear() {
	if (dx_) {
		if (auto* renderer = Renderer::GetInstance()) {
			renderer->WaitGPU();
		}
	}
	current_.reset();
	currentName_.clear();
	currentParams_ = {};
	pendingNext_.clear();
	factories_.clear();
}

} // namespace Engine
