// SceneMusic.h
// シーンごとの BGM の再生。曲のファイルと音量は Resources/Data/music.json に置く。
#pragma once
#include "../../Engine/Audio.h"
#include "../Data/GameData.h"
#include <array>
#include <string>

namespace Game::Music {
// 曲の種類 (タイトル・セレクト・戦闘・勝利・敗北)
enum Track { Title, Select, Battle, Victory, Defeat, TrackCount };

// 曲のファイルと音量
struct TrackInfo { std::string path; float volume = 0; };

// music.json を読み込む (最初に使われたときに 1 回だけ)
inline const std::array<TrackInfo, TrackCount>& Tracks() {
    static const std::array<TrackInfo, TrackCount> tracks = [] {
        const auto& json = Data::Require(Data::Load("music.json"), "tracks", "music.json");
        const char* names[TrackCount] = {"Title", "Select", "Battle", "Victory", "Defeat"};
        std::array<TrackInfo, TrackCount> out;
        for (int i = 0; i < TrackCount; ++i) {
            const auto& track = Data::Require(json, names[i], "music.json tracks");
            out[size_t(i)] = {track.at("path").get<std::string>(), track.at("volume").get<float>()};
        }
        return out;
    }();
    return tracks;
}

// 曲のファイルのパス
inline const std::string& Path(Track track) { return Tracks()[size_t(track)].path; }

// 曲を music.json の音量で再生する (同じ曲なら続きから)
inline void Play(Track track) {
    if (auto* audio = Engine::Audio::GetInstance()) audio->PlayBGM(Path(track), Tracks()[size_t(track)].volume);
}
}
