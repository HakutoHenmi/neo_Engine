// RogueliteRules.h
// ローグライトの強化カード (18 種) と、経験値・レベル・カードの抽選・選択・効果の計算。
// カードの名前・説明・系統・希少度・最大ランクと、経験値の曲線などは Resources/Data/roguelite.json に置く。
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include "CinematicRules.h"
#include "../Data/GameData.h"
namespace Game::Chrono {
// カードの系統 (手数・領域・デバフ・防壁・眷属)
enum class RogueTag { Rapid, Domain, Debuff, Guard, Familiar };
// カード (名前・効果・説明・系統・属性・希少度・最大ランク・技が増えるカードか)
struct RogueCard { std::string name,effect,flavor; RogueTag tag; int attribute,rarity,maxRank; bool skill=false; };
// カードの枚数 (効果はカードの番号で決まるので、JSON の枚数もこれに合わせる)
inline constexpr size_t RogueCardCount=18;
// roguelite.json の中身 (カード・系統の名前と相乗効果・経験値などの規則)
struct RogueData {
    std::array<RogueCard,RogueCardCount> cards;
    std::array<std::string,5> tags,synergies;
    int xpBase=0,xpPerLevel=0,rerolls=0,synergyCount=0;
    std::array<int,3> rarityWeights{};
};
// roguelite.json を読み込む (最初に使われたときに 1 回だけ)
inline const RogueData& LoadRogueData(){
    static const RogueData data=[]{
        const auto& json=Data::Load("roguelite.json");
        RogueData out;
        const auto& rules=Data::Require(json,"rules","roguelite.json");
        out.xpBase=rules.at("xpBase").get<int>();out.xpPerLevel=rules.at("xpPerLevel").get<int>();
        out.rerolls=rules.at("rerolls").get<int>();out.synergyCount=rules.at("synergyCount").get<int>();
        out.rarityWeights=Data::ReadArray<int,3>(rules.at("rarityWeights"),"roguelite.json rules.rarityWeights");
        // 系統は id の順 (Rapid / Domain / Debuff / Guard / Familiar) で並べる
        const char* ids[]={"Rapid","Domain","Debuff","Guard","Familiar"};
        const auto& tags=Data::Require(json,"tags","roguelite.json");
        if(tags.size()!=out.tags.size())throw std::runtime_error("roguelite.json: tags must have 5 entries");
        for(size_t i=0;i<out.tags.size();++i){
            if(tags[i].at("id").get<std::string>()!=ids[i])throw std::runtime_error("roguelite.json: tags must be ordered Rapid, Domain, Debuff, Guard, Familiar");
            out.tags[i]=tags[i].at("name").get<std::string>();out.synergies[i]=tags[i].at("synergy").get<std::string>();
        }
        const auto& cards=Data::Require(json,"cards","roguelite.json");
        if(cards.size()!=RogueCardCount)throw std::runtime_error("roguelite.json: cards must have 18 entries");
        for(size_t i=0;i<RogueCardCount;++i){
            const auto& c=cards[i];auto& card=out.cards[i];
            card.name=c.at("name").get<std::string>();card.effect=c.at("effect").get<std::string>();card.flavor=c.at("flavor").get<std::string>();
            const std::string tag=c.at("tag").get<std::string>();
            card.tag=RogueTag(std::find_if(std::begin(ids),std::end(ids),[&](const char* id){return tag==id;})-std::begin(ids));
            if(int(card.tag)>=5)throw std::runtime_error("roguelite.json: unknown tag "+tag);
            card.attribute=c.at("attribute").get<int>();card.rarity=std::clamp(c.at("rarity").get<int>(),0,2);card.maxRank=c.at("maxRank").get<int>();card.skill=c.value("skill",false);
        }
        return out;
    }();
    return data;
}
// 強化カードの一覧 (RogueCards[i] の書き方のまま JSON の中身を返す)
struct RogueCardTable {
    const RogueCard& operator[](size_t i)const{return LoadRogueData().cards[i];}
    static constexpr size_t size(){return RogueCardCount;}
};
inline constexpr RogueCardTable RogueCards{};
// 系統の名前と、同じ系統を集めたときの相乗効果 (RogueTags[i] / RogueSynergies[i])
struct RogueTextTable {
    bool synergy;
    const std::string& operator[](size_t i)const{const auto& data=LoadRogueData();return synergy?data.synergies[i]:data.tags[i];}
};
inline constexpr RogueTextTable RogueTags{false},RogueSynergies{true};
// ローグライトの状態 (レベル・経験値・選べる回数・ランク・提示中のカード・メニューの演出)
struct Roguelite {
    // 有効か・メニューを開いているか・レベル・経験値・選べる回数・引き直しの回数・選択
    bool enabled=true,menu=false; int level=1,xp=0,pending=0,rerolls=LoadRogueData().rerolls,selected=0;
    // カードごとのランクと、提示中の 3 枚
    std::array<int,RogueCardCount> ranks{}; std::array<int,3> offers{{-1,-1,-1}};
    // 乱数・待機の時間・効果の時間
    uint32_t random=0x73192u; float idle=0,haste=0,orbit=0,resonance=0,interceptClock=0,returnDelay=0;
    // メニューの演出
    float menuBlend=0,choiceDelay=0,cardAge=0;bool leaving=false;
    // 次のレベルに必要な経験値 / 経験値を得る (たまれば選べる回数を増やす)
    int Required()const{const auto& data=LoadRogueData();return data.xpBase+data.xpPerLevel*(level-1);}
    void Gain(int amount){if(!enabled||amount<=0)return;xp+=amount;while(xp>=Required()){xp-=Required();++level;++pending;}}
    void TickGameplay(float dt){returnDelay=std::max(0.f,returnDelay-dt);}
    // 強化を選べる状態か / メニューの緩急 / メニュー中の待機の姿勢
    bool Ready()const{return enabled&&pending>0&&!menu;}
    float MenuEase()const{return menuBlend*menuBlend*(3-2*menuBlend);}
    // メニュー中は待機の姿勢を揺らす
    float IdleAspect(float gameplayAspect)const{float blend=MenuEase();return gameplayAspect+(1-gameplayAspect)*blend+.045f*std::sin(idle*2.4f)*blend;}
    // カードを選べるか / メニューの時間を進める (閉じ終えたら戦闘に戻る)
    bool CanChoose()const{return menu&&!leaving&&menuBlend>=1&&choiceDelay<=0;}
    void TickMenu(float dt){idle+=dt;cardAge+=dt;choiceDelay=std::max(0.f,choiceDelay-dt);
        // 開くときは 0.65 秒、閉じるときは 0.55 秒で動かす
        menuBlend=std::clamp(menuBlend+(leaving?-dt/.55f:dt/.65f),0.f,1.f);
        if(leaving&&menuBlend<=0){menu=false;leaving=false;returnDelay=.1f;}}
    // 系統ごとのランクの合計 / 相乗効果が出ているか
    int Tags(RogueTag tag)const {int count=0;for(size_t i=0;i<ranks.size();++i)if(RogueCards[i].tag==tag)count+=ranks[i];return count;}
    bool Synergy(RogueTag tag)const{return Tags(tag)>=LoadRogueData().synergyCount;}
    // xorshift の疑似乱数
    uint32_t Random(){random^=random<<13;random^=random>>17;random^=random<<5;return random;}
    // 最大ランクに達していないカードから 3 枚を重ならないように引く
    void Roll(){cardAge=0;choiceDelay=CardRevealSeconds;offers.fill(-1);for(size_t slot=0;slot<offers.size();++slot){std::vector<int> eligible;
        for(int i=0;i<int(ranks.size());++i)if(ranks[size_t(i)]<RogueCards[size_t(i)].maxRank&&std::find(offers.begin(),offers.end(),i)==offers.end()){int weight=LoadRogueData().rarityWeights[size_t(RogueCards[size_t(i)].rarity)];for(int n=0;n<weight;++n)eligible.push_back(i);}
        if(!eligible.empty())offers[slot]=eligible[Random()%uint32_t(eligible.size())];}selected=0;}
    // メニューを開く / 引き直し / 選ぶ / 閉じる
    void Open(){menu=true;leaving=false;menuBlend=0;idle=0;Roll();cardAge=-.18f;}
    bool Reroll(){if(!CanChoose()||rerolls<=0)return false;--rerolls;Roll();return true;}
    // 選んだカードのランクを上げ、選べる回数を減らす
    bool Choose(int slot){if(!CanChoose()||slot<0||slot>=3)return false;int id=offers[size_t(slot)];if(id<0)return false;++ranks[size_t(id)];Close();return true;}
    // 次があれば引き直し、無ければメニューを閉じる
    void Close(){if(!CanChoose())return;pending=std::max(0,pending-1);choiceDelay=.25f;
        if(pending>0)Roll();else leaving=true;}
    // 移動の速さ・威力・被ダメージの倍率
    float Move()const{return std::max(.6f,1-.1f*ranks[17])*(haste>0?1+.2f*ranks[1]:1);}
    float Power(float area)const {return (1+std::min(1.f,area/4000.f)*float(ranks[4])+.5f*ranks[17])*(Synergy(RogueTag::Domain)?1.2f:1.f);}
    float Mitigation()const{return (orbit>0?1-.15f*ranks[12]:1)*(Synergy(RogueTag::Guard)?.9f:1.f);}
};
}
