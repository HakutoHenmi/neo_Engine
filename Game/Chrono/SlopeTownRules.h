// SlopeTownRules.h
// 吸収モードの坂の町 (ステージ 2) の地面の高さと置く物の決まり。エンジンに依存しない。
// 南の川と線路から北の神社の山へ約 45m 上がる斜面の町。坂道は斜面に沿って曲がりながら上り、道の外は家の敷地 (ひな壇) と、
// 等高線に沿った段々畑・庭の段になる。石段は坂道の近道。段差は飛び降りられるが、上るには段差ジャンプ (体の直径まで) が要る。
// 地形は scripts/terrain/slope_town_design.py が作る Resources/Data/slope_town_terrain.json (1m のマスの高さと種類・道・石段・敷地・境内)。
// 置く物・色・塀は Resources/Data/slope_town.json。どちらもメートルで書き、読み込むときにゲームの単位 (unit 倍) へ直す
#pragma once
#include "CityStageRules.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace Game::Chrono {
// カメラを地面より上に保つ高さ (部屋の床と同じ) と、擁壁の面にめり込まないよう周りの地面を調べる半径
inline constexpr float SlopeCameraLift=.7f,SlopeCameraReach=1.5f;
// マスの種類 (slope_town_terrain.json の kinds の数字)
enum class SlopeKind : uint8_t {Ground,Lot,Road,Stairs,Water,Rail,Field,Plaza};
inline constexpr int SlopeKindCount=8;
// 隣のマスと角をならす高さの差 (これより大きいと擁壁になる。メートル)
inline constexpr float SlopeSmoothStep=.7f;
// 石段の両側の平らな肩の幅 (石段の箱をこの幅まで広げ、地面の高さもこの幅まで石段にする。地形の STAIRS_SHOULDER と同じ値。メートル)
inline constexpr float SlopeStairsOverlap=1.5f;
// 石段の箱を石段のマスより広げる量 (1m のマスの対角の半分。斜めの石段を 1m のマスに分けたときのはみ出しを覆う。メートル)
inline constexpr float SlopeStairsCover=.75f;
// 坂の町のカメラの手前の面 (単位。部屋は 0.3): カメラから体の表面 (半径 × SlopeCameraNearBody) までの距離の半分にする。下限と上限
inline constexpr float SlopeCameraNear=.8f,SlopeCameraNearMax=12,SlopeCameraNearBody=1.4f;
// 石段のマスの面を、段の上の面より下げる量 (上に重なる石段の箱と同じ高さで面がちらつかないように。メートル)
inline constexpr float SlopeStairsDrop=.3f;
// 道: 名前・種類・幅と、2m ごとの点 (y は道の高さ)
struct SlopeRoad {std::string id,name,kind;float width=0;std::vector<Vec> path;};
// 石段: 下の端・上の端 (y は高さ)・幅・段の数
// side は段ごとの箱の左右の広がり (石段の軸から左・右。単位)、back と ahead は一番下の段を手前へ・一番上の段を奥へ伸ばす長さ。
// どれも石段のマスを覆うだけの広さ (読み込むときに決める。道のマスの上へははみ出さない)
struct SlopeStairs {std::string id,name;Vec from{},to{};float width=0;int steps=1;std::vector<std::array<float,2>> side;float back=0,ahead=0;};
// 家の敷地: 中心 (y は敷地の高さ)・正面の向き (道の方。0 で +z)・面する道・間口と奥行き
struct SlopeLot {Vec at{};float yaw=0,width=0,depth=0;std::string road;};
// 境内: 中心 (y は高さ) と半径
struct SlopePlaza {std::string name;Vec at{};float radius=0;};
// 記録に使う区画 (高さで分ける): 名前とその区画の上の端の高さ
struct SlopeArea {std::string id,name;float below=0;};
// 1 つずつ置く物: モデル・位置 (y は地面)・向き (ラジアン。0 で正面が +z)・吸収の量の割合 (0 は飲めない飾り)・実寸に対する倍率。
// fit が 0 より大きければ、上から見た長い辺がその長さになる大きさにする。landmark は、その足元に重なる家の敷地に家を建てない目印
struct SlopeProp {std::string id;Vec at{};float yaw=0,fill=0,mult=1,fit=0;bool landmark=false;};
// 線に沿って並べる物: from から to まで step ごとに、ids を順に使う。skip の割合で間を空け、jitter だけ前後にずらす
struct SlopeRow {std::vector<std::string> ids;Vec from{},to{};float step=1,yaw=0,fill=0,mult=1,skip=0,jitter=0;};
// 範囲にばらまく物: 範囲・数・吸収の量の割合・倍率の範囲・置いてよいマスの種類 (ビットの組)
// sets があれば、1 つの代わりにその中から選んだ組 (SlopeSet) を置く
struct SlopeScatter {std::vector<std::string> ids,sets;CityRect rect{};int count=0;float fill=1,multLow=1,multHigh=1;uint32_t kinds=0;};
// 組で置く物 (玄関まわり・物置まわり・ごみ置き場など): 組の向きに合わせた位置 (x は右、z は前。y は使わない)・向き (ラジアン)・実寸に対する倍率
struct SlopeSetItem {std::string id;Vec at{};float yaw=0,mult=1;};
struct SlopeSet {std::string name;std::vector<SlopeSetItem> items;};
// 家の敷地に置く組: 家のモデルごと・面する道の種類ごとの候補 (家のモデルの組は必ず 1 つ置く)。count は 1 軒に置く組の数 (前の左右の角、後ろの左右の角の順)
struct SlopeLotSets {std::vector<std::pair<std::string,std::vector<std::string>>> byHouse,byRoad;std::vector<std::string> fallback;int count=0;float fill=1;};
// 道ぞいに置く組 (ごみ置き場・自販機など): 道の種類 (空なら全部)・間隔・始まりの距離・道の端からのずれ。組の前 (+z) を道へ向けて、左右交互に置く
// onRoad は道のマスの上に置いてよい組 (路上駐車)。ほかは道のマスにかかる物を置かない
struct SlopeRoadSets {std::vector<std::string> sets,kinds;float step=1,start=0,offset=0,fill=1;bool onRoad=false;};
// 決まった所に置く組 (公園の遊び道具など)
struct SlopeSpot {std::string set;Vec at{};float yaw=0,fill=1;};
// その場で動く物 (鳥がついばむ・犬が跳ねる・風鈴や洗濯物や木が揺れる): モデルごとの速さ・左右の揺れ (ラジアン)・跳ねる高さ (単位)
struct SlopeMotion {std::string id;float speed=1,sway=0,hop=0;};
// 道を歩く物 (犬・自転車): 道の種類 (空なら全部)・数・速さ (単位/秒)・往復する長さ・道の端から内へ寄せる量 (単位)
struct SlopeWalker {std::string id;std::vector<std::string> kinds;int count=0;float speed=1,length=1,inset=0,fill=1;};
// その場を回って歩く物 (鳥・猫・犬): 輪の大きさ (単位)・速さ (単位/秒)・回る割合・正面が +z のモデルか (ほかは +x)
struct SlopeWander {std::string id;float radius=1,speed=1,chance=1;bool frontZ=false;};
// 道ぞいに置く物 (電柱など): 道の名前 (空なら全部)・間隔・道の端からのずれ
struct SlopeRoadside {std::vector<std::string> ids,roads;float step=1,offset=0,fill=0;};
// 道の上に並べる物 (スタートの前のおやつの列など): 道・始まりと終わりの距離・間隔
struct SlopeTrail {std::string road;std::vector<std::string> ids;float from=0,to=0,step=1,fill=1;};
// 往復する物 (電車・バス): 通る点 [x, z] か道の名前・速さ (単位/秒)。道の上を走る物は地面の高さに合わせて上り下りする
struct SlopeMover {std::string id,road;std::vector<Vec> path;float speed=0,fill=0;};
// 家の敷地に建てる家: 面する道の種類ごとのモデルの候補・敷地に収める間口の割合・吸収の量の割合。塀 (庭の小物は lotSets の組で置く)
// sink は家を地面へ沈める深さ (単位。敷地の縁は隣とならして少し下がるので、家の下にすき間を見せない)
struct SlopeHouses {std::vector<std::pair<std::string,std::vector<std::string>>> byRoad;std::vector<std::string> fallback,fences;float fit=.9f,fill=.5f,fenceFill=.8f,sink=0;};
// 町の端の塀: 高さ・厚さ・地面に合わせる区切りの長さ・控え柱の間隔 (単位) と色の名前
struct SlopeBorder {float height=0,thick=0,step=1,post=0;std::string color,cap;};
struct SlopeTownData {
    float unit=6,cell=6,halfX=0,halfZ=0,outside=0;int n=0,halfCells=0;SlopeBorder border;
    // 地形: マスの高さ (単位)・種類・ならした 4 つの角の高さ (-x-z, +x-z, +x+z, -x+z の順)
    std::vector<float> height;std::vector<uint8_t> kind;std::vector<std::array<float,4>> corner;
    std::vector<SlopeRoad> roads;std::vector<SlopeStairs> stairs;std::vector<SlopeLot> lots;std::vector<SlopePlaza> plazas;std::vector<SlopeArea> areas;
    std::vector<SlopeProp> props;std::vector<SlopeRow> rows;std::vector<SlopeScatter> scatters;std::vector<SlopeRoadside> roadside;std::vector<SlopeTrail> trails;std::vector<SlopeMover> movers;SlopeHouses houses;
    std::vector<SlopeSet> sets;SlopeLotSets lotSets;std::vector<SlopeRoadSets> roadSets;std::vector<SlopeSpot> spots;
    // fronts は正面 (+z) のある物: 正面のすぐ前が塀・家・高い段なら後ろ向きに置き直す (自販機の正面が壁を向かないように)
    std::vector<SlopeMotion> motions;std::vector<SlopeWalker> walkers;std::vector<std::string> fronts,glideThrough;std::vector<SlopeWander> wanders;
    // 町の乗り物の輪の場所 (おみこし・こま)。近くの平らな所へ寄せて置く
    std::vector<Vec> mikoshi,koma,hydrants;
    const SlopeWander* Wander(const std::string& id)const{for(const auto& w:wanders)if(w.id==id)return &w;return nullptr;}
    const SlopeMotion* Motion(const std::string& id)const{for(const auto& motion:motions)if(motion.id==id)return &motion;return nullptr;}
    const SlopeSet& Set(const std::string& name)const{for(const auto& set:sets)if(set.name==name)return set;throw std::runtime_error("slope_town.json: unknown set "+name);}
    // 色の名前 → 色。マスの種類ごとの地面の色と、擁壁の色 (道に面する所とそれ以外)
    std::vector<std::pair<std::string,Vec>> colors;std::array<std::string,SlopeKindCount> kindColor;std::string roadWall,wall;
    Vec Color(const std::string& name)const{for(const auto& [key,value]:colors)if(key==name)return value;throw std::runtime_error("slope_town.json: unknown color "+name);}
    float H(int i,int j)const{return height[size_t(j)*size_t(n)+size_t(i)];}
    SlopeKind K(int i,int j)const{return SlopeKind(kind[size_t(j)*size_t(n)+size_t(i)]);}
    const std::array<float,4>& Corner(int i,int j)const{return corner[size_t(j)*size_t(n)+size_t(i)];}
    const SlopeRoad* Road(const std::string& id)const{for(const auto& r:roads)if(r.id==id)return &r;return nullptr;}
};
// マスの種類の名前 (slope_town.json の kinds などで使う)
inline int SlopeKindIndex(const std::string& name){
    static const char* names[SlopeKindCount]={"ground","lot","road","stairs","water","rail","field","plaza"};
    for(int i=0;i<SlopeKindCount;++i)if(name==names[i])return i;throw std::runtime_error("slope_town.json: unknown kind "+name);
}
// slope_town.json と slope_town_terrain.json を読み込む (最初に使われたときに 1 回だけ)
inline const SlopeTownData& SlopeData(){
    static const SlopeTownData data=[]{
        const char* file="slope_town.json";const auto& json=Data::Load(file);SlopeTownData out;
        const float u=out.unit=json.at("unit").get<float>();out.cell=u;
        auto half=Data::ReadArray<float,2>(json.at("half"),file);out.halfX=half[0]*u;out.halfZ=half[1]*u;out.outside=json.at("outside").get<float>()*u;
        {const auto& b=Data::Require(json,"border",file);out.border={b.at("height").get<float>()*u,b.at("thick").get<float>()*u,b.at("step").get<float>()*u,b.at("post").get<float>()*u,b.at("color").get<std::string>(),b.at("cap").get<std::string>()};
            if(out.border.step<=0||out.border.post<=0)throw std::runtime_error(std::string(file)+": border step and post must be positive");}
        for(const auto& c:Data::Require(json,"colors",file).items()){auto a=Data::ReadArray<float,3>(c.value(),file);out.colors.push_back({c.key(),{a[0],a[1],a[2]}});}
        {const auto& k=Data::Require(json,"kindColors",file);for(int i=0;i<SlopeKindCount;++i)out.kindColor[size_t(i)]=k.at(i).get<std::string>();
            out.roadWall=json.at("wallColors").at("road").get<std::string>();out.wall=json.at("wallColors").at("other").get<std::string>();}
        for(const auto& a:Data::Require(json,"areas",file))out.areas.push_back({a.at("id").get<std::string>(),a.at("name").get<std::string>(),a.at("below").get<float>()*u});
        if(out.areas.empty())throw std::runtime_error(std::string(file)+": no areas");
        auto point=[&](const nlohmann::json& v){auto a=Data::ReadArray<float,2>(v,file);return Vec{a[0]*u,0,a[1]*u};};
        auto ids=[&](const nlohmann::json& v){return v.contains("ids")?v.at("ids").get<std::vector<std::string>>():std::vector<std::string>{v.at("id").get<std::string>()};};
        constexpr float degree=3.14159265f/180;
        // 1 つずつ置く物は {id, at [x, z], yaw (度), fill, mult, fit (m), landmark}
        for(const auto& p:json.at("props")){SlopeProp prop;prop.id=p.at("id").get<std::string>();prop.at=point(p.at("at"));prop.yaw=p.value("yaw",0.f)*degree;prop.fill=p.value("fill",0.f);
            prop.mult=p.value("mult",1.f);prop.fit=p.value("fit",0.f)*u;prop.landmark=p.value("landmark",false);out.props.push_back(prop);}
        for(const auto& r:json.at("rows")){SlopeRow row;row.ids=ids(r);row.from=point(r.at("from"));row.to=point(r.at("to"));row.step=r.at("step").get<float>()*u;
            row.yaw=r.value("yaw",0.f)*degree;row.fill=r.value("fill",0.f);row.mult=r.value("mult",1.f);row.skip=r.value("skip",0.f);row.jitter=r.value("jitter",0.f)*u;
            if(row.step<=0)throw std::runtime_error(std::string(file)+": row step must be positive");out.rows.push_back(row);}
        for(const auto& s:json.at("scatter")){SlopeScatter scatter;if(s.contains("sets"))scatter.sets=s.at("sets").get<std::vector<std::string>>();else scatter.ids=ids(s);auto a=Data::ReadArray<float,4>(s.at("rect"),file);scatter.rect={a[0]*u,a[1]*u,a[2]*u,a[3]*u};
            scatter.count=s.at("count").get<int>();scatter.fill=s.value("fill",1.f);auto m=s.contains("mult")?Data::ReadArray<float,2>(s.at("mult"),file):std::array<float,2>{1,1};scatter.multLow=m[0];scatter.multHigh=m[1];
            for(const auto& k:s.at("kinds"))scatter.kinds|=1u<<SlopeKindIndex(k.get<std::string>());out.scatters.push_back(scatter);}
        for(const auto& s:json.at("roadside")){SlopeRoadside side;side.ids=ids(s);side.roads=s.value("roads",std::vector<std::string>{});side.step=s.at("step").get<float>()*u;side.offset=s.value("offset",0.f)*u;side.fill=s.value("fill",0.f);
            if(side.step<=0)throw std::runtime_error(std::string(file)+": roadside step must be positive");out.roadside.push_back(side);}
        for(const auto& s:json.at("trails")){SlopeTrail trail;trail.road=s.at("road").get<std::string>();trail.ids=ids(s);trail.from=s.at("from").get<float>()*u;trail.to=s.at("to").get<float>()*u;
            trail.step=s.at("step").get<float>()*u;trail.fill=s.value("fill",1.f);if(trail.step<=0)throw std::runtime_error(std::string(file)+": trail step must be positive");out.trails.push_back(trail);}
        for(const auto& m:json.at("movers")){SlopeMover mover;mover.id=m.at("id").get<std::string>();mover.road=m.value("road",std::string());
            if(m.contains("path"))for(const auto& p:m.at("path"))mover.path.push_back(point(p));mover.speed=m.at("speed").get<float>()*u;mover.fill=m.value("fill",0.f);
            if(mover.road.empty()&&mover.path.size()<2)throw std::runtime_error(std::string(file)+": mover needs a road or 2 points");out.movers.push_back(mover);}
        {const auto& h=Data::Require(json,"houses",file);for(const auto& r:h.at("byRoad").items())out.houses.byRoad.push_back({r.key(),r.value().get<std::vector<std::string>>()});
            out.houses.fallback=h.at("default").get<std::vector<std::string>>();out.houses.fences=h.at("fences").get<std::vector<std::string>>();
            out.houses.fit=h.at("fit").get<float>();out.houses.fill=h.at("fill").get<float>();out.houses.fenceFill=h.at("fenceFill").get<float>();out.houses.sink=h.value("sink",0.f)*u;
            if(out.houses.fallback.empty())throw std::runtime_error(std::string(file)+": houses need a default model");}
        // 組: 名前 → [[id, x, z, yaw (度), mult]] (x, z はメートル)
        for(const auto& set:Data::Require(json,"sets",file).items()){SlopeSet one;one.name=set.key();
            for(const auto& item:set.value()){SlopeSetItem it;it.id=item.at(0).get<std::string>();it.at={item.at(1).get<float>()*u,0,item.at(2).get<float>()*u};
                it.yaw=item.size()>3?item.at(3).get<float>()*degree:0.f;it.mult=item.size()>4?item.at(4).get<float>():1.f;one.items.push_back(it);}
            if(one.items.empty())throw std::runtime_error(std::string(file)+": empty set "+one.name);out.sets.push_back(one);}
        {const auto& l=Data::Require(json,"lotSets",file);for(const auto& r:l.at("byHouse").items())out.lotSets.byHouse.push_back({r.key(),r.value().get<std::vector<std::string>>()});
            for(const auto& r:l.at("byRoad").items())out.lotSets.byRoad.push_back({r.key(),r.value().get<std::vector<std::string>>()});
            out.lotSets.fallback=l.at("default").get<std::vector<std::string>>();out.lotSets.count=l.at("count").get<int>();out.lotSets.fill=l.value("fill",1.f);}
        for(const auto& s:json.at("roadSets")){SlopeRoadSets side;side.sets=s.at("sets").get<std::vector<std::string>>();side.kinds=s.value("kinds",std::vector<std::string>{});
            side.step=s.at("step").get<float>()*u;side.start=s.value("start",0.f)*u;side.offset=s.value("offset",0.f)*u;side.fill=s.value("fill",1.f);side.onRoad=s.value("onRoad",false);
            if(side.step<=0)throw std::runtime_error(std::string(file)+": roadSets step must be positive");out.roadSets.push_back(side);}
        for(const auto& s:json.at("spots")){SlopeSpot spot;spot.set=s.at("set").get<std::string>();spot.at=point(s.at("at"));spot.yaw=s.value("yaw",0.f)*degree;spot.fill=s.value("fill",1.f);out.spots.push_back(spot);}
        for(const auto& m:Data::Require(json,"motion",file).items()){auto a=Data::ReadArray<float,3>(m.value(),file);out.motions.push_back({m.key(),a[0],a[1]*degree,a[2]*u});}
        for(const auto& w:json.at("walkers")){SlopeWalker walker;walker.id=w.at("id").get<std::string>();walker.kinds=w.value("kinds",std::vector<std::string>{});walker.count=w.at("count").get<int>();
            walker.speed=w.at("speed").get<float>()*u;walker.length=w.at("length").get<float>()*u;walker.inset=w.value("inset",0.f)*u;walker.fill=w.value("fill",1.f);
            if(walker.length<=0)throw std::runtime_error(std::string(file)+": walker length must be positive");out.walkers.push_back(walker);}
        out.fronts=json.at("fronts").get<std::vector<std::string>>();
        // こいのぼりがぶつからない背の高い物 (木・電柱など。枝の広がりで止めると、すぐに落ちてしまう)
        out.glideThrough=json.value("glideThrough",std::vector<std::string>{});
        // おみこしとこまの輪 ({"mikoshi": [[x, z], ...], "koma": [...]}、m)
        if(json.contains("rides")){const auto& rides=json.at("rides");for(const auto& p:rides.value("mikoshi",nlohmann::json::array()))out.mikoshi.push_back(point(p));
            for(const auto& p:rides.value("koma",nlohmann::json::array()))out.koma.push_back(point(p));
            for(const auto& p:rides.value("hydrant",nlohmann::json::array()))out.hydrants.push_back(point(p));}
        // 回って歩く物は {id: [輪の半径 (m), 速さ (m/秒), 回る割合, 正面が +z なら 1]}
        for(const auto& w:Data::Require(json,"wander",file).items()){auto a=Data::ReadArray<float,4>(w.value(),file);out.wanders.push_back({w.key(),a[0]*u,a[1]*u,a[2],a[3]>.5f});}
        // 組の名前がそろっているか (読み込むときに確かめる)
        {auto check=[&](const std::string& name){(void)out.Set(name);};
            for(const auto& [k,v]:out.lotSets.byHouse)for(const auto& n:v)check(n);for(const auto& [k,v]:out.lotSets.byRoad)for(const auto& n:v)check(n);for(const auto& n:out.lotSets.fallback)check(n);
            for(const auto& r:out.roadSets)for(const auto& n:r.sets)check(n);for(const auto& sp:out.spots)check(sp.set);for(const auto& sc:out.scatters)for(const auto& n:sc.sets)check(n);}
        // ---- 地形 ----
        const char* terrainFile="slope_town_terrain.json";const auto& t=Data::Load(terrainFile);
        out.halfCells=t.at("half").get<int>();out.n=out.halfCells*2+1;const size_t cells=size_t(out.n)*size_t(out.n);
        {const auto& h=t.at("heights");const std::string kinds=t.at("kinds").get<std::string>();
            if(h.size()!=cells||kinds.size()!=cells)throw std::runtime_error(std::string(terrainFile)+": heights and kinds must cover every cell");
            out.height.resize(cells);out.kind.resize(cells);
            for(size_t i=0;i<cells;++i){out.height[i]=h[i].get<float>()*.01f*u;const int k=kinds[i]-'0';if(k<0||k>=SlopeKindCount)throw std::runtime_error(std::string(terrainFile)+": unknown kind");out.kind[i]=uint8_t(k);}}
        for(const auto& r:t.at("roads")){SlopeRoad road;road.id=r.at("id").get<std::string>();road.name=r.at("name").get<std::string>();road.kind=r.at("kind").get<std::string>();road.width=r.at("width").get<float>()*u;
            for(const auto& p:r.at("path"))road.path.push_back({p.at(0).get<float>()*u,p.at(2).get<float>()*u,p.at(1).get<float>()*u});
            if(road.path.size()<2)throw std::runtime_error(std::string(terrainFile)+": road needs 2 points");out.roads.push_back(road);}
        for(const auto& s:t.at("stairs")){SlopeStairs st;st.id=s.at("id").get<std::string>();st.name=s.at("name").get<std::string>();auto a=Data::ReadArray<float,2>(s.at("from"),terrainFile),b=Data::ReadArray<float,2>(s.at("to"),terrainFile);
            st.from={a[0]*u,s.at("h0").get<float>()*u,a[1]*u};st.to={b[0]*u,s.at("h1").get<float>()*u,b[1]*u};st.width=s.at("width").get<float>()*u;st.steps=std::max(1,s.at("steps").get<int>());out.stairs.push_back(st);}
        // 石段の箱の広さ: 段ごとに、その段の範囲にかかる石段のマス (この石段の近くのもの) の角を軸へ写し、左右の一番外まで広げる。
        // (以前は全部の段を石段の幅 + 2.25m に広げていたので、石段の横を通る道の上に段の板がはみ出していた)
        for(auto& st:out.stairs){const float ax=st.to.x-st.from.x,az=st.to.z-st.from.z,length=std::sqrt(ax*ax+az*az);if(length<1e-3f)continue;
            const float ux=ax/length,uz=az/length,run=length/float(st.steps),reach=st.width*.5f+(SlopeStairsOverlap+SlopeStairsCover)*u+out.cell;
            st.side.assign(size_t(st.steps),{st.width*.5f,st.width*.5f});
            for(int j=0;j<out.n;++j)for(int i=0;i<out.n;++i){if(out.K(i,j)!=SlopeKind::Stairs)continue;
                const float cx=float(i-out.halfCells)*out.cell,cz=float(j-out.halfCells)*out.cell;
                const float ca=(cx-st.from.x)*ux+(cz-st.from.z)*uz,cb=-(cx-st.from.x)*uz+(cz-st.from.z)*ux;
                if(ca<-out.cell||ca>length+out.cell||std::abs(cb)>reach)continue;
                float a0=1e9f,a1=-1e9f,b0=1e9f,b1=-1e9f;
                for(int k=0;k<4;++k){const float x=cx+((k&1)?.5f:-.5f)*out.cell,z=cz+((k&2)?.5f:-.5f)*out.cell,a=(x-st.from.x)*ux+(z-st.from.z)*uz,b=-(x-st.from.x)*uz+(z-st.from.z)*ux;
                    a0=std::min(a0,a);a1=std::max(a1,a);b0=std::min(b0,b);b1=std::max(b1,b);}
                st.back=std::max(st.back,-a0);st.ahead=std::max(st.ahead,a1-length);
                const int k0=std::clamp(int(std::floor(a0/run)),0,st.steps-1),k1=std::clamp(int(std::floor(a1/run)),0,st.steps-1);
                for(int k=k0;k<=k1;++k){auto& e=st.side[size_t(k)];e[0]=std::max(e[0],-b0);e[1]=std::max(e[1],b1);}}
            st.back=std::min(st.back,SlopeStairsCover*u);st.ahead=std::min(st.ahead,SlopeStairsCover*u);}
        for(const auto& l:t.at("lots"))out.lots.push_back({{l.at("x").get<float>()*u,l.at("h").get<float>()*u,l.at("z").get<float>()*u},l.at("yaw").get<float>(),l.at("w").get<float>()*u,l.at("d").get<float>()*u,l.at("road").get<std::string>()});
        for(const auto& p:t.at("plazas"))out.plazas.push_back({p.at("name").get<std::string>(),{p.at("x").get<float>()*u,p.at("h").get<float>()*u,p.at("z").get<float>()*u},p.at("r").get<float>()*u});
        for(const auto& m:out.movers)if(!m.road.empty()&&!out.Road(m.road))throw std::runtime_error(std::string(file)+": mover on unknown road "+m.road);
        for(const auto& tr:out.trails)if(!out.Road(tr.road))throw std::runtime_error(std::string(file)+": trail on unknown road "+tr.road);
        // 角の高さ: 格子の点ごとに、点を囲む 4 マス (石段のマスを除く) を「高さの近いまとまり」に分ける (高さの順に並べて、隣との差が
        // SlopeSmoothStep 未満ならつなぐ)。まとまりの平均を、そのまとまりのマスの角の高さにする。坂はなめらかにつながり、段差は縦の面になる。
        // (以前はマスごとに「自分と近い高さ」の平均にしていたので、3〜4 種類の高さが集まる点では隣のマスと角の高さが食い違い、
        //  面が折れて斜めの三角形ができたり、縦の面がねじれて穴が開いたりしていた)
        // 石段のマスは段の上の面より少し下の平らな面 (上に石段の箱が重なる。石段の横の地面との間にも縦の面を作り、下が見えないようにする)
        out.corner.resize(cells);const float close=SlopeSmoothStep*u,drop=SlopeStairsDrop*u;
        // 石段のマスの高さは、石段の箱と同じ決まり (一番上の段は境内と同じ高さで下げない。下げると境内との境に、斜めのぎざぎざの段が見えていた)
        for(int j=0;j<out.n;++j)for(int i=0;i<out.n;++i){if(out.K(i,j)!=SlopeKind::Stairs)continue;
            const float x=float(i-out.halfCells)*out.cell,z=float(j-out.halfCells)*out.cell;float h=out.H(i,j)-drop;
            for(const auto& s:out.stairs){const float ax=s.to.x-s.from.x,az=s.to.z-s.from.z,length=std::sqrt(ax*ax+az*az);if(length<1e-3f)continue;
                const float a=((x-s.from.x)*ax+(z-s.from.z)*az)/length,b=(-(x-s.from.x)*az+(z-s.from.z)*ax)/length;
                if(a<-out.cell||a>length+out.cell||std::abs(b)>s.width*.5f+SlopeStairsOverlap*u+out.cell)continue;
                const float k=std::clamp(std::floor(a/length*float(s.steps)),0.f,float(s.steps)-1),top=s.from.y+(s.to.y-s.from.y)*(k+1)/float(s.steps);
                h=k>=float(s.steps)-1?top:top-drop;break;}
            out.corner[size_t(j)*size_t(out.n)+size_t(i)].fill(h);}
        for(int vj=0;vj<=out.n;++vj)for(int vi=0;vi<=out.n;++vi){
            // 点を囲む 4 マスと、そのマスでのこの点の角の番号 (-x-z, +x-z, +x+z, -x+z の順)
            const int ci[4]={vi-1,vi,vi,vi-1},cj[4]={vj-1,vj-1,vj,vj},ck[4]={2,3,0,1};
            struct Item {size_t cell;int k;float h;};Item items[4];int count=0;
            for(int m=0;m<4;++m){const int i=ci[m],j=cj[m];if(i<0||j<0||i>=out.n||j>=out.n||out.K(i,j)==SlopeKind::Stairs)continue;items[count++]={size_t(j)*size_t(out.n)+size_t(i),ck[m],out.H(i,j)};}
            std::sort(items,items+count,[](const Item& a,const Item& b){return a.h<b.h;});
            for(int a=0;a<count;){int b=a+1;float total=items[a].h;while(b<count&&items[b].h-items[b-1].h<close){total+=items[b].h;++b;}
                const float average=total/float(b-a);for(int m=a;m<b;++m)out.corner[items[m].cell][size_t(items[m].k)]=average;a=b;}}
        return out;}();
    return data;
}
// 位置のマスの番号 (町の外は端のマス)
inline int SlopeCellX(float x){const auto& d=SlopeData();return std::clamp(int(std::floor(x/d.cell+.5f))+d.halfCells,0,d.n-1);}
inline int SlopeCellZ(float z){const auto& d=SlopeData();return std::clamp(int(std::floor(z/d.cell+.5f))+d.halfCells,0,d.n-1);}
inline SlopeKind SlopeKindAt(Vec p){return SlopeData().K(SlopeCellX(p.x),SlopeCellZ(p.z));}
// 石段の上なら、その段の高さ (石段の外は負の無限大)。段は石段の向きに平らで、下の端から一段ずつ上がる
inline float SlopeStairsHeight(const SlopeStairs& s,Vec p,float margin=0){
    Vec axis{s.to.x-s.from.x,0,s.to.z-s.from.z};const float length=Length(axis);if(length<1e-3f)return -1e9f;axis=axis*(1/length);
    const float a=(p.x-s.from.x)*axis.x+(p.z-s.from.z)*axis.z,b=-(p.x-s.from.x)*axis.z+(p.z-s.from.z)*axis.x;
    if(a<-s.back-margin||a>length+s.ahead+margin)return -1e9f;
    const float k=std::clamp(std::floor(a/length*float(s.steps)),0.f,float(s.steps)-1);
    // 段の箱の左右の広がり (読み込むときに決めた広さ。無ければ石段の幅)
    const float left=s.side.empty()?s.width*.5f:s.side[size_t(k)][0],right=s.side.empty()?s.width*.5f:s.side[size_t(k)][1];
    if(b<-left-margin||b>right+margin)return -1e9f;
    return s.from.y+(s.to.y-s.from.y)*(k+1)/float(s.steps);
}
// その点の地面の高さ: 石段の上は段の高さ、ほかはマスの 4 つの角の高さを補間する (描く地面と同じ形)
inline float SlopeGround(Vec p){
    const auto& d=SlopeData();
    for(const auto& s:d.stairs){const float h=SlopeStairsHeight(s,p);if(h>-1e8f)return h;}
    const float fx=p.x/d.cell+float(d.halfCells),fz=p.z/d.cell+float(d.halfCells);
    const int i=SlopeCellX(p.x),j=SlopeCellZ(p.z);
    const float u=std::clamp(fx-float(i)+.5f,0.f,1.f),v=std::clamp(fz-float(j)+.5f,0.f,1.f);
    const auto& c=d.Corner(i,j);
    return (c[0]*(1-u)+c[1]*u)*(1-v)+(c[3]*(1-u)+c[2]*u)*v;
}
// 擁壁の近くか (半径 margin の中で地面の高さが急に変わる。物をばらまくときに避ける)
inline bool SlopeNearWall(Vec p,float margin){const float h=SlopeGround(p),step=.35f*SlopeData().unit;const float dx[]={1,-1,0,0,.7f,-.7f,.7f,-.7f},dz[]={0,0,1,-1,.7f,.7f,-.7f,-.7f};
    for(int i=0;i<8;++i)if(std::abs(SlopeGround(p+Vec{dx[i]*margin,0,dz[i]*margin})-h)>step)return true;return false;}
