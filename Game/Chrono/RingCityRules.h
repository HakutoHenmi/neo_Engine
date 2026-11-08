// RingCityRules.h
// 吸収モードの町 (中心の路地裏から外へ、住宅街の輪が広がる) の規則。エンジンに依存しない。
// 輪の大きさ・道路の間隔・階数・アイテムと炎の位置は Resources/Data/ring_city.json に置く。
// 建物は道路の格子で区切った区画に並べ、区画は 1 つの輪の中に収まるものだけを使う (輪の境目は広い通りになる)。
// 建物は大きさが足りるまで通れない壁で、飲み込むと通れるようになる
// 輪の境目には壁が立ち、門 (大通りの上に 4 つ) を飲み込むか砕くと壁が崩れて次の輪が開く。
// 大通りは中心から町の端まで十字に伸びる見通しのよい道で、ごみの列が並ぶ (タイヤの道)。
// 広場は建物の無い区画で、ごちそうの山と真ん中の大物がある
#pragma once
#include "CityStageRules.h"
#include <string>
#include <vector>
namespace Game::Chrono {
// 輪の種類 (路地裏・住宅街)
enum class RingStyle : uint8_t {Alley,House};
inline constexpr int RingStyleCount=2;
// 輪: 外側の端 (中心からの x と z の大きい方)・道路の格子の間隔と幅・1 区画の建物の数・階数の範囲・1 階の高さ・小物の数
struct RingZone {RingStyle style=RingStyle::Alley;std::string name;float outer=0,block=0,road=0;int lotsX=1,lotsZ=1,floorsMin=1,floorsMax=1;float floorHeight=19;int props=0;
    // 大通りの幅・門の半径 (この輪へ入る門)・広場の数・広場ごとのごちそうの数・大通り 1 本のごみの数
    float avenue=0,gateRadius=0;int plazas=0,plazaFood=0,lineFood=0;};
struct RingCityData {std::vector<RingZone> zones;std::vector<CityFireSpot> fires;};
// ring_city.json を読み込む (最初に使われたときに 1 回だけ)
inline const RingCityData& RingData(){
    static const RingCityData data=[]{
        const char* file="ring_city.json";const auto& json=Data::Load(file);RingCityData out;
        const char* ids[]={"Alley","House"};
        for(const auto& z:Data::Require(json,"zones",file)){RingZone zone;std::string id=z.at("id").get<std::string>();
            int style=-1;for(int i=0;i<RingStyleCount;++i)if(id==ids[i])style=i;if(style<0)throw std::runtime_error(std::string(file)+": unknown zone "+id);
            zone.style=RingStyle(style);zone.name=z.at("name").get<std::string>();zone.outer=z.at("outer").get<float>();
            if(zone.style!=RingStyle::Alley){zone.block=z.at("block").get<float>();zone.road=z.at("road").get<float>();
                auto lots=Data::ReadArray<int,2>(z.at("lots"),file);zone.lotsX=lots[0];zone.lotsZ=lots[1];
                auto floors=Data::ReadArray<int,2>(z.at("floors"),file);zone.floorsMin=floors[0];zone.floorsMax=floors[1];
                zone.floorHeight=z.at("floorHeight").get<float>();zone.props=z.at("props").get<int>();
                zone.avenue=z.at("avenue").get<float>();zone.gateRadius=z.at("gateRadius").get<float>();zone.plazas=z.at("plazas").get<int>();zone.plazaFood=z.at("plazaFood").get<int>();}
            zone.lineFood=z.at("lineFood").get<int>();
            if(!out.zones.empty()&&zone.outer<=out.zones.back().outer)throw std::runtime_error(std::string(file)+": zones must grow outward");
            out.zones.push_back(zone);}
        if(out.zones.size()!=size_t(RingStyleCount))throw std::runtime_error(std::string(file)+": zones must be Alley, House");
        for(const auto& f:json.at("fires")){auto a=Data::ReadArray<float,3>(f,file);out.fires.push_back({{a[0],0,a[1]},a[2]});}
        return out;}();
    return data;
}
inline const std::vector<RingZone>& RingZones(){return RingData().zones;}
// 中心からの距離 (x と z の大きい方。輪は正方形)
inline float RingDistance(Vec p){return std::max(std::abs(p.x),std::abs(p.z));}
// 位置の輪の番号 (町の外は一番外の輪)
inline int RingZoneAt(Vec p){float d=RingDistance(p);const auto& zones=RingZones();
    for(size_t i=0;i<zones.size();++i)if(d<zones[i].outer)return int(i);return int(zones.size())-1;}
// 町の外の端 (これより外へは出られない)
inline float RingCityHalf(){return RingZones().back().outer;}
// 整数から 0〜1 の疑似乱数
inline float RingNoise(int seed){float n=std::sin(float(seed)*12.9898f+78.233f)*43758.5453f;return n-std::floor(n);}
// 建物の敷地: 敷地の矩形・輪の番号・階数・見た目の種類 (同じ輪の中で使い回す型の番号)・正面の向き (0〜3。+z から 90 度ずつ)
// gate は門なら開ける輪の番号 (0 は普通の建物)、height は門の高さ
struct RingLot {CityRect foot{};int zone=0,floors=1,variant=0,facing=0,gate=0;float height=0;float top=0;};
// 輪の型の数 (同じ輪の建物は、この数の型の見た目を使い回す)
inline constexpr int RingVariants=6;
// 矩形の中で中心に一番近い点・一番遠い点の距離 (x と z の大きい方)
inline float RingNearest(const CityRect& r){float x=r.x0<=0&&r.x1>=0?0:std::min(std::abs(r.x0),std::abs(r.x1)),z=r.z0<=0&&r.z1>=0?0:std::min(std::abs(r.z0),std::abs(r.z1));return std::max(x,z);}
inline float RingFarthest(const CityRect& r){return std::max(std::max(std::abs(r.x0),std::abs(r.x1)),std::max(std::abs(r.z0),std::abs(r.z1)));}
// 広場: 中心・半径・輪の番号。輪の真ん中に、大通りを避けて等間隔に並べる
struct RingPlaza {Vec at{};float radius=0;int zone=0;};
inline std::vector<RingPlaza> RingPlazas(){
    std::vector<RingPlaza> plazas;const auto& zones=RingZones();
    for(size_t z=1;z<zones.size();++z){const auto& zone=zones[z];float inner=zones[z-1].outer,mid=(inner+zone.outer)*.5f;
        for(int k=0;k<zone.plazas;++k){float a=6.2831853f*(float(k)+.5f)/float(zone.plazas),x=std::cos(a),y=std::sin(a),edge=std::max(std::abs(x),std::abs(y));
            plazas.push_back({{x/edge*mid,0,y/edge*mid},.2f*(zone.outer-inner),int(z)});}}
    return plazas;
}
// 輪の壁の位置 (z は壁の向こうの輪の番号) と高さ
inline float RingWallAt(int z){return RingZones()[size_t(z-1)].outer;}
inline float RingWallHeight(int z){return 1.1f*RingZones()[size_t(z)].gateRadius;}
// 道路の格子で区切った区画 (歩道の範囲) と輪の番号。輪に収まる区画だけ
struct RingBlock {CityRect rect{};int zone=0;};
inline std::vector<RingBlock> RingBlocks(){
    std::vector<RingBlock> blocks;const auto& zones=RingZones();const auto plazas=RingPlazas();
    for(size_t z=1;z<zones.size();++z){const auto& zone=zones[z];float inner=zones[z-1].outer,outer=zone.outer,b=zone.block,half=zone.road*.5f;
        int n=int(std::ceil(outer/b));
        for(int ix=-n;ix<n;++ix)for(int iz=-n;iz<n;++iz){CityRect block{float(ix)*b+half,float(iz)*b+half,float(ix+1)*b-half,float(iz+1)*b-half};
            // 輪の境目にかかる区画は使わない (境目は広い通りになる)
            if(RingNearest(block)<inner+half||RingFarthest(block)>outer-half)continue;
            // 大通りと広場にかかる区画も使わない
            float avenue=zone.avenue*.5f;if((block.x0<avenue&&block.x1>-avenue)||(block.z0<avenue&&block.z1>-avenue))continue;
            bool open=false;for(const auto& plaza:plazas){float dx=std::max({block.x0-plaza.at.x,0.f,plaza.at.x-block.x1}),dz=std::max({block.z0-plaza.at.z,0.f,plaza.at.z-block.z1});open|=dx*dx+dz*dz<plaza.radius*plaza.radius;}
            if(open)continue;
            blocks.push_back({block,int(z)});}}
    return blocks;
}
// 敷地を並べる: 輪ごとに道路の格子で区画を作り、輪に収まる区画を lotsX × lotsZ の敷地に分ける。
// 型 (variant) は階数の範囲を型の数で分けたもので、同じ型は同じ形・同じ材質になる
inline std::vector<RingLot> RingLots(){
    std::vector<RingLot> lots;const auto& zones=RingZones();int seed=0;
    for(const auto& [block,z]:RingBlocks()){const auto& zone=zones[size_t(z)];{
            float w=(block.x1-block.x0)/float(zone.lotsX),d=(block.z1-block.z0)/float(zone.lotsZ),gap=std::min(6.f,w*.08f);
            for(int lx=0;lx<zone.lotsX;++lx)for(int lz=0;lz<zone.lotsZ;++lz){
                CityRect foot{block.x0+float(lx)*w+gap,block.z0+float(lz)*d+gap,block.x0+float(lx+1)*w-gap,block.z0+float(lz+1)*d-gap};
                RingLot lot;lot.foot=foot;lot.zone=z;lot.variant=int(RingNoise(++seed)*RingVariants)%RingVariants;
                lot.floors=zone.floorsMin+(zone.floorsMax-zone.floorsMin)*lot.variant/std::max(1,RingVariants-1);
                // 正面は区画の外側 (道路側) を向ける
                Vec c=foot.Center(),bc=block.Center();
                lot.facing=zone.lotsX==1&&zone.lotsZ==1?int(RingNoise(++seed)*4)%4:std::abs(c.x-bc.x)>std::abs(c.z-bc.z)?(c.x>bc.x?1:3):(c.z>bc.z?0:2);
                lots.push_back(lot);}}}
    // 門: 輪の壁の上、大通りの真ん中に 4 つ。大きさがそのまま飲み込むのに必要な量になる
    for(size_t z=1;z<zones.size();++z){float r=zones[z].gateRadius,at=zones[z-1].outer,t=std::clamp(r*.25f,3.f,24.f)*.5f;
        for(int side=0;side<4;++side){RingLot lot;lot.zone=int(z);lot.gate=int(z);lot.variant=RingVariants;lot.height=1.25f*r;lot.facing=side%2;
            float s=side<2?at:-at;lot.foot=side%2==0?CityRect{-r,s-t,r,s+t}:CityRect{s-t,-r,s+t,r};lots.push_back(lot);}}
    return lots;
}
// 建物の高さ (1 階は少し高い)
inline float RingLotHeight(const RingLot& lot){if(lot.height>0)return lot.height;const auto& zone=RingZones()[size_t(lot.zone)];return zone.floorHeight*(float(lot.floors)+.3f);}
// 建物を吸収の対象にするときの半径 (幅と高さの大きい方の半分) と、壁までの余裕 (中心から壁までの距離の目安)
inline float RingLotRadius(const RingLot& lot){return .5f*std::max({lot.foot.x1-lot.foot.x0,lot.foot.z1-lot.foot.z0,RingLotHeight(lot)});}
inline float RingLotReach(const RingLot& lot){return .5f*std::min(lot.foot.x1-lot.foot.x0,lot.foot.z1-lot.foot.z0);}
// 半径 margin の円が歩けるか: 町の中で、まだ飲み込んでいない建物 (blocking が true の敷地) に入らない
template<class Blocking>
inline bool RingWalkable(Vec p,float margin,const std::vector<RingLot>& lots,Blocking&& blocking){
    float edge=RingCityHalf()-2;
    if(std::abs(p.x)+margin>edge||std::abs(p.z)+margin>edge)return false;
    for(size_t i=0;i<lots.size();++i){const auto& f=lots[i].foot;
        if(p.x+margin<f.x0||p.x-margin>f.x1||p.z+margin<f.z0||p.z-margin>f.z1)continue;
        // 円と矩形が重なるか (角は丸く)
        float dx=std::max({f.x0-p.x,0.f,p.x-f.x1}),dz=std::max({f.z0-p.z,0.f,p.z-f.z1});
        if(dx*dx+dz*dz<margin*margin&&blocking(i))return false;}
    return true;
}
}
