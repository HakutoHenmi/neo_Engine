#pragma once

#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"
#include "../ObjectTypes.h"
#include <array>
#include <string>

namespace Game {

class EquipmentScene : public Engine::IScene {
public:
    ~EquipmentScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;
    void DrawEditor() override;

private:
    struct Rect {
        float x;
        float y;
        float w;
        float h;
    };

    struct CanOption {
        CanType type;
        const char* name;
        const char* detail;
        Engine::Vector4 color;
    };

    static constexpr int kCanCount = 8;
    static constexpr int kCanColumns = 4;
    static constexpr int kButtonCount = 3;

    Rect CanRect(int index) const;
    Rect ButtonRect(int index) const;
    bool PointInRect(float x, float y, const Rect& rect) const;
    bool IsEquipped(CanType type) const;
    int EquippedCount() const;
    void ToggleCan(CanType type);
    void ClearEquipped();
    void SaveAndStart();
    void ConfirmSelection();
    void MoveHorizontal(int dir);
    void MoveVertical(int dir);
    void DrawEquipmentScreen();
    void DrawRect(float x, float y, float w, float h, const Engine::Vector4& color, int layer = 0);
    void DrawSpriteRect(Engine::Renderer::TextureHandle texture, float x, float y, float w, float h, const Engine::Vector4& color, int layer = 0, float rotationRad = 0.0f);
    void DrawCenteredText(const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color);
    void DrawCanIcon(CanType type, float centerX, float centerY, float w, float h, const Engine::Vector4& color, bool selected);

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Renderer::TextureHandle whiteTexture_ = 0;
    Engine::Renderer::TextureHandle canPatternTexture_ = 0;
    std::array<CanType, 4> equipped_{};
    int selectedIndex_ = 0;

    bool prevA_ = false;
    bool prevB_ = false;
    bool prevDpadUp_ = false;
    bool prevDpadDown_ = false;
    bool prevDpadLeft_ = false;
    bool prevDpadRight_ = false;
    bool stickUp_ = false;
    bool stickDown_ = false;
    bool stickLeft_ = false;
    bool stickRight_ = false;

    static const CanOption canOptions_[kCanCount];
    static const char* buttonLabels_[kButtonCount];
};

} // namespace Game
