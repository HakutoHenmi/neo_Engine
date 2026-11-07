// LoadingUI.h
// ロード画面・起動スプラッシュ・シーン切り替えの演出。物差しの UI と同じ巻尺の上で動く。
// ロード画面はタイトルと同じ物を、読み込みに合わせてスライムが左から飲み込んでいく。
#pragma once
// 物差しの UI の共通部品
#include "ScaleUI.h"
// ロード画面の状態とシーン管理 (エンジン側)
#include "../../Engine/LoadingScreen.h"
#include "../../Engine/SceneManager.h"
#include "../Data/GameData.h"
#include <array>
#include <chrono>
#include <cmath>

namespace Game::UI {
namespace Loading {
namespace S=Scale;
inline float Smooth(float a,float b,float x) { float t=std::clamp((x-a)/(b-a),0.f,1.f); return t*t*(3-2*t); }
// 巻尺の目盛りの文字
inline const std::vector<S::Mark>& Marks(){static const std::vector<S::Mark> marks={{5},{30},{100},{500},{5000},{12600}};return marks;}

// 遊び方の 1 ページ (見出しと本文)
struct Page { std::string title; std::vector<std::string> lines; };
// loading.json の中身 (ページの文章・ロード中の文・自動でページを送る時間・起動画面の説明文)
struct LoadingData {
    std::vector<Page> pages;
    std::string pageHintPad,pageHintKeys,ready,preparingEngine,generatingField,loading,splashTagline;
    float autoPageSeconds=5;
};
// loading.json を読み込む (最初に使われたときに 1 回だけ。ロード用のスレッドからも呼ばれる)
inline const LoadingData& LoadingTexts() {
    static const LoadingData data=[]{
        const auto& json=Data::Load("loading.json");
        LoadingData out;
        for(const auto& page:Data::Require(json,"pages","loading.json"))out.pages.push_back({page.at("title").get<std::string>(),Data::ReadStrings(page.at("lines"))});
        if(out.pages.empty())throw std::runtime_error("loading.json: pages must not be empty");
        const auto& texts=Data::Require(json,"texts","loading.json");
        out.pageHintPad=texts.at("pageHintPad").get<std::string>();out.pageHintKeys=texts.at("pageHintKeys").get<std::string>();
        out.ready=texts.at("ready").get<std::string>();out.preparingEngine=texts.at("preparingEngine").get<std::string>();
        out.generatingField=texts.at("generatingField").get<std::string>();out.loading=texts.at("loading").get<std::string>();
        out.autoPageSeconds=Data::Require(json,"autoPageSeconds","loading.json").get<float>();
        out.splashTagline=Data::Require(json,"splashTagline","loading.json").get<std::string>();
        return out;
    }();
    return data;
}

// ページめくりの状態 (表示中のページ・放置時間・表示中の進捗)
struct PagerState { int page=0; float idle=0,shown=0,lastElapsed=0,last=0; };
// ページめくりの状態 (起動中ずっと残る)
inline PagerState& Pager() { static PagerState state; return state; }

// 巻尺の上のスライムと物。progress に合わせて左から飲んでいく。飲まれた物は体の中に見える
inline void Lineup(Canvas& ui,float progress,float now) {
    const float front=40+1200*progress,w=90+640*progress,h=w/S::RollAspect,cx=front-w*.5f;
    for(int i=0;i<S::LineupCount;++i){
        const auto& prop=S::Lineup[i];const float pull=S::Clamp01((front-w*.12f-prop.cx)/140);
        if(pull<=0){S::Prop(ui,*prop.sprite,prop.cx,S::TapeTop,prop.height);continue;}
        // 体の中の決まった場所へ吸い込まれ、小さくなってゆっくり回る
        const float seed=float(i)*2.399f,tx=cx+std::cos(seed)*w*.26f,ty=S::TapeTop-h*(.22f+.3f*Noise(seed));
        const float smallest=(std::min)(.6f,h*.42f/prop.height),scale=1-(1-smallest)*pull,height=prop.height*scale,width=height*prop.sprite->aspect;
        const float x=prop.cx+(tx-prop.cx)*pull,y=S::TapeTop-prop.height*.5f+(ty-S::TapeTop+prop.height*.5f)*pull;
        ui.Photo(S::SpritePath(prop.sprite->name),{x-width*.5f,y-height*.5f,width,height},{1,1,1,1},pull*(i%2?.7f:-.7f)+now*.2f*(i%2?1:-1)*pull);
    }
    S::Slime(ui,cx,S::TapeTop,w,true);
    S::Tape(ui,Marks());
}

// ロード画面全体。alpha で文字をフェードさせる (巻尺と物は常に表示)
// ロード用のスレッドで動くので、入力と使用中の機器はすべて state から読む
inline void LoadingContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha,bool interactive) {
    auto& pager=Pager();const auto& texts=LoadingTexts();
    // 前のフレームからの時間 (最大 0.1 秒)
    const float now=Seconds(),dt=std::clamp(now-pager.last,0.f,.1f);pager.last=now;
    const int count=int(texts.pages.size());
    // ロードのたびに次のページから始める
    if(state.elapsed<pager.lastElapsed){pager.shown=0;pager.page=(pager.page+1)%count;pager.idle=0;}
    pager.lastElapsed=state.elapsed;
    // ページの送り (クリック・左右キー・放置)
    if(interactive){
        int step=state.step;if(state.clicked)step=1;
        pager.idle+=dt;if(!step&&pager.idle>texts.autoPageSeconds)step=1;
        if(step){pager.page=(pager.page+step+count)%count;pager.idle=0;}
    }
    // 進捗: 見積もりが飛んでも止まって見えないよう滑らかに追従させる
    pager.shown+=(state.progress-pager.shown)*(1-std::exp(-(state.finished?20.f:6.f)*dt));
    const float shown=std::clamp(state.finished?(std::max)(pager.shown,.995f):pager.shown,0.f,1.f);
    Lineup(ui,shown,now);
    if(alpha<=.01f)return;
    Canvas c=ui;c.MultiplyAlpha(alpha);
    // 遊び方
    const Page& page=texts.pages[size_t(pager.page)];
    c.Text(Str("scale.howTo")+" "+std::to_string(pager.page+1)+" / "+std::to_string(count),48,22,26,S::Black,S::Jp);
    c.Big(page.title,46,52,68,S::Black,S::JpBig,S::JpBigBase);
    for(size_t i=0;i<page.lines.size();++i)c.Text(page.lines[i],50,150+float(i)*40,28,S::Black,S::Jp);
    S::Hint(c,"keyboard_arrows_horizontal","xbox_dpad",state.padMode?texts.pageHintPad:texts.pageHintKeys,50,240,S::Black,20,state.padMode?1:0);
    // 割合と状態の文
    const std::string percent=std::to_string(int(std::round(shown*100)));
    const float numberWidth=c.BigWidth(percent,96,S::NumBig,S::NumBigBase);
    c.Big(percent,1190-numberWidth,6,96,S::Black,S::NumBig,S::NumBigBase);c.Text("%",1196,44,44,S::Black,S::Num);
    const bool engine=state.scene.find("+Engine")!=std::string::npos;
    const std::string& status=state.finished?texts.ready:engine&&state.progress<.62f?texts.preparingEngine:state.scene.rfind("Game",0)==0?texts.generatingField:texts.loading;
    c.Text(status,1240-c.Width(status,20,S::Jp),150,20,S::Black,S::Jp);
}

// スプラッシュの下に出す説明文
inline const std::string& SplashTagline(){return LoadingTexts().splashTagline;}
// 起動スプラッシュ: 巻尺が伸びて、ロゴが小さい順に押し出される。読み込みの進み具合は巻尺の上の赤い線
inline void SplashContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha) {
    if(alpha<=.01f)return;
    Canvas c=ui;c.MultiplyAlpha(alpha);
    const float t=state.elapsed,now=Seconds();
    for(int i=0;i<4;++i){const float pop=S::Overshoot((t-.45f-float(i)*.2f)/.3f),size=S::LogoSizes[i]*pop;
        if(t>.45f+float(i)*.2f)S::LogoGlyph(c,i,S::LogoX(i)+(S::LogoSizes[i]-size)*.5f,S::LogoBase-size,size);}
    c.Text("neoEngine",48,30,44,S::Black,S::Num);c.Text(SplashTagline(),50,92,17,S::Black,S::Jp);
    S::Slime(c,38,S::TapeTop-6,70);
    S::Tape(c,Marks(),S::TapeTop,S::EaseOut(t/.45f));
    c.Fill({0,S::TapeTop-6,1280*std::clamp(state.progress,0.f,1.f),6},S::Red);
    // 読み込み済みで 2.4 秒経ったら押すように案内する
    if(state.finished&&t>=2.4f)c.Text(state.padMode?"PRESS ANY BUTTON":"PRESS ANY KEY",50,540,26,WithAlpha(S::Black,.65f+.35f*std::sin(now*4)),S::Num);
}

