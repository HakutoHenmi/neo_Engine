#pragma once
#include "GameUI.h"
#include "../Chrono/RogueliteRules.h"
namespace Game::UI {
// Reroll / skip sit well below the cards, and on the gamepad they are reached only
// through their Y / X shortcuts: a stray A press can never reroll.
inline constexpr Rect RogueReroll{56,630,250,48},RogueSkip{322,630,300,48};
inline constexpr Rect PaletteResume{860,566,360,64},PaletteTitle{860,644,290,46};
inline Rect RogueChoice(int i){return {56,158+float(i)*150,700,134};}
inline Rect PaletteCard(size_t i){return {56+float(i%3)*258,170+float(i/3)*66,246,56};}
inline Engine::Vector4 RogueColor(int attribute){return attribute==0?Teal:attribute==1?Gold:attribute==2?Lime:Engine::Vector4{.76f,.5f,1,1};}
inline void RogueIcon(Canvas& ui,float x,float y,float r,int attribute){
    const auto color=RogueColor(attribute);
    ui.Halo(x,y,r*1.9f,WithAlpha(color,.18f));ui.Dot(x,y,r,color);ui.Dot(x-r*.35f,y-r*.4f,r*.2f,WithAlpha(Paper,.5f));
    const float k=r/23;
    if(attribute==0){ui.Line(x-11*k,y+8*k,x,y-10*k,3,Ink);ui.Line(x,y-10*k,x+11*k,y+8*k,3,Ink);ui.Line(x+11*k,y+8*k,x-11*k,y+8*k,3,Ink);}
    else if(attribute==1){for(int i=0;i<3;++i)ui.Line(x-11*k+float(i)*9*k,y+10*k,x-5*k+float(i)*9*k,y-10*k,3,Ink);}
    else ui.Center(attribute==2?"S":"!",x,y-r*.62f,r*1.25f,Ink);
}
inline void RogueSelection(Canvas& ui,const Chrono::Roguelite& state){
    // Dark slime curtain on the left, fading into the frozen battlefield.
    ui.Fill({-200,-200,1020,1120},WithAlpha(Night,.9f));
    ui.ImageRot(Art::Fade,{910-560,360-90,1120,180},-1.5707963f,WithAlpha(Night,.9f));
    ui.Label("SLIME EVOLUTION",58,40,14,Lime,3.4f);
    const std::string level="LV "+std::to_string(state.level);
    const float levelX=60+ui.SpacedWidth("SLIME EVOLUTION",14,3.4f)+18;
    ui.Rounded({levelX,34,ui.SpacedWidth(level,13,2)+24,28},WithAlpha(Lime,.16f),14);ui.Label(level,levelX+12,41,13,Lime,2);
    ui.Text("粘液パレットを1枚",52,62,40,Paper,JapaneseFont);
    ui.Text(state.leaving?"ゲームプレイに戻ります":state.pending>1?"連続強化中　残り "+std::to_string(state.pending)+" 枚":"同じタグを3枚集めるとシナジーが発動",58,118,17,Muted,JapaneseFont);
    for(int i=0;i<3;++i){
        const int id=state.offers[size_t(i)];if(id<0)continue;
        const auto& card=Chrono::RogueCards[size_t(id)];const Rect slot=RogueChoice(i);auto b=slot;
        const float reveal=Chrono::CardReveal(state.cardAge,i),ease=1-std::pow(1-reveal,3.f);
        const bool active=state.CanChoose()&&(i==state.selected||ui.Hover(slot));
        ui.Track(slot,true);
        b.x-=760*(1-ease);b.x+=12*std::sin(reveal*3.141593f)*(1-reveal)+(active?14.f:0.f);
        if(state.leaving)b.x-=160*(1-state.MenuEase());
        Canvas c=ui;c.MultiplyAlpha(reveal);
        const auto attribute=RogueColor(card.attribute);
        const auto rim=card.rarity==2?Gold:card.rarity==1?Teal:WithAlpha(Paper,.14f);
        if(card.rarity==2)c.Halo(b.x+b.w*.5f,b.y+b.h*.5f,b.w*.6f,WithAlpha(Gold,.1f));
        if(active)c.Drips(b.x+60,b.y+b.h-6,b.w-120,WithAlpha(Lime,.85f),float(id)*3.3f,1.1f);
        c.Rounded(b,active?Engine::Vector4{.1f,.17f,.13f,.98f}:Engine::Vector4{.07f,.115f,.09f,.97f},20);
        c.RoundedRing(b,active?Lime:rim,20);
        c.Rounded({b.x+12,b.y+16,7,b.h-32},attribute,4);
        RogueIcon(c,b.x+60,b.y+52,27,card.attribute);
        c.Dot(b.x+82,b.y+86,12,Ink);c.Center(std::to_string(i+1),b.x+82,b.y+76,17,active?Lime:Muted);
        c.Text(card.name,b.x+108,b.y+14,27,Paper,JapaneseFont);
        const std::string tag=Chrono::RogueTags[int(card.tag)];
        const float tagX=b.x+120+c.Width(card.name,27,JapaneseFont),tagW=c.Width(tag,14,JapaneseFont)+22;
        c.Rounded({tagX,b.y+20,tagW,24},WithAlpha(attribute,.16f),12);c.Text(tag,tagX+11,b.y+23,14,attribute,JapaneseFont);
        c.Label("RANK "+std::to_string(state.ranks[size_t(id)]+1),tagX+tagW+12,b.y+25,12,Muted,2);
        c.Text(card.effect,b.x+108,b.y+56,19,Paper,JapaneseFont);
        c.Text(card.flavor,b.x+108,b.y+92,15,Muted,JapaneseFont);
        const char* rarity=card.rarity==2?"LEGEND":card.rarity==1?"RARE":"COMMON";
        c.Label(rarity,b.x+b.w-22-c.SpacedWidth(rarity,12,2.6f),b.y+18,12,card.rarity?rim:Muted,2.6f);
        if(UsingPad()&&active)c.PadGlyph("xbox_button_a",b.x+b.w-54,b.y+b.h-52,36);
    }
    const bool pad=UsingPad();
    ui.Button(RogueReroll,(pad?"REROLL  ":"R  REROLL  ")+std::to_string(state.rerolls),false,"xbox_button_y",false);
    ui.Button(RogueSkip,pad?"SKIP  +60 BODY":"X  SKIP  +60 BODY",false,"xbox_button_x",false);
    if(pad){float x=ui.PadPrompt("xbox_dpad","CHOOSE",58,688,22,15);ui.PadPrompt("xbox_button_a","CONFIRM",x+20,688,22,15);}
    else ui.Label("1 / 2 / 3  CHOOSE      ENTER  CONFIRM",58,692,12,Muted,2);
}
inline void RoguePalette(Canvas& ui,const Chrono::Roguelite& state){
    ui.Background("SLIME PALETTE / PAUSED");
    ui.Brush({40,94,440,82},WithAlpha(Lime,.92f),-.02f);
    ui.Text("取得済み強化",66,114,34,Ink,JapaneseFont);
    ui.Label("LEVEL "+std::to_string(state.level),480,128,14,Lime,3);
    int hovered=-1;
    for(size_t i=0;i<state.ranks.size();++i){
        const Rect b=PaletteCard(i);const auto& card=Chrono::RogueCards[i];const bool owned=state.ranks[i]>0;
        ui.Track(b);ui.FocusFrame(b,14);if(ui.Hover(b))hovered=int(i);
        ui.Rounded(b,owned?Engine::Vector4{.1f,.17f,.13f,.97f}:Engine::Vector4{.055f,.09f,.072f,.9f},14);
        if(hovered==int(i))ui.RoundedRing(b,WithAlpha(Lime,.8f),14);
        ui.Rounded({b.x+8,b.y+10,5,b.h-20},owned?RogueColor(card.attribute):WithAlpha(Muted,.3f),3);
        ui.Text(card.name,b.x+22,b.y+8,16,owned?Paper:WithAlpha(Muted,.7f),JapaneseFont);
        for(int k=0;k<std::max(card.maxRank,1)&&k<6;++k)ui.Dot(b.x+28+float(k)*14,b.y+42,4.5f,k<state.ranks[i]?Lime:WithAlpha(Muted,.25f));
        if(owned&&(i==5||i==7||i==11||i==12||i==13||i==14||i==15))ui.Label("SKILL",b.x+b.w-ui.SpacedWidth("SKILL",11,2)-14,b.y+37,11,Gold,2);
    }
    ui.Label("SYNERGY",862,120,14,Gold,3.4f);
    for(int i=0;i<5;++i){
        const bool active=state.Synergy(Chrono::RogueTag(i));const int tags=state.Tags(Chrono::RogueTag(i));const float y=150+float(i)*78;
        ui.Rounded({856,y,364,68},active?Engine::Vector4{.12f,.16f,.08f,.97f}:Engine::Vector4{.06f,.1f,.08f,.9f},16);
        if(active)ui.RoundedRing({856,y,364,68},WithAlpha(Gold,.7f),16);
        ui.Text(Chrono::RogueTags[i],874,y+10,20,active?Paper:Muted,JapaneseFont);
        for(int k=0;k<3;++k)ui.Dot(1180-float(2-k)*18,y+22,6,k<tags?(active?Gold:Lime):WithAlpha(Muted,.25f));
        ui.Text(active?std::string(Chrono::RogueSynergies[i]):"あと"+std::to_string(std::max(0,3-tags))+"枚で発動",874,y+40,15,active?Gold:Muted,JapaneseFont);
    }
    const bool pad=UsingPad();
    ui.Button(PaletteResume,pad?"RESUME":"ENTER  RESUME",true,"xbox_button_b");ui.Button(PaletteTitle,pad?"BACK TO TITLE":"TAB  TITLE");
    ui.Rounded({56,574,770,96},{.055f,.09f,.072f,.9f},18);
    if(hovered>=0){const auto& card=Chrono::RogueCards[size_t(hovered)];
        ui.Text(card.name,78,588,18,RogueColor(card.attribute),JapaneseFont);
        ui.Text(card.effect,78,614,18,Paper,JapaneseFont);ui.Text(card.flavor,78,642,15,Muted,JapaneseFont);}
    else if(pad){float x=ui.PadPrompt("xbox_dpad","カードを選ぶと効果を表示",78,606,26,17,Muted,JapaneseFont);
        ui.PadPrompt("xbox_button_b","再開",x+24,606,26,17,Muted,JapaneseFont);}
    else ui.Text("カードにカーソルを合わせると効果を表示　/　ESCで再開",78,612,17,Muted,JapaneseFont);
}
}
