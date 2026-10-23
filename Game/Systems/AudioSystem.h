// AudioSystem.h
// 音の発信源 (AudioSource) の再生開始と、聞く位置からの距離による音量の減衰を行うシステム。
#pragma once
#include "ISystem.h"
#include "../../Engine/Audio.h"
#include <cmath>

namespace Game {

// 音のシステム
class AudioSystem : public ISystem {
public:
	// PLAY 中だけ動かす
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// 聞く位置: 有効な AudioListener があればその位置、無ければカメラの位置
		DirectX::XMFLOAT3 listenerPos = ctx.camera ? ctx.camera->Position() : DirectX::XMFLOAT3{0, 0, 0};
		// 有効な AudioListener を持つ最初のエンティティを聞く位置にする
		auto listenerView = registry.view<AudioListenerComponent, TransformComponent>();
		for (auto entity : listenerView) {
			auto& al = listenerView.get<AudioListenerComponent>(entity);
			if (al.enabled) {
				auto& tc = listenerView.get<TransformComponent>(entity);
				listenerPos = tc.translate;
				break;
			}
		}

		// 各発信源: 自動再生の開始と、距離減衰とマスター音量を掛けた音量の反映
		auto* audio = Engine::Audio::GetInstance();
		if (!audio) return;

		// 音の発信源とトランスフォームを持つエンティティを処理する
		auto sourceView = registry.view<AudioSourceComponent, TransformComponent>();
		for (auto entity : sourceView) {
			auto& as = sourceView.get<AudioSourceComponent>(entity);
			auto& tc = sourceView.get<TransformComponent>(entity);
			if (!as.enabled) continue;

			// 開始時に再生する設定で、まだ再生していなければ再生を始める
			if (as.playOnStart && !as.isPlaying && as.soundHandle != 0xFFFFFFFF) {
				as.voiceHandle = audio->Play(as.soundHandle, as.loop, as.volume);
				as.isPlaying = true;
			}

			// 再生中なら音量を計算し直す
			if (as.isPlaying && as.voiceHandle != 0) {
				float finalVol = as.volume;
				// 3D の音は、最大距離で 0 になるよう距離の2乗で減衰させる
				if (as.is3D) {
					// 発信源と聞く位置の距離
					float dx = tc.translate.x - listenerPos.x;
					float dy = tc.translate.y - listenerPos.y;
					float dz = tc.translate.z - listenerPos.z;
					float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (as.maxDistance > 0.001f) {
						float atten = 1.0f - (dist / as.maxDistance);
						if (atten < 0.0f) atten = 0.0f;
						atten = atten * atten;
						finalVol *= atten;
					}
				}
				
				// カテゴリごとのマスター音量を掛ける
				if (as.category == AudioCategory::BGM) {
					finalVol *= audio->GetMasterBGMVolume();
				} else {
					finalVol *= audio->GetMasterSEVolume();
				}
				
				// 計算した音量を設定する
				audio->SetVolume(as.voiceHandle, finalVol);
			}
		}
	}
};

} // namespace Game
