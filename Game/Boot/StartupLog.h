// StartupLog.h
// 起動ログ (game.json の logFile) への書き込み。
// 起動途中で落ちたときに、どこまで進んだかを調べるために使う。
#pragma once

#include <string>

namespace Game::Boot {

// ログファイルの場所を決めて、中身を空にして作り直す (作業ディレクトリを決めた後に呼ぶ)
void ResetStartupLog(const std::string& path);

} // namespace Game::Boot

// 起動ログに 1 行追記する (ファイルが決まる前は起動時間の計測だけ行う)
void LogFileMain(const char* msg);
