// TitleScene.cpp
// タイトル画面の初期化・入力処理・描画。
#include "../UI/SceneMusic.h"
#include "../UI/CreditsUI.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TitleScene.h"
#include "../UI/GameUI.h"
#include "../UI/ScaleUI.h"
#include "../Data/GameConfig.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include "../Systems/UISystem.h"
#include "../ObjectTypes.h"
#include "../../externals/imgui/imgui.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <filesystem>

// main.cpp の起動ログ出力関数
void LogFileMain(const char* msg);

namespace Game {

// ============================================================
// Initialize: カメラ・ライト・ポストエフェクトと画面の状態を初期化する
// ============================================================
void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& /*params*/) {
    LogFileMain("    TitleScene::Initialize 1");
    // 初期状態に戻す (クレジットを閉じた状態から始める)
    dx_ = dx;
    creditsOpen_ = false;
    creditsPage_ = 0;
    collectionOpen_ = false;
    // タイトル BGM を流す
    Music::Play(Music::Title);
    // 次のセレクト画面の曲を裏で展開しておく (読み込み画面で MP3 の展開を待たない)
    Music::Preload(Music::Select);
    renderer_ = Engine::Renderer::GetInstance();
    lastTime_ = std::chrono::steady_clock::now();

    // カメラの位置と向き
    camStartPos_ = { 0.0f, 0.8f, -2.5f };
    camera_.Initialize();
    currentFov_ = camStartFov_;
    float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
    camera_.SetProjection(currentFov_, aspect, 0.1f, 500.0f);
    camera_.SetPosition(camStartPos_);
    camera_.LookAt(camStartTarget_, { 0, 1, 0 });

    // 環境光と平行光源
    renderer_->SetAmbientColor({ 0.15f, 0.15f, 0.2f });
    renderer_->SetDirectionalLight(
        { 0.3f, -0.8f, 0.5f },
        { 0.6f, 0.6f, 0.7f },
        true
    );

    // ポストエフェクトを標準にする (前のシーンの設定を引き継がない)
    renderer_->SetPostProcessEnabled(true);
    renderer_->SetPostEffect("Default");
    renderer_->SetPostProcessParams({});

    // 経過時間と出発の状態をリセットする。セレクトから戻ったときは登場を途中から始め、
    // ゲームから黄色の面で覆って戻ったときは最初から登場する。検証の撮影では飛ばす
    static bool seen = false;
    totalTime_ = 0.0f;
    intro_ = UI::Scale::SkipIntro() ? 9.0f : seen && !Engine::SceneManager::GetInstance()->ArrivedCovered() ? .9f : 0.0f;
    seen = true;
    leaving_ = -1; eatenAt_.fill(-1.0f); menu_ = 0; pointerReady_ = false;
    UI::Scale::PreloadSlime(renderer_);
    
    // タイトルではマウスで操作するのでカーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
    
    LogFileMain("    TitleScene::Initialize Complete");
}

