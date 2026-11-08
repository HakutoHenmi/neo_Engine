// CreditsUI.h
// クレジット画面 (音楽・素材・フォント・ソフトウェアの表記) の描画。
// ページの見出しと各行の文章は Resources/Data/credits.json に置く。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "ScaleUI.h"
#include "../Data/GameData.h"
#include <vector>

// UI 関連はすべて Game::UI 名前空間に置く
namespace Game::UI {
// 操作の案内の当たり判定と、巻尺の上の章の帯
inline constexpr Rect CreditsBack{1066,36,160,42};
inline Rect CreditsTab(int page){return {20+float(page)*250,564,234,43};}

// クレジットの 1 ページ (見出しと、名前・詳細の行)
struct CreditsPage {
    std::string heading,note,tab;
    std::vector<std::pair<std::string,std::string>> rows;
};
// credits.json の中身 (画面の題名とページ)
struct CreditsData { std::string title; std::vector<CreditsPage> pages; };

// credits.json を読み込む (最初に使われたときに 1 回だけ)
inline const CreditsData& Credits() {
    static const CreditsData data=[]{
        const auto& json=Data::Load("credits.json");
        CreditsData out;
        out.title=Data::Require(json,"title","credits.json").get<std::string>();
        for(const auto& page:Data::Require(json,"pages","credits.json")){
            CreditsPage p;p.heading=page.at("heading").get<std::string>();p.note=page.value("note",std::string{});p.tab=page.at("tab").get<std::string>();
            for(const auto& row:page.at("rows"))p.rows.push_back({row.at("title").get<std::string>(),row.at("detail").get<std::string>()});
            out.pages.push_back(std::move(p));
        }
        if(out.pages.empty())throw std::runtime_error("credits.json: pages must not be empty");
        return out;
    }();
    return data;
}
// ページ数
inline int CreditsPageCount(){return int(Credits().pages.size());}

// 操作: 左右 / LB RB / 章の帯のクリックでめくる。閉じるなら true を返す
inline bool UpdateCredits(Canvas& ui,int& page){
    if(BackPressed()||ui.Click(CreditsBack))return true;
    const int count=CreditsPageCount();
    const int step=(Pressed(DIK_RIGHT)||Pressed(DIK_D)||PadPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER|XINPUT_GAMEPAD_DPAD_RIGHT)?1:0)-(Pressed(DIK_LEFT)||Pressed(DIK_A)||PadPressed(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_DPAD_LEFT)?1:0);
    const int before=page;page=std::clamp(page+step,0,count-1);
    if(!UsingPad())for(int i=0;i<count;++i)if(ui.Click(CreditsTab(i)))page=i;
    if(page!=before)PlayUISound(UISound::Move);
    return false;
}
// タイトル画面と GPU のレイアウト検証で共有する。座標はすべて Canvas の座標系
inline void DrawCredits(Canvas& ui, int page) {
    namespace S=Scale;
    const auto& credits=Credits();
    page=std::clamp(page,0,CreditsPageCount()-1);
    const auto& current=credits.pages[size_t(page)];
    S::Backdrop(ui);
    // 見出しと表記
    ui.Text(credits.title,48,22,26,S::Black,S::Jp);
    ui.Big(current.heading,46,50,92,S::Black,S::JpBig,S::JpBigBase);
    ui.Text(current.note,50,166,22,S::Black,S::Jp);
    // 各行: 名前と詳細
    for(size_t i=0;i<current.rows.size();++i){const float y=202+float(i)*58;
        ui.Text(current.rows[i].first,50,y,26,S::Black,S::Jp);ui.Text(current.rows[i].second,52,y+31,17,S::Black,S::Jp);}
    S::Hint(ui,"keyboard_arrows_horizontal","xbox_dpad",Str("scale.creditsPage"),822,40);
    S::Hint(ui,"keyboard_escape","xbox_button_b",Str("scale.creditsBack"),1070,40);
    // 章ごとに飾りの物を変える
    static const S::Sprite* decor[]={&S::Kago,&S::Jihanki,&S::Neko,&S::Danboru,&S::Inugoya};const S::Sprite* sprite=decor[page%5];
    S::Prop(ui,*sprite,1110,564,sprite->aspect>1?120.f:190.f);
    // 章の帯: スライムが今の章へ滑っていく
    static float slimeX=0;const float target=CreditsTab(page).x+70;slimeX=slimeX<=0?target:slimeX+(target-slimeX)*.18f;
    S::Slime(ui,slimeX,564,150);
    for(int i=0;i<CreditsPageCount();++i){const Rect b=CreditsTab(i);const bool on=i==page;ui.Fill(b,on?S::Black:WithAlpha(S::Black,.14f));
        ui.Text(std::to_string(i+1)+" "+credits.pages[size_t(i)].tab,b.x+12,b.y+8,22,on?S::Yellow:S::Black,S::Jp);}
    S::Tape(ui,{{5},{30},{100},{500},{2200}});
}
}
