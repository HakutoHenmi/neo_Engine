// CityStageRules.h
// 街のステージの規則 (歩ける範囲・壁に沿った移動・敵の種類と能力・段階ごとの出現・弾道)。
// 床の配置・門・敵の能力・段階と出現の位置などの数値は Resources/Data/city_stage.json に置く。
#pragma once
#include "SwarmRules.h"
#include "SlimeLifeRules.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "../Data/GameData.h"
// スライムの大きさで進む、1 つにつながった現代の街のステージ (Docs/SlimeLife.md)。
// 脱出路地 (塀とフェンス, ハンマー) → 都心大通り (道路封鎖のバリケード, タイヤ)
// → 大火事現場 (鋼鉄のシャッターの門, 消火栓) → 事故多発交差点と守護者。ワールドの単位は
// 約 6 単位で 1 メートル (交差点は約 127 m 四方)。遊べる床は y=0 で平ら
namespace Game::Chrono {
// 街のエリアと、敵の種類
enum class CityArea : uint8_t {Alley,Avenue,Fire,Crossing};
inline constexpr int CityAreaCount=4;
enum class CityEnemy : uint8_t {Rat,Guard,Archer,Shield,Knight,Cannon,Soldier,Guardian};
// XZ 平面の矩形
struct CityRect {
    float x0,z0,x1,z1;
    bool Contains(Vec p)const {return p.x>=x0&&p.x<=x1&&p.z>=z0&&p.z<=z1;}
    Vec Center()const {return {(x0+x1)*.5f,0,(z0+z1)*.5f};}
};
// 路地裏をふさぐ壁の種類 (板塀・ブロック塀・金網フェンス) と情報
enum class CityWallKind : uint8_t {Wood,Block,Fence};
struct CityWall {
    CityWallKind kind=CityWallKind::Wood;std::string name;CityRect rect{};float health=0;
    // 壁の中心と、壁が延びる向き (長い辺の向き) と長さ
    Vec Center()const {return rect.Center();}
    bool AlongX()const {return rect.x1-rect.x0>=rect.z1-rect.z0;}
    Vec Axis()const {return AlongX()?Vec{1,0,0}:Vec{0,0,1};}
    float Span()const {return AlongX()?rect.x1-rect.x0:rect.z1-rect.z0;}
};
// 敵の能力 (大きさ・体力・取り込みに必要な量・報酬・装甲)
struct CityEnemyStats {float scale,health,required,reward,armor;};
// 出現の情報 / 段階の情報 (エリア・題名・目標)
struct CitySpawn {CityEnemy kind;Vec at,patrol;};
struct CityStep {CityArea area;const char* title;const char* goal;};
// 火事場の炎 (位置と半径)
struct CityFireSpot {Vec at;float radius;};
// 段階の数と敵の種類の数 (JSON もこの数にする)
inline constexpr int CityStepCount=8;
inline constexpr int CityEnemyKindCount=8;
// city_stage.json の中身
struct CityStageData {
    std::vector<CityRect> walk;std::vector<CityWall> walls;CityRect barricade{},ironGate{};
    Vec barricadeCenter{},gateCenter{},crossing{},start{},tireItem{},signalItem{};float roadHalf=0;std::array<float,CityAreaCount-1> areaEnds{};
    std::array<std::string,CityAreaCount> areaNames;
    float barricadeHealth=0,gateHealth=0,gateAbsorbMass=0,gateReward=0,arrowDamage=0,shellDamage=0,fireDamage=0,fireHealth=0;
    std::vector<CityFireSpot> fires;
    std::array<CityEnemyStats,CityEnemyKindCount> stats{};std::array<float,CityEnemyKindCount> damage{},alertRange{},standoff{};
    std::array<std::string,CityEnemyKindCount> names;
    std::array<std::string,CityStepCount> titles,goals;std::array<CityStep,CityStepCount> steps{};
    std::array<std::vector<CitySpawn>,CityStepCount> spawns;
};
// city_stage.json を読み込む (最初に使われたときに 1 回だけ)
inline const CityStageData& CityData(){
    static const CityStageData data=[]{
        const char* file="city_stage.json";
        const auto& json=Data::Load(file);
        CityStageData out;
        auto vec=[&](const nlohmann::json& v,const char* where){auto a=Data::ReadArray<float,3>(v,std::string(file)+" "+where);return Vec{a[0],a[1],a[2]};};
        auto rect=[&](const nlohmann::json& v,const char* where){auto a=Data::ReadArray<float,4>(v,std::string(file)+" "+where);return CityRect{a[0],a[1],a[2],a[3]};};
        const char* areaIds[]={"Alley","Avenue","Fire","Crossing"};
        const char* kindIds[]={"Rat","Guard","Archer","Shield","Knight","Cannon","Soldier","Guardian"};
        const char* wallIds[]={"Wood","Block","Fence"};
        auto areaOf=[&](const std::string& id){for(int i=0;i<CityAreaCount;++i)if(id==areaIds[i])return CityArea(i);throw std::runtime_error(std::string(file)+": unknown area "+id);};
        auto kindOf=[&](const std::string& id){for(int i=0;i<CityEnemyKindCount;++i)if(id==kindIds[i])return CityEnemy(i);throw std::runtime_error(std::string(file)+": unknown enemy "+id);};
        auto wallOf=[&](const std::string& id){for(int i=0;i<3;++i)if(id==wallIds[i])return CityWallKind(i);throw std::runtime_error(std::string(file)+": unknown wall "+id);};
        // 床の配置・路地の壁・門
        const auto& layout=Data::Require(json,"layout",file);
        for(const auto& w:layout.at("walk"))out.walk.push_back(rect(w,"layout.walk"));
        if(out.walk.empty())throw std::runtime_error(std::string(file)+": layout.walk is empty");
        for(const auto& w:layout.at("walls"))out.walls.push_back({wallOf(w.at("kind").get<std::string>()),w.at("name").get<std::string>(),rect(w.at("rect"),"layout.walls.rect"),w.at("health").get<float>()});
        if(out.walls.empty()||out.walls.size()>32)throw std::runtime_error(std::string(file)+": layout.walls must have 1 to 32 walls");
        out.barricade=rect(layout.at("barricade"),"layout.barricade");out.ironGate=rect(layout.at("ironGate"),"layout.ironGate");
        out.barricadeCenter=vec(layout.at("barricadeCenter"),"layout.barricadeCenter");out.gateCenter=vec(layout.at("gateCenter"),"layout.gateCenter");
        out.crossing=vec(layout.at("crossing"),"layout.crossing");out.start=vec(layout.at("start"),"layout.start");
        out.tireItem=vec(layout.at("tireItem"),"layout.tireItem");out.signalItem=vec(layout.at("signalItem"),"layout.signalItem");
        out.roadHalf=layout.at("roadHalf").get<float>();out.areaEnds=Data::ReadArray<float,CityAreaCount-1>(layout.at("areaEnds"),std::string(file)+" layout.areaEnds");
        const auto& areas=Data::Require(json,"areas",file);
        for(int i=0;i<CityAreaCount;++i)out.areaNames[size_t(i)]=areas.at(areaIds[i]).get<std::string>();
        const auto& gates=Data::Require(json,"gates",file);
        out.barricadeHealth=gates.at("barricadeHealth").get<float>();out.gateHealth=gates.at("gateHealth").get<float>();
        out.gateAbsorbMass=gates.at("gateAbsorbMass").get<float>();out.gateReward=gates.at("gateReward").get<float>();
        // 火事場の炎
        const auto& fires=Data::Require(json,"fires",file);
        out.fireDamage=fires.at("damage").get<float>();out.fireHealth=fires.at("health").get<float>();
        for(const auto& f:fires.at("spots")){auto a=Data::ReadArray<float,3>(f,std::string(file)+" fires.spots");out.fires.push_back({{a[0],0,a[1]},a[2]});}
        const auto& shots=Data::Require(json,"shotDamage",file);
        out.arrowDamage=shots.at("arrow").get<float>();out.shellDamage=shots.at("shell").get<float>();
        // 敵の種類ごとの能力 (列挙型の順)
        const auto& enemies=Data::Require(json,"enemies",file);
        if(enemies.size()!=size_t(CityEnemyKindCount))throw std::runtime_error(std::string(file)+": enemies must have 8 entries");
        for(size_t i=0;i<enemies.size();++i){
            const auto& e=enemies[i];
            if(kindOf(e.at("id").get<std::string>())!=CityEnemy(i))throw std::runtime_error(std::string(file)+": enemies must follow Rat, Guard, Archer, Shield, Knight, Cannon, Soldier, Guardian");
            out.stats[i]={e.at("scale").get<float>(),e.at("health").get<float>(),e.at("required").get<float>(),e.at("reward").get<float>(),e.at("armor").get<float>()};
            out.damage[i]=e.at("damage").get<float>();out.alertRange[i]=e.at("alertRange").get<float>();out.standoff[i]=e.at("standoff").get<float>();
            out.names[i]=e.at("name").get<std::string>();
        }
        // 段階ごとの題名・目標・出現 (patrol が無ければその場にとどまる。敵のいない段階もある)
        const auto& steps=Data::Require(json,"steps",file);
        if(steps.size()!=size_t(CityStepCount))throw std::runtime_error(std::string(file)+": steps must have 8 entries");
        for(size_t i=0;i<steps.size();++i){
            const auto& step=steps[i];
            out.titles[i]=step.at("title").get<std::string>();out.goals[i]=step.at("goal").get<std::string>();
            out.steps[i]={areaOf(step.at("area").get<std::string>()),out.titles[i].c_str(),out.goals[i].c_str()};
            for(const auto& spawn:step.at("spawns")){
                Vec at=vec(spawn.at("at"),"steps.spawns.at");
                out.spawns[i].push_back({kindOf(spawn.at("kind").get<std::string>()),at,spawn.contains("patrol")?vec(spawn.at("patrol"),"steps.spawns.patrol"):at});
            }
        }
        return out;
    }();
    return data;
}
// 床の範囲。外側の端にはすべて建物が並ぶ。重なりで隣の範囲とつながる
inline const std::vector<CityRect>& CityWalk(){return CityData().walk;}
// 路地裏の壁 (ハンマーで壊す)
inline const std::vector<CityWall>& CityWalls(){return CityData().walls;}
// バリケードと鉄の門の範囲・中心・交差点・開始位置・変形アイテムの位置
inline const CityRect& CityBarricade(){return CityData().barricade;}
inline const CityRect& CityIronGate(){return CityData().ironGate;}
inline Vec CityBarricadeCenter(){return CityData().barricadeCenter;}
inline Vec CityGateCenter(){return CityData().gateCenter;}
inline Vec CityCrossing(){return CityData().crossing;}
inline Vec CityTireItem(){return CityData().tireItem;}
inline Vec CitySignalItem(){return CityData().signalItem;}
// 交差する 2 本の道路の幅の半分
inline float CityRoadHalf(){return CityData().roadHalf;}
inline Vec CityStart(){return CityData().start;}
// バリケードと門の体力、門を取り込むのに必要な量と報酬
inline float CityBarricadeHealth(){return CityData().barricadeHealth;}
inline float CityGateHealth(){return CityData().gateHealth;}
inline float CityGateAbsorbMass(){return CityData().gateAbsorbMass;}
inline float CityGateReward(){return CityData().gateReward;}
// 火事場の炎: 位置と半径・触れたときのダメージ・消すのに必要な水の量
inline const std::vector<CityFireSpot>& CityFires(){return CityData().fires;}
inline float CityFireDamage(){return CityData().fireDamage;}
inline float CityFireHealth(){return CityData().fireHealth;}
// 位置からエリアを求める / エリアの名前
inline CityArea CityAreaAt(Vec p){const auto& ends=CityData().areaEnds;int area=0;while(area<CityAreaCount-1&&p.z>=ends[size_t(area)])++area;return CityArea(area);}
inline const char* CityAreaName(CityArea area){return CityData().areaNames[size_t(area)].c_str();}
// 門が開いているか。路地の壁は壊れたものの番号のビット
struct CityGates {
    bool barricadeOpen=false,gateOpen=false;uint32_t wallsOpen=0;
    bool WallOpen(size_t i)const {return (wallsOpen>>i)&1u;}
    // 路地の壁がすべて壊れたか
    bool AlleyOpen()const {for(size_t i=0;i<CityWalls().size();++i)if(!WallOpen(i))return false;return true;}
    // すべて開いた状態 (確認用)
    static CityGates AllOpen(){CityGates g;g.barricadeOpen=g.gateOpen=true;g.wallsOpen=0xffffffffu;return g;}
};
// 半径 margin の円が床の上にあること: 中心と縁の 8 点を調べるので、
// 重なった範囲のつなぎ目は通れ、壁と閉じた門はふさぐ
inline bool CityWalkable(Vec p,float margin,CityGates gates){
    auto onFloor=[&](Vec q){
        if(!gates.barricadeOpen&&CityBarricade().Contains(q))return false;
        if(!gates.gateOpen&&CityIronGate().Contains(q))return false;
        const auto& walls=CityWalls();
        for(size_t i=0;i<walls.size();++i)if(!gates.WallOpen(i)&&walls[i].rect.Contains(q))return false;
        for(const auto& rect:CityWalk())if(rect.Contains(q))return true;
        return false;};
    if(!onFloor(p))return false;
    for(int i=0;i<8;++i){float a=float(i)*.7853982f;if(!onFloor(p+Vec{std::cos(a)*margin,0,std::sin(a)*margin}))return false;}
    return true;
}
// 軸ごとに壁に沿って滑らせる。壁に触れたまま大きくなった体は今取れる
// 最大の余裕を使うので、必ず滑って抜けられ、深く埋まることはない
inline Vec CityConstrain(Vec from,Vec to,float margin,CityGates gates){
    margin=std::clamp(margin,.5f,18.f);
    while(margin>.5f&&!CityWalkable(from,margin,gates))margin=std::max(.5f,margin*.85f);
    if(CityWalkable(to,margin,gates))return to;
    // ふさがれた: その一歩の向きに最も進める、歩ける候補を選ぶ。
    // 軸の滑りはまっすぐな壁に沿い、接線や全長の軸の移動は角を回る
    Vec step{to.x-from.x,0,to.z-from.z};float length=Length(step);
    if(length<1e-5f)return {from.x,to.y,from.z};
    // 目標の位置で、縁のふさがれた部分から離れる向き
    Vec push{};
    for(int i=0;i<8;++i){float a=float(i)*.7853982f;Vec rim{std::cos(a),0,std::sin(a)};
        if(!CityWalkable(Vec{to.x,0,to.z}+rim*margin,.01f,gates))push=push-rim;}
    Vec n=Length(push)>1e-4f?Unit(push):Vec{};
    Vec best{from.x,to.y,from.z};float bestProgress=0;
    auto consider=[&](Vec candidate){candidate.y=to.y;Vec moved{candidate.x-from.x,0,candidate.z-from.z};
        float progress=Dot(moved,step)/length;
        // 一歩をふさいだ壁の方へは戻らない (角で行ったり来たりするため)
        if(Dot(moved,n)< -1e-4f)return;
        if(progress>bestProgress&&CityWalkable(candidate,margin,gates)){best=candidate;bestProgress=progress;}};
    consider({to.x,0,from.z});consider({from.x,0,to.z});
    for(Vec axis:{Vec{1,0,0},Vec{-1,0,0},Vec{0,0,1},Vec{0,0,-1}})consider(from+axis*length);
    if(Length(n)>0){Vec tangent{n.z,0,-n.x};
        consider(from+Unit(Unit(step)+n)*length);consider(from+tangent*length);consider(from-tangent*length);
        // どれも進めない: 壁からまっすぐ離れる (後ろではなく) ので、次の一歩で進める
        Vec away=from+n*length;away.y=to.y;
        if(best.x==from.x&&best.z==from.z&&Dot(n,step)>-.8f*length&&CityWalkable(away,margin,gates))return away;}
    return best;
}
// 敵の能力 (city_stage.json の enemies)
inline CityEnemyStats CityStats(CityEnemy kind){return CityData().stats[size_t(kind)];}
// 接触と突進のダメージ。遠距離の敵は代わりに弾で傷つける (CityShotDamage)
inline float CityDamage(CityEnemy kind){return CityData().damage[size_t(kind)];}
// 砲弾と矢のダメージ / 敵の名前
inline float CityShotDamage(bool shell){return shell?CityData().shellDamage:CityData().arrowDamage;}
inline const char* CityEnemyName(CityEnemy kind){return CityData().names[size_t(kind)].c_str();}
// 群れの敵のデータから街の敵の種類を取り出す
inline CityEnemy CityKind(const SwarmEnemy& enemy){return CityEnemy(enemy.kind);}
// 盾持ちは剣以外のすべてを正面で防ぐ。守護者の装甲はハンマーでしか削れない
inline bool CityShieldBlocks(const SwarmEnemy& enemy,Vec from,bool sword){
    if(CityKind(enemy)!=CityEnemy::Shield||sword||enemy.stagger>0)return false;
    Vec d=from-enemy.at;d.y=0;return Length(d)>.01f&&Dot(Unit(d),enemy.heading)>.2f;
}
// 段階ごとの題名と目標
inline const CityStep& CityStepInfo(int step){return CityData().steps[size_t(std::clamp(step,0,CityStepCount-1))];}
// 段階ごとの敵の出現 (種類・位置・巡回の地点)
inline std::vector<CitySpawn> CityStepSpawns(int step){
    if(step<0||step>=CityStepCount)return {};
    return CityData().spawns[size_t(step)];
}
// 全段階の敵の合計
inline int CityEnemyTotal(){int n=0;for(int s=0;s<CityStepCount;++s)n+=int(CityStepSpawns(s).size());return n;}
// 出現の情報から街の敵を作る
inline SwarmEnemy CitySpawnEnemy(const CitySpawn& spawn,uint32_t id,int step){
    SwarmEnemy enemy;enemy.id=id;enemy.step=int8_t(step);enemy.kind=uint8_t(spawn.kind);enemy.flying=enemy.bomber=false;
    auto stats=CityStats(spawn.kind);enemy.scale=stats.scale;enemy.health=enemy.maxHealth=stats.health;enemy.armor=stats.armor;
    enemy.at=enemy.home=spawn.at;enemy.patrol=spawn.patrol;
    // すべての敵はプレイヤーが来る向き (南, -z) を向く
    enemy.heading={0,0,-1};enemy.attackDelay=1.5f+float(id%4)*.6f;enemy.shotClock=1.2f+float(id%5)*.45f;
    enemy.alerted=spawn.kind==CityEnemy::Soldier||spawn.kind==CityEnemy::Guardian||step==6;
    return enemy;
}
// 落とす物と取り込みの条件は、出現の順ではなく敵の役割で決まる
inline float CityEnemyRequired(const SwarmEnemy& enemy){return CityStats(CityKind(enemy)).required;}
inline float CityEnemyReward(const SwarmEnemy& enemy){return CityStats(CityKind(enemy)).reward;}
inline LifeCollectible CityEnemyDrop(const SwarmEnemy& enemy){return {enemy.at,CityEnemyRequired(enemy),CityEnemyReward(enemy),true,0xffffffffu,true};}
// 敵が侵入者に気付く距離
inline float CityAlertRange(CityEnemy kind){return CityData().alertRange[size_t(kind)];}
// 弓兵は目標からこの距離を保つ。それ以外は近づく
inline float CityStandoff(CityEnemy kind){return CityData().standoff[size_t(kind)];}
// 放物線の砲弾: from の砲口から flight 秒後に target に落ちる
inline Vec CityShellVelocity(Vec from,Vec target,float flight,float gravity=30){
    Vec d=target-from;return {d.x/flight,d.y/flight+.5f*gravity*flight,d.z/flight};
}
}
