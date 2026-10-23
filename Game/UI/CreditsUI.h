// CreditsUI.h
// クレジット画面 (音楽・素材・フォント・ソフトウェアの表記) の描画。
// ページの見出しと各行の文章は Resources/Data/credits.json に置く。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "GameUI.h"
#include "../Data/GameData.h"
#include <vector>

// UI 関連はすべて Game::UI 名前空間に置く
namespace Game::UI {
// ボタンの位置 (1280x720 の UI 座標)
inline constexpr Rect CreditsButton{96,580,220,50};
inline constexpr Rect CreditsBack{1012,600,200,50}, CreditsPrev{96,600,190,50}, CreditsNext{300,600,190,50};

// クレジットの 1 ページ (見出しと、名前・詳細の行)
struct CreditsPage {
    std::string heading;
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
            CreditsPage p;p.heading=page.at("heading").get<std::string>();
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

// タイトル画面と GPU のレイアウト検証で共有する。座標はすべて Canvas の座標系
inline void DrawCredits(Canvas& ui, int page) {
    const auto& credits=Credits();
    page=std::clamp(page,0,CreditsPageCount()-1);
    // 背景と、画面の上に出る見出し
    ui.Background(credits.title.c_str());
    // ページ番号を大きく薄い数字で置き、見出しは筆の塗りの上に描く
    ui.Text("0"+std::to_string(page+1),1040,92,200,{.1f,.16f,.125f,1});
    // 見出し: 筆の塗りの上に文字を置く
    const std::string& heading=credits.pages[size_t(page)].heading;
    // 見出しの文字幅
    const float w=ui.Width(heading,34);
    // 文字幅より少し広い筆の塗り (少し傾ける)
    ui.Brush({66,114,w+100,82},WithAlpha(Lime,.92f),-.02f);
    // 塗りの上に濃い色で見出しを書く
    ui.Text(heading,96,136,34,Ink);
    // 各行 (名前と詳細)
    const auto& rows=credits.pages[size_t(page)].rows;
    for(size_t index=0;index<rows.size();++index){
        // 行の上端 (1 行 68 ピクセル)
        const float y=226.f+float(index)*68.f;
        // 行頭の点 (行ごとに色を変える)
        ui.Dot(104,y+14,5,index%2?Teal:Lime);
        // 名前と、その下に薄い色の詳細
        ui.Text(rows[index].first,124,y,25);ui.Text(rows[index].second,126,y+30,18,Muted);
    }
    // 前後のページボタン・ページの点・戻るボタン
    if(page>0)ui.Button(CreditsPrev,Str("common.previous"),false,"xbox_lb");
    if(page+1<CreditsPageCount())ui.Button(CreditsNext,Str("common.next"),false,"xbox_rb");
    // 今のページの点は大きく明るく、それ以外は小さく薄く
    for(int i=0;i<CreditsPageCount();++i)ui.Dot(540+float(i)*22,625,i==page?6.f:4.f,i==page?Lime:WithAlpha(Muted,.4f));
    // 戻るボタン (ゲームパッドのときはキーの表記を省く)
    ui.Button(CreditsBack,Str(UsingPad()?"common.back":"common.backEsc"),false,"xbox_button_b");
}
}
