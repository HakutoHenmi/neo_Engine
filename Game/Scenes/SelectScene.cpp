// SelectScene.cpp
// ステージ選択画面の初期化・入力処理・描画。
#include "../UI/SceneMusic.h"
#include "SelectScene.h"
// 共通の UI 描画と、入力・シーン管理 (エンジン側)
#include "../UI/GameUI.h"
#include "../Data/GameConfig.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <cmath>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {
namespace {
namespace S=UI::Scale;
// 物が立つ高さ・ステージの帯の高さ・操作の案内の当たり判定・出発の長さ
constexpr float kBarTop=564,kBarHeight=43,kLeaveTime=.45f;
constexpr UI::Rect kDeploy{822,40,122,36},kBack{1112,40,128,36};
// ステージごとに並べる物
// (実際にそのステージにある物を、巻尺の実寸の位置に置く。後ろの物から描く)
const std::vector<S::LineupProp>& PropsOf(int stage){
    static const std::vector<S::LineupProp> room={S::Place(S::Tsumiki),S::Place(S::Mikan),S::Place(S::Nuigurumi),S::Place(S::Kago)};
    static const std::vector<S::LineupProp> town={S::Place(S::MyHouse),S::Place(S::KeiCar),S::Place(S::Jitensha)};
    return stage==0?room:town;
}
// ステージの帯の場所
UI::Rect BarOf(const S::StageView& stage){const float x=S::TapeX(stage.startCm);return {x,kBarTop,S::TapeX(stage.lastCm)-x,kBarHeight};}
// スライムが座る大きさ (記録が無ければ開始の大きさ)。ステージの帯の中に収めるので、選んだステージへ必ず移動する
float SlimeCm(const S::StageView& stage){return std::clamp(stage.played?stage.bestCm:stage.startCm,stage.startCm,stage.lastCm);}
}

// 破棄: 特に後片付けは無い
SelectScene::~SelectScene() {
}

// 初期化: BGM・ポストエフェクト・保存した記録
void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    (void)params;
    dx_ = dx;
    Music::Play(Music::Select);
    renderer_ = Engine::Renderer::GetInstance();
    if (renderer_) {
        // 前のシーンで使ったポストエフェクトの効果を全部切る
        auto pp = renderer_->GetPostProcessParams();
        pp.noiseStrength = 0.0f;
        pp.distortion = 0.0f;
        pp.chromaShift = 0.0f;
        pp.vignette = 0.0f;
        pp.scanline = 0.0f;
        pp.san = 0.0f;
        renderer_->SetPostProcessParams(pp);
        renderer_->SetPostEffect("Default");
    }
    // 前回のステージから始める。未解放のステージは選ばない
    stages_ = S::LoadStages(&selected_);
    if (!stages_[size_t(selected_)].unlocked) selected_ = 0;
    slimeX_ = (std::max)(36.f, S::TapeX(SlimeCm(stages_[size_t(selected_)])));
    slimeWidth_ = selected_ ? 260.f : 140.f;
    S::PreloadSlime(renderer_);
    age_ = S::SkipIntro() ? 9.f : 0.f;
    lastTime_ = std::chrono::steady_clock::now();
    // マウスで操作するのでカーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
}

// 選んでいるステージへ出発する。本編は保存したステージ番号を読んで始まる
void SelectScene::Confirm() {
    if (!stages_[size_t(selected_)].unlocked) { shake_ = .4f; UI::PlayUISound(UI::UISound::Back); return; }
    UI::PlayUISound(UI::UISound::Confirm);
    S::SaveStage(selected_);
    leaving_ = 0;
}

// 更新: ステージの選択・出発・タイトルへ戻る
void SelectScene::Update() {
    Engine::WindowDX::SetCursorVisible(true);
    const auto now = std::chrono::steady_clock::now();
    const float dt = (std::min)(.1f, std::chrono::duration<float>(now - lastTime_).count());
    lastTime_ = now;
    age_ += dt;
    shake_ = (std::max)(0.f, shake_ - dt);
    // スライムは選んだステージの記録の位置へ滑っていく
    const float follow = 1 - std::exp(-dt * 9);
    slimeX_ += ((std::max)(36.f, S::TapeX(SlimeCm(stages_[size_t(selected_)]))) - slimeX_) * follow;
    slimeWidth_ += ((selected_ ? 260.f : 140.f) - slimeWidth_) * follow;
    if (leaving_ >= 0) {
        leaving_ += dt;
        if (leaving_ >= kLeaveTime) {
            Engine::SceneParameters params;
            params.stagePath = Data::MainStagePath();
            Engine::SceneManager::GetInstance()->RequestChange("Game", params);
        }
        return;
    }
#ifndef NDEBUG
    // F1 で評価課題のデモ (Assignment) へ移動する (ステージを増やさずにデモを残すため)
    if (Engine::Input::GetInstance()->Trigger(0x3B)) {
        Engine::SceneManager::GetInstance()->RequestChange("Assignment");
        return;
    }
#endif
    UI::Canvas ui(renderer_);
    if (UI::BackPressed() || ui.Click(kBack)) { Engine::SceneManager::GetInstance()->RequestInstantChange("Title"); return; }
    // 左右でステージを選ぶ
    const int count = int(stages_.size());
    int step = (UI::Pressed(DIK_RIGHT) || UI::Pressed(DIK_D) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT | Engine::Input::kPadLsRight) ? 1 : 0)
        - (UI::Pressed(DIK_LEFT) || UI::Pressed(DIK_A) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT | Engine::Input::kPadLsLeft) ? 1 : 0);
    if (step) { selected_ = (selected_ + step + count) % count; UI::PlayUISound(UI::UISound::Move); }
    // 帯のクリック: 選んでいない帯は選ぶだけ、選んでいる帯は出発
    if (!UI::UsingPad()) for (int i = 0; i < count; ++i) if (ui.Click(BarOf(stages_[size_t(i)]))) {
        if (i == selected_) Confirm(); else selected_ = i;
        return;
    }
    if (UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A) || (!UI::UsingPad() && ui.Click(kDeploy))) Confirm();
}

