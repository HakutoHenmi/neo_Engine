// AudioSystem.h
// 音の発信源 (AudioSource) の再生開始と、聞く位置からの距離による音量の減衰を行うシステム。
#pragma once
#include "ISystem.h"

namespace Game {

// 音のシステム
class AudioSystem : public ISystem {
public:
	// PLAY 中だけ動かす
	void Update(entt::registry& registry, GameContext& ctx) override;
};

} // namespace Game