// 石段の上か (物をばらまくときに避ける)
// 歩く体が乗る地面の高さ: 石段の上だけは、段の高さではなく下の端から上の端へまっすぐ上がる坂にする (見た目の段はそのまま)。
// 段の高さのままだと、段を 1 つ上るたびに体が約 0.18m ずつ跳ね上がり、カメラも一緒にガクガクしていた
inline constexpr bool SlopeStairsRamp=true;
// 石段の坂として上れる傾き (高さ ÷ 長さ。神社の石段は約 0.7)。体の前の端を調べるとき、この傾きで上がる分は段差に数えない
inline constexpr float SlopeStairsGrade=1.f;
inline float SlopeWalkGround(Vec p){
    if(SlopeStairsRamp)for(const auto& s:SlopeData().stairs){if(SlopeStairsHeight(s,p)<-1e8f)continue;
        Vec axis{s.to.x-s.from.x,0,s.to.z-s.from.z};const float length=Length(axis);const float a=((p.x-s.from.x)*axis.x+(p.z-s.from.z)*axis.z)/std::max(length,1e-3f);
        return s.from.y+(s.to.y-s.from.y)*std::clamp(a/std::max(length,1e-3f),0.f,1.f);}
    return SlopeGround(p);
}
inline bool SlopeOnStairs(Vec p,float margin){for(const auto& s:SlopeData().stairs)if(SlopeStairsHeight(s,p,margin)>-1e8f)return true;return false;}
// 記録に使う区画の番号 (地面の高さで分ける)
inline int SlopeAreaAt(Vec p){const auto& areas=SlopeData().areas;const float h=SlopeGround(p);for(size_t i=0;i<areas.size();++i)if(h<areas[i].below)return int(i);return int(areas.size())-1;}
// カメラの一番低い高さ: 周りの地面 (擁壁のきわでは上の段) より少し上。下の段では床まで下げられる
inline float SlopeCameraFloor(Vec p){float h=SlopeWalkGround(p);const float dx[]={1,-1,0,0},dz[]={0,0,1,-1};
    for(int i=0;i<4;++i)h=std::max(h,SlopeWalkGround(p+Vec{dx[i]*SlopeCameraReach,0,dz[i]*SlopeCameraReach}));return h+SlopeCameraLift;}
