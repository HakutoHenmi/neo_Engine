#pragma once
// ロード時間の内訳を調べるための計測 (--load-profile を付けたときだけ動く)。
// 1 件ごとに「プロセス開始からの秒・区分・所要ミリ秒・名前」を load-profile.txt へ追記する。
// ゲーム側のヘッダーでも使うため、min / max マクロを持ち込まない
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <string>

namespace Engine::LoadProfile {
inline bool Enabled() {
	static const bool enabled = wcsstr(GetCommandLineW(), L"--load-profile") != nullptr;
	return enabled;
}
inline void Record(const char* category, const std::string& name, double ms) {
	if (!Enabled()) return;
	static bool first = true;
	// プロセス開始からの秒 (起動から遷移までを同じ時間軸で見る)
	FILETIME created{}, exited{}, kernel{}, user{}, now{};
	GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
	GetSystemTimePreciseAsFileTime(&now);
	ULARGE_INTEGER start{}, end{};
	start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
	end.LowPart = now.dwLowDateTime; end.HighPart = now.dwHighDateTime;
	FILE* file = nullptr;
	fopen_s(&file, "load-profile.txt", first ? "w" : "a");
	if (!file) return;
	first = false;
	fprintf(file, "%.3f\t%s\t%.2f\t%s\n", double(end.QuadPart - start.QuadPart) / 10000000.0, category, ms, name.c_str());
	fclose(file);
}
// スコープを抜けたときに所要時間を記録する
class Scope {
public:
	Scope(const char* category, std::string name) : category_(category), name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}
	~Scope() { if (Enabled()) Record(category_, name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count()); }
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;
private:
	const char* category_;
	std::string name_;
	std::chrono::steady_clock::time_point start_;
};
inline std::string Narrow(const std::wstring& text) {
	if (!Enabled() || text.empty()) return {};
	std::string out(size_t(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr)), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), int(out.size()), nullptr, nullptr);
	return out;
}
}
