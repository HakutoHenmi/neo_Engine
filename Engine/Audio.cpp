#include "Audio.h"
#include "PathUtils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "NetworkProfiler.h"
#include "LoadProfile.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace Engine {
namespace {
// Decoder calls can run on a startup worker. Balance COM on that same thread;
// voices and decoded PCM are handed off only after the worker has joined.
struct AudioCOMScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~AudioCOMScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
}


Audio* Audio::instance_ = nullptr;

Audio::Audio() {}
Audio::~Audio() { Shutdown(); }

Audio* Audio::GetInstance() { return instance_; }

bool Audio::Initialize() {
	instance_ = this; // インスタンス登録

	AudioCOMScope com;
	HRESULT hr;
	if (FAILED(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR)))
		return false;

	if (FAILED(xa_->CreateMasteringVoice(&master_)))
		return false;

	hr = MFStartup(MF_VERSION);
	mfStarted_ = SUCCEEDED(hr);
	if (FAILED(hr))
		return false;

	return true;
}

void Audio::Shutdown() {
	// Join background decodes before Media Foundation shuts down.
	pendingBgm_.clear();
	bgmVoice_ = retiringBgmVoice_ = 0;
	bgmPath_.clear();
	bgmSounds_.clear();
	// 全ボイス停止・破棄
	for (auto& pair : activeVoices_) {
		if (pair.second.source) {
			pair.second.source->Stop();
			pair.second.source->DestroyVoice();
		}
	}
	activeVoices_.clear();

	if (master_) {
		master_->DestroyVoice();
		master_ = nullptr;
	}
	xa_.Reset();
	if (mfStarted_) MFShutdown();
	mfStarted_ = false;

	instance_ = nullptr;
}

// std::string 版 Load
uint32_t Audio::Load(const std::string& path) {
	LoadProfile::Scope profile("audio", path);
	// ★修正: GetUnifiedPath を使用して絶対パスを解決し、ワイド文字に変換
	std::wstring wpath = Engine::PathUtils::FromUTF8(Engine::PathUtils::GetUnifiedPath(path));

	SoundData newSound = {};
	if (LoadViaMF(wpath, newSound))
		return Register(path, std::move(newSound));
	return 0xFFFFFFFF; // エラー
}

uint32_t Audio::Register(const std::string& path, SoundData&& sound) {
	float sizeMB = (float)sound.data.size() / (1024.0f * 1024.0f);
	std::string detailsStr = std::to_string(sound.wfx.nSamplesPerSec) + "Hz " + std::to_string(sound.wfx.nChannels) + "ch PCM";
	NetworkProfiler::GetInstance().RegisterAsset(path, "Audio", sizeMB, detailsStr);
	soundDatas_.push_back(std::move(sound));
	return (uint32_t)(soundDatas_.size() - 1);
}

// ハンドル指定再生
size_t Audio::Play(uint32_t soundHandle, bool loop, float volume, float pitch) {
	// 定期お掃除 (再生終わったボイスを消す)
	GarbageCollect();

	if (!xa_ || soundHandle >= soundDatas_.size())
		return 0;

	const auto& sd = soundDatas_[soundHandle];
	IXAudio2SourceVoice* src = nullptr;

	if (FAILED(xa_->CreateSourceVoice(&src, &sd.wfx))) {
		return 0;
	}

	src->SetVolume(volume);
	src->SetFrequencyRatio(std::clamp(pitch,0.5f,2.0f));

	XAUDIO2_BUFFER buf{};
	buf.pAudioData = sd.data.data();
	buf.AudioBytes = (UINT32)sd.data.size();
	buf.Flags = XAUDIO2_END_OF_STREAM;
	if (loop) {
		buf.LoopCount = XAUDIO2_LOOP_INFINITE;
	}

	src->SubmitSourceBuffer(&buf);
	src->Start();

	size_t handle = nextVoiceHandle_++;
	VoiceData vd;
	vd.source = src;
	vd.isLoop = loop;
	activeVoices_[handle] = vd;

	return handle;
}

