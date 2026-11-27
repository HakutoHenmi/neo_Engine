#pragma once
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#undef max
#undef min
#include <wrl.h>
#include <xaudio2.h>
#include <algorithm>
#include <future>

// Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace Engine {

// 音声データ構造体
struct SoundData {
	std::vector<BYTE> data;
	WAVEFORMATEX wfx;
};

// 再生中のボイス情報
struct VoiceData {
	IXAudio2SourceVoice* source;
	bool isLoop;
};

class Audio {
public:
	// コンストラクタ・デストラクタ
	Audio();
	~Audio();

	// シングルトンインスタンス取得
	static Audio* GetInstance();

	// 初期化
	bool Initialize();

	// 終了処理
	void Shutdown();

	// 音声ファイル読み込み (mp3/wav対応)
	// 戻り値: 音声ハンドル (失敗時 -1/0xFFFFFFFF)
	uint32_t Load(const std::string& path);

	// 再生
	// 戻り値: 再生ハンドル (停止時に使用)
	// volume: 0.0f 〜 1.0f
	size_t Play(uint32_t soundHandle, bool loop = false, float volume = 1.0f, float pitch = 1.0f);

	// 停止
	void Stop(size_t voiceHandle);

	// ボイスの音量変更
	void SetVolume(size_t voiceHandle, float volume);

	// 再生中のボイスの音程 (再生速度) を変える。0.5 ~ 2.0
	void SetPitch(size_t voiceHandle, float pitch);

	// BGM 以外の音を一時停止する (ポーズの間、掃除機などの連続音を止める)
	void SetSEPaused(bool paused);

	// マスター音量管理
	float GetMasterBGMVolume() const {
		return masterBGMVolume_;
	}
	void SetMasterBGMVolume(float volume) {
		masterBGMVolume_ = (std::max)(0.0f, (std::min)(1.0f, volume));
	}
	float GetMasterSEVolume() const {
		return masterSEVolume_;
	}
	void SetMasterSEVolume(float volume) {
		masterSEVolume_ = (std::max)(0.0f, (std::min)(1.0f, volume));
	}

	// 全てのボイスを停止 (Playモード終了時用)
	void StopAll();

	// 効果音とは別の、シーンの音楽用チャンネル。実時間で進める。
	// 最初のシーンの前にワーカーで先読みし、音声に再び触る前に合流させる。
	bool PreloadBGM(const std::string& path);
	// BGM を裏のスレッドでデコードしておき、後の PlayBGM (シーンや結果画面の曲) では
	// MP3 全体を 1 フレーム内でデコードせず、出来上がった PCM を XAudio2 に渡すだけにする。
	void PreloadBGMAsync(const std::string& path);
	bool PlayBGM(const std::string& path, float volume = 0.35f);
	void StopBGM();
	void UpdateBGM(float dt);
	void SetBGMDucked(bool ducked) {
		bgmDuckTarget_ = ducked ? 0.3f : 1.0f;
	}
	// BGM の速さ (1 が元の速さ)。なめらかに変わり、次の PlayBGM で元に戻る
	void SetBGMPitch(float pitch) {
		bgmPitchTarget_ = pitch;
	}
	bool IsBGMPlaying() const;
	const std::string& CurrentBGM() const {
		return bgmPath_;
	}

private:
	// 内部用ロード関数 (メンバーに触れないので、どのスレッドからでも呼べる)
	static bool LoadViaMF(const std::wstring& path, SoundData& outData);
	uint32_t Register(const std::string& path, SoundData&& sound);

	// 終了したボイスのお掃除
	void GarbageCollect();

private:
	static Audio* instance_;

	Microsoft::WRL::ComPtr<IXAudio2> xa_;
	IXAudio2MasteringVoice* master_ = nullptr;
	bool mfStarted_ = false;

	// 読み込んだ音声データ (Indexがハンドルになる)
	std::vector<SoundData> soundDatas_;

	// 再生中のボイス管理
	// Key: 発行した再生ハンドル, Value: ボイス実体
	std::map<size_t, VoiceData> activeVoices_;
	size_t nextVoiceHandle_ = 1;
	std::map<std::string, uint32_t> bgmSounds_;
	// 裏のスレッドでデコード中の BGM (データが空なら失敗)
	std::map<std::string, std::future<SoundData>> pendingBgm_;
	std::string bgmPath_;
	size_t bgmVoice_ = 0, retiringBgmVoice_ = 0;
	float bgmGain_ = 0.35f, bgmFade_ = 0, retiringBgmGain_ = 0, retiringBgmFade_ = 0;
	float bgmDuck_ = 1, bgmDuckTarget_ = 1;
	float bgmPitch_ = 1, bgmPitchTarget_ = 1;
	bool sePaused_ = false;

	// マスター音量
	float masterBGMVolume_ = 1.0f;
	float masterSEVolume_ = 1.0f;
};

} // namespace Engine
