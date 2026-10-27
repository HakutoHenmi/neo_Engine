#pragma once
#include "../Game/Editor/EditorUI.h"
#include "../Game/Scenes/TitleScene.h"
#include "../Engine/LoadingScreen.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <thread>

// Exercises a real SceneManager reload between editor frames. GPU captures verify
// that the animated loader changes the Game view while preserving the outer UI.
class EditorLoadingValidationScene final : public Engine::IScene {
    Engine::WindowDX* window_ = nullptr;
    Game::TitleScene title_;
    int frames_ = 0;
    inline static bool reloading_ = false, passed_ = true;
    inline static DirectX::ScratchImage before_;
    inline static float left_ = 0, top_ = 0, right_ = 0, bottom_ = 0;
    HRESULT Capture(DirectX::ScratchImage& image) {
        return DirectX::CaptureTexture(window_->Queue(), window_->GetCurrentBackBufferResource(), false, image,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
    }
public:
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters&) override {
        window_ = dx;
        if (reloading_) {
            auto* r = Engine::Renderer::GetInstance();
            float x = (left_ + right_) * .5f, y = (top_ + bottom_) * .5f;
            passed_ &= r->MapLoadingPointer(x, y) && std::abs(x - 960.f) < 2 && std::abs(y - 540.f) < 2;
            x = 0; y = 0; passed_ &= !r->MapLoadingPointer(x, y);
            // Keep the worker alive long enough to render multiple swap-chain buffers.
            for (int i = 0; i < 140; ++i) {
                Engine::LoadingScreen::Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        }
        title_.Initialize(dx, {});
    }
    void Update() override {
        if (++frames_ != (reloading_ ? 1 : 40)) return;
        if (!reloading_) {
            std::filesystem::create_directories("tests/out");
            passed_ &= SUCCEEDED(Capture(before_));
            RECT rc{}; GetClientRect(window_->GetHwnd(), &rc);
            const auto lo = Game::EditorUI::GetGameImageMin(), hi = Game::EditorUI::GetGameImageMax();
            left_ = lo.x * 1920.f / rc.right; right_ = hi.x * 1920.f / rc.right;
            top_ = lo.y * 1080.f / rc.bottom; bottom_ = hi.y * 1080.f / rc.bottom;
            passed_ &= right_ > left_ && bottom_ > top_ && left_ > 0;
            reloading_ = true;
            Engine::SceneManager::GetInstance()->RequestChange("EditorLoadingValidation");
            return;
        }
        DirectX::ScratchImage after;
        passed_ &= SUCCEEDED(Capture(after));
        size_t outer = 0, changedOuter = 0, changedInner = 0;
        const auto* a = before_.GetImage(0, 0, 0); const auto* b = after.GetImage(0, 0, 0);
        if (a && b) {
            DirectX::SaveToWICFile(*b, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"tests/out/editor-loading.png");
            for (size_t y = 0; y < a->height; ++y) for (size_t x = 0; x < a->width; ++x) {
                bool changed = false;
                for (size_t c = 0; c < 3; ++c)
                    changed |= std::abs(int(a->pixels[y*a->rowPitch+x*4+c]) - int(b->pixels[y*b->rowPitch+x*4+c])) > 8;
                if (x > left_ && x < right_ && y > top_ && y < bottom_) changedInner += changed;
                else { ++outer; changedOuter += changed; }
            }
        } else passed_ = false;
        // FPS/status text may change between the reference and cover frames.
        passed_ &= outer > 0 && changedOuter < outer / 100 && changedInner > 1000;
        std::ofstream("tests/out/ui-smoke.txt") << (passed_ ? "PASS" : "FAIL")
            << " editor loading: outside changes=" << changedOuter << "/" << outer
            << ", Game view changes=" << changedInner << ", pointer mapping checked";
        PostQuitMessage(passed_ ? 0 : 2);
    }
    void Draw() override { title_.Draw(); }
    void DrawEditor() override { Game::EditorUI::PresentScene(title_, "Title"); }
};