// ロード用スレッドが始まる前にメインスレッドで (完全に透明で) 一度描き、
// ロード用スレッドで使うテクスチャと文字を先にキャッシュしておく
inline void Warmup(Canvas& ui) {
    Canvas w=ui;w.MultiplyAlpha(0);
    const auto& texts=LoadingTexts();
    // 画像: 物と影・スライムの全コマ・ロゴ・操作の案内
    for(int i=0;i<S::LineupCount;++i){S::Prop(w,*S::Lineup[i].sprite,0,0,10);w.Photo(S::SpritePath(S::Lineup[i].sprite->name),{0,0,4,4});}
    for(int i=0;i<S::RollFrames;++i)w.Photo(S::SlimeFrame(true,i),{0,0,4,4});
    for(int i=0;i<S::IdleFrames;++i)w.Photo(S::SlimeFrame(false,i),{0,0,4,4});
    w.Image("Resources/Textures/UI/Scale/slime_shadow.png",{0,0,4,4});
    for(int i=0;i<4;++i)S::LogoGlyph(w,i,0,0,4);
    S::Hint(w,"keyboard_arrows_horizontal","xbox_dpad","",0,0,S::Black,20,0);S::Hint(w,"keyboard_arrows_horizontal","xbox_dpad","",0,0,S::Black,20,1);
    // 文字: ロード用のスレッドで新しい字を作らないよう、全部を一度描く。字の解像度は大きさで変わるので、ロード画面で使う大きさごとに描く
    auto jp=[&](const std::string& text){for(float size:{17.f,20.f,26.f,28.f})w.Text(text,0,0,size,S::Black,S::Jp);};
    for(const auto& page:texts.pages){w.Big(page.title,0,0,68,S::Black,S::JpBig,S::JpBigBase);for(const auto& line:page.lines)jp(line);}
    for(const std::string& text:{texts.pageHintPad,texts.pageHintKeys,texts.ready,texts.preparingEngine,texts.generatingField,texts.loading,SplashTagline(),Str("scale.howTo"),std::string("/ 0123456789")})jp(text);
    w.Big("0123456789",0,0,96,S::Black,S::NumBig,S::NumBigBase);
    for(float size:{26.f,27.f,44.f})w.Text("0123456789%cmneoEngine PRESS ANY KEY BUTTON",0,0,size,S::Black,S::Num);
    S::Tape(w,Marks());
}

