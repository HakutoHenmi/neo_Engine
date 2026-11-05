// UISound.h
// メニューの操作音 (カーソル移動・決定・戻る)。音は Resources/Sound/Devour/ の ui-*.wav。
#pragma once
#include "../../Engine/Audio.h"
#include <chrono>

namespace Game::UI {
// 操作音の種類
enum class UISound { Move, Confirm, Back, Count };

// 操作音を鳴らす。マウスのクリックとキーの決定が同時に届いても 1 回だけ鳴らす
inline void PlayUISound(UISound sound) {
    auto* audio = Engine::Audio::GetInstance();
    if (!audio) return;
    static const uint32_t handles[] = {audio->Load("Resources/Sound/Devour/ui-move.wav"),
        audio->Load("Resources/Sound/Devour/ui-confirm.wav"), audio->Load("Resources/Sound/Devour/ui-back.wav")};
    static std::chrono::steady_clock::time_point last[int(UISound::Count)];
    const auto now = std::chrono::steady_clock::now();
    auto& played = last[int(sound)];
    if (now - played < std::chrono::milliseconds(60)) return;
    played = now;
    audio->Play(handles[int(sound)], false, .7f * audio->GetMasterSEVolume());
}
}