// タイトルの物: 小さい順に巻尺の上へ落ちる
namespace {
namespace S=UI::Scale;
constexpr int kPropCount=S::LineupCount;
// 物が落ち始める時刻・巻尺が伸びる時間・登場の終わり・出発の長さ
inline float LandAt(int index){return .45f+float(index)*.11f;}
constexpr float kTapeTime=.45f,kIntroEnd=1.9f,kLeaveTime=1.7f;
// メニューの当たり判定
constexpr int kMenuCount=4;
constexpr UI::Rect kMenu[kMenuCount]={{48,38,330,66},{48,110,260,38},{48,152,260,38},{48,194,260,38}};
// 出発のときのスライム: 右へ転がりながら大きくなり、最後は画面全体を覆う
float LeaveWidth(float t){return 60+4600*std::pow(S::Clamp01(t/kLeaveTime),3.f);}
float LeaveX(float t){return 30+700*S::Clamp01(t/kLeaveTime);}
// 出発のときの巻尺の 1cm の長さ: 家を飲み込むと縮み始める (最初は今の巻尺と同じ 8 ピクセル)。u は 0〜1
float LeaveZoom(float u){return 8*std::exp(-6*std::pow(S::Clamp01(u),1.4f));}
// 巻き込まれる物の番号: 物・ロゴ・メニュー・案内・音楽の表記の順
constexpr int kLogoItem=kPropCount,kMenuItem=kLogoItem+4,kHintItem=kMenuItem+kMenuCount,kMusicItem=kHintItem+1,kItemCount=kMusicItem+1;
// 物の元の位置 (中央)
std::array<float,2> ItemPoint(int index){
    if(index<kPropCount)return {S::Lineup[index].cx,S::TapeTop-S::Lineup[index].height*.5f};
    if(index<kMenuItem){const int i=index-kLogoItem;return {S::LogoX(i)+S::LogoSizes[i]*.5f,S::LogoBase-S::LogoSizes[i]*.5f};}
    if(index<kHintItem){const auto& b=kMenu[index-kMenuItem];return {b.x+b.w*.4f,b.y+b.h*.5f};}
    return index==kHintItem?std::array<float,2>{110,262}:std::array<float,2>{1120,20};
}
// スライムの体に入ったか: 体を床に乗った半楕円として調べる
bool InsideSlime(float x,float y,float t){
    const float w=LeaveWidth(t),h=w/S::RollAspect,dx=(x-LeaveX(t))/(w*.47f),dy=(S::TapeTop-y)/(h*.85f);
    return dy>=0&&dx*dx+dy*dy<1;
}
// 飲まれた物の今の様子: 体の中の決まった場所へ吸い込まれ、回りながら小さくなる
struct Swallowed{float x,y,scale,spin;};
Swallowed Swallow(int index,float eatenAt,float now){
    const auto from=ItemPoint(index);const float age=now-eatenAt,pull=S::EaseOut(age/.5f);
    const float w=LeaveWidth(now),h=w/S::RollAspect,seed=float(index)*2.399f;
    const float tx=LeaveX(now)+std::cos(seed)*w*.26f,ty=S::TapeTop-h*(.22f+.3f*UI::Noise(seed));
    return {from[0]+(tx-from[0])*pull,from[1]+(ty-from[1])*pull,1-.45f*pull,age*(1.2f+UI::Noise(seed*3))*(index%2?1:-1)};
}
}