// ロード画面の文字の不透明度 (短いロードでは巻尺と物だけを見せる)
inline float ContentAlpha(const Engine::LoadingScreen::State& state) {
    return state.startup?Smooth(0,.35f,state.elapsed):Smooth(.45f,.8f,state.elapsed);
}

// ロード画面の 1 フレームを描く (起動直後はスプラッシュ、それ以外はロード画面)
inline void DrawLoadingFrame(const Engine::LoadingScreen::State& state) {
    Canvas ui(Engine::Renderer::GetInstance());
    // 最初のフレームは先にキャッシュを作る
    if(state.warmup)Warmup(ui);
    S::Backdrop(ui);
    if(state.scene=="Startup"){SplashContent(ui,state,1);return;}
    LoadingContent(ui,state,ContentAlpha(state),!state.warmup);
}

// シーン切り替えの演出: 黄色の面が右から入って覆い、読み込みの後は右へ抜ける
inline void DrawTransition(const Engine::SceneManager::Transition& transition) {
    Canvas ui(Engine::Renderer::GetInstance());
    using Phase=Engine::SceneManager::Transition::Phase;
    const float t=transition.progress;
    if(transition.phase==Phase::Cover){
        const float x=1280*(1-S::EaseOut(t));
        ui.Fill({x,-200,1680,1120},S::Yellow);
        ui.Fill({x-10,-200,10,1120},S::Black);
        return;
    }
    const float x=1280*S::EaseIn(t);
    ui.Fill({x,-200,1680,1120},S::Yellow);
    ui.Fill({x-10,-200,10,1120},S::Black);
}

// エンジンの起動前に1回呼び、起動時のロードも覆う
inline void InstallPresentation() {
    Engine::LoadingScreen::SetDrawer(DrawLoadingFrame);
}
// シーン切り替えの演出を登録する
inline void InstallTransitions(Engine::SceneManager& scenes) {
    scenes.SetTransitionDrawer(DrawTransition);
}
}
}
