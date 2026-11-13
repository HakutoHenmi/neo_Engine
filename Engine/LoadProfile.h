#pragma once
// ロード時間の内訳を調べるための計測 (--load-profile を付けたときだけ詳しく記録する)。
// 1 件ごとに「プロセス開始からの秒・区分・所要ミリ秒・名前」を load-profile.txt へ追記する。
// あわせて、引っかかり (ロード画面が止まった・ゲーム中に 1 フレームが長かった) だけは常に
// %LOCALAPPDATA%/LiquidTime/load-log.txt に残す。ほかの PC で遅いと言われたとき、このファイルを送ってもらえば原因の段階がわかる。
// ゲーム側のヘッダーでも使うため、min / max マクロを持ち込まない
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Engine::LoadProfile {
inline bool Enabled() {
	static const bool enabled = wcsstr(GetCommandLineW(), L"--load-profile") != nullptr;
	return enabled;
}
inline double ProcessSeconds() {
	FILETIME created{}, exited{}, kernel{}, user{}, now{};
	GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
	GetSystemTimePreciseAsFileTime(&now);
	ULARGE_INTEGER start{}, end{};
	start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
	end.LowPart = now.dwLowDateTime; end.HighPart = now.dwHighDateTime;
	return double(end.QuadPart - start.QuadPart) / 10000000.0;
}
inline void Record(const char* category, const std::string& name, double ms) {
	if (!Enabled()) return;
	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);
	static bool first = true;
	FILE* file = nullptr;
	fopen_s(&file, "load-profile.txt", first ? "w" : "a");
	if (!file) return;
	first = false;
	fprintf(file, "%.3f\t%s\t%.2f\t%s\n", ProcessSeconds(), category, ms, name.c_str());
	fclose(file);
}

// 常に残す短い記録 (%LOCALAPPDATA%/LiquidTime/load-log.txt、起動ごとに作り直す)。
// 引っかかりとロードの合計だけを書くので、普段のプレイではほとんど書かない
inline void Log(const char* category, const std::string& text, double ms) {
	Record(category, text, ms);
	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);
	static const std::filesystem::path path = [] {
		wchar_t buffer[MAX_PATH]{};
		const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return std::filesystem::path{};
		std::error_code ec;
		auto dir = std::filesystem::path(buffer) / L"LiquidTime";
		std::filesystem::create_directories(dir, ec);
		return dir / L"load-log.txt";
	}();
	static int lines = 0;
	if (path.empty() || lines > 2000) return;
	FILE* file = nullptr;
	_wfopen_s(&file, path.c_str(), lines++ == 0 ? L"w" : L"a");
	if (!file) return;
	fprintf(file, "%.3f\t%s\t%.1f\t%s\n", ProcessSeconds(), category, ms, text.c_str());
	fclose(file);
}

// メインスレッドで今進めている段階の名前 (ロード画面のスレッドが、止まった理由を書くときに読む)。
// あわせて、直近で時間のかかった段階を覚えておき、ゲーム中の引っかかりの説明に使う
struct StepState {
	std::mutex mutex;
	std::thread::id main;
	std::string current;
	std::string slow;    // 直近の遅い段階 (ms 付き、; 区切り)
};
inline StepState& Steps() { static StepState s; return s; }
inline void SetMainThread() { std::lock_guard<std::mutex> lock(Steps().mutex); Steps().main = std::this_thread::get_id(); }
inline bool OnMainThread() { return Steps().main == std::this_thread::get_id(); }
inline std::string CurrentStep() { std::lock_guard<std::mutex> lock(Steps().mutex); return Steps().current; }
// 直近の遅い段階を取り出して空にする
inline std::string TakeSlowSteps() { std::lock_guard<std::mutex> lock(Steps().mutex); std::string out; out.swap(Steps().slow); return out; }

// スコープを抜けたときに所要時間を記録する
class Scope {
public:
	// recordAboveMs: 毎フレーム通る段階は、これより長いときだけ load-profile.txt に書く
	Scope(const char* category, std::string name, double recordAboveMs = 0) : category_(category), name_(std::move(name)), recordAboveMs_(recordAboveMs), start_(std::chrono::steady_clock::now()) {
		if (!OnMainThread()) return;
		tracked_ = true;
		std::lock_guard<std::mutex> lock(Steps().mutex);
		previous_ = std::move(Steps().current);
		Steps().current = std::string(category_) + ": " + name_;
	}
	~Scope() {
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
		if (Enabled() && ms >= recordAboveMs_) Record(category_, name_, ms);
		if (!tracked_) return;
		std::lock_guard<std::mutex> lock(Steps().mutex);
		Steps().current = std::move(previous_);
		// 1 フレームを大きく越えた段階は覚えておく (外側の段階も入るので、内訳がわかる)
		if (ms >= 30 && Steps().slow.size() < 1500) {
			char buffer[32];
			sprintf_s(buffer, " %.0fms; ", ms);
			Steps().slow += std::string(category_) + ": " + name_ + buffer;
		}
	}
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;
private:
	const char* category_;
	std::string name_;
	std::string previous_;
	double recordAboveMs_ = 0;
	bool tracked_ = false;
	std::chrono::steady_clock::time_point start_;
};
inline std::string Narrow(const std::wstring& text) {
	if (text.empty()) return {};
	std::string out(size_t(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr)), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), int(out.size()), nullptr, nullptr);
	return out;
}
}