// ============================================================
// Update: 経過時間の更新とメニューの操作
// ============================================================
void TitleScene::Update() {
    // 前フレームからの経過時間 (長く止まった後は 1/60 秒として扱う)
    auto now = std::chrono::steady_clock::now();
    dt_ = std::chrono::duration<float>(now - lastTime_).count();
    lastTime_ = now;
    if (dt_ > 0.1f) dt_ = 1.0f / 60.0f;
    totalTime_ += dt_;
    camera_.Tick(dt_);
    renderer_->SetCamera(camera_);
    intro_ += dt_;
    // 出発の途中: スライムが物も文字も飲みながら転がり、画面を埋めたらセレクトへ
    if (leaving_ >= 0) {
        leaving_ += dt_;
        for (int i = 0; i < kItemCount; ++i) {
            const auto point = ItemPoint(i);
            if (eatenAt_[size_t(i)] >= 0 || !InsideSlime(point[0], point[1], leaving_)) continue;
            eatenAt_[size_t(i)] = leaving_;
            if (i < kMenuItem) S::PlayGulp(i >= 7 && i < kPropCount || i == kMenuItem - 1);
        }
        if (leaving_ >= kLeaveTime) Engine::SceneManager::GetInstance()->RequestInstantChange("Select");
        return;
    }
    auto* input = Engine::Input::GetInstance();
    if (!input) return;
    UI::Canvas ui(renderer_);
    // クレジット表示中はクレジットの操作だけを受け付ける
    if (creditsOpen_) {
        if (UI::UpdateCredits(ui, creditsPage_)) creditsOpen_ = false;
        return;
    }
    // ずかん表示中はずかんの操作だけを受け付ける
    if (collectionOpen_) {
        if (UI::UpdateCollection(ui, collection_, dt_)) collectionOpen_ = false;
        return;
    }
    const bool enter = UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_START);
    // 登場の途中は、決定の操作で最後の状態まで飛ばす
    if (intro_ < kIntroEnd) {
        if (enter || UI::PadPressed(XINPUT_GAMEPAD_A) || input->IsMouseTrigger(0)) intro_ = kIntroEnd;
        return;
    }
    // 上下キーで項目を動かす
    int step = (UI::Pressed(DIK_DOWN) || UI::Pressed(DIK_S) ? 1 : 0) - (UI::Pressed(DIK_UP) || UI::Pressed(DIK_W) ? 1 : 0);
    if (step) { menu_ = (menu_ + step + kMenuCount) % kMenuCount; UI::PlayUISound(UI::UISound::Move); }
    // マウスを動かしたときだけ、乗っている項目を選ぶ (止まったマウスがキーの操作を上書きしないように)
    const auto pointer = ui.Pointer();
    const bool moved = pointerReady_ && std::abs(pointer[0] - pointerX_) + std::abs(pointer[1] - pointerY_) > .5f;
    pointerX_ = pointer[0]; pointerY_ = pointer[1]; pointerReady_ = true;
    int chosen = enter ? menu_ : -1;
    for (int i = 0; i < kMenuCount; ++i) {
        ui.Track(kMenu[i], i == 0);
        if (ui.Focused(kMenu[i]) || (moved && ui.Hover(kMenu[i]))) menu_ = i;
        if (ui.Click(kMenu[i])) chosen = i;
    }
    if (chosen == 0) { UI::PlayUISound(UI::UISound::Confirm); leaving_ = 0; }
    else if (chosen == 1) { collectionOpen_ = true; UI::OpenCollection(collection_); }
    else if (chosen == 2) { creditsOpen_ = true; creditsPage_ = 0; }
    else if (chosen == 3) PostQuitMessage(0);
}

