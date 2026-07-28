#include "SceneManager.h"
#include "Renderer.h"
#include <Windows.h> // OutputDebugStringA
#include <cstdio>

static void LogFileSM(const char* msg) {
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
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
	current_ = it->second();
	currentName_ = name;

	if (current_) {
		OutputDebugStringA("[SceneManager] Initialize scene\n");
		current_->Initialize(dx_, params);
	}

	pendingNext_.clear();
	pendingParams_ = {};
	return true;
}

void SceneManager::RequestChange(const std::string& name, const SceneParameters& params) {
	pendingNext_ = name;
	pendingParams_ = params;
}

void SceneManager::ProcessPendingChange() {
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
}

void SceneManager::Clear() {
	if (dx_) {
		if (auto* renderer = Renderer::GetInstance()) {
			renderer->WaitGPU();
		}
	}
	current_.reset();
	currentName_.clear();
	pendingNext_.clear();
	factories_.clear();
}

} // namespace Engine
