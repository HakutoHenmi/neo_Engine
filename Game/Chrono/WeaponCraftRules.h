// WeaponCraftRules.h
// 変形武器づくり (準備フェーズのあと、チャレンジの前) の規則。エンジンに依存しない。
//  - 集めた物から 3 つ選んで、武器を 1 本だけ作る (どちらのチャレンジでも同じ)
//  - 3 つの物の技は、順に出るのではなく混ざって 1 つの新しい武器になる (合成)。
//    物ごとに「性質」(追尾・貫通・重い・跳ねる・爆発…) を持ち、3 つの性質を全部あわせ持った弾を撃つ。
//    例: 鉛筆 (貫通・速い) + ボウリングの玉 (転がる・重い) + 紙飛行機 (追尾) = 重い玉が転がって追いかけ、突き抜ける
//    同じ性質が重なると強くなる (性質のレベル)。1 つ目の物 (本体) は、撃ち出す物の見た目と、体の変形の形の土台になる
//  - 体の形は 3 つの物のモデルを組み合わせて作るので、材料が 1 つでも違えば (並べる順が違っても) 違う形になる
//  - 回数の制限はない。枠に入れなかった残りの物は強化 (弾数・威力・連射) の点になり、大きい物ほど点が高い。
//    強化の伸びは大きく、1 つに極端に振ると 1 回で何十発・巨大な弾・機関銃のような連射になる (上限は動きが重くならない所で止める)
#pragma once
#include "DevourBattleRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace Game::Chrono {
inline constexpr int CraftParts=3;
// 作る画面が開いているか (開いている間は、プレイ中でもマウスカーソルを真ん中に固定しない: GameScene)
inline bool& CraftScreenOpen(){static bool open=false;return open;}
inline const char* CraftPartRole(int part){static constexpr const char* names[]={"本体","材料","材料"};return names[std::clamp(part,0,CraftParts-1)];}