void Audio::Stop(size_t voiceHandle) {
	auto it = activeVoices_.find(voiceHandle);
	if (it != activeVoices_.end()) {
		if (it->second.source) {
			it->second.source->Stop();
			it->second.source->FlushSourceBuffers();
			it->second.source->DestroyVoice();
		}
		activeVoices_.erase(it);
	}
}

void Audio::SetVolume(size_t voiceHandle, float volume) {
	auto it = activeVoices_.find(voiceHandle);
	if (it != activeVoices_.end() && it->second.source) {
		it->second.source->SetVolume(volume);
	}
}

void Audio::SetPitch(size_t voiceHandle, float pitch) {
	auto it = activeVoices_.find(voiceHandle);
	if (it != activeVoices_.end() && it->second.source) {
		it->second.source->SetFrequencyRatio(std::clamp(pitch, 0.5f, 2.0f));
	}
}

void Audio::SetSEPaused(bool paused) {
	if (paused == sePaused_) return;
	sePaused_ = paused;
	for (auto& pair : activeVoices_) {
		if (pair.first == bgmVoice_ || pair.first == retiringBgmVoice_ || !pair.second.source) continue;
		if (paused) pair.second.source->Stop();
		else pair.second.source->Start();
	}
}

void Audio::StopAll() {
	sePaused_ = false;
	bgmVoice_ = retiringBgmVoice_ = 0;
	bgmPath_.clear();
	for(auto& pair : activeVoices_) {
		if(pair.second.source) {
			pair.second.source->Stop();
			pair.second.source->FlushSourceBuffers();
			pair.second.source->DestroyVoice();
		}
	}
	activeVoices_.clear();
}

void Audio::PreloadBGMAsync(const std::string& path) {
	if (path.empty() || !mfStarted_ || bgmSounds_.contains(path) || pendingBgm_.contains(path)) return;
	const std::wstring wpath = PathUtils::FromUTF8(PathUtils::GetUnifiedPath(path));
	pendingBgm_[path] = std::async(std::launch::async, [wpath, path] {
		LoadProfile::Scope profile("audio-async", path);
		// The decoder is the only CPU-heavy work here; keep it behind the game and loading threads.
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		SoundData sound{};
		if (!LoadViaMF(wpath, sound)) sound.data.clear();
		return sound;
	});
}

bool Audio::PreloadBGM(const std::string& path) {
	if (bgmSounds_.contains(path)) return true;
	if (auto pending = pendingBgm_.find(path); pending != pendingBgm_.end()) {
		// Usually finished long ago; otherwise this waits only for the rest of the decode.
		LoadProfile::Scope profile("audio-wait", path);
		SoundData sound = pending->second.get();
		pendingBgm_.erase(pending);
		if (!sound.data.empty()) {
			bgmSounds_[path] = Register(path, std::move(sound));
			return true;
		}
	}
	const uint32_t sound = Load(path);
	if (sound == 0xFFFFFFFF) return false;
	bgmSounds_[path] = sound;
	return true;
}

bool Audio::PlayBGM(const std::string& path, float volume) {
	bgmPitchTarget_ = 1;
	if (path == bgmPath_ && IsBGMPlaying()) return true;
	uint32_t sound = PreloadBGM(path) ? bgmSounds_.at(path) : 0xFFFFFFFF;
	if (sound == 0xFFFFFFFF) {
		OutputDebugStringA(("BGM load failed: " + path + "\n").c_str());
		return false;
	}
	bgmSounds_[path] = sound;
	const size_t voice = Play(sound, true, 0);
	if (!voice) return false;
	StopBGM();
	bgmVoice_ = voice;
	bgmPath_ = path;
	bgmPitch_ = 1;
	bgmGain_ = std::clamp(volume, 0.0f, 1.0f);
	bgmFade_ = 0;
	bgmDuckTarget_ = 1;
	return true;
}

