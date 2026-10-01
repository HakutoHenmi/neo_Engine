#pragma once
#include "GameUI.h"
#include "../Chrono/RogueliteRules.h"
namespace Game::UI {
inline constexpr Rect RogueReroll{44,620,290,54},RogueSkip{354,620,290,54};
inline Rect RogueChoice(int i){return {44,150+float(i)*146,690,132};}
inline Engine::Vector4 RogueColor(int attribute){return attribute==0?Engine::Vector4{.2f,1,.75f,1}:attribute==1?Gold:attribute==2?Lime:Engine::Vector4{.76f,.45f,1,1};}
inline void RogueIcon(Canvas& ui,float x,float y,int attribute){auto color=RogueColor(attribute);ui.Panel({x,y,46,46},color);
    if(attribute==0){ui.Line(x+9,y+30,x+22,y+12,3,Ink);ui.Line(x+22,y+12,x+36,y+30,3,Ink);ui.Line(x+36,y+30,x+9,y+30,3,Ink);}
    else if(attribute==1){for(int i=0;i<3;++i)ui.Line(x+12+float(i)*9,y+34,x+18+float(i)*9,y+12,3,Ink);}
    else ui.Center(attribute==2?"S":"!",x+23,y+5,30,Ink);
}
inline void RogueSelection(Canvas& ui,const Chrono::Roguelite& state){
    ui.Fill({0,0,770,720},{.025f,.045f,.04f,.88f});ui.Text("SLIME EVOLUTION",44,32,32,Lime);
    ui.Text("LEVEL "+std::to_string(state.level)+"  /  残り強化 "+std::to_string(state.pending),44,76,22,Paper,JapaneseFont);
    ui.Text("粘液パレットを1枚選択",44,110,22,Muted,JapaneseFont);
    for(int i=0;i<3;++i){int id=state.offers[size_t(i)];if(id<0)continue;const auto& card=Chrono::RogueCards[size_t(id)];auto b=RogueChoice(i);
        Engine::Vector4 frame=card.rarity==2?Gold:card.rarity==1?Engine::Vector4{.3f,.66f,1,1}:Paper;
        bool active=i==state.selected||ui.Hover(b);ui.Panel(b,frame);ui.Panel({b.x+3,b.y+3,b.w-6,b.h-6},active?Engine::Vector4{.12f,.22f,.19f,1}:Ink);
        RogueIcon(ui,b.x+14,b.y+17,card.attribute);
        ui.Text(std::to_string(i+1)+"  "+card.name,b.x+72,b.y+13,24,frame,JapaneseFont);
        ui.Text("【"+std::string(Chrono::RogueTags[int(card.tag)])+"】  Rank "+std::to_string(state.ranks[size_t(id)]+1),b.x+450,b.y+16,17,RogueColor(card.attribute),JapaneseFont);
        ui.Text(card.effect,b.x+72,b.y+51,18,Paper,JapaneseFont);
        ui.Text(card.flavor,b.x+72,b.y+88,16,Muted,JapaneseFont);
        const char* attributes[]={"軌跡","ホーミング弾","スライム本体","状態異常"};ui.Text(attributes[card.attribute],b.x+450,b.y+89,16,RogueColor(card.attribute),JapaneseFont);
        ui.Text(card.rarity==2?"LEGEND":card.rarity==1?"RARE":"COMMON",b.x+14,b.y+82,12,frame);
    }
    ui.Button(RogueReroll,"R: REROLL  "+std::to_string(state.rerolls));ui.Button(RogueSkip,"X: SKIP / +60 BODY");
    ui.Text("1 / 2 / 3 : CHOOSE    ENTER : CONFIRM",44,689,17,Muted);
    ui.Panel({870,540,350,115});ui.Text("YOUR SLIME",895,555,26,Lime);
    ui.Text("同じタグ3枚でシナジー発動",895,595,17,Paper,JapaneseFont);
}
inline void RoguePalette(Canvas& ui,const Chrono::Roguelite& state){
    ui.Background("SLIME PALETTE / PAUSED");ui.Text("取得済み強化  /  LEVEL "+std::to_string(state.level),44,114,24,Lime,JapaneseFont);
    for(size_t i=0;i<state.ranks.size();++i){int col=int(i)%3,row=int(i)/3;float x=44+float(col)*264,y=164+float(row)*71;const auto& card=Chrono::RogueCards[i];
        ui.Panel({x,y,252,62},state.ranks[i]>0?Engine::Vector4{.13f,.24f,.18f,1}:Ink);
        ui.Fill({x,y,3,62},state.ranks[i]>0?RogueColor(card.attribute):Muted);
        ui.Text(card.name,x+10,y+6,16,state.ranks[i]>0?Paper:Muted,JapaneseFont);
        ui.Text("RANK "+std::to_string(state.ranks[i]),x+10,y+33,17,state.ranks[i]>0?Lime:Muted);
        if(state.ranks[i]>0&&(i==5||i==7||i==11||i==12||i==13||i==14||i==15))ui.Text("スキル有効",x+126,y+35,14,Gold,JapaneseFont);
    }
    ui.Text("発動スキル / シナジー",870,115,24,Gold,JapaneseFont);
    for(int i=0;i<5;++i){bool active=state.Synergy(Chrono::RogueTag(i));float y=167+float(i)*67;
        ui.Text(std::string(Chrono::RogueTags[i])+"  "+std::to_string(state.Tags(Chrono::RogueTag(i)))+" / 3",870,y,21,active?Lime:Muted,JapaneseFont);
        ui.Text(active?std::string(Chrono::RogueSynergies[i]):"あと"+std::to_string(3-state.Tags(Chrono::RogueTag(i)))+"枚で発動",870,y+28,17,active?Paper:Muted,JapaneseFont);}
    ui.Button({870,530,350,54},"ENTER: RESUME",true);ui.Button({870,595,350,54},"TAB: BACK TO TITLE");
    int hovered=-1;for(size_t i=0;i<state.ranks.size();++i)if(ui.Hover({44+float(i%3)*264,164+float(i/3)*71,252,62}))hovered=int(i);
    if(hovered>=0){const auto& card=Chrono::RogueCards[size_t(hovered)];ui.Text(card.effect,44,610,18,Paper,JapaneseFont);ui.Text(card.flavor,44,638,17,Muted,JapaneseFont);}
    else ui.Text("カードにカーソルを合わせると効果を表示 / ESCで再開",44,620,17,Muted,JapaneseFont);
}
}
