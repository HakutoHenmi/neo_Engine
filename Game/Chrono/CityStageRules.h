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
// 裏路地 → (工事バリケード, ハンマー) → 大通り → (鋼鉄のシャッターの門, 取り込みか破壊)
// → スクランブル交差点と守護者。ワールドの単位は約 6 単位で 1 メートル (交差点は
// 約 127 m 四方)。遊べる床は y=0 で平ら
namespace Game::Chrono {
// 街のエリアと、敵の種類
enum class CityArea : uint8_t {Alley,Street,Plaza};
enum class CityEnemy : uint8_t {Rat,Guard,Archer,Shield,Knight,Cannon,Soldier,Guardian};
// XZ 平面の矩形
struct CityRect {
    float x0,z0,x1,z1;
    bool Contains(Vec p)const {return p.x>=x0&&p.x<=x1&&p.z>=z0&&p.z<=z1;}
    Vec Center()const {return {(x0+x1)*.5f,0,(z0+z1)*.5f};}
};
// 敵の能力 (大きさ・体力・取り込みに必要な量・報酬・装甲)
struct CityEnemyStats {float scale,health,required,reward,armor;};
// 出現の情報 / 段階の情報 (エリア・題名・目標)
struct CitySpawn {CityEnemy kind;Vec at,patrol;};
struct CityStep {CityArea area;const char* title;const char* goal;};
// 段階の数と敵の種類の数 (JSON もこの数にする)
inline constexpr int CityStepCount=8;
inline constexpr int CityEnemyKindCount=8;
// city_stage.json の中身
struct CityStageData {
    std::array<CityRect,8> walk{};CityRect barricade{},ironGate{};
    Vec barricadeCenter{},gateCenter{},crossing{},start{};float roadHalf=0,alleyEndZ=0,streetEndZ=0;
    std::array<std::string,3> areaNames;
    float barricadeHealth=0,gateHealth=0,gateAbsorbMass=0,gateReward=0,arrowDamage=0,shellDamage=0;
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
        const char* areaIds[]={"Alley","Street","Plaza"};
        const char* kindIds[]={"Rat","Guard","Archer","Shield","Knight","Cannon","Soldier","Guardian"};
        auto areaOf=[&](const std::string& id){for(int i=0;i<3;++i)if(id==areaIds[i])return CityArea(i);throw std::runtime_error(std::string(file)+": unknown area "+id);};
        auto kindOf=[&](const std::string& id){for(int i=0;i<CityEnemyKindCount;++i)if(id==kindIds[i])return CityEnemy(i);throw std::runtime_error(std::string(file)+": unknown enemy "+id);};
        // 床の配置と門
        const auto& layout=Data::Require(json,"layout",file);
        const auto& walk=layout.at("walk");
        if(walk.size()!=out.walk.size())throw std::runtime_error(std::string(file)+": layout.walk must have 8 rectangles");
        for(size_t i=0;i<out.walk.size();++i)out.walk[i]=rect(walk[i],"layout.walk");
        out.barricade=rect(layout.at("barricade"),"layout.barricade");out.ironGate=rect(layout.at("ironGate"),"layout.ironGate");
        out.barricadeCenter=vec(layout.at("barricadeCenter"),"layout.barricadeCenter");out.gateCenter=vec(layout.at("gateCenter"),"layout.gateCenter");
        out.crossing=vec(layout.at("crossing"),"layout.crossing");out.start=vec(layout.at("start"),"layout.start");
        out.roadHalf=layout.at("roadHalf").get<float>();out.alleyEndZ=layout.at("alleyEndZ").get<float>();out.streetEndZ=layout.at("streetEndZ").get<float>();
        const auto& areas=Data::Require(json,"areas",file);
        for(int i=0;i<3;++i)out.areaNames[size_t(i)]=areas.at(areaIds[i]).get<std::string>();
        const auto& gates=Data::Require(json,"gates",file);
        out.barricadeHealth=gates.at("barricadeHealth").get<float>();out.gateHealth=gates.at("gateHealth").get<float>();
        out.gateAbsorbMass=gates.at("gateAbsorbMass").get<float>();out.gateReward=gates.at("gateReward").get<float>();
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
        // 段階ごとの題名・目標・出現 (patrol が無ければその場にとどまる)
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
inline const std::array<CityRect,8>& CityWalk(){return CityData().walk;}
// バリケードと鉄の門の範囲・中心・交差点・開始位置
inline const CityRect& CityBarricade(){return CityData().barricade;}
inline const CityRect& CityIronGate(){return CityData().ironGate;}
inline Vec CityBarricadeCenter(){return CityData().barricadeCenter;}
inline Vec CityGateCenter(){return CityData().gateCenter;}
inline Vec CityCrossing(){return CityData().crossing;}
// 交差する 2 本の道路の幅の半分
inline float CityRoadHalf(){return CityData().roadHalf;}
inline Vec CityStart(){return CityData().start;}
// バリケードと門の体力、門を取り込むのに必要な量と報酬
inline float CityBarricadeHealth(){return CityData().barricadeHealth;}
inline float CityGateHealth(){return CityData().gateHealth;}
inline float CityGateAbsorbMass(){return CityData().gateAbsorbMass;}
inline float CityGateReward(){return CityData().gateReward;}
// 位置からエリアを求める / エリアの名前
inline CityArea CityAreaAt(Vec p){const auto& d=CityData();return p.z<d.alleyEndZ?CityArea::Alley:p.z<d.streetEndZ?CityArea::Street:CityArea::Plaza;}
inline const char* CityAreaName(CityArea area){return CityData().areaNames[size_t(area)].c_str();}
// 門が開いているか
struct CityGates {bool barricadeOpen=false,gateOpen=false;};
// 半径 margin の円が床の上にあること: 中心と縁の 8 点を調べるので、
// 重なった範囲のつなぎ目は通れ、壁と閉じた門はふさぐ
inline bool CityWalkable(Vec p,float margin,CityGates gates){
    auto onFloor=[&](Vec q){
        if(!gates.barricadeOpen&&CityBarricade().Contains(q))return false;
        if(!gates.gateOpen&&CityIronGate().Contains(q))return false;
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
