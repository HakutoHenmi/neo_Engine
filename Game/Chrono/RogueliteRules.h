#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "CinematicRules.h"
namespace Game::Chrono {
enum class RogueTag { Rapid, Domain, Debuff, Guard, Familiar };
struct RogueCard { const char* name; const char* effect; const char* flavor; RogueTag tag; int attribute,rarity,maxRank; };
inline constexpr std::array<RogueCard,18> RogueCards{{
    {"多腔粘液腺","最低弾数 +12 / 総弾数 +12","小さな輪にも、尽きない雨を。",RogueTag::Rapid,1,0,5},
    {"閉環加速","囲い完成後 2秒間 移動速度 +20%","閉じた瞬間、身体がほどける。",RogueTag::Rapid,2,0,3},
    {"硬質弾核","弾の固定ダメージ +2","柔らかい身体に、硬い決意。",RogueTag::Rapid,1,1,5},
    {"長命の軌跡","記録上限 +384点 / 領域持続 +3秒","この一筆は、まだ終わらない。",RogueTag::Domain,0,0,4},
    {"面積増幅","面積1000㎡ごと 弾威力 +25% (上限+100%)","大地を抱くほど、重くなる。",RogueTag::Domain,0,1,3},
    {"スライム隕石","通常囲いの全弾を 1発に統合 / 爆風半径18m","空に溜めた質量が、降る。",RogueTag::Domain,1,2,1},
    {"酸性軌跡","軌跡の周囲3mに 毎秒4ダメージ","草の上に、酸の記憶を残す。",RogueTag::Debuff,0,0,3},
    {"腐食の沼","完成領域を8秒維持 / 敵速度-35% / 被ダメ+25%","輪の内側は、もう私の海。",RogueTag::Debuff,3,1,1},
    {"腐食弾","命中で4秒腐食 / 毎秒2ダメージ","粘液は、装甲の隙間を知る。",RogueTag::Debuff,3,0,3},
    {"凍結弾","命中で2秒減速 -50%","冷たい雫が、翼を縛る。",RogueTag::Debuff,3,1,1},
    {"炎上弾","命中で3秒炎上 / 毎秒3ダメージ","緑の身体に、熱い鼓動。",RogueTag::Debuff,3,1,3},
    {"迎撃粘液","迎撃速度 +4発/秒 (半径18m)","雨を落とす前に、雨を消す。",RogueTag::Guard,1,1,3},
    {"周回防壁","囲いで8秒防壁 / 毎秒4ダメージ / 被ダメ-15%","帰る場所は、自分の周り。",RogueTag::Guard,2,1,3},
    {"反応閉環","ジャスト回避で半径5mの囲いを自動発動","危機をすり抜け、輪を結ぶ。",RogueTag::Guard,2,2,1},
    {"小さな眷属","囲いで砲台召喚 / 12秒持続 / 毎秒2発 / 最大6体","ひとりの輪から、仲間が生まれる。",RogueTag::Familiar,2,1,1},
    {"共鳴連鎖","囲い完成後3秒 砲台間レーザー / 毎秒6ダメージ","離れていても、同じ粘液。",RogueTag::Familiar,1,2,3},
    {"眷属の記憶","砲台の寿命 +6秒 / 弾威力 +25%","小さな身体は、長く覚える。",RogueTag::Familiar,2,0,4},
    {"呪い：重い領域","弾威力 +50% / 移動速度 -10%","抱いた大地は、足にも残る。",RogueTag::Domain,0,2,2}
}};
inline constexpr const char* RogueTags[]={"手数","領域","デバフ","防壁","眷属"};
inline constexpr const char* RogueSynergies[]={"発射速度 +30%","領域の威力 +20%","状態異常ダメージ +50%","被ダメージ -10%","砲台発射速度 +50%"};
struct Roguelite {
    bool enabled=true,menu=false; int level=1,xp=0,pending=0,rerolls=3,selected=0;
    std::array<int,18> ranks{}; std::array<int,3> offers{{-1,-1,-1}};
    uint32_t random=0x73192u; float idle=0,haste=0,orbit=0,resonance=0,interceptClock=0,returnDelay=0;
    float menuBlend=0,choiceDelay=0,cardAge=0;bool leaving=false;
    int Required()const{return 100+50*(level-1);}
    void Gain(int amount){if(!enabled||amount<=0)return;xp+=amount;while(xp>=Required()){xp-=Required();++level;++pending;}}
    void TickGameplay(float dt){returnDelay=std::max(0.f,returnDelay-dt);}
    bool Ready()const{return enabled&&pending>0&&!menu;}
    float MenuEase()const{return menuBlend*menuBlend*(3-2*menuBlend);}
    float IdleAspect(float gameplayAspect)const{float blend=MenuEase();return gameplayAspect+(1-gameplayAspect)*blend+.045f*std::sin(idle*2.4f)*blend;}
    bool CanChoose()const{return menu&&!leaving&&menuBlend>=1&&choiceDelay<=0;}
    void TickMenu(float dt){idle+=dt;cardAge+=dt;choiceDelay=std::max(0.f,choiceDelay-dt);
        menuBlend=std::clamp(menuBlend+(leaving?-dt/.55f:dt/.65f),0.f,1.f);
        if(leaving&&menuBlend<=0){menu=false;leaving=false;returnDelay=.1f;}}
    int Tags(RogueTag tag)const {int count=0;for(size_t i=0;i<ranks.size();++i)if(RogueCards[i].tag==tag)count+=ranks[i];return count;}
    bool Synergy(RogueTag tag)const{return Tags(tag)>=3;}
    uint32_t Random(){random^=random<<13;random^=random>>17;random^=random<<5;return random;}
    void Roll(){cardAge=0;choiceDelay=CardRevealSeconds;offers.fill(-1);for(size_t slot=0;slot<offers.size();++slot){std::array<int,90> eligible{};int count=0;
        for(int i=0;i<int(ranks.size());++i)if(ranks[size_t(i)]<RogueCards[size_t(i)].maxRank&&std::find(offers.begin(),offers.end(),i)==offers.end()){int weight=RogueCards[size_t(i)].rarity==0?5:RogueCards[size_t(i)].rarity==1?3:1;for(int n=0;n<weight;++n)eligible[size_t(count++)]=i;}
        if(count)offers[slot]=eligible[Random()%uint32_t(count)];}selected=0;}
    void Open(){menu=true;leaving=false;menuBlend=0;idle=0;Roll();cardAge=-.18f;}
    bool Reroll(){if(!CanChoose()||rerolls<=0)return false;--rerolls;Roll();return true;}
    bool Choose(int slot){if(!CanChoose()||slot<0||slot>=3)return false;int id=offers[size_t(slot)];if(id<0)return false;++ranks[size_t(id)];Close();return true;}
    void Close(){if(!CanChoose())return;pending=std::max(0,pending-1);choiceDelay=.25f;
        if(pending>0)Roll();else leaving=true;}
    float Move()const{return std::max(.6f,1-.1f*ranks[17])*(haste>0?1+.2f*ranks[1]:1);}
    float Power(float area)const {return (1+std::min(1.f,area/4000.f)*float(ranks[4])+.5f*ranks[17])*(Synergy(RogueTag::Domain)?1.2f:1.f);}
    float Mitigation()const{return (orbit>0?1-.15f*ranks[12]:1)*(Synergy(RogueTag::Guard)?.9f:1.f);}
};
}