void Audio::StopBGM() {
	Stop(retiringBgmVoice_);
	retiringBgmVoice_ = bgmVoice_;
	retiringBgmGain_ = bgmGain_ * bgmFade_;
	retiringBgmFade_ = 1;
	bgmVoice_ = 0;
	bgmPath_.clear();
}

bool Audio::IsBGMPlaying() const {
	auto it = activeVoices_.find(bgmVoice_);
	if (it == activeVoices_.end() || !it->second.source) return false;
	XAUDIO2_VOICE_STATE state{};
	it->second.source->GetState(&state);
	return state.BuffersQueued != 0;
}

void Audio::UpdateBGM(float dt) {
	dt = std::clamp(dt, 0.0f, 0.1f);
	bgmDuck_ += (bgmDuckTarget_ - bgmDuck_) * std::min(1.0f, dt * 8.0f);
	bgmFade_ = std::min(1.0f, bgmFade_ + dt / 0.65f);
	SetVolume(bgmVoice_, bgmGain_ * bgmFade_ * masterBGMVolume_ * bgmDuck_);
	if (bgmPitch_ != bgmPitchTarget_) {
		bgmPitch_ += (bgmPitchTarget_ - bgmPitch_) * std::min(1.0f, dt * 3.0f);
		if (std::abs(bgmPitch_ - bgmPitchTarget_) < 0.001f) bgmPitch_ = bgmPitchTarget_;
		SetPitch(bgmVoice_, bgmPitch_);
	}
	if (retiringBgmVoice_) {
		retiringBgmFade_ = std::max(0.0f, retiringBgmFade_ - dt / 0.65f);
		SetVolume(retiringBgmVoice_, retiringBgmGain_ * retiringBgmFade_ * masterBGMVolume_ * bgmDuck_);
		if (retiringBgmFade_ == 0) { Stop(retiringBgmVoice_); retiringBgmVoice_ = 0; }
	}
}

void Audio::GarbageCollect() {
	// 再生が終了している非ループボイスを削除
	for (auto it = activeVoices_.begin(); it != activeVoices_.end();) {
		if (!it->second.isLoop) {
			XAUDIO2_VOICE_STATE state;
			it->second.source->GetState(&state);
			if (state.BuffersQueued == 0) { // 再生完了
				it->second.source->DestroyVoice();
				it = activeVoices_.erase(it);
				continue;
			}
		}
		++it;
	}
}

// MediaFoundationを使ったロード実装
bool Audio::LoadViaMF(const std::wstring& path, SoundData& outData) {
	AudioCOMScope com;
	HRESULT hr;
	Microsoft::WRL::ComPtr<IMFSourceReader> reader;
	hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
	if (FAILED(hr))
		return false;

	hr = reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	if (FAILED(hr))
		return false;
	hr = reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
	if (FAILED(hr))
		return false;

	Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
	MFCreateMediaType(&mediaType);
	mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mediaType.Get());
	if (FAILED(hr))
		return false;

	Microsoft::WRL::ComPtr<IMFMediaType> outputMediaType;
	reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outputMediaType);

	WAVEFORMATEX* pWfx = nullptr;
	UINT32 size = 0;
	MFCreateWaveFormatExFromMFMediaType(outputMediaType.Get(), &pWfx, &size);
	if (pWfx) {
		outData.wfx = *pWfx;
		CoTaskMemFree(pWfx);
	}

	while (true) {
		Microsoft::WRL::ComPtr<IMFSample> sample;
		DWORD flags = 0;
		hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
			break;

		if (sample) {
			Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
			sample->ConvertToContiguousBuffer(&buffer);
			BYTE* pAudioData = nullptr;
			DWORD cbCurrentLength = 0;
			if (SUCCEEDED(buffer->Lock(&pAudioData, nullptr, &cbCurrentLength))) {
				size_t oldSize = outData.data.size();
				outData.data.resize(oldSize + cbCurrentLength);
				memcpy(outData.data.data() + oldSize, pAudioData, cbCurrentLength);
				buffer->Unlock();
			}
		}
	}
	return true;
}

} // namespace Engine
