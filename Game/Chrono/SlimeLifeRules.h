// SlimeLifeRules.h
// スライムの生活モードの規則 (調合・体の動き・取り込み・成長・被弾と分裂)。エンジンに依存しない。
#pragma once
#include "ChronoRules.h"
#include <array>
#include <cstdint>
#include <vector>
#include <string>
namespace Game::Chrono {
// 開始と最大の量・変形に必要な量・接触吸収できる量・体の数 (本体 1 + 子 12)
inline constexpr float LifeStartMass=100,LifeMaxMass=6000;
inline constexpr float LifeInitialGroupMass=LifeStartMass;
inline constexpr float LifeHammerMass=180,LifeSwordMass=300,LifePlaneMass=480;
inline constexpr float LifeContactAbsorbMass=180;
inline constexpr size_t LifeBodyCount=13;
// 子分の行動範囲: 本体から離れて狩れる距離・索敵の距離・本体へ戻り始める距離・見失って呼び戻す距離・撤退する体力の割合
inline constexpr float LifeChildHuntRange=48,LifeChildSight=36,LifeChildLeash=62,LifeChildLostRange=120,LifeChildRetreatHealth=.35f;
// 体の行動 (待機・取り込み・各武器・回復) と、変形を解放するアイテム
enum class LifeAction {Idle,Engulf,Hammer,Sword,Plane,Tire,Hydrant,Restore,Signal};
enum class LifeMorphItem {None,Tire,Hydrant,Signal};
// 変形に必要な量 / 量が足りるか
inline float LifeRequiredMass(LifeAction action){
    return action==LifeAction::Hammer?LifeHammerMass:action==LifeAction::Sword?LifeSwordMass:action==LifeAction::Plane?LifePlaneMass:(action==LifeAction::Tire||action==LifeAction::Hydrant||action==LifeAction::Signal)?1.f:0;
}
inline bool LifeCanMorph(float mass,LifeAction action){
    return LifeRequiredMass(action)>0&&mass>=LifeRequiredMass(action);
}
// スライムの色 (HSV)。RGB との変換と、古い保存形式の色
struct SlimeColor {
    float h=.26190476f,s=.875f,v=.8f;
    // HSV から RGB へ変換する
    Vec ToRGB()const {
        float hue=(h-std::floor(h))*6,sv=std::clamp(s,0.f,1.f),value=std::clamp(v,0.f,1.f),f=hue-std::floor(hue);
        float p=value*(1-sv),q=value*(1-sv*f),t=value*(1-sv*(1-f));
        switch(int(hue)%6){case 0:return {value,t,p};case 1:return {q,value,p};case 2:return {p,value,t};case 3:return {p,q,value};case 4:return {t,p,value};default:return {value,p,q};}
    }
    // RGB から HSV へ変換する
    static SlimeColor FromRGB(Vec rgb){
        rgb={std::clamp(rgb.x,0.f,1.f),std::clamp(rgb.y,0.f,1.f),std::clamp(rgb.z,0.f,1.f)};
        float hi=std::max({rgb.x,rgb.y,rgb.z}),lo=std::min({rgb.x,rgb.y,rgb.z}),d=hi-lo;SlimeColor out;out.v=hi;out.s=hi>0?d/hi:0;out.h=0;
        if(d>1e-6f){out.h=(hi==rgb.x?(rgb.y-rgb.z)/d:hi==rgb.y?2+(rgb.z-rgb.x)/d:4+(rgb.x-rgb.y)/d)/6;out.h-=std::floor(out.h);}return out;
    }
    // 古い番号の色
    static SlimeColor Legacy(int index){const Vec colors[]={{.4f,.8f,.1f},{.12f,.65f,.95f},{.9f,.28f,.58f},{.65f,.32f,.95f},{.95f,.64f,.12f},{.25f,.85f,.65f}};return FromRGB(colors[std::clamp(index,0,5)]);}
};
// 調合のレシピ (水・ホウ砂水・洗濯のり・泡の割合と色)。割合から各性能を計算する
struct SlimeRecipe {
    // 材料の配合 (水・ホウ砂水・洗濯のり・泡)
    std::array<int,4> mix{40,35,25,0};
    SlimeColor color;
    // 材料の割合 (合計に対する)
    float Ratio(int i)const {return float(mix[size_t(i)])/float(std::max(1,mix[0]+mix[1]+mix[2]+mix[3]));}
    // 移動の速さ・加速・減速・重力・跳ねる高さ・反発
    float Speed()const {return 1.2f*(8+20*Ratio(0)-2*Ratio(2)+6*Ratio(3));}
    float Acceleration()const {return 7+13*Ratio(0)-2*Ratio(2)+3*Ratio(1);}
    float Braking()const {return 10-6*Ratio(0)+5*Ratio(2);}
    float Gravity()const {return 30-16*Ratio(3);}
    float Bounce()const {return 8+16*Ratio(1)+7*Ratio(3);}
    float Rebound()const {return Ratio(1)>=.5f?3+4*Ratio(1):0;}
    // 取り込みの時間・距離・正面の広さ・つかみ続ける余裕
    float AbsorbDuration()const {return .48f-.14f*Ratio(0)-.30f*Ratio(2)+.06f*Ratio(1);}
    float AbsorbRange()const {return 8.f*(1+.85f*Ratio(0));}
    float AbsorbDot()const {return .35f-.23f*Ratio(0);}
    float AbsorbHold()const {return 1+2*Ratio(2);}
    // 踏みつけのダメージと範囲
    float StompDamage()const {return 12+42*Ratio(1);}
    float StompRadius()const {return 3.2f+4.8f*Ratio(1);}
    // 被ダメージの倍率・分裂の量・子の速さ・子の攻撃
    float DamageMultiplier()const {return 1+.35f*(Ratio(0)-.4f)+.8f*Ratio(3)-.4f*(Ratio(2)-.25f);}
    float SplitCap()const {return 20+12*(Ratio(0)-.4f)+10*Ratio(3)-8*(Ratio(2)-.25f);}
    float SplitRatio()const {return .25f+.18f*(Ratio(0)-.4f)+.12f*Ratio(3)-.1f*(Ratio(2)-.25f);}
    float ChildSpeed()const {return Speed()*(1+1.3f*Ratio(3));}
    float ChildCooldown()const {return .95f/(1+2.5f*Ratio(3));}
    float ChildDamage()const {return 6+5*Ratio(3);}
    // 一番多い材料で決まる型・名前・得意・注意
    int Style()const {int best=0;for(int i=1;i<4;++i)if(mix[size_t(i)]>mix[size_t(best)])best=i;return Ratio(best)>=.5f?best:-1;}
    const char* StyleName()const {switch(Style()){case 0:return "さらさら回収型";case 1:return "ぷるぷる踏みつけ型";case 2:return "ねばねば本体型";case 3:return "ふわふわ群れ型";default:return "バランス型";}}
    const char* Strength()const {switch(Style()){case 0:return "得意：高速移動・広い取り込み";case 1:return "得意：高いジャンプ・広く強い踏みつけ";case 2:return "得意：素早く包む・被弾時に本体を保つ";case 3:return "得意：長い滞空・子が速く何度も攻撃";default:return "得意：移動・取り込み・踏みつけを両立";}}
    const char* Weakness()const {switch(Style()){case 0:return "注意：止まりにくい・被弾で大きくちぎれる";case 1:return "注意：取り込みが遅い・着地で一度弾む";case 2:return "注意：移動と切り返しが重い・子は小さい";case 3:return "注意：本体が傷つきやすく吹き飛ばされる";default:return "材料を半分以上にすると個性が強くなる";}}
    // 液体の見た目の値 (広がり・粘性・減衰・まとまり・表面張力・揺れ)
    float Spread()const {return 1+.6f*Ratio(0)-.25f*Ratio(1)-.15f*Ratio(2)+.3f*Ratio(3);}
    float Viscosity()const {return 3+105*Ratio(2)+28*Ratio(1);}
    float Damping()const {return 1.5f+7*Ratio(2)+2*Ratio(1);}
    float Cohesion()const {return 30+65*Ratio(1)+25*Ratio(2);}
    float SurfaceTension()const {return 8+45*Ratio(2)+18*Ratio(1);}
    float Slosh()const {return 1.2f+1.6f*Ratio(0)-.65f*Ratio(2)-.7f*Ratio(1);}
};
// 武器の攻撃の状態 (時間・先行入力・攻撃中か・形ができたか)
struct LifeWeaponAttack {
    // 攻撃の経過時間・前フレームの経過時間・先行入力
    float age=0,previousAge=0,buffer=0;
    bool active=false,ready=false;
    // タイヤ: その場で溜めた回転 (0〜1)・突進の残り時間・突進の強さ・溜めているか
    float charge=0,dash=0,dashPower=0;
    bool charging=false;
};
// 触手でつかんだ対象 (対象・位置・報酬・確定・取り消し)
struct LifeGrab {
    uint32_t target=0xffffffffu;
    Vec at{};
    float reward=0;
    float cancelAge=0;
    bool committed=false,cancelled=false;
};
// スライムの体 1 つの状態 (位置・向き・液体の形・量・体力・行動・武器・つかみ・粒子数・解放)
struct LifeBody {
    // 位置・前方向・液体の前方向・取り込みの位置・速度
    Vec at{},forward{0,0,1},liquidForward{0,0,1},engulfPoint{},velocity{};
    Vec previousForward{0,0,1}; // 前フレームの武器の基準の向き (角速度に使う)
    // ワールド空間の水平のひずみ (xx-zz, xz)。長い体を回すのではなく形を混ぜて、
    // 古い伸びが新しい向きへ緩むようにする
    Vec flowShape{};
    // 液体のずれ・核の先行・核の速度
    Vec fluidOffset{},coreLead{},coreVelocity{};
    // 縦横比・空中の割合・動きの量
    float aspect=1,aspectVelocity=0,air=0,motion=0;
    // 量・体力・待ち時間・時間・行動の時間・成長の予約・戻る長さ
    float mass=0,health=0,maxHealth=0,cooldown=0,age=0,actionAge=0,pendingGrowth=0,restoreDuration=.4f;
    // 取り込みの目標
    uint32_t target=0xffffffffu;
    LifeAction action=LifeAction::Idle;
    // 攻撃の番号
    uint64_t attackSerial=0;
    LifeWeaponAttack weapon;
    std::vector<LifeGrab> grabs;
    // つかみ始めた位置
    Vec grabOrigin{};
    // 割り当てられた粒子数
    uint32_t particles=0;
    // 倒れたか・取り込みを確定したか・反発できるか
    bool down=false,committed=false,reboundAvailable=false;
    bool tireUnlocked=false;
    bool hydrantUnlocked=false;
    bool signalUnlocked=false;
    float tireAngle=0;
    float tireSpin=0; // その場で溜めているタイヤの回転の速さ (ラジアン/秒)
    // 子分の自律行動: 狙っている敵・巡回の目標と残り時間・撤退中か
    uint32_t hunt=0xffffffffu;
    Vec patrol{};
    float patrolClock=0;
    bool retreating=false;
};
// 体が変形できるか (タイヤ・消火栓・信号機は解放が必要)
inline bool LifeCanMorph(const LifeBody& body,LifeAction action){
    return LifeCanMorph(body.mass,action)&&(action!=LifeAction::Tire||body.tireUnlocked)&&(action!=LifeAction::Hydrant||body.hydrantUnlocked)&&(action!=LifeAction::Signal||body.signalUnlocked);
}
// 開始の大きさより上では強く成長する。小さな破片は小さな体積のまま
inline float LifeScale(float mass){float ratio=std::max(1.f,mass)/LifeStartMass;return ratio>1?std::sqrt(ratio):std::cbrt(ratio);}
// 量あたりの粒子の密度
inline float LifeDensity(float mass,uint32_t particles,uint32_t totalParticles){
    // 粒子数と大きさから密度を求める
    float scale=LifeScale(mass);return 275.f*float(particles)/(float(std::max(1u,totalParticles))*scale*scale*scale);
}
// 取り込める条件: 量が足り、相手の体力が 3 割以下
inline bool LifeCanAbsorb(float mass,float required,float health=0,float maxHealth=1){
    return mass>=required&&health<=maxHealth*.3f;
}
// なめらかな 0〜1 / 水平の距離
inline float LifeSmooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
inline float LifeDistance(Vec a,Vec b){Vec d=a-b;d.y=0;return Length(d);}
// 移動の速度を目標へ近づける (加速と減速、空中では弱い)
inline Vec LifeMoveVelocity(Vec velocity,Vec wish,const SlimeRecipe& recipe,bool grounded,float dt,float speedScale=1){
    // 入力があれば加速、無ければ減速 (空中は弱い)
    float rate=Length(wish)>.05f?recipe.Acceleration():recipe.Braking();rate*=grounded?1.f:.65f+.6f*recipe.Ratio(3);
    float blend=1-std::exp(-rate*dt);velocity.x+=(wish.x*recipe.Speed()*speedScale-velocity.x)*blend;velocity.z+=(wish.z*recipe.Speed()*speedScale-velocity.z)*blend;return velocity;
}
// 体の動きの更新: 液体の向き・中心の遅れ・流れによる伸び・着地のつぶれ・空中の割合
inline void LifeTickMotion(LifeBody& b,const SlimeRecipe& recipe,Vec velocity,bool grounded,bool landed,float dt,Vec facing={}){
    b.velocity=velocity;
    Vec planar{velocity.x,0,velocity.z};float speed=Length(planar);
    // 向きの目標 (無ければ移動の向き)
    if(Length(facing)<.1f)facing=speed>.25f?Unit(planar):b.forward;
    // 液体の前方向を目標の向きへ回す
    if(Length(facing)>.1f){float yaw=std::atan2(b.liquidForward.x,b.liquidForward.z),goal=std::atan2(facing.x,facing.z);
        float delta=std::remainder(goal-yaw,6.2831853f),rate=14+4*recipe.Ratio(2);
        float step=std::clamp(delta*(1-std::exp(-rate*dt)),-12*dt,12*dt);yaw+=step;b.liquidForward={std::sin(yaw),0,std::cos(yaw)};}
    // 体の半径
    float radius=(2.55f*recipe.Spread()*(1-b.air)+1.8f*b.air)*LifeScale(b.mass);
    // 先に進むのはコントローラーと中心だけ。柔らかい外側は別に遅れて付いていく。
    // シミュレーションの外側の粒子を平行移動や回転はしない
    b.fluidOffset=(b.fluidOffset-planar*dt)*std::exp(-(10+8*recipe.Ratio(2)+5*recipe.Ratio(1))*dt);
    // 遅れの上限
    float lag=Length(b.fluidOffset),limit=radius*.32f;
    if(lag>limit)b.fluidOffset=b.fluidOffset*(limit/lag);
    // 核の先行 (前へ少し出る)
    Vec previousLead=b.coreLead;
    b.coreLead=Lerp(b.coreLead,b.liquidForward*(radius*(.16f+.08f*std::min(1.f,speed/12))),1-std::exp(-18*dt));
    b.coreVelocity=velocity+(dt>1e-5f?(b.coreLead-previousLead)*(1/dt):Vec{});
    // 移動の向きに体を伸ばすひずみ
    Vec strain{};
    if(speed>.25f){Vec direction=planar*(1/speed);float amount=(.08f+.02f*recipe.Ratio(0)) * std::min(1.f,speed/12) * (grounded?1.f:.25f);
        strain={amount*(direction.x*direction.x-direction.z*direction.z),0,amount*2*direction.x*direction.z};}
    b.flowShape=Lerp(b.flowShape,strain,1-std::exp(-(7+2*recipe.Ratio(0)+3*recipe.Ratio(2))*dt));
    // 着地したら縦の揺れを起こす
    if(landed)b.aspectVelocity-=6*recipe.Slosh();
    // 移動は横に伸ばし、上から潰すのは衝撃だけ
    float target=grounded?1:1+std::min(.45f,std::abs(velocity.y)*.028f)*recipe.Slosh();
    // 1/120 秒刻みでばねの揺れを計算する
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    for(int i=0;i<steps;++i){b.aspectVelocity+=((35+80*recipe.Ratio(1))*(target-b.aspect)-(3+7*recipe.Ratio(2)+3*recipe.Ratio(1))*b.aspectVelocity)*h;b.aspect=std::clamp(b.aspect+b.aspectVelocity*h,.48f,1.65f);}
    // 空中の割合と動きの量
    b.air+=((grounded?0.f:1.f)-b.air)*(1-std::exp(-14*dt));
    b.motion+=(std::min(1.f,Length(Vec{velocity.x,0,velocity.z})/18)-b.motion)*(1-std::exp(-7*dt));
}
// 体の半径 (広がり・高さの比・空中の丸さ・大きさ)
inline Vec LifeRadii(const LifeBody& b,const SlimeRecipe& recipe){
    // 広がり・大きさ・縦横比
    float spread=recipe.Spread(),s=LifeScale(b.mass),cross=1/std::sqrt(b.aspect);
    // 地上の楕円と空中の球を空中の割合で補間する
    Vec base=Lerp({2.55f*spread,1.8f/(spread*spread),2.55f*spread},{1.8f,1.8f,1.8f},b.air);
    float y=std::max(.65f*s,base.y*b.aspect*s),width=std::sqrt(base.y*b.aspect*s/y);
    return {base.x*cross*s*width,y,base.z*cross*s*width};
}
// 対象が口の前の取り込み範囲にあるか
inline bool LifeInMouth(Vec from,Vec forward,Vec target,float mass,float extra=0,const SlimeRecipe& recipe=SlimeRecipe{}){
    // 対象との差 (高さは別に見る)
    Vec delta=target-from;float height=std::abs(delta.y);delta.y=0;
    // 取り込みの距離
    float range=recipe.AbsorbRange()*LifeScale(mass)+extra;
    // 高さが近く、距離の範囲内で、正面にあれば口の中
    return height<3*LifeScale(mass)&&Length(delta)<=range&&(Length(delta)<.5f||Dot(Unit(delta),forward)>recipe.AbsorbDot());
}
// 調合室の試し / 取り込める物 (位置・必要な量・報酬・見た目・変形アイテム)
enum class SlimeTrial {None,Move,Absorb,Hit};
struct LifeCollectible {Vec at{};float required=20,reward=12;bool active=true;uint32_t visual=0xffffffffu;bool orb=false;LifeMorphItem morphItem=LifeMorphItem::None;};
// 報酬の大きさ別の段階・球の半径・色
inline int LifeOrbTier(float reward){return reward>=30?2:reward>=18?1:0;}
inline float LifeOrbRadius(float reward){const float radii[]={.42f,.68f,.96f};return radii[LifeOrbTier(reward)];}
inline Vec LifeOrbColor(float reward){const Vec colors[]={{.12f,.85f,1},{1,.68f,.12f},{.76f,.3f,1}};return colors[LifeOrbTier(reward)];}
// 信号機の色と状態: 次に出す色・次に使えるまでの待ち時間 (とその長さ)・光らせる残り時間と色。
// 色は赤 → 青 → 黄の順にしか進まず、黄の後は長く点検する
enum class SignalColor : uint8_t {Red,Green,Yellow};
struct SignalState {SignalColor next=SignalColor::Red,flashed=SignalColor::Red;float wait=0,waitTotal=0,flash=0;};
// スライムの生活の全体の状態 (体・取り込める物・レシピ・調合室の見本・成績・段階・入力の前回値)
struct SlimeLife {
    // 本体・子 (13 体)
    std::array<LifeBody,LifeBodyCount> bodies{};
    // 取り込める物
    std::vector<LifeCollectible> props;
    // 調合のレシピ
    SlimeRecipe recipe;
    // 調合室の見本と子
    LifeBody preview;
    LifeBody previewChild;
    // 試しの状態
    SlimeTrial trial=SlimeTrial::None;
    float trialAge=0,trialDistance=0,trialMessageAge=0;
    float previewImpact=0;
    std::string trialMessage;
    // 調合室の時間・ジャンプの時間・材料の滴
    float labAge=0,previewJumpClock=0,previewMix=0;
    bool previewAuto=true,previewPoke=false,previewJump=false;
    int colorDrag=0;
    // 選んでいる変形
    LifeAction form=LifeAction::Tire;
    // 時間・メッセージ・待ち時間・変形の時間
    float age=0,messageAge=0,stompCooldown=0,contactCooldown=0,morphAge=0;
    // 取り込んだ量・タックル・タイヤのお知らせ
    float absorbedMass=0,tackleTime=0,tireNotice=0,signalNotice=0;
    // 信号機の色の順と待ち時間
    SignalState signal;
    // 消火栓のお知らせ・水の発射の残り
    float hydrantNotice=0,waterAttackBudget=0,waterGuardBudget=0,waterHitClock=0;
    bool waterAttack=false,waterGuard=false;
    // 水の命中と防御の数
    int waterHits=0,waterBlocks=0;
    int waterGuardRow=0;
    // 成績
    int absorbed=0,childHits=0,stomps=0,hammerHits=0,splits=0;
    int step=0;      // 街のステージの段階 (CityStageRules.h)
    float stepDelay=2;
    // 変形の版と攻撃の番号
    uint64_t revision=1;
    uint64_t nextAttack=1;
    // 調合室・前フレームの入力
    bool lab=true,previousAbsorb=false,previousFire=false,previousReturn=false;
    bool chargeLocked=false; // 変形に使った押下では、離すまでタイヤの溜めを始めない
    bool previousLab=false,stomping=false,deployRequested=false;
    // メッセージ
    std::string message;
    // 全体の量 / 予約中を含む量
    float TotalMass()const {float n=0;for(const auto& b:bodies)n+=b.mass;return n;}
    float ReservedMass()const {
        float n=TotalMass();for(const auto& b:bodies){n+=b.pendingGrowth;
            if(b.action==LifeAction::Engulf)for(const auto& grab:b.grabs)if(!grab.committed&&!grab.cancelled)n+=grab.reward;}
        return n;
    }
    // 子の上限 (成長で増える) / 子の数
    int ChildLimit()const {float growth=LifeStartMass+absorbedMass;return growth>=LifePlaneMass?12:growth>=LifeHammerMass?6:2;}
    int ChildCount()const {int count=0;for(size_t i=1;i<bodies.size();++i)if(bodies[i].mass>0)++count;return count;}
};
// 解放は取り込みが確定したときだけ (触手でも接触でも)。子が
// 集めても本体の形が解放される。予約や取り消しでは解放しない
inline void LifeUnlockMorphItem(SlimeLife& life,uint32_t target){
    // 小物でなければ何もしない
    if(!(target&0x80000000u))return;
    size_t index=target&0x7fffffffu;
    // タイヤか消火栓のアイテムなら本体の形を解放する
    if(index<life.props.size()&&life.props[index].morphItem==LifeMorphItem::Tire)life.bodies[0].tireUnlocked=true;
    if(index<life.props.size()&&life.props[index].morphItem==LifeMorphItem::Hydrant)life.bodies[0].hydrantUnlocked=true;
    if(index<life.props.size()&&life.props[index].morphItem==LifeMorphItem::Signal)life.bodies[0].signalUnlocked=true;
}
// 対象が既に触手でつかまれているか
inline bool LifeTargetReserved(const SlimeLife& life,uint32_t target){
    // 取り込み中の体のつかみを調べる
    for(const auto& body:life.bodies)if(body.action==LifeAction::Engulf)
        for(const auto& grab:body.grabs)if(grab.target==target&&!grab.cancelled)return true;
    return false;
}
// 接触吸収できるか (十分に大きく、相手が小さい)
inline bool LifeCanContactAbsorb(const LifeBody& body,Vec from,Vec target,float required,float hp,float maxHp,const SlimeRecipe& recipe){
    // 本体が大きく、相手が小さく、取り込める状態か
    if(body.mass<LifeContactAbsorbMass||body.down||body.health<=0||required>body.mass*.25f||!LifeCanAbsorb(body.mass,required,hp,maxHp))return false;
    // 足元を掃引して、速く動いても小さな球をまたがないようにする
    // 前の位置から今の位置の線分で、一番近い点を求める
    Vec travel=body.at-from;travel.y=0;Vec delta=target-from;delta.y=0;float squared=Dot(travel,travel);
    float t=squared>.00001f?std::clamp(Dot(delta,travel)/squared,0.f,1.f):0;
    Vec nearest=Lerp(from,body.at,t);float scale=LifeScale(body.mass),radius=2.1f*recipe.Spread()*scale;
    return LifeDistance(nearest,target)<=radius&&std::abs(nearest.y-target.y)<1.8f*scale;
}
// 触れた物をすぐ吸収して成長する
inline bool LifeCollectTouch(SlimeLife& life,uint32_t target,Vec at,float required,float hp,float maxHp,float reward,Vec from){
    auto& body=life.bodies[0];
    if(reward<=0||life.ReservedMass()+reward>LifeMaxMass||LifeTargetReserved(life,target)||
        !LifeCanContactAbsorb(body,from,at,required,hp,maxHp,life.recipe))return false;
    // 接触での成長は、待っている触手のつかみや武器の行動とは別
    // 量・最大体力・体力を増やし、形を解放する
    body.mass+=reward;body.maxHealth+=reward;body.health=std::min(body.maxHealth,body.health+reward);life.absorbedMass+=reward;++life.absorbed;LifeUnlockMorphItem(life,target);return true;
}
// 取り込みの触手でつかむ対象を追加する
inline bool LifeAddGrab(SlimeLife& life,LifeBody& body,uint32_t target,Vec at,float required,float hp,float maxHp,float reward){
    // 待機か取り込み中で、上限内で、取り込めて、口の中にあれば
    if(body.mass<=0||body.down||(body.action!=LifeAction::Idle&&body.action!=LifeAction::Engulf)||
        reward<=0||life.ReservedMass()+reward>LifeMaxMass||LifeTargetReserved(life,target)||
        !LifeCanAbsorb(body.mass,required,hp,maxHp)||!LifeInMouth(body.at,body.forward,at,body.mass,0,life.recipe))return false;
    // 待機なら取り込みを始める
    if(body.action==LifeAction::Idle){body.grabs.clear();body.grabOrigin=body.at;body.action=LifeAction::Engulf;body.actionAge=0;body.committed=false;body.restoreDuration=.12f;}
    body.grabs.push_back({target,at,reward});return true;
}
// つかんだ対象を持ち続けられるか
inline bool LifeHoldGrab(const LifeBody& body,Vec at,const SlimeRecipe& recipe){
    // 一度手を伸ばしたら、通り過ぎたり向きを変えたりしてもつかみは取り消さない。
    // 動く敵は、元の取り込み範囲から逃げられる
    // つかんだ位置からの範囲
    float scale=LifeScale(body.mass),range=recipe.AbsorbRange()*scale+recipe.AbsorbHold();
    // 高さと距離の範囲内か
    return std::abs(at.y-body.grabOrigin.y)<3*scale+recipe.AbsorbHold()&&LifeDistance(body.grabOrigin,at)<=range&&
        LifeDistance(body.at,at)<=range+recipe.Speed()*recipe.AbsorbDuration();
}
// つかんだ対象の取り込みを確定する (成長は回復の後)
inline bool LifeCommitGrab(SlimeLife& life,LifeBody& body,LifeGrab& grab){
    if(grab.committed||grab.cancelled)return false;
    // 確定して成長を予約し、形を解放する
    grab.committed=true;body.committed=true;body.pendingGrowth+=grab.reward;++life.absorbed;LifeUnlockMorphItem(life,grab.target);return true;
}
// 取り込みを始めたいか / 行動中の移動の速さの倍率
inline bool LifeWantsAbsorb(bool held,const LifeBody& body){return held&&body.action==LifeAction::Idle&&!body.down;}
inline float LifeTravelScale(const LifeBody& body){
    // タイヤは速いまま、消火栓は少し遅い
    if(body.action==LifeAction::Tire)return 1;
    if(body.action==LifeAction::Hydrant)return .85f;
    // 信号機は構えて撃つ形なのでゆっくり
    if(body.action==LifeAction::Signal)return .6f;
    // 武器は攻撃中は遅く、それ以外も少し遅い
    if(LifeRequiredMass(body.action)>0)return body.weapon.active?.4f:.7f;
    return body.action==LifeAction::Restore?.8f:1.f;
}
// 体を初期化する (本体だけ)
inline void LifeInitializeBodies(SlimeLife& life,uint32_t particles){
    // すべての体を空にし、本体だけ開始の量にする
    life.bodies={};life.absorbedMass=0;
    auto& parent=life.bodies[0];parent.mass=parent.health=parent.maxHealth=LifeStartMass;
    parent.particles=particles;
}
// プレイヤーと子のどちらが取り込んでも本体が成長する。成長は
// 回復の後で確定し、待っている報酬も上限に数える
inline float LifeCompleteGrowth(SlimeLife& life,LifeBody& collector){
    // 予約した成長を本体に加える (上限まで)
    float amount=std::clamp(collector.pendingGrowth,0.f,std::max(0.f,LifeMaxMass-life.TotalMass()));collector.pendingGrowth=0;
    auto& parent=life.bodies[0];parent.mass+=amount;parent.maxHealth+=amount;parent.health=std::min(parent.maxHealth,parent.health+amount);life.absorbedMass+=amount;
    return amount;
}
// 受けた攻撃はすべて体力を減らす (子の上限でも)。生きている破片は
// 本体からまとまりを奪う。最低体力の保護は無い
inline int LifeTakeHit(SlimeLife& life,float damage){
    // 本体
    auto& parent=life.bodies[0];
    // 攻撃が無効なら何もしない
    if(damage<=0||parent.health<=0||parent.down)return -1;
    // 体力を減らす (配合の倍率)
    parent.health=std::max(0.f,parent.health-damage*life.recipe.DamageMultiplier());
    // 体力・量・粒子が無ければ分裂しない
    if(parent.health<=0||parent.mass<=0||parent.particles<2)return -1;
    // 空いている子の枠を探す
    for(size_t i=1;i<=size_t(life.ChildLimit());++i)if(life.bodies[i].mass<=0){
        // 分ける量 (配合の上限と割合)
        float mass=std::min(life.recipe.SplitCap(),parent.mass*life.recipe.SplitRatio()),fraction=mass/parent.mass;
        // 子を作り、体力と粒子を割合で分ける
        auto& child=life.bodies[i];child={};child.at=parent.at;child.forward=parent.forward;child.mass=mass;
        child.maxHealth=parent.maxHealth*fraction;child.health=std::min(parent.health,child.maxHealth);
        child.particles=std::clamp(uint32_t(std::round(float(parent.particles)*fraction)),1u,parent.particles-1);
        // 本体から引く
        parent.particles-=child.particles;parent.mass-=mass;parent.health-=child.health;parent.maxHealth-=child.maxHealth;
        ++life.splits;return int(i);
    }
    return -1;
}
}
