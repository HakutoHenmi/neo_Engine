#pragma once
#include "../../Engine/Audio.h"

namespace Game::Music {
inline constexpr const char* Title = "Resources/Sound/BGM/Call to Adventure.mp3";
inline constexpr const char* Select = "Resources/Sound/BGM/Carefree.mp3";
inline constexpr const char* Battle = "Resources/Sound/BGM/Volatile Reaction.mp3";
inline constexpr const char* Victory = "Resources/Sound/BGM/Fanfare for Space.mp3";
inline constexpr const char* Defeat = "Resources/Sound/BGM/Evening Fall Harp.mp3";
inline void Play(const char* path, float volume = .32f) {
    if (auto* audio = Engine::Audio::GetInstance()) audio->PlayBGM(path, volume);
}
}