// ============================================================
// Draw: 巻尺・落ちてくる物・大きくなるロゴ・メニュー
// ============================================================
void TitleScene::Draw() {
    if (!renderer_) return;
    // クレジット表示中はクレジットだけを描く
    if (creditsOpen_) { UI::Canvas ui(renderer_); UI::DrawCredits(ui, creditsPage_); return; }
    if (collectionOpen_) { UI::Canvas ui(renderer_); UI::DrawCollection(ui, collection_); return; }
    UI::Canvas ui(renderer_);
    const float t = intro_;
    S::Backdrop(ui);
    const auto eaten = [&](int index) { return leaving_ >= 0 && eatenAt_[size_t(index)] >= 0; };
    const auto swallow = [&](int index) { return Swallow(index, eatenAt_[size_t(index)], leaving_); };
    const std::vector<S::Mark> marks = {{5, true}, {30}, {100}, {500}, {2200, true}};
    // ロゴ: 左から右へ大きくなる。物の着地に合わせて下から押し出される
    for (int i = 0; i < 4; ++i) {
        const float size = S::LogoSizes[i], x = S::LogoX(i), landed = LandAt(1 + i * 2) + .25f;
        if (eaten(kLogoItem + i)) {
            const auto s = swallow(kLogoItem + i);
            S::LogoGlyph(ui, i, s.x - size * s.scale * .5f, s.y - size * s.scale * .5f, size * s.scale, S::Black, s.spin);
        } else if (t > landed) {
            const float pop = S::Overshoot((t - landed) / .3f), now = size * pop;
            S::LogoGlyph(ui, i, x + (size - now) * .5f, S::LogoBase - now, now);
        }
    }
    // 物
    for (int i = 0; i < kPropCount; ++i) {
        const auto& prop = S::Lineup[i];
        const float local = t - LandAt(i);
        if (local < 0) continue;
        if (eaten(i)) {
            const auto s = swallow(i);
            const float h = prop.height * s.scale, w = h * prop.sprite->aspect;
            ui.Photo(S::SpritePath(prop.sprite->name), {s.x - w * .5f, s.y - h * .5f, w, h}, {1, 1, 1, 1}, s.spin);
            continue;
        }
        const float landed = S::Clamp01((local - .24f) / .16f), squash = 1 - .1f * std::sin(landed * 3.14159f);
        S::Prop(ui, *prop.sprite, prop.cx, S::TapeTop, prop.height, 1, S::Drop(local / .4f) * 560, squash);
    }
    // メニュー: 飲まれた文字は体の中で小さくなって漂う
    const float menuAlpha = S::Clamp01((t - .2f) / .3f);
    const auto ink = UI::WithAlpha(S::Black, menuAlpha);
    const std::string labels[kMenuCount] = {UI::Str("title.start"), UI::Str("title.collection"), UI::Str("title.credits"), UI::Str("title.exit")};
    float y = 34;
    for (int i = 0; i < kMenuCount; ++i) {
        const bool on = i == menu_;
        const float size = on ? 56.f : 28.f;
        if (eaten(kMenuItem + i)) {
            const auto s = swallow(kMenuItem + i);
            ui.Big(labels[i], s.x - ui.BigWidth(labels[i], size * s.scale, S::JpBig, S::JpBigBase) * .5f, s.y, size * s.scale, ink, S::JpBig, S::JpBigBase);
        } else {
            if (on) S::Arrow(ui, 48, y + 10, 30, UI::WithAlpha(S::Red, menuAlpha));
            if (on) ui.Big(labels[i], 82, y - 6, size, ink, S::JpBig, S::JpBigBase);
            else ui.Text(labels[i], 50, y, size, ink, S::Jp);
        }
        y += on ? 74 : 42;
    }
    if (eaten(kHintItem)) { const auto s = swallow(kHintItem); S::Hint(ui, "keyboard_enter", "xbox_button_a", UI::Str("title.confirm"), s.x - 60, s.y, ink, 18 * s.scale); }
    else S::Hint(ui, "keyboard_enter", "xbox_button_a", UI::Str("title.confirm"), 48, 244, ink, 18);
    const std::string& music = UI::Str("title.music");
    if (eaten(kMusicItem)) { const auto s = swallow(kMusicItem); ui.Text(music, s.x - 120, s.y, 13, ink, S::Jp); }
    else ui.Text(music, 1250 - ui.Width(music, 13, S::Jp), 12, 13, UI::WithAlpha(S::Black, menuAlpha), S::Jp);
    // スライム: 左端に座る。出発すると転がりながら全部を体の中へ入れる
    if (leaving_ >= 0) {
        // 巻尺は物の影の上、スライムの下に描く。一番大きい家を飲み込むまでは動かさない
        const float house = eatenAt_[size_t(kPropCount - 1)];
        if (house >= 0) S::ZoomTape(ui, LeaveZoom((leaving_ - house) / (std::max)(.1f, kLeaveTime - house)), 1 - S::Clamp01((leaving_ / kLeaveTime - .86f) / .08f));
        else S::Tape(ui, marks);
        S::Slime(ui, LeaveX(leaving_), S::TapeTop, LeaveWidth(leaving_), true);
        // 最後は緑に溶けて、セレクトの始まりの色につなげる
        ui.Clear(UI::WithAlpha(S::SlimeFill, S::Clamp01((leaving_ / kLeaveTime - .86f) / .14f)));
    } else if (t > .3f) {
        const float cycle = std::fmod(totalTime_, 3.1f), hop = cycle < .35f ? std::sin(cycle / .35f * 3.14159f) * 12 : 0;
        S::Slime(ui, 34, S::TapeTop - hop, 62);
        S::Tape(ui, marks, S::TapeTop, S::EaseOut(t / kTapeTime));
    } else S::Tape(ui, marks, S::TapeTop, S::EaseOut(t / kTapeTime));
}

// ============================================================
// DrawUI: ImGui 用 (タイトルでは使わない)
// ============================================================
void TitleScene::DrawUI() {}

} // namespace Game
