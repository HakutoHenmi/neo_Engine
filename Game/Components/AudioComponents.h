// AudioComponents.h
// 音のコンポーネント: 音の発信源と、音を聞く位置。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "ComponentBase.h"

namespace Game {

// 音の分類 (音量設定を BGM と効果音で分けるため)
enum class AudioCategory { BGM, SE };

// 音の発信源 (再生するファイル・音量・ループ・3D 減衰)
struct AudioSourceComponent : public Component {
	std::string soundPath = "";       // 音声ファイルパス
	uint32_t soundHandle = 0xFFFFFFFF; // Audio::Load()で取得したハンドル
	size_t voiceHandle = 0;            // 再生中のボイスハンドル
	float volume = 1.0f;               // 音量 (0.0〜1.0)
	bool loop = false;                 // ループ再生
	bool playOnStart = false;          // Play時に自動再生
	bool is3D = true;                  // 3Dサウンド（距離減衰あり）
	float maxDistance = 50.0f;         // 減衰最大距離
	bool isPlaying = false;            // 再生中フラグ
	AudioCategory category = AudioCategory::SE; // 音のカテゴリ (BGM / 効果音)
	AudioSourceComponent() { type = ComponentType::AudioSource; }
};

// 音を聞く位置 (通常はカメラに付ける)
struct AudioListenerComponent : public Component {
	AudioListenerComponent() { type = ComponentType::AudioListener; }
};

} // namespace Game