// 描画: メニュー画面を描く
void SelectScene::Draw() {
    DrawMenu();
}

// ImGui 用 (この画面では使わない)
void SelectScene::DrawUI() {
}

// メニュー画面の描画 (ステージ名と記録・巻尺の上の範囲と物・スライム)
void SelectScene::DrawMenu() {
    if (!renderer_ || stages_.empty()) return;
    UI::Canvas ui(renderer_);
    S::Backdrop(ui);
    const auto& stage = stages_[size_t(selected_)];
    const float shift = shake_ > 0 ? std::sin(shake_ * 60) * 8 * shake_ / .4f : 0;
    const float enter = S::EaseOut(age_ / .35f), out = leaving_ >= 0 ? 1 - S::EaseIn(leaving_ / kLeaveTime) : 1;
    // ステージ名と記録
    const auto ink = UI::WithAlpha(S::Black, enter);
    ui.Text(UI::Str("select.stage") + " " + std::to_string(selected_ + 1), 48, 22, 30, ink, S::Num);
    ui.Big(stage.name, 48 + shift, 62, 86, ink, S::JpBig, S::JpBigBase);
    const std::string values[4] = {S::TickText(stage.goalCm), stage.played ? S::SizeText(stage.bestCm) : "-", stage.played ? stage.rank : "-", S::Clock(stage.time)};
    const char* labels[4] = {"select.goal", "select.best", "select.rank", "select.time"};
    float x = 48;
    for (int i = 0; i < 4; ++i) {
        ui.Text(UI::Str(labels[i]), x, 172, 18, ink, S::Jp);
        ui.Text(values[i], x, 192, 46, i == 1 ? UI::WithAlpha(S::Red, enter) : ink, S::Num);
        x += (std::max)(ui.Width(values[i], 46, S::Num), ui.Width(UI::Str(labels[i]), 18, S::Jp)) + 34;
    }
    // 操作の案内
    S::Hint(ui, "keyboard_enter", "xbox_button_a", UI::Str("select.deploy"), 822, 40);
    S::Hint(ui, "keyboard_arrows_horizontal", "xbox_dpad", UI::Str("select.choose"), 958, 40);
    S::Hint(ui, "keyboard_escape", "xbox_button_b", UI::Str("select.back"), 1112, 40);
    // ステージごとの物: 選んでいれば本物、未解放なら形だけ、それ以外は薄く
    for (int i = 0; i < int(stages_.size()); ++i) {
        const bool chosen = i == selected_, open = stages_[size_t(i)].unlocked;
        for (const auto& prop : PropsOf(i)) {
            const float land = S::EaseOut((age_ - .1f - prop.cx * .0002f) / .3f), base = kBarTop - (1 - land) * -40;
            if (open) S::Prop(ui, *prop.sprite, prop.cx, base, prop.height, (chosen ? 1 : .55f) * land);
            else S::PropShape(ui, *prop.sprite, prop.cx, base, prop.height, UI::WithAlpha(S::Silhouette, land));
        }
    }
    // スライム: 記録の目盛りの上に座る
    S::Slime(ui, slimeX_, kBarTop - (leaving_ >= 0 ? std::sin(S::Clamp01(leaving_ / kLeaveTime) * 3.14159f) * 90 : 0), slimeWidth_, leaving_ >= 0);
    // ステージの帯: 左から伸びる
    for (int i = 0; i < int(stages_.size()); ++i) {
        const auto& item = stages_[size_t(i)];
        const bool chosen = i == selected_;
        UI::Rect bar = BarOf(item);
        bar.w *= S::EaseOut((age_ - .1f) / .4f);
        if (chosen) bar.x += shift;
        ui.Fill(bar, chosen ? S::Black : UI::WithAlpha(S::Black, .14f));
        std::string label = std::to_string(i + 1) + " " + item.name;
        if (!item.unlocked) label += "  " + stages_[size_t(i - 1)].name + UI::Str("select.unlockMiddle") + S::TickText(stages_[size_t(i - 1)].goalCm) + UI::Str("select.unlockTail");
        if (age_ > .4f) ui.Text(label, bar.x + 12, bar.y + 6, 22, chosen ? S::Yellow : S::Black, S::Jp);
    }
    // 巻尺: 選んだステージの目標を赤くする。未解放を決定すると解放の目標が点滅する
    const float locked = selected_ > 0 && !stage.unlocked ? stages_[size_t(selected_ - 1)].goalCm : stage.goalCm;
    const bool flash = shake_ <= 0 || std::fmod(shake_, .12f) > .06f;
    S::Tape(ui, {{5}, {locked, flash}, {30}, {100}, {500}, {2200}}, S::TapeTop, out);
    // タイトルでスライムが画面を埋めた続きから、緑が引いて始まる
    if (age_ < .25f) ui.Clear(UI::WithAlpha(S::SlimeFill, 1 - age_ / .25f));
}
// エディタ用 (この画面では追加の項目なし)
void SelectScene::DrawEditor() {
}

} // namespace Game
