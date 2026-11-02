// DevourRules.h
// 吸収モード (敵なし。制限時間内に物を飲み込んで大きくなり、街を飲み込む) の規則。エンジンに依存しない。
// 体の大きさは SlimeLifeRules.h の LifeScale (量の平方根) をそのまま使い、成長の量も同じ割合で決める。
#pragma once
#include "SlimeLifeRules.h"
namespace Game::Chrono {
// 吸収モードの最大の量・制限時間・段階が上がったときに足す時間
inline constexpr float DevourMaxMass=6e6f,DevourTimeLimit=480,DevourTierBonus=30;
// 飛びかかりの時間の区切り: 溜め (ぐっ)・跳ぶ (ズバーッ)・覆う (ガバッ)・満足 (ぷるん)
inline constexpr float DevourWindup=.1f,DevourLeap=.2f,DevourCover=.12f,DevourSettle=.36f;
inline constexpr float DevourLandAt=DevourWindup+DevourLeap,DevourCoverEnd=DevourLandAt+DevourCover,DevourPounceTime=DevourCoverEnd+DevourSettle;
// 覆える物の大きさ (体の半径に対する割合)・触れるだけで沈む物の大きさ・開始時の体の半径
// (路地の段ボール・木箱・ゴミ袋は体と同じくらいの大きさなので、最初から覆えるように 0.8 にしている)
inline constexpr float DevourCoverRatio=.8f,DevourSinkRatio=.3f,DevourStartRadius=3;
// 大きさの段階 (必要な量・名前・食べられるようになる物)。最後の段階に届けばクリア
struct DevourTier {float mass;const char* name;const char* food;};
// (輪の町の建物は、覆うのに必要な量が 家 約 0.6〜1.4 万・店 約 4.5〜6 万・オフィス 約 10〜24 万・高層ビル 約 53〜165 万。
//  高層ビル 1 棟で増える量は約 3〜9 万なので、最後の段階には高層街のビルを何十棟も飲み込んで届く)
inline constexpr std::array<DevourTier,10> DevourTiers{{
    {100,"ちびスライム","ゴミ袋・段ボール"},
    {300,"こぶりスライム","木箱・植木鉢"},
    {1000,"ひとかかえスライム","ベンチ・ゴミ箱・像"},
    {3000,"くるまサイズ","車・小さな木"},
    {8000,"いえサイズ","2〜3 階の家"},
    {45000,"みせサイズ","4〜6 階の店"},
    {120000,"オフィスサイズ","8〜12 階のオフィス"},
    {550000,"高層ビルサイズ","18 階からの高層ビル"},
    {1800000,"超高層スライム","町いちばんの高層ビル"},
    {4000000,"街を飲み込むスライム","高層街のビルを次々と"}}};
inline constexpr int DevourFinalTier=int(DevourTiers.size())-1;
// 量から今の段階を求める
inline int DevourTierAt(float mass){int tier=0;while(tier<DevourFinalTier&&mass>=DevourTiers[size_t(tier+1)].mass)++tier;return tier;}
// 体の半径と、覆える物・沈む物の大きさ
inline float DevourBodyRadius(float mass){return DevourStartRadius*LifeScale(mass);}
inline bool DevourCanCover(float mass,float radius){return radius<=DevourBodyRadius(mass)*DevourCoverRatio;}
inline bool DevourSinks(float mass,float radius){return radius<=DevourBodyRadius(mass)*DevourSinkRatio;}
// 半径 radius の物を覆うのに必要な量 (案内の表示に使う)
inline float DevourMassToCover(float radius){float scale=std::max(1.f,radius/(DevourStartRadius*DevourCoverRatio));return LifeStartMass*scale*scale;}
// 物の大きさから成長の量。体と同じく半径の 2 乗で増え、詰まっていない分だけ少なくする。
// (覆える一番大きい物を食べると、体の量の約 16% 増える。DevourGrowth で全体の育つ速さを調整する)
inline constexpr float DevourGrowth=25;
inline float DevourReward(float radius){float k=radius/DevourStartRadius;return DevourGrowth*k*k;}
// 大きさに合わせた移動の速さの倍率 (体の大きさの 0.75 乗。大きいほど体に対しては少しゆっくり)
inline float DevourSpeedScale(float mass){return std::pow(std::max(1.f,LifeScale(mass)),.75f);}
// 飛びかかって覆う広さ (体より広げる) と、飛びかかれる距離
inline float DevourCoverRadius(float mass){return 1.5f*DevourBodyRadius(mass);}
inline float DevourLeapRange(float mass){return 12*LifeScale(mass);}
// 着地した位置で覆いかぶされるか (reach は塀のように平たく長い物の分の余裕)
inline bool DevourUnderCover(Vec landing,Vec at,float mass,float radius,float reach=0){
    return LifeDistance(landing,at)<=DevourCoverRadius(mass)+radius*.5f+reach&&std::abs(at.y-landing.y)<3*LifeScale(mass)+radius;
}
// 飛びかかりの目標: 前の半分 (真横まで) にある覆える物を、近く正面の順に選ぶ。無ければ近くの大きすぎる物 (弾かれる)、それも無ければ正面へ跳ぶ
struct DevourPick {int index=-1;Vec at{};bool fits=false;};
inline DevourPick DevourChooseTarget(Vec from,Vec forward,float mass,const std::vector<LifeCollectible>& props){
    DevourPick best,blocked;float bestScore=1e9f,blockedScore=1e9f,range=DevourLeapRange(mass);
    for(size_t i=0;i<props.size();++i){const auto& prop=props[i];if(!prop.active)continue;
        Vec delta=prop.at-from;delta.y=0;float distance=Length(delta)-prop.reach;
        if(distance>range+prop.radius||std::abs(prop.at.y-from.y)>4*LifeScale(mass)+prop.radius)continue;
        float facing=Length(delta)>.5f?Dot(Unit(delta),forward):1;if(facing<-.05f)continue;
        float score=std::max(0.f,distance)+range*.5f*(1-facing);
        if(DevourCanCover(mass,prop.radius)){if(score<bestScore){bestScore=score;best={int(i),prop.at,true};}}
        // 大きすぎる物は、表面まで跳んで届くときだけ弾かれる目標にする
        else if(distance-prop.radius*.5f<range&&score<blockedScore){blockedScore=score;blocked={int(i),prop.at,false};}
    }
    if(best.index>=0)return best;
    if(blocked.index>=0)return blocked;
    DevourPick ahead;ahead.at=from+forward*(range*.35f);return ahead;
}
// 跳んでから覆い終えるまでに進んだ割合 (勢いよく飛び出し、覆いながら目標の上で止まる)。
// 跳ぶ間は体の上の方が先に物の上へ届き、根元は覆う間に追いつく。体は地面に着いたまま進むので上には上がらない
inline float DevourLeapProgress(float age){float u=std::clamp((age-DevourWindup)/(DevourLeap+DevourCover),0.f,1.f);return 1-(1-u)*(1-u);}
// 制限時間と段階: 段階が上がるたびに時間を足す。足した段階の数を返す
struct DevourClock {float left=DevourTimeLimit;int tier=0,combo=0;float comboClock=0,tierFlash=0;};
inline int DevourAdvance(DevourClock& clock,float mass){
    int tier=DevourTierAt(mass),gained=tier-clock.tier;if(gained<=0)return 0;
    clock.tier=tier;clock.left+=DevourTierBonus*float(gained);clock.tierFlash=3;return gained;
}
// 量を足す (上限まで)。実際に増えた量を返す
inline float DevourGrow(LifeBody& body,float amount){
    float gain=std::clamp(amount,0.f,std::max(0.f,DevourMaxMass-body.mass));body.mass+=gain;body.maxHealth+=gain;body.health+=gain;return gain;
}
// 炎などで体が剥がれる量 (開始の大きさより小さくはならない)
inline float DevourShed(float mass,float damage){return std::clamp(mass*.03f+damage,0.f,std::max(0.f,mass-LifeStartMass));}
}
