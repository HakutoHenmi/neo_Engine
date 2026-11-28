// ChallengeCaptureScene.h
// 変形チャレンジの画面の確認 (--challenge-capture)。本編のシーンをそのまま動かし、決まった秒数で画面を tests/out/challenge-*.png に撮る。
// 例: --challenge-capture --devour-no-tutorial --devour-time=36 --devour-battle-auto --devour-battle-time=10 --challenge=blocks
//  (36 秒から始めると 6 秒で自動でチャレンジへ移る。撮るのは成長中の札・チャレンジの始まり・チャレンジ中・結果画面)
//  --challenge-capture-burst を足すと、9 秒 (=秒 で変えられる) から 0.1 秒おきに 8 枚 (challenge-burst-0〜7.png) も撮る (しぶきが広がる動きの確認)
#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <chrono>
#include <filesystem>
#include <fstream>
class ChallengeCaptureScene final : public Game::GameScene {
	Engine::WindowDX* window_ = nullptr;
	std::chrono::steady_clock::time_point start_;
	int next_ = 0, burst_ = 0;
	// (--challenge-capture-frames で、1 秒ごとのフレームの平均と一番長い時間 (ms) を tests/out/challenge-frames.txt に書く)
	std::chrono::steady_clock::time_point lastFrame_{};
	int frameSecond_ = -1, frameCount_ = 0;
	float frameSum_ = 0, frameMax_ = 0;
	std::string frameLog_;
	void Capture(const wchar_t* path) {
		DirectX::ScratchImage image;
		auto result = DirectX::CaptureTexture(
		    window_->Queue(), window_->GetCurrentBackBufferResource(), false, image, D3D12_RESOURCE_STATE_PRESENT,
		    D3D12_RESOURCE_STATE_PRESENT);
		if (SUCCEEDED(result))
			DirectX::SaveToWICFile(
			    *image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path);
	}

public:
	void Initialize(Engine::WindowDX* window, const Engine::SceneParameters&) override {
		window_ = window;
		Engine::SceneParameters params;
		params.stagePath = Game::Data::MainStagePath();
		Game::GameScene::Initialize(window, params);
		std::filesystem::create_directories("tests/out");
		start_ = std::chrono::steady_clock::now();
	}
	void Update() override {
		Game::GameScene::Update();
		struct Shot {
			float at;
			const wchar_t* path;
		};
		static constexpr Shot shots[] = {
		    {3.f, L"tests/out/challenge-growth.png"},
		    {7.5f, L"tests/out/challenge-intro.png"},
		    {14.f, L"tests/out/challenge-play.png"},
		    {24.f, L"tests/out/challenge-result.png"}};
		const auto now = std::chrono::steady_clock::now();
		const float elapsed = std::chrono::duration<float>(now - start_).count();
		static const bool logFrames = wcsstr(GetCommandLineW(), L"--challenge-capture-frames") != nullptr;
		if (logFrames && lastFrame_.time_since_epoch().count() != 0) {
			const float ms = std::chrono::duration<float, std::milli>(now - lastFrame_).count();
			if (int(elapsed) != frameSecond_) {
				if (frameCount_ > 0)
					frameLog_ += std::to_string(frameSecond_) + "s avg=" + std::to_string(frameSum_ / float(frameCount_)) +
					             " max=" + std::to_string(frameMax_) + '\n';
				frameSecond_ = int(elapsed);
				frameCount_ = 0;
				frameSum_ = frameMax_ = 0;
			}
			++frameCount_;
			frameSum_ += ms;
			frameMax_ = (std::max)(frameMax_, ms);
		}
		lastFrame_ = now;
		if (next_ < int(std::size(shots)) && elapsed >= shots[next_].at) {
			Capture(shots[next_].path);
			++next_;
		}
		static const wchar_t* burstArg = wcsstr(GetCommandLineW(), L"--challenge-capture-burst");
		static const bool burst = burstArg != nullptr;
		static const float burstAt = burstArg && burstArg[25] == L'=' ? float(_wtof(burstArg + 26)) : 9.f;
		// (--challenge-capture-burst-frames=枚数:間隔 で、撮る枚数と間隔を変えられる。無ければ 8 枚・0.1 秒)
		static const wchar_t* framesArg = wcsstr(GetCommandLineW(), L"--challenge-capture-burst-frames=");
		static const int burstCount = framesArg ? _wtoi(framesArg + 33) : 8;
		static const float burstStep = [] {
			const wchar_t* f = wcsstr(GetCommandLineW(), L"--challenge-capture-burst-frames=");
			if (!f)
				return .1f;
			const wchar_t* colon = wcschr(f + 33, L':');
			return colon ? float(_wtof(colon + 1)) : .1f;
		}();
		if (burst && burst_ < burstCount && elapsed >= burstAt + burstStep * float(burst_)) {
			const std::wstring path = L"tests/out/challenge-burst-" + std::to_wstring(burst_) + L".png";
			Capture(path.c_str());
			++burst_;
		}
		// (--challenge-capture-end=秒 で、終わる時刻を遅らせられる。長い結果の演出を連写するとき)
		static const float quitAt = [] {
			const wchar_t* e = wcsstr(GetCommandLineW(), L"--challenge-capture-end=");
			return e ? float(_wtof(e + 24)) : 26.f;
		}();
		if (next_ >= int(std::size(shots)) && elapsed > quitAt) {
			if (logFrames && !frameLog_.empty()) {
				std::ofstream("tests/out/challenge-frames.txt") << frameLog_;
				frameLog_.clear();
			}
			PostQuitMessage(0);
		}
	}
};
