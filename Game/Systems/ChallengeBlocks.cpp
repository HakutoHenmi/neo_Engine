// ChallengeBlocks.cpp
// 変形チャレンジの物 (ChronoSystem の一部)。今は「積み木クラッシャー」(規則と配置は Chrono/ChallengeRules.h)。
//  - 積み木は能力の衝撃 (BattleImpact) を受けて壊れる。効き目ごとに反応が違うので、44 種のどの能力でも何かが起きる
//      当てる: 削る (硬い積み木は弱い当たりが効かない)・引き寄せる: 吸い寄せて回収 (硬い積み木は動かない)・押し出す (においの雲): 少しずつ削る
//      止める: 当たりがあれば削る。無ければひび (まんが)・弱らせる: ひび・やけど: 硬さを無視して少しずつ削る・閉じ込める: そのまま回収
//      場所に置く: 色ぬりの床の上の積み木は得点が上がる
//  - 下の積み木が無くなると、上の積み木は落ちて次の支えに乗る
//  - どのチャレンジでも同じ、自分に効く能力 (ApplyChallengeSelf): 回復で時間が増える・リモコンで時計が止まる・守る/だるまで体当たり
#include "ChronoSystemInternal.h"
#include <random>

namespace Game {
namespace {
std::mt19937& BlockRandom(){static std::mt19937 random{std::random_device{}()};return random;}
float BR(float low,float high){return std::uniform_real_distribution<float>(low,high)(BlockRandom());}
// 積み木の色: 普通はおもちゃの色を並べる。硬いは青灰色、金色は金
Engine::Vector4 BlockColor(BlockType type,uint32_t id){
    static constexpr Engine::Vector4 toys[]={{.95f,.42f,.38f,1},{.98f,.78f,.3f,1},{.4f,.72f,.95f,1},{.5f,.85f,.45f,1},{.95f,.6f,.85f,1},{.98f,.95f,.88f,1}};
    switch(type){case BlockType::Hard:return {.42f,.48f,.58f,1};case BlockType::Gold:return {1,.78f,.18f,1};default:return toys[id%6];}
}
}

// チャレンジの場の位置 (unit を単位にした、場の真ん中からの x,z と床からの高さ)
Vec ChronoSystem::ArenaPoint(float x,float z,float y)const{const float u=battle_.unit;V at=battle_.arenaAt+V{x*u,0,z*u};at.y=InkGround(at)+y*u;return at;}

// チャレンジの物を作る (始めるときとやり直しのとき)。積み木の配置は毎回ランダムに選ぶ (--challenge-pattern=番号 で決められる)
void ChronoSystem::BuildChallenge(){
    auto& b=battle_;b.blocks.clear();b.debris.clear();b.broken={};b.timeStop=b.heavy=0;b.territory={};
    if(b.legacy)return;
    if(b.challenge==ChallengeKind::Territory){BuildTerritory();return;}
    if(b.challenge!=ChallengeKind::Blocks)return;
    b.blockPattern=b.forcedPattern>=0?b.forcedPattern:int(BR(0,float(ChallengeBlockPatterns)-.001f));
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--challenge-pattern="))b.blockPattern=std::clamp(_wtoi(arg+20),0,ChallengeBlockPatterns-1);
    const float u=b.unit,size=u*ChallengeBlockSize;const V center=b.arenaAt;
    for(const auto& spot:ChallengeBlockLayout(b.blockPattern)){ChallengeBlock blk;blk.id=ChallengeBlockIdBase+uint32_t(b.blocks.size());blk.type=spot.type;blk.size=size;
        blk.at=center+V{spot.x*u,0,spot.z*u};blk.at.y=InkGround(blk.at)+size*(.5f+float(spot.level));
        blk.hp=blk.maxHp=ChallengeBlockHp[int(spot.type)];b.blocks.push_back(blk);}
}

// 状況の文字 (HUD の左上の札の 3 行目)
std::string ChronoSystem::ChallengeStatus()const{
    const auto& b=battle_;if(b.legacy)return {};if(b.challenge==ChallengeKind::Territory)return TerritoryStatus();if(b.challenge!=ChallengeKind::Blocks)return {};
    int left=0;for(const auto& blk:b.blocks)if(blk.alive)++left;
    return std::string(ChallengeBlockPatternName(b.blockPattern))+"  壊した "+std::to_string(b.broken[0]+b.broken[1]+b.broken[2])+"  (金 "+std::to_string(b.broken[2])+")  のこり "+std::to_string(left);
}

// 今の威力: 体の大きさの威力に、おやつパワー (回復のあとしばらく) をかける
float ChronoSystem::ChallengePowerNow()const{const float base=BattlePower(CurrentDevourStage(),life_.bodies[0].mass);return battle_.snack>0?base*ChallengeSnackPower:base;}

// どのチャレンジでも同じ、自分に効く能力
void ChronoSystem::ApplyChallengeSelf(BattleAction action){
    auto& b=battle_;if(b.legacy)return;auto& body=life_.bodies[0];const V top=body.at+V{0,DevourBodyRadius(body.mass)*1.4f,0};
    switch(action){
    case BattleAction::Heal:case BattleAction::Brew:{
        // 時間を足す (合わせて ChallengeTimeBonusMax 秒まで)
        const float add=std::min(action==BattleAction::Heal?ChallengeHealBonus:ChallengeBrewBonus,std::max(0.f,ChallengeTimeBonusMax-b.timeBonus));b.time+=add;b.timeBonus+=add;
        if(action==BattleAction::Heal)b.snack=ChallengeSnackTime;
        b.pops.push_back({top,(add>0?"+"+std::to_string(int(std::round(add)))+"秒  ":std::string())+(action==BattleAction::Heal?"おやつパワー！":add>0?"":"もう時間は増えない"),0,{.5f,.95f,.5f,1}});break;}
    case BattleAction::Pause:b.timeStop=std::max(b.timeStop,BattlePauseTime);b.message="一時停止！ 時計が止まった";b.messageAge=1.5f;break;
    case BattleAction::Guard:case BattleAction::Roly:b.heavy=std::max(b.heavy,action==BattleAction::Guard?BattleGuardTime:BattleActionTime(BattleAction::Roly));b.heavySource=b.impactSerial++;b.heavyBy=action;
        b.message=action==BattleAction::Guard?"盾タックル！ しばらく体当たりで壊せる":"体が重く硬くなった！ 体当たりで壊せる";b.messageAge=1.5f;break;
    default:break;}
}

// 積み木を削る: ひびがあれば強く効き、硬さを無視する。硬さに負けたら効かない (「かたい！」)
void ChronoSystem::DamageBlock(GameContext& ctx,ChallengeBlock& blk,float amount,V push,bool ignoreArmor){
    if(!blk.alive||amount<=0)return;
    if(blk.fragile>0){amount*=ChallengeFragileTake;ignoreArmor=true;}
    if(!ignoreArmor){amount-=ChallengeBlockArmor[int(blk.type)];
        if(amount<=0){if(blk.flash<=0){battle_.pops.push_back({blk.at+V{0,blk.size*.6f,0},"かたい！",0,{.75f,.8f,.9f,1}});PlayDevourSound(devourSounds_.bounce,.3f,1.6f);}blk.flash=.12f;return;}}
    blk.hp-=amount;blk.flash=.12f;
    if(blk.hp<=0)BreakBlock(ctx,blk,push,nullptr);
}

// 積み木が壊れた: 得点 (色ぬりなら増える)・かけらを飛ばす・上の積み木を落とす
void ChronoSystem::BreakBlock(GameContext& ctx,ChallengeBlock& blk,V push,const char* how){
    auto& b=battle_;if(!blk.alive)return;blk.alive=false;
    const int type=int(blk.type);++b.broken[size_t(type)];
    const float points=float(ChallengeBlockScore[type])*(blk.painted?ChallengePaintScore:1.f);
    AddChallengeScore(int(std::round(points)),blk.at+V{0,blk.size*.6f,0},how?how:blk.type==BlockType::Gold?"金！":blk.type==BlockType::Hard?"硬い！":"");
    const Engine::Vector4 color=BlockColor(blk.type,blk.id);
    for(int k=0;k<4;++k){ChallengeDebris d;d.at=blk.at+V{BR(-.3f,.3f),BR(-.3f,.3f),BR(-.3f,.3f)}*blk.size;d.size=blk.size*.45f;d.color=color;
        d.velocity=push*.6f+V{BR(-1,1),0,BR(-1,1)}*(b.unit*3)+V{0,b.unit*BR(4,8),0};d.spin=V{BR(-8,8),BR(-8,8),BR(-8,8)};b.debris.push_back(d);}
    if(blk.type==BlockType::Gold){PlayDevourSound(devourSounds_.rankUp,.7f,1.3f);LifeImpact(ctx,ImpactKill,blk.at,{0,1,0},.6f);}
    else PlayDevourSound(blk.type==BlockType::Hard?devourSounds_.shatter:devourSounds_.hit,blk.type==BlockType::Hard?.5f:.3f,BR(.9f,1.3f));
    DropBlocksAbove(blk);
}

// 下が無くなった: 真上に重なっている積み木を全部落とす
void ChronoSystem::DropBlocksAbove(const ChallengeBlock& below){
    for(auto& blk:battle_.blocks){if(!blk.alive||&blk==&below||blk.at.y<=below.at.y)continue;
        if(std::abs(blk.at.x-below.at.x)<blk.size*.9f&&std::abs(blk.at.z-below.at.z)<blk.size*.9f)blk.falling=true;}
}

// 積み木が衝撃を受ける (効き目ごとの反応)。何かが起きたら true
bool ChronoSystem::ApplyImpactToBlock(GameContext& ctx,ChallengeBlock& blk,const BattleImpact& i){
    if(!blk.alive)return false;auto& b=battle_;const float unit=b.unit;
    V d=blk.at-i.at;d.y=0;const float dist=Length(d);V flatDir{i.dir.x,0,i.dir.z};flatDir=Length(flatDir)>.001f?Unit(flatDir):V{0,0,1};
    const V radial=dist>.01f?d*(1/dist):flatDir;const V push=(i.radial?radial:flatDir)*(unit*i.push);
    switch(i.effect){
    case ImpactEffect::Hit:DamageBlock(ctx,blk,i.damage,push,false);return true;
    case ImpactEffect::Pull:{
        // 硬い積み木は重くて動かない。それ以外は中心へ滑り、中心まで来たら回収する
        if(blk.type==BlockType::Hard)return false;
        if(dist<blk.size*ChallengeSuckReach){BreakBlock(ctx,blk,V{},"回収");return true;}
        const float strength=i.pullCap>0?unit*std::min(dist,i.pullCap)*i.push:unit*i.push*8;
        if(!blk.loose){blk.loose=true;DropBlocksAbove(blk);}blk.velocity=blk.velocity-radial*strength;return true;}
    case ImpactEffect::PushOut:if(blk.type==BlockType::Hard)return false;DamageBlock(ctx,blk,ChallengeSmellDps*b.frameDt,V{},true);return true;
    case ImpactEffect::Stun:
        if(i.action==BattleAction::Pause)return false;   // リモコンは時計を止める (ApplyChallengeSelf)
        if(i.damage>0)DamageBlock(ctx,blk,i.damage,push,false);else blk.fragile=std::max(blk.fragile,i.seconds);
        return true;
    case ImpactEffect::Weaken:blk.fragile=std::max(blk.fragile,i.seconds);DamageBlock(ctx,blk,i.damage,push,false);return true;
    case ImpactEffect::Burn:blk.burn=i.seconds;blk.burnDps=i.damage;blk.burnTick=0;return true;
    case ImpactEffect::Capture:BreakBlock(ctx,blk,V{},"回収");return true;
    case ImpactEffect::Zone:if(i.action!=BattleAction::Paint)return false;blk.painted=true;return true;
    default:return false;}
}

// チャレンジの物を進める: 体当たり (重い体)・ひびとやけど・吸い寄せられて滑る・落ちる・かけら
void ChronoSystem::TickChallenge(GameContext& ctx,float dt){
    auto& b=battle_;if(b.legacy)return;auto& body=life_.bodies[0];const float R=DevourBodyRadius(body.mass);
    // 守る・だるま: 体に触れた物に当てる
    if(b.heavy>0){b.heavy-=dt;BattleImpact i;i.action=b.heavyBy;i.at=body.at;i.dir=body.forward;i.reach=R*1.05f;i.damage=ChallengeHeavyDamage*ChallengePowerNow();i.push=4;i.radial=true;
        i.source=b.heavySource;i.rehit=ChallengeHeavyRehit;EmitBattleImpact(ctx,i);}
    if(b.challenge==ChallengeKind::Territory)TickTerritory(ctx,dt);
    if(b.blocks.empty()&&b.debris.empty())return;
    const float g=b.unit*30;
    for(auto& blk:b.blocks){if(!blk.alive)continue;
        blk.flash=std::max(0.f,blk.flash-dt);blk.fragile=std::max(0.f,blk.fragile-dt);
        if(blk.burn>0){blk.burn-=dt;blk.burnTick-=dt;if(blk.burnTick<=0){blk.burnTick=.5f;DamageBlock(ctx,blk,blk.burnDps*.5f,V{},true);if(!blk.alive)continue;}}
        // 吸い寄せられて滑る (床の上を摩擦で止まる)
        if(blk.loose){blk.at=blk.at+V{blk.velocity.x,0,blk.velocity.z}*dt;blk.velocity=blk.velocity*std::exp(-4*dt);
            if(Length(blk.velocity)<b.unit*.2f){blk.loose=false;blk.velocity={};}blk.falling=true;}
        // 落ちる: 真下の一番高い支え (止まっている積み木の上の面か床) まで
        if(blk.falling){float support=InkGround(blk.at);
            for(const auto& o:b.blocks){if(!o.alive||&o==&blk||o.falling||o.at.y>=blk.at.y)continue;
                if(std::abs(o.at.x-blk.at.x)<blk.size*.9f&&std::abs(o.at.z-blk.at.z)<blk.size*.9f)support=std::max(support,o.at.y+o.size*.5f);}
            blk.velocity.y-=g*dt;blk.at.y+=blk.velocity.y*dt;
            if(blk.at.y-blk.size*.5f<=support){blk.at.y=support+blk.size*.5f;blk.velocity.y=0;if(!blk.loose)blk.falling=false;}}}
    for(auto& d:b.debris){d.age+=dt;d.velocity.y-=g*dt;d.at=d.at+d.velocity*dt;const float floor=InkGround(d.at)+d.size*.5f;if(d.at.y<floor){d.at.y=floor;d.velocity=d.velocity*.4f;d.velocity.y=std::abs(d.velocity.y);}}
    b.debris.erase(std::remove_if(b.debris.begin(),b.debris.end(),[](const ChallengeDebris& d){return d.age>d.life;}),b.debris.end());
}

// 積み木とかけらを描く (ひびは白っぽく、やけどは橙、色ぬりは明るく、当たった瞬間は白く光る)
void ChronoSystem::DrawChallenge(GameContext& ctx){
    const auto& b=battle_;if(cube_==0)return;
    if(!b.legacy&&b.challenge==ChallengeKind::Territory)DrawTerritory(ctx);
    auto box=[&](V at,float size,V rotate,Engine::Vector4 c){Engine::Transform t;t.translate=EV(at);t.scale={size*.5f,size*.5f,size*.5f};t.rotate=EV(rotate);
        const Engine::Vector4 linear{std::pow(c.x,2.2f),std::pow(c.y,2.2f),std::pow(c.z,2.2f),c.w};ctx.renderer->DrawMeshInstanced(cube_,white_,t,linear);};
    auto mix=[](Engine::Vector4 a,Engine::Vector4 c,float k){return Engine::Vector4{a.x+(c.x-a.x)*k,a.y+(c.y-a.y)*k,a.z+(c.z-a.z)*k,1};};
    for(const auto& blk:b.blocks){if(!blk.alive)continue;Engine::Vector4 c=BlockColor(blk.type,blk.id);
        if(blk.painted)c=mix(c,{1,.35f,.75f,1},.35f);if(blk.fragile>0)c=mix(c,{1,1,1,1},.35f);if(blk.burn>0)c=mix(c,{1,.45f,.15f,1},.45f);if(blk.flash>0)c=mix(c,{1,1,1,1},.7f);
        // 削れた分だけ少し縮める (硬い・金色)
        const float worn=.9f+.1f*std::clamp(blk.hp/std::max(1.f,blk.maxHp),0.f,1.f);
        box(blk.at,blk.size*.96f*worn,{0,0,0},c);}
    for(const auto& d:b.debris){const float fade=1-std::clamp((d.age-d.life*.6f)/(d.life*.4f),0.f,1.f);box(d.at,d.size*fade,d.spin*d.age,d.color);}
}

}