// ---- 性質 ----
// Homing 追いかける・Pierce 突き抜ける・Bounce 跳ねる・Roll 床を転がる・Heavy 重い (大きく強いが遅い)・Split 当たると分かれる・
// Explode 当たる/落ちると爆発・Chain 次の相手へ跳び移る・Spin 回って当たりが広い・Stun 当てた相手を止める・Burn 熱い (やけどと広がる体液)・
// Magnet まわりを吸い寄せる・Speed 速い・Paint 通った跡もたっぷり塗る・Wall 止まった所に壁 (積み木の塔) を建てる・Float ふわふわ長く浮く・
// Weaken 当てた相手を弱らせる・Return ブーメランのように戻ってくる
enum class FuseTrait : uint8_t {Homing,Pierce,Bounce,Roll,Heavy,Split,Explode,Chain,Spin,Stun,Burn,Magnet,Speed,Paint,Wall,Float,Weaken,Return,Count};
inline constexpr int FuseTraitCount=int(FuseTrait::Count);
inline const char* FuseTraitName(FuseTrait t){static constexpr const char* names[]={"追尾","貫通","跳ねる","転がる","重い","分裂","爆発","連鎖","回転","止める","熱い","吸い寄せ","速い","塗り","壁","ふわふわ","弱らせる","戻る"};return names[std::min(int(t),FuseTraitCount-1)];}
// 武器の名前に付ける言葉
inline const char* FuseTraitWord(FuseTrait t){static constexpr const char* words[]={"追いかける","突き抜ける","跳ねる","転がる","重い","分かれる","爆発する","跳び移る","回る","止める","熱い","吸い寄せる","速い","たっぷり塗る","壁を建てる","ふわふわ","弱らせる","戻ってくる"};return words[std::min(int(t),FuseTraitCount-1)];}
// 物の技ごとの性質 (2 つまで。Count は無し)。ふつうに投げる物は性質なし (本体の大きさだけ効く)
inline std::array<FuseTrait,2> FuseTraitsOf(BattleAction a){
    using T=FuseTrait;constexpr T N=T::Count;
    switch(a){
    case BattleAction::Ride:return {T::Speed,T::Roll};case BattleAction::Rc:return {T::Speed,T::Homing};case BattleAction::Fly:return {T::Homing,T::Speed};
    case BattleAction::Smash:return {T::Heavy,T::Explode};case BattleAction::Guard:return {T::Bounce,T::Wall};case BattleAction::Heal:return {T::Paint,T::Float};
    case BattleAction::Heli:return {T::Return,T::Float};case BattleAction::Water:return {T::Paint,T::Speed};case BattleAction::Roll:return {T::Roll,T::Heavy};
    case BattleAction::Spin:return {T::Spin,N};case BattleAction::Sword:return {T::Pierce,T::Spin};case BattleAction::Spring:return {T::Bounce,N};
    case BattleAction::Vacuum:return {T::Magnet,N};case BattleAction::Fu:return {T::Pierce,N};case BattleAction::Kyo:return {T::Pierce,T::Speed};
    case BattleAction::Kei:return {T::Bounce,T::Split};case BattleAction::Gin:return {T::Split,N};case BattleAction::Kin:return {T::Split,T::Heavy};
    case BattleAction::Kaku:return {T::Pierce,T::Split};case BattleAction::Hi:return {T::Pierce,T::Speed};case BattleAction::Ou:return {T::Explode,T::Heavy};
    case BattleAction::Marbles:return {T::Split,T::Roll};case BattleAction::Tower:return {T::Wall,N};case BattleAction::Domino:return {T::Chain,T::Wall};
    case BattleAction::Paint:return {T::Paint,N};case BattleAction::Decoy:return {T::Explode,T::Magnet};case BattleAction::Coin:return {T::Chain,T::Speed};
    case BattleAction::Disc:return {T::Roll,T::Bounce};case BattleAction::Bounce:return {T::Bounce,N};case BattleAction::Homing:return {T::Homing,N};
    case BattleAction::Fan:return {T::Split,T::Speed};case BattleAction::Pierce:return {T::Pierce,T::Speed};case BattleAction::Pause:return {T::Stun,N};
    case BattleAction::Read:return {T::Stun,T::Heavy};case BattleAction::Cage:return {T::Stun,T::Magnet};case BattleAction::Sticky:return {T::Stun,N};
    case BattleAction::Erase:return {T::Weaken,N};case BattleAction::Tea:return {T::Burn,N};case BattleAction::Roly:return {T::Bounce,T::Heavy};
    case BattleAction::Beckon:return {T::Magnet,N};case BattleAction::Brew:return {T::Burn,T::Paint};case BattleAction::Fluff:return {T::Float,N};
    case BattleAction::Smell:return {T::Burn,T::Float};
    default:return {N,N};}
}
using FuseLevels=std::array<int,FuseTraitCount>;
inline FuseLevels FuseLevelsOf(const std::array<BattleAction,CraftParts>& actions,int filled){
    FuseLevels lv{};for(int i=0;i<filled;++i)for(FuseTrait t:FuseTraitsOf(actions[size_t(i)]))if(t!=FuseTrait::Count)++lv[size_t(t)];return lv;}
// 武器の名前: 強い順に性質の言葉を 3 つまで + 本体の名前 (性質が無ければ本体の名前だけ)
inline std::string FuseWeaponName(const FuseLevels& lv,const std::string& body){
    std::vector<int> order;for(int t=0;t<FuseTraitCount;++t)if(lv[size_t(t)]>0)order.push_back(t);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return lv[size_t(a)]>lv[size_t(b)];});
    std::string name;for(size_t i=0;i<order.size()&&i<3;++i)name+=(i?"・":"")+std::string(FuseTraitWord(FuseTrait(order[i])));
    return name.empty()?body:name+" "+body;
}

// ---- 弾の強さ (性質のレベルで決まる。unit は戦闘の大きさの基準) ----
// 速さ (unit/秒)・残る秒・大きさ (半径。unit に対する倍率)・当たり・当たれる数
inline constexpr float FuseSpeed=14,FuseSpeedStep=.35f,FuseHeavySlow=.22f,FuseFloatSlow=.45f;
inline constexpr float FuseLife=2.6f,FuseFloatLife=1.6f,FuseBounceLife=.8f,FuseReturnLife=1.2f;
inline constexpr float FuseRadius=.32f,FuseHeavyGrow=.4f,FuseSpinGrow=.45f;
inline constexpr float FuseDamageHeavy=.6f;
inline constexpr int FusePierceHits=3,FuseBounces=3,FuseChainHops=3,FuseSplitPieces=3;
// 追尾の曲がる速さ (ラジアン/秒、レベルごと)・爆発の半径 (unit)・止める秒・吸い寄せの半径 (unit)・塗りの跡の間隔 (秒)・重力 (unit/秒^2)
inline constexpr float FuseHomingTurn=3.f,FuseExplodeReach=1.6f,FuseExplodeStep=1.1f,FuseStunTime=1.3f,FuseMagnetReach=3.5f,FusePaintEvery=.07f,FuseGravity=9;
// 分かれた弾の大きさ・場に出せる弾の数の上限 (重くならないように)
inline constexpr float FuseSplitScale=.6f;inline constexpr int FuseShotLimit=220;

