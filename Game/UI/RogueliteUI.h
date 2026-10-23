// RogueliteUI.h
// スライムの強化 (ローグライトのカード) の選択画面と、ポーズ中の取得済み強化 (パレット) 画面。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "GameUI.h"
// 強化カードの定義と、強化の状態
#include "../Chrono/RogueliteRules.h"
// UI 関連はすべて Game::UI 名前空間に置く
namespace Game::UI {
// 引き直し・スキップのボタンはカードから離れた下に置き、ゲームパッドでは Y / X のショートカットでしか押せない
// (A ボタンの押し間違いで引き直さないため)
inline constexpr Rect RogueReroll{56,630,250,48},RogueSkip{322,630,300,48};
// パレット画面のボタンの位置
inline constexpr Rect PaletteResume{860,566,360,64},PaletteTitle{860,644,290,46};
// 強化候補のカード i 枚目の位置 / パレットのカード i 枚目の位置
inline Rect RogueChoice(int i){return {56,158+float(i)*150,700,134};}
inline Rect PaletteCard(size_t i){return {56+float(i%3)*258,170+float(i/3)*66,246,56};}
// カードの属性ごとの色
inline Engine::Vector4 RogueColor(int attribute){return attribute==0?Teal:attribute==1?Gold:attribute==2?Lime:Engine::Vector4{.76f,.5f,1,1};}
// 属性のアイコン (三角・縦線・文字)
inline void RogueIcon(Canvas& ui,float x,float y,float r,int attribute){
    // 属性の色
    const auto color=RogueColor(attribute);
    // 後ろの光・円・左上の小さな光
    ui.Halo(x,y,r*1.9f,WithAlpha(color,.18f));ui.Dot(x,y,r,color);ui.Dot(x-r*.35f,y-r*.4f,r*.2f,WithAlpha(Paper,.5f));
    // アイコンの線の大きさの倍率
    const float k=r/23;
    // 属性 0: 三角形
    if(attribute==0){ui.Line(x-11*k,y+8*k,x,y-10*k,3,Ink);ui.Line(x,y-10*k,x+11*k,y+8*k,3,Ink);ui.Line(x+11*k,y+8*k,x-11*k,y+8*k,3,Ink);}
    // 属性 1: 斜めの 3 本線
    else if(attribute==1){for(int i=0;i<3;++i)ui.Line(x-11*k+float(i)*9*k,y+10*k,x-5*k+float(i)*9*k,y-10*k,3,Ink);}
    // 属性 2 は S、それ以外は ! の文字
    else ui.Center(attribute==2?"S":"!",x,y-r*.62f,r*1.25f,Ink);
}
// 強化の選択画面: 3枚の候補カード (横から滑り込む)、引き直し・スキップのボタン
inline void RogueSelection(Canvas& ui,const Chrono::Roguelite& state){
    // 左側に暗いスライムの幕を引き、止まった戦場へなじませる
    ui.Fill({-200,-200,1020,1120},WithAlpha(Night,.9f));
    ui.ImageRot(Art::Fade,{910-560,360-90,1120,180},-1.5707963f,WithAlpha(Night,.9f));
    // 見出しの小さな英字
    const std::string& section=Str("roguelite.section");
    ui.Label(section,58,40,14,Lime,3.4f);
    // 今のレベルの札 (英字の右に置く)
    const std::string level=Format(Str("roguelite.level"),state.level);
    const float levelX=60+ui.SpacedWidth(section,14,3.4f)+18;
    ui.Rounded({levelX,34,ui.SpacedWidth(level,13,2)+24,28},WithAlpha(Lime,.16f),14);ui.Label(level,levelX+12,41,13,Lime,2);
    // 見出し
    ui.Text(Str("roguelite.heading"),52,62,40,Paper,JapaneseFont);
    // 戻る途中・連続強化中の残り枚数・ふだんの説明のどれかを出す
    ui.Text(state.leaving?Str("roguelite.leaving"):state.pending>1?Format(Str("roguelite.chain"),state.pending):Str("roguelite.hint"),58,118,17,Muted,JapaneseFont);
    // 候補カード: 属性の色の帯・アイコン・番号・名前・タグ・ランク・効果・説明・レア度
    for(int i=0;i<3;++i){
        // 空の候補は飛ばす
        const int id=state.offers[size_t(i)];if(id<0)continue;
        // カードの定義と、置く位置
        const auto& card=Chrono::RogueCards[size_t(id)];const Rect slot=RogueChoice(i);auto b=slot;
        // 出てくる進み具合と、なめらかな動き
        const float reveal=Chrono::CardReveal(state.cardAge,i),ease=1-std::pow(1-reveal,3.f);
        // 選べる状態で、選択中かマウスが乗っているものを強調する
        const bool active=state.CanChoose()&&(i==state.selected||ui.Hover(slot));
        // マウス・パッドの選択対象として登録する
        ui.Track(slot,true);
        // 左から滑り込み、着く前に少し揺れる。強調中は右へ少しずらす
        b.x-=760*(1-ease);b.x+=12*std::sin(reveal*3.141593f)*(1-reveal)+(active?14.f:0.f);
        // 画面を閉じるときは左へ引っ込む
        if(state.leaving)b.x-=160*(1-state.MenuEase());
        // 出てくる途中は半透明にする
        Canvas c=ui;c.MultiplyAlpha(reveal);
        // 属性の色と、レア度の縁の色
        const auto attribute=RogueColor(card.attribute);
        const auto rim=card.rarity==2?Gold:card.rarity==1?Teal:WithAlpha(Paper,.14f);
        // 伝説のカードは金色の光
        if(card.rarity==2)c.Halo(b.x+b.w*.5f,b.y+b.h*.5f,b.w*.6f,WithAlpha(Gold,.1f));
        // 強調中は下からしずくを垂らす
        if(active)c.Drips(b.x+60,b.y+b.h-6,b.w-120,WithAlpha(Lime,.85f),float(id)*3.3f,1.1f);
        // カードの台と縁
        c.Rounded(b,active?Engine::Vector4{.1f,.17f,.13f,.98f}:Engine::Vector4{.07f,.115f,.09f,.97f},20);
        c.RoundedRing(b,active?Lime:rim,20);
        // 左端の属性の色の帯
        c.Rounded({b.x+12,b.y+16,7,b.h-32},attribute,4);
        // 属性のアイコンと番号
        RogueIcon(c,b.x+60,b.y+52,27,card.attribute);
        c.Dot(b.x+82,b.y+86,12,Ink);c.Center(std::to_string(i+1),b.x+82,b.y+76,17,active?Lime:Muted);
        // カードの名前
        c.Text(card.name,b.x+108,b.y+14,27,Paper,JapaneseFont);
        // タグの札 (名前の右)
        const std::string tag=Chrono::RogueTags[int(card.tag)];
        const float tagX=b.x+120+c.Width(card.name,27,JapaneseFont),tagW=c.Width(tag,14,JapaneseFont)+22;
        c.Rounded({tagX,b.y+20,tagW,24},WithAlpha(attribute,.16f),12);c.Text(tag,tagX+11,b.y+23,14,attribute,JapaneseFont);
        // 今のランク (取ると 1 つ上がる)
        c.Label(Format(Str("roguelite.rank"),state.ranks[size_t(id)]+1),tagX+tagW+12,b.y+25,12,Muted,2);
        // 効果と説明
        c.Text(card.effect,b.x+108,b.y+56,19,Paper,JapaneseFont);
        c.Text(card.flavor,b.x+108,b.y+92,15,Muted,JapaneseFont);
        // 右上のレア度
        const std::string& rarity=Str(card.rarity==2?"roguelite.rarityLegend":card.rarity==1?"roguelite.rarityRare":"roguelite.rarityCommon");
        c.Label(rarity,b.x+b.w-22-c.SpacedWidth(rarity,12,2.6f),b.y+18,12,card.rarity?rim:Muted,2.6f);
        // パッドで選択中なら A ボタンの印
        if(UsingPad()&&active)c.PadGlyph("xbox_button_a",b.x+b.w-54,b.y+b.h-52,36);
    }
    // 引き直し・スキップのボタンと操作の案内
    const bool pad=UsingPad();
    // 引き直し (残り回数付き) とスキップ
    ui.Button(RogueReroll,Format(Str(pad?"roguelite.reroll":"roguelite.rerollKey"),state.rerolls),false,"xbox_button_y",false);
    ui.Button(RogueSkip,Str(pad?"roguelite.skip":"roguelite.skipKey"),false,"xbox_button_x",false);
    // 操作の案内
    if(pad){float x=ui.PadPrompt("xbox_dpad",Str("roguelite.padChoose"),58,688,22,15);ui.PadPrompt("xbox_button_a",Str("roguelite.padConfirm"),x+20,688,22,15);}
    else ui.Label(Str("roguelite.keyHint"),58,692,12,Muted,2);
}
// パレット画面 (ポーズ中): 取得済みのカードとシナジーの状態、選んだカードの説明
inline void RoguePalette(Canvas& ui,const Chrono::Roguelite& state){
    // 背景と見出し
    ui.Background(Str("roguelite.paletteSection").c_str());
    ui.Brush({40,94,440,82},WithAlpha(Lime,.92f),-.02f);
    ui.Text(Str("roguelite.paletteHeading"),66,114,34,Ink,JapaneseFont);
    // 今のレベル
    ui.Label(Format(Str("roguelite.paletteLevel"),state.level),480,128,14,Lime,3);
    // マウスが乗っているカードの番号 (無ければ -1)
    int hovered=-1;
    // カードの一覧 (取得済みは明るく、ランクを点で表示。スキル付きのカードには SKILL の印)
    for(size_t i=0;i<state.ranks.size();++i){
        // カードの位置・定義・取得済みか
        const Rect b=PaletteCard(i);const auto& card=Chrono::RogueCards[i];const bool owned=state.ranks[i]>0;
        // 選択対象として登録し、乗っていれば覚える
        ui.Track(b);ui.FocusFrame(b,14);if(ui.Hover(b))hovered=int(i);
        // カードの台 (取得済みは明るく) と、乗っているときの縁
        ui.Rounded(b,owned?Engine::Vector4{.1f,.17f,.13f,.97f}:Engine::Vector4{.055f,.09f,.072f,.9f},14);
        if(hovered==int(i))ui.RoundedRing(b,WithAlpha(Lime,.8f),14);
        // 左端の属性の色の帯 (未取得は灰色)
        ui.Rounded({b.x+8,b.y+10,5,b.h-20},owned?RogueColor(card.attribute):WithAlpha(Muted,.3f),3);
        // 名前
        ui.Text(card.name,b.x+22,b.y+8,16,owned?Paper:WithAlpha(Muted,.7f),JapaneseFont);
        // ランクの点 (最大 6 つ)
        for(int k=0;k<std::max(card.maxRank,1)&&k<6;++k)ui.Dot(b.x+28+float(k)*14,b.y+42,4.5f,k<state.ranks[i]?Lime:WithAlpha(Muted,.25f));
        // スキルを持つカードには SKILL の印
        if(owned&&card.skill)ui.Label(Str("roguelite.skill"),b.x+b.w-ui.SpacedWidth(Str("roguelite.skill"),11,2)-14,b.y+37,11,Gold,2);
    }
    // シナジー (同じタグを3枚集めると発動)
    ui.Label(Str("roguelite.synergy"),862,120,14,Gold,3.4f);
    for(int i=0;i<5;++i){
        // 発動しているか・集めた枚数・行の高さ
        const bool active=state.Synergy(Chrono::RogueTag(i));const int tags=state.Tags(Chrono::RogueTag(i));const float y=150+float(i)*78;
        // 行の台 (発動中は金色の縁)
        ui.Rounded({856,y,364,68},active?Engine::Vector4{.12f,.16f,.08f,.97f}:Engine::Vector4{.06f,.1f,.08f,.9f},16);
        if(active)ui.RoundedRing({856,y,364,68},WithAlpha(Gold,.7f),16);
        // タグ名と、集めた枚数の点
        ui.Text(Chrono::RogueTags[i],874,y+10,20,active?Paper:Muted,JapaneseFont);
        for(int k=0;k<3;++k)ui.Dot(1180-float(2-k)*18,y+22,6,k<tags?(active?Gold:Lime):WithAlpha(Muted,.25f));
        // 発動中はシナジーの効果、未発動ならあと何枚か
        ui.Text(active?Chrono::RogueSynergies[size_t(i)]:Format(Str("roguelite.synergyRemaining"),std::max(0,Chrono::LoadRogueData().synergyCount-tags)),874,y+40,15,active?Gold:Muted,JapaneseFont);
    }
    const bool pad=UsingPad();
    // 再開とタイトルへ戻るボタン
    ui.Button(PaletteResume,Str(pad?"roguelite.resume":"roguelite.resumeKey"),true,"xbox_button_b");ui.Button(PaletteTitle,Str(pad?"roguelite.backToTitle":"roguelite.titleKey"));
    // 選んだカードの説明欄
    ui.Rounded({56,574,770,96},{.055f,.09f,.072f,.9f},18);
    // 乗っているカードがあれば名前・効果・説明
    if(hovered>=0){const auto& card=Chrono::RogueCards[size_t(hovered)];
        ui.Text(card.name,78,588,18,RogueColor(card.attribute),JapaneseFont);
        ui.Text(card.effect,78,614,18,Paper,JapaneseFont);ui.Text(card.flavor,78,642,15,Muted,JapaneseFont);}
    // 無ければ操作の案内
    else if(pad){float x=ui.PadPrompt("xbox_dpad",Str("roguelite.padHint"),78,606,26,17,Muted,JapaneseFont);
        ui.PadPrompt("xbox_button_b",Str("roguelite.padResume"),x+24,606,26,17,Muted,JapaneseFont);}
    else ui.Text(Str("roguelite.keyHintPalette"),78,612,17,Muted,JapaneseFont);
}
}
