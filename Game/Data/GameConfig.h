// GameConfig.h
// ゲーム全体の設定 (Resources/Data/game.json): 最初のシーン・本編のシーンファイル・
// 予備のシーン・ログのファイル名・起動オプション (コマンドライン引数 → シーン)。
#pragma once
#include "GameData.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Game::Data {

// コマンドライン引数で起動シーンを差し替える設定
struct LaunchOption {
    std::string flag, scene, log;
    bool debugOnly = false;
};

// game.json の中身
struct GameConfig {
    std::string initialScene, mainStage, fallbackStage, logFile;
    std::vector<std::string> fullRendererScenes;
    std::vector<LaunchOption> launchOptions;
};

// game.json を読み込む (最初に使われたときに 1 回だけ)
inline const GameConfig& Config() {
    static const GameConfig config = [] {
        const char* file = "game.json";
        const auto& json = Load(file);
        GameConfig out;
        out.initialScene = Require(json, "initialScene", file).get<std::string>();
        out.mainStage = Require(json, "mainStage", file).get<std::string>();
        out.fallbackStage = Require(json, "fallbackStage", file).get<std::string>();
        out.logFile = Require(json, "logFile", file).get<std::string>();
        out.fullRendererScenes = ReadStrings(Require(json, "fullRendererScenes", file));
        for (const auto& option : Require(json, "launchOptions", file)) {
            LaunchOption o;
            o.flag = option.at("flag").get<std::string>();
            o.scene = option.at("scene").get<std::string>();
            o.log = option.value("log", std::string());
            o.debugOnly = option.value("debugOnly", false);
            out.launchOptions.push_back(std::move(o));
        }
        return out;
    }();
    return config;
}

// 本編のシーンファイル
inline const std::string& MainStagePath() { return Config().mainStage; }

inline constexpr const char* SlimePlayerDemoStage = "Resources/Scenes/slime_player_demo.json";
inline bool IsSlimePlayerDemoStage(const std::string& path) {
    return std::filesystem::path(path).filename() == "slime_player_demo.json";
}

// 本編のシーンか (空ならシーンの指定なし = 本編)。ファイル名で比べるので絶対パスでもよい
inline bool IsMainStage(const std::string& stagePath) {
    if (stagePath.empty()) return true;
    const std::string name = std::filesystem::path(MainStagePath()).filename().string();
    return stagePath.size() >= name.size() && stagePath.compare(stagePath.size() - name.size(), name.size(), name) == 0;
}

} // namespace Game::Data
