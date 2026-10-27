#pragma once
#include "../Engine/Renderer.h"
#include "../Engine/SceneManager.h"
#include "../Engine/LoadProfile.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>

// Runs against the real startup scene and frame path, including the editor.
// Opt-in only: normal game launches never capture or drive scene transitions.
class StartupValidation {
    const bool startupSmoke = wcsstr(GetCommandLineW(), L"--startup-smoke") != nullptr;
    const bool startupTransitions = wcsstr(GetCommandLineW(), L"--startup-transition-smoke") != nullptr;
    bool startupRecorded = false, startupPassed = false;
    int startupPhase = 0, startupFrames = 0;
    // --load-profile-auto: キー入力を送らず Title -> Select -> Game (本編) と進め、各区間を load-profile.txt に残す
    const bool loadDrive = wcsstr(GetCommandLineW(), L"--load-profile-auto") != nullptr;
    int loadPhase = 0, loadFrames = 0;
    void DriveLoad(Engine::WindowDX& window, Engine::SceneManager& scenes, ID3D12Resource* presented) {
		if (scenes.InTransition()) return;
		const auto& name = scenes.CurrentName();
		if (loadPhase == 0 && name == "Title" && ++loadFrames >= 10) {
			Engine::LoadProfile::Record("drive", "Title presented, request Select", 0);
			loadPhase = 1;
			scenes.RequestChange("Select");
		} else if (loadPhase == 1 && name == "Select") {
			Engine::LoadProfile::Record("drive", "Select presented, request Game", 0);
			loadPhase = 2; loadFrames = 0;
			Engine::SceneParameters params; params.stagePath = "Resources/Scenes/chrono.json";
			scenes.RequestChange("Game", params);
		} else if (loadPhase == 2 && name == "Game" && ++loadFrames >= 30) {
			Engine::LoadProfile::Record("drive", "Game presented 30 frames", 0);
			// 遷移後のゲーム画面 (HUD を含む) を残し、見た目の確認に使う
			DirectX::ScratchImage image;
			if (SUCCEEDED(DirectX::CaptureTexture(window.Queue(), presented, false, image, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT)))
				DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"load-game.png");
			loadPhase = 3;
			PostQuitMessage(0);
		}
    }
public:
    void OnPresented(Engine::WindowDX& window, Engine::Renderer& renderer,
        Engine::SceneManager& scenes, ID3D12Resource* presented) {
		if (loadDrive) { DriveLoad(window, scenes, presented); return; }
		if (!startupSmoke || scenes.InTransition()) return;
		if (startupRecorded) {
			if (!startupTransitions) return;
			const auto& name = scenes.CurrentName();
			if (startupPhase == 1 && name == "Select") {
				startupPassed &= !renderer.Ready3D();
				startupPhase = 2;
				scenes.RequestChange("Game");
			} else if (startupPhase == 2 && name == "Game") {
				startupPassed &= renderer.Ready3D();
				startupPhase = 3;
				scenes.RequestChange("Title");
			} else if (startupPhase == 3 && name == "Title") {
				startupPassed &= renderer.Ready3D();
				std::ofstream("tests/out/startup-transitions.txt") << (startupPassed ? "PASS" : "FAIL")
					<< " Title -> Select (2D) -> Game (3D) -> Title" << '\n';
				PostQuitMessage(startupPassed ? 0 : 2);
				startupPhase = 4;
			}
			return;
		}
		// ImGui docking needs its first layout frames before the Game view is visible.
		if (++startupFrames < 4) return;
		startupRecorded = true;
		FILETIME created{}, exited{}, kernel{}, user{}, now{};
		GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
		GetSystemTimePreciseAsFileTime(&now);
		ULARGE_INTEGER start{}, end{};
		start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
		end.LowPart = now.dwLowDateTime; end.HighPart = now.dwHighDateTime;
		const double seconds = double(end.QuadPart - start.QuadPart) / 10000000.0;
		std::filesystem::create_directories("tests/out");
		DirectX::ScratchImage image;
		auto hr = DirectX::CaptureTexture(window.Queue(), presented, false, image,
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
		if (SUCCEEDED(hr)) hr = DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"tests/out/startup.png");
		size_t greenPixels = 0;
		if (const auto* captured = image.GetImage(0, 0, 0)) {
			for (size_t y = 0; y < captured->height && greenPixels < 1000; ++y)
				for (size_t x = 0; x < captured->width && greenPixels < 1000; ++x) {
					const auto* pixel = captured->pixels + y * captured->rowPitch + x * 4;
					greenPixels += pixel[1] > int(pixel[0]) + 25 && pixel[1] > int(pixel[2]) + 25;
				}
		}
		const bool passed = greenPixels >= 1000 && seconds < 5.0 && scenes.CurrentName() == "Title" && !renderer.Ready3D() && SUCCEEDED(hr);
		std::ofstream("tests/out/startup-smoke.txt") << (passed ? "PASS" : "FAIL")
			<< " process-to-title=" << seconds << "s scene=" << scenes.CurrentName()
			<< " titleVisible=" << (greenPixels >= 1000) << " renderer3D=" << renderer.Ready3D() << " capture=" << SUCCEEDED(hr) << '\n';
		startupPassed = passed;
		if (startupTransitions) {
			startupPhase = 1;
			scenes.RequestChange("Select");
		} else PostQuitMessage(passed ? 0 : 2);
    }
};
