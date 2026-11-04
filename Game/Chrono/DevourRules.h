// DevourRules.h
// 吸収モード (敵なし。制限時間内に物を飲み込んで大きくなり、街を飲み込む) の規則。エンジンに依存しない。
// 体の大きさは SlimeLifeRules.h の LifeScale (量の平方根) をそのまま使い、成長の量も同じ割合で決める。
#pragma once
#include "SlimeLifeRules.h"
namespace Game::Chrono {
// 吸収モードの最大の量・制限時間・段階が上がったときに足す時間
inline constexpr float DevourMaxMass=6e6f,DevourTimeLimit=300,DevourTierBonus=15;
// 飛びかかりの時間の区切り: 溜め (ぐっ)・口を開けて飛び込む (ズバーッ)・包んで閉じる (ガバッ)・満足 (ぷるん)
inline constexpr float DevourWindup=.08f,DevourLeap=.2f,DevourCover=.14f,DevourSettle=.34f;
inline constexpr float DevourLandAt=DevourWindup+DevourLeap,DevourCoverEnd=DevourLandAt+DevourCover,DevourPounceTime=DevourCoverEnd+DevourSettle;
// 覆える物の大きさ (体の半径に対する割合)・触れるだけで沈む物の大きさ・開始時の体の半径
// (路地の段ボール・木箱・ゴミ袋は体と同じくらいの大きさなので、最初から覆えるように 0.8 にしている)
inline constexpr float DevourCoverRatio=.8f,DevourSinkRatio=.2f,DevourStartRadius=3;
// タイヤで転がっている間と突進中に沈む大きさ
inline constexpr float DevourTireSinkRatio=.55f,DevourTireDashSinkRatio=.7f;
// 大きさの段階 (必要な量・名前・食べられるようになる物)。時間切れの大きさで競い、段階の名前が称号になる。
// 最後の段階に届いても終わらず、時間切れまで大きくなれる
struct DevourTier {float mass;const char* name;const char* food;};
// (輪の町の建物は、覆うのに必要な量が 家 約 0.6〜1.4 万・店 約 4.5〜6 万・オフィス 約 10〜24 万・高層ビル 約 53〜165 万。
//  最後の段階は、町でいちばん高いビルを飲める大きさ)
inline constexpr std::array<DevourTier,10> DevourTiers{{
    {100,"ちびスライム","ゴミ袋・段ボール"},
    {300,"こぶりスライム","木箱・植木鉢"},
    {1000,"ひとかかえスライム","ベンチ・ゴミ箱・像"},
    {3000,"くるまサイズ","車・小さな木"},
    {8000,"いえサイズ","2〜3 階の家"},
    {45000,"みせサイズ","4〜6 階の店"},
    {120000,"オフィスサイズ","8〜12 階のオフィス"},
    {400000,"高層ビルサイズ","18 階からの高層ビル"},
    {900000,"超高層スライム","町いちばんの高層ビル"},
    {1600000,"街を飲み込むスライム","高層街のビルを次々と"}}};
inline constexpr int DevourFinalTier=int(DevourTiers.size())-1;
// 量から今の段階を求める
inline int DevourTierAt(float mass){int tier=0;while(tier<DevourFinalTier&&mass>=DevourTiers[size_t(tier+1)].mass)++tier;return tier;}
// 体の半径と、覆える物・沈む物の大きさ
inline float DevourBodyRadius(float mass){return DevourStartRadius*LifeScale(mass);}
inline bool DevourCanCover(float mass,float radius){return radius<=DevourBodyRadius(mass)*DevourCoverRatio;}
inline bool DevourSinks(float mass,float radius,float ratio=DevourSinkRatio){return radius<=DevourBodyRadius(mass)*ratio;}
// 画面に出す体の大きさ (m)
inline float DevourSizeMeters(float mass){return 2*DevourBodyRadius(mass)/6;}
// 半径 radius の物を覆うのに必要な量 (案内の表示に使う)
inline float DevourMassToCover(float radius){float scale=std::max(1.f,radius/(DevourStartRadius*DevourCoverRatio));return LifeStartMass*scale*scale;}
// 砕ける大きさ (覆える大きさの 1.5 倍まで)・欠片の数・欠片全部の成長の量 (元の物に対する割合)・砕ける溜め突進の強さ。
// 育ってから丸のみするほうが得になるよう、砕くと量は減る
inline constexpr float DevourShatterRatio=1.5f,DevourShardShare=.6f,DevourTireSmashPower=.5f;
inline constexpr int DevourShardCount=6;
inline bool DevourCanShatter(float mass,float radius){return !DevourCanCover(mass,radius)&&radius<=DevourBodyRadius(mass)*DevourCoverRatio*DevourShatterRatio;}
inline float DevourMassToShatter(float radius){return DevourMassToCover(radius/DevourShatterRatio);}
// 物の大きさから成長の量。体と同じく半径の 2 乗で増え、詰まっていない分だけ少なくする。
// (覆える一番大きい物を食べると、体の量の約 4.5% 増える。DevourGrowth で全体の育つ速さを調整する。
//  約 5 分で高層街まで届く速さにしている)
inline constexpr float DevourGrowth=7;
inline float DevourReward(float radius){float k=radius/DevourStartRadius;return DevourGrowth*k*k;}
// 大きさに合わせた移動の速さの倍率 (体の大きさの 0.55 乗。大きいほど体に対してはゆっくりで、町を横切るのに時間がかかる)
inline float DevourSpeedScale(float mass){return std::pow(std::max(1.f,LifeScale(mass)),.55f);}
// 飛びかかって覆う広さ (体より少し広げる) と、飛びかかれる距離 (体の半径の 2.5 倍)。
// 飛びかかりは 1 個〜数個の物を狙う動きで、小物の群れはタイヤ、少し大きい物はハンマーの役目
inline float DevourCoverRadius(float mass){return 1.1f*DevourBodyRadius(mass);}
inline float DevourLeapRange(float mass){return 7.5f*LifeScale(mass);}
// 着地した位置で覆いかぶされるか (reach は塀のように平たく長い物の分の余裕)
inline bool DevourUnderCover(Vec landing,Vec at,float mass,float radius,float reach=0){
    return LifeDistance(landing,at)<=DevourCoverRadius(mass)+radius*.5f+reach&&std::abs(at.y-landing.y)<3*LifeScale(mass)+radius;
}
// 飛びかかりの目標: 前の半分 (真横まで) にある覆える物を、近く正面の順に選ぶ。無ければ近くの大きすぎる物 (弾かれる)、それも無ければ正面へ跳ぶ
struct DevourPick {int index=-1;Vec at{};bool fits=false;};
inline DevourPick DevourChooseTarget(Vec from,Vec forward,float mass,const std::vector<LifeCollectible>& props){
    DevourPick best,blocked;float bestScore=1e9f,blockedScore=1e9f,range=DevourLeapRange(mass);
    for(size_t i=0;i<props.size();++i){const auto& prop=props[i];if(!prop.active||prop.sealed)continue;
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
// 飛び込んでから包み終えるまでに進んだ割合 (勢いよく飛び出し、口で包みながら目標の上で止まる)。
// 体は地面に着いたまま進むので上には上がらない
inline float DevourLeapProgress(float age){float u=std::clamp((age-DevourWindup)/(DevourLeap+DevourCover),0.f,1.f);return 1-(1-u)*(1-u);}
// 飛びかかり中に体の粒子を形の点へ引くばねの強さ (毎秒の角速度)。
// wall はその粒子が開いた口の壁 (空洞を囲む所) である割合 (0〜1、DevourPose が返す。口を閉じると 0)。
// 強いと粒子が形の点に貼りついて固い作り物に見えるので、口の壁だけを強めて穴が開かないように形を保ち、
// 尾と、口を開けていない間の体はごく弱くして、普段の液体と同じように揺れ、遅れて流れるようにする
inline constexpr float DevourBodyStiffness=6;
inline float DevourStiffness(float age,float wall){
    auto smooth=[](float a,float b,float x){float u=std::clamp((x-a)/(b-a),0.f,1.f);return u*u*(3-2*u);};
    float mouth=12+10*smooth(DevourWindup,DevourLandAt,age)-14*smooth(DevourCoverEnd,DevourPounceTime,age);
    return DevourBodyStiffness+std::max(0.f,mouth-DevourBodyStiffness)*std::clamp(wall,0.f,1.f);
}
// 普段の液体から飛びかかりの形への移り (0 今の粒子の位置のまま〜1 形の点)。
// 飛びかかりの始めに粒子を一度に形の点へ引くと、液体が急に固い形にはまって見えるので、
// 各粒子の今の位置から形の点へ、口を開けきるまでにゆっくり寄せる
inline float DevourFormBlend(float age){
    float u=std::clamp(age/(DevourWindup+DevourLeap*.7f),0.f,1.f);return u*u*(3-2*u);
}
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
// 飲み込んだ量はいったん体の中に溜め、約 0.4 秒かけて体へ移す (一瞬で大きくならず、むくっと膨らむ)。
// 体の量が少しずつ増えるので、カメラの引きとゲージも同じ速さで動く
inline constexpr float DevourDigestRate=7;
// 溜めに足す (溜めの分も含めて上限まで)。足した量を返す
inline float DevourSwallow(float mass,float& pending,float amount){
    float gain=std::clamp(amount,0.f,std::max(0.f,DevourMaxMass-mass-pending));pending+=gain;return gain;
}
// 溜めから体へ移す (残りがわずかなら全部移す)
inline void DevourDigest(LifeBody& body,float& pending,float dt){
    if(pending<=0)return;float step=pending<=body.mass*1e-3f?pending:pending*(1-std::exp(-DevourDigestRate*dt));
    DevourGrow(body,step);pending-=step;
}
// 飲み込みの重さ (0.2〜1): 数と、体に対して増えた割合の大きい方。覆える一番大きい物 1 つか 5 個で最大
inline float DevourGulpWeight(int count,float gained,float mass){return std::clamp(std::max(.2f*float(count),gained/std::max(1.f,mass)/.16f),.2f,1.f);}
// 飲み込んだ後の体のつぶれ (正でつぶれ、負で伸びる): 一気につぶれ、揺れながら戻る
inline float DevourSquash(float age,float weight){
    constexpr float down=.07f;if(age<0||age>=.7f)return 0;float peak=.12f+.22f*weight;
    if(age<down){float u=age/down;return peak*u*(2-u);}
    float t=age-down;return peak*std::exp(-7*t)*std::cos(14*t);
}
// 飲み込んだ後にカメラを少し引く量 (映す半径に対する割合)
inline float DevourCameraKick(float age,float weight){return .18f*weight*std::exp(-3.5f*age);}
// 炎などで体が剥がれる量 (開始の大きさより小さくはならない)
inline float DevourShed(float mass,float damage){return std::clamp(mass*.03f+damage,0.f,std::max(0.f,mass-LifeStartMass));}
}