// ---- 材料の点と強化 ----
// 材料の点: 物の大きさ (半径。unit に対する割合) で 1〜3 点
inline constexpr float CraftValueMid=.45f,CraftValueBig=.9f;
inline int CraftValue(float radius,float unit){const float r=radius/std::max(.01f,unit);return r<CraftValueMid?1:r<CraftValueBig?2:3;}
// 強化の種類: 弾数 (1 回で撃つ数)・威力 (弾の大きさ・当たり・塗る量)・連射 (撃つ間隔)
enum class CraftBoost : uint8_t {Shots,Power,Rate,Count};
inline constexpr int CraftBoostCount=int(CraftBoost::Count);
inline const char* CraftBoostName(int boost){static constexpr const char* names[]={"弾数","威力","連射"};return names[std::clamp(boost,0,CraftBoostCount-1)];}
// 弾数: 3 点ごとに 1 発増える (最大 40 発)。威力: 1 点ごとに +10% (最大 8 倍)。連射: 1 点ごとに間隔が縮む (最短 0.04 秒)
inline constexpr int CraftShotsMax=40;inline constexpr float CraftPowerStep=.1f,CraftPowerMax=8,CraftIntervalBase=.55f,CraftRateStep=.14f,CraftIntervalMin=.04f;
inline int CraftShots(int points){return std::min(CraftShotsMax,1+std::max(0,points)/3);}
inline float CraftPower(int points){return std::min(CraftPowerMax,1+CraftPowerStep*float(std::max(0,points)));}
inline float CraftInterval(int points,int heavy){return std::max(CraftIntervalMin,CraftIntervalBase*(1+.25f*float(heavy))/(1+CraftRateStep*float(std::max(0,points))));}
// 何発も撃つときの扇の広がり (ラジアン、1 発ごと・最大)
inline constexpr float CraftSpreadStep=.11f,CraftSpreadMax=1.4f;
// 体が武器の形でいる秒 (撃つたびに延びる)
inline constexpr float CraftFormTime=.5f;
// (F5) 形が出てくる (ぽんと大きくなる) 秒・1 発の反動 (前後に縮む割合)
// (CraftFlap は翼のはばたきの角度 (ラジアン)。後ろからのカメラでも見えるよう大きめ)
inline constexpr float CraftPopTime=.28f,CraftKick=.14f,CraftFormHold=.4f,CraftFlap=.55f;

// ---- 形の組み合わせ方 (形の座標。先が +z、下が y=0) ----
// 本体はモデルの一番長い辺を CraftBaseSize にそろえる。材料の部品は本体に対する大きさ
//  2 つ目: 本体の前 (+z) の上の方に、少し重ねて付ける
//  3 つ目: 本体の左右に 1 つずつ (左は鏡に映す)、高さの真ん中に付ける
inline constexpr float CraftBaseSize=1,CraftFrontScale=.72f,CraftSideScale=.56f,CraftOverlap=.22f,CraftFrontHeight=.55f,CraftSideHeight=.45f;
// (F2) なめらかな基本形で作るとき: 太さの下限 (最後の大きさ。F1 で測った「液体の表面で消える太さ」1.6 より少し太く)・
// つなぎ目の丸み (本体の一番長い辺を 1 とした長さ)・前の部品を本体の高さの何割上げるか・左右の部品を本体の長さの何割後ろへずらすか
inline constexpr float CraftMinThick=1.9f,CraftBlend=.06f,CraftFrontLift=.25f,CraftSideBack=.15f;
// 組み合わせた形の体積 (粒子の数は決まっているので、どの形もこの体積にそろえる)・形を作る箱の細かさ (一番長い辺を何個に分けるか)
inline constexpr float CraftShapeVolume=30;
inline constexpr int CraftItemCells=12,CraftShapeCells=44;
}
