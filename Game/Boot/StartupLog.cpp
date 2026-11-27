// StartupLog.cpp
// 起動ログへの書き込み。
#include "StartupLog.h"
#include "../../Engine/Core/StartupTiming.h"
#include <cstdio>

namespace {

// 起動ログのファイル (ResetStartupLog で決まるまでは空)
std::string g_logPath;

} // namespace

namespace Game::Boot {

void ResetStartupLog(const std::string& path) {
	g_logPath = path;
	FILE* file = nullptr;
	fopen_s(&file, g_logPath.c_str(), "w");
	if (!file) {
		return;
	}
	fputs("WinMain started\n", file);
	fclose(file);
}

} // namespace Game::Boot

void LogFileMain(const char* msg) {
	Engine::StartupTiming::Mark(msg);
	if (g_logPath.empty()) {
		return;
	}
	// 追記モードで開き、開けたときだけ 1 行書いて閉じる
	FILE* file = nullptr;
	fopen_s(&file, g_logPath.c_str(), "a");
	if (!file) {
		return;
	}
	fputs(msg, file);
	fputc('\n', file);
	fclose(file);
}