// 注視点からカメラまでの視線が地面の下をくぐらない、カメラの一番低い高さ (視線上の点を調べて、地面の上を通るまで持ち上げる)。
// skip より注視点に近い所 (体の中) は調べない。体のすぐ近くの地面で調べると、少しの段差でもカメラが大きく跳ね上がる
inline constexpr int SlopeCameraSamples=24;
// 地形でカメラを持ち上げる・下ろす速さ (1/秒。上げるのは速く、下ろすのはゆっくり)
inline constexpr float SlopeCameraRise=10,SlopeCameraFall=3;
// カメラの注視点が体の粒子の中心を追う速さ (1/秒。坂で体が傾いて中心が揺れても、カメラが横に揺れないように)
inline constexpr float SlopeCameraFocusRate=6;
inline float SlopeCameraOver(Vec focus,Vec eye,float skip){
    float y=eye.y;const float length=std::max(Length(eye-focus),1e-3f);
    for(int k=1;k<=SlopeCameraSamples;++k){const float t=float(k)/float(SlopeCameraSamples);if(t*length<skip)continue;const Vec p=focus+(eye-focus)*t;
        const float need=SlopeWalkGround(p)+SlopeCameraLift;const float line=focus.y+(y-focus.y)*t;
        if(line<need)y=focus.y+(need-focus.y)/t;}
    return y;
}
// カメラの置き場所: 地面に視線をさえぎられないように、まず上げる (距離の SlopeCameraMaxLift 倍まで)。
// それでも足りない所 (体のすぐ後ろに高い段がある所) は、真上から見下ろすほど上げる代わりに、さえぎる段の手前までカメラを体へ寄せる。
// 寄せる先は、体のすぐ後ろの段も含めて調べる (体の大きさの内側を飛ばすと、体の真後ろの壁を見落として、壁の上からカメラが地面の中を見ていた)。
// 体へ寄せるのは、体の大きさの半分 (skip の半分) まで
inline constexpr float SlopeCameraMaxLift=.6f;
inline Vec SlopeCameraResolve(Vec focus,Vec eye,float skip){
    const float distance=std::max(Length(eye-focus),1e-3f);
    Vec lifted=eye;lifted.y=std::max(eye.y,SlopeCameraFloor(eye));lifted.y=SlopeCameraOver(focus,lifted,skip);
    if(lifted.y-eye.y<=SlopeCameraMaxLift*distance)return lifted;
    Vec capped=eye;capped.y=eye.y+SlopeCameraMaxLift*distance;float keep=1;
    const float nearest=std::min(1.f,skip*.5f/distance);
    for(int k=1;k<=SlopeCameraSamples*2;++k){const float t=float(k)/float(SlopeCameraSamples*2);const Vec p=focus+(capped-focus)*t;
        if(p.y<SlopeGround(p)+SlopeCameraLift){keep=t-1.f/float(SlopeCameraSamples*2);break;}}
    keep=std::max(keep,nearest);
    // (変数名に near は使わない: Windows のヘッダーが空のマクロにしている)
    Vec closer=focus+(capped-focus)*keep;closer.y=std::max(closer.y,SlopeCameraFloor(closer));return closer;
}
// 歩ける範囲 (町の端)
inline bool SlopeInside(Vec p,float margin){const auto& d=SlopeData();return std::abs(p.x)+margin<=d.halfX&&std::abs(p.z)+margin<=d.halfZ;}
// 道の長さと、始まりから s だけ進んだ点 (y は道の高さ)。dir に進む向き (水平) を返す
inline float SlopeRoadLength(const SlopeRoad& road){float L=0;for(size_t i=1;i<road.path.size();++i)L+=Length(Vec{road.path[i].x-road.path[i-1].x,0,road.path[i].z-road.path[i-1].z});return L;}
inline Vec SlopeRoadPoint(const SlopeRoad& road,float s,Vec* dir=nullptr){
    for(size_t i=1;i<road.path.size();++i){const Vec a=road.path[i-1],b=road.path[i];const Vec flat{b.x-a.x,0,b.z-a.z};const float L=Length(flat);
        if(s<=L||i+1==road.path.size()){if(dir)*dir=L>1e-4f?flat*(1/L):Vec{0,0,1};const float t=L>1e-4f?std::clamp(s/L,0.f,1.f):0.f;return a+(b-a)*t;}s-=L;}
    if(dir)*dir={0,0,1};return road.path.empty()?Vec{}:road.path.back();
}
}
