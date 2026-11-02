// RingCity.cpp
// 吸収モードの町 (ChronoSystem の一部)。中心の路地裏から外へ、住宅街・商店街・オフィス街・高層街の輪が広がる
// (配置の規則は Game/Chrono/RingCityRules.h、数値は Resources/Data/ring_city.json)。
// 建物は輪ごとに数種類の型を作って使い回し (同じ型は同じメッシュなのでまとめて描ける)、1 棟ずつ別のエンティティにする。
// 建物も吸収の対象で、飲み込むと見た目と当たり判定が消え、通れるようになる。
// 素材はすべて CC0 (Poly Haven・ambientCG。Resources/Credits/CityRing.md)
#include "CityBuilder.h"
#include <filesystem>
#include <tuple>

namespace Game {
using namespace CityParts;
namespace {
using Chrono::Vec;
// 建物の型: 材質ごとのメッシュと高さ
struct RingPiece {int material=0;uint32_t mesh=0;};
struct RingTemplate {std::vector<RingPiece> pieces;float height=0;};
// 壁に窓を並べる: 壁の中心 center・壁に沿う向き along・外向き out・壁の幅 width。各階の高さ y0 + floor*height
void RingWindows(CityBuilder& b,int material,Vec center,Vec along,Vec out,float width,int floors,float y0,float floorHeight,float spacing,float halfWidth,float skip=0){
    int count=std::max(1,int(width/spacing));float step=width/float(count);
    for(int f=0;f<floors;++f)for(int i=0;i<count;++i){float s=-width*.5f+step*(float(i)+.5f);if(f==0&&std::abs(s)<skip)continue;
        b.FacadeBox(material,center+along*s+Vec{0,y0+floorHeight*(float(f)+.55f),0},along,out,halfWidth,floorHeight*.24f,0,.25f);}
}
// 4 面の壁に窓を並べる (箱の半分の大きさ hw・hd)
void RingWindowsAround(CityBuilder& b,int material,float hw,float hd,int floors,float y0,float floorHeight,float spacing,float halfWidth,float frontSkip=0){
    RingWindows(b,material,{0,0,hd},{1,0,0},{0,0,1},hw*2-6,floors,y0,floorHeight,spacing,halfWidth,frontSkip);
    RingWindows(b,material,{0,0,-hd},{1,0,0},{0,0,-1},hw*2-6,floors,y0,floorHeight,spacing,halfWidth);
    RingWindows(b,material,{hw,0,0},{0,0,1},{1,0,0},hd*2-6,floors,y0,floorHeight,spacing,halfWidth);
    RingWindows(b,material,{-hw,0,0},{0,0,1},{-1,0,0},hd*2-6,floors,y0,floorHeight,spacing,halfWidth);
}
// 平らな屋根と、縁の手すり壁
void RingFlatRoof(CityBuilder& b,int roof,int trim,float hw,float hd,float h){
    b.Polygon(roof,{{-hw,h,-hd},{hw,h,-hd},{hw,h,hd},{-hw,h,hd}},{0,1,0});
    const float t=1.2f,p=3;
    b.Box(trim,{-hw,h,-hd},{hw,h+p,-hd+t});b.Box(trim,{-hw,h,hd-t},{hw,h+p,hd});
    b.Box(trim,{-hw,h,-hd},{-hw+t,h+p,hd});b.Box(trim,{hw-t,h,-hd},{hw,h+p,hd});
}
// 型の形を作る。W と D は敷地の幅と奥行き (ローカルの x と z。正面は +z)、variant で材質と形を変える
void RingBuildTemplate(CityBuilder& b,Chrono::RingStyle style,int variant,float W,float D,int floors,float fh){
    using Chrono::RingStyle;
    if(style==RingStyle::House){
        // 2〜3 階の家: 壁 (レンガ・しっくい・板張り)・切妻の瓦屋根・窓・玄関・煙突。前と横に庭を残す
        const int walls[]={CityBrick,CityPlaster,CitySiding},roofs[]={CityRoofClay,CityRoofGrey,CityRoofSlate};
        int wall=walls[variant%3],roof=roofs[(variant+variant/3)%3];
        float hw=W*.4f,hd=D*.38f,h=fh*float(floors),rise=hd*.75f,o=1.8f,eave=h-o*rise/hd;
        b.Walls(wall,{-hw,0,-hd},{hw,h,hd});
        // 切妻の屋根 (棟は x の向き) と、両端の三角の壁
        b.Polygon(roof,{{-hw-o,eave,hd+o},{hw+o,eave,hd+o},{hw+o,h+rise,0},{-hw-o,h+rise,0}},{0,hd,rise});
        b.Polygon(roof,{{hw+o,eave,-hd-o},{-hw-o,eave,-hd-o},{-hw-o,h+rise,0},{hw+o,h+rise,0}},{0,hd,-rise});
        b.Polygon(wall,{{hw,h,-hd},{hw,h,hd},{hw,h+rise,0}},{1,0,0});
        b.Polygon(wall,{{-hw,h,hd},{-hw,h,-hd},{-hw,h+rise,0}},{-1,0,0});
        RingWindowsAround(b,CityGlass,hw,hd,floors,0,fh,13,2.6f,6);
        // 玄関の扉と、上の小さな庇
        b.FacadeBox(CityWood,{0,fh*.4f,hd},{1,0,0},{0,0,1},2.6f,fh*.4f,0,.35f);
        b.FacadeBox(roof,{0,fh*.86f,hd},{1,0,0},{0,0,1},4.5f,.35f,0,3.2f);
        // 煙突
        if(variant%2==0)b.Box(CityWhiteBrick,{hw*.45f,h,-hd*.4f},{hw*.45f+3.2f,h+rise+4,-hd*.4f+3.2f});
        return;
    }
    if(style==RingStyle::Shop){
        // 4〜6 階の店: 1 階はシャッターとガラスの店先と庇、上の階は窓、平らな屋根と手すり壁と室外機
        const int walls[]={CityWhiteBrick,CityFacadeTiles,CityCladding,CityGreyTiles};
        int wall=walls[variant%4];float hw=W*.47f,hd=D*.45f,h=fh*float(floors)+4;
        b.Walls(wall,{-hw,0,-hd},{hw,h,hd});RingFlatRoof(b,CitySlab,CityConcrete,hw,hd,h);
        // 店先: 区切りごとにシャッター (閉まった店) かガラス (開いた店)
        int bays=std::max(1,int((hw*2-4)/16));float bay=(hw*2-4)/float(bays);
        for(int i=0;i<bays;++i){float x=-hw+2+bay*(float(i)+.5f);bool shut=(i+variant)%3==0;
            b.FacadeBox(shut?((i+variant)%2?CityRustShutter:CityShutter):CityGlass,{x,fh*.42f,hd},{1,0,0},{0,0,1},bay*.42f,fh*.38f,0,.3f);}
        b.FacadeBox(CityMetal,{0,fh*.9f,hd},{1,0,0},{0,0,1},hw-1,.45f,0,5);
        RingWindows(b,CityGlass,{0,0,hd},{1,0,0},{0,0,1},hw*2-6,floors-1,fh,fh,11,3);
        RingWindows(b,CityGlass,{0,0,-hd},{1,0,0},{0,0,-1},hw*2-6,floors,0,fh,11,3);
        RingWindows(b,CityGlass,{hw,0,0},{0,0,1},{1,0,0},hd*2-6,floors,0,fh,11,3);
        RingWindows(b,CityGlass,{-hw,0,0},{0,0,1},{-1,0,0},hd*2-6,floors,0,fh,11,3);
        b.Box(CityMetal,{-hw*.5f,h,-hd*.3f},{-hw*.5f+6,h+4,-hd*.3f+5});b.Box(CityMetal,{hw*.2f,h,hd*.1f},{hw*.2f+5,h+3.5f,hd*.1f+5});
        return;
    }
    if(style==RingStyle::Office){
        // 8〜12 階のオフィス: コンクリートの低層部、上はガラスの外壁、屋上の機械室
        const int facades[]={CityOffice1,CityOffice2,CityOffice3,CityOffice4};
        int facade=facades[variant%4];float hw=W*.45f,hd=D*.42f,base=fh*1.3f,h=fh*float(floors)+6;
        b.Walls(CitySlab,{-hw-1.5f,0,-hd-1.5f},{hw+1.5f,base,hd+1.5f});
        b.Polygon(CitySlab,{{-hw-1.5f,base,-hd-1.5f},{hw+1.5f,base,-hd-1.5f},{hw+1.5f,base,hd+1.5f},{-hw-1.5f,base,hd+1.5f}},{0,1,0});
        b.Walls(facade,{-hw,base,-hd},{hw,h,hd});RingFlatRoof(b,CitySlab,CityDarkTiles,hw,hd,h);
        // 入口のガラス
        b.FacadeBox(CityGlass,{0,fh*.5f,hd+1.5f},{1,0,0},{0,0,1},std::min(14.f,hw*.4f),fh*.45f,0,.3f);
        b.Box(CityDarkTiles,{-hw*.4f,h,-hd*.4f},{hw*.4f,h+fh*.8f,hd*.2f});
        return;
    }
    // 18〜32 階の高層ビル: 石の低層部、下 7 割は敷地いっぱい、上は一回り細く、頂部の冠とアンテナ
    const int facades[]={CityTower1,CityTower2,CityTower3,CityTower4,CityTower5};
    int facade=facades[variant%5];float hw=W*.44f,hd=D*.44f,base=fh*2,h=fh*float(floors)+8,mid=base+(h-base)*.7f,hw2=hw*.8f,hd2=hd*.8f;
    b.Walls(CityDarkTiles,{-hw-2,0,-hd-2},{hw+2,base,hd+2});
    b.Polygon(CitySlab,{{-hw-2,base,-hd-2},{hw+2,base,-hd-2},{hw+2,base,hd+2},{-hw-2,base,hd+2}},{0,1,0});
    b.Walls(facade,{-hw,base,-hd},{hw,mid,hd});
    b.Polygon(CitySlab,{{-hw,mid,-hd},{hw,mid,-hd},{hw,mid,hd},{-hw,mid,hd}},{0,1,0});
    b.Walls(facade,{-hw2,mid,-hd2},{hw2,h,hd2});RingFlatRoof(b,CitySlab,CityDarkTiles,hw2,hd2,h);
    b.FacadeBox(CityGlass,{0,fh*.8f,hd+2},{1,0,0},{0,0,1},std::min(20.f,hw*.4f),fh*.7f,0,.3f);
    b.Box(CityDarkTiles,{-hw2*.6f,h,-hd2*.6f},{hw2*.6f,h+fh*1.2f,hd2*.6f});
    if(variant%2==0)b.Box(CityMetal,{-.8f,h+fh*1.2f,-.8f},{.8f,h+fh*1.2f+40,.8f});
}
// 輪ごとの吸収の量の割合 (建物は中が空いているので、大きさの割に少し少ない)
float RingFill(Chrono::RingStyle style){using Chrono::RingStyle;return style==RingStyle::House?.5f:style==RingStyle::Shop?.45f:style==RingStyle::Office?.4f:.35f;}
}
// 輪の町の地面: 町全体のアスファルト・路地裏の床・区画の歩道と縁石・道路の中央線
void ChronoSystem::BuildRingGround(CityBuilder& city){
    using namespace Chrono;
    // (大きくなると遠くまで見えるので、地面は町の外まで広く敷く)
    const float half=RingCityHalf()+4000,alley=RingZones()[0].outer;
    city.Polygon(CityRoad,{{-half,-.2f,-half},{half,-.2f,-half},{half,-.2f,half},{-half,-.2f,half}},{0,1,0});
    city.Polygon(CityAlleyFloor,{{-alley,0,-alley},{alley,0,-alley},{alley,0,alley},{-alley,0,alley}},{0,1,0});
    for(const auto& block:RingBlocks()){const auto& w=block.rect;auto style=RingZones()[size_t(block.zone)].style;
        int floor=style==RingStyle::House?CityPaving:style==RingStyle::Shop?CityPavingBrick:CitySidewalk;
        city.Polygon(floor,{{w.x0,.15f,w.z0},{w.x1,.15f,w.z0},{w.x1,.15f,w.z1},{w.x0,.15f,w.z1}},{0,1,0});
        // 縁石
        city.Box(CityConcrete,{w.x0-.6f,-.05f,w.z0},{w.x0+.6f,.45f,w.z1});city.Box(CityConcrete,{w.x1-.6f,-.05f,w.z0},{w.x1+.6f,.45f,w.z1});
        city.Box(CityConcrete,{w.x0,-.05f,w.z0-.6f},{w.x1,.45f,w.z0+.6f});city.Box(CityConcrete,{w.x0,-.05f,w.z1-.6f},{w.x1,.45f,w.z1+.6f});
        // 東と北の道路の中央の破線 (隣の区画と重ならないよう、各区画は 2 辺だけ塗る)
        const float road=RingZones()[size_t(block.zone)].road*.5f;
        for(float z=w.z0+4;z<w.z1-4;z+=22)city.Strip(CityPaint,{w.x1+road,0,z},{w.x1+road,0,z+11},.9f,PaintLaneY);
        for(float x=w.x0+4;x<w.x1-4;x+=22)city.Strip(CityPaint,{x,0,w.z1+road},{x+11,0,w.z1+road},.9f,PaintLaneY);}
}
// 輪の町の建物・小物・お助け変形アイテム
void ChronoSystem::BuildRingCity(entt::registry& r,const RingCityTools& tools){
    using namespace Chrono;auto* render=Engine::Renderer::GetInstance();
    ringLots_=RingLots();ringLotProps_.assign(ringLots_.size(),-1);devourBuildingParts_.clear();
    // 建物の型 (輪・型の番号・90 度回したか) ごとのメッシュ
    std::map<std::tuple<int,int,bool>,RingTemplate> templates;
    auto shape=[&](const RingLot& lot,bool turned)->const RingTemplate& {
        auto key=std::make_tuple(lot.zone,lot.variant,turned);auto found=templates.find(key);if(found!=templates.end())return found->second;
        const auto& zone=RingZones()[size_t(lot.zone)];float w=lot.foot.x1-lot.foot.x0,d=lot.foot.z1-lot.foot.z0;if(turned)std::swap(w,d);
        CityBuilder b;RingBuildTemplate(b,zone.style,lot.variant,w,d,lot.floors,zone.floorHeight);
        RingTemplate t;t.height=RingLotHeight(lot);
        for(int m=0;m<CityMaterialCount;++m){auto& g=b.geometry[size_t(m)];if(g.indices.empty())continue;t.pieces.push_back({m,render->CreateDynamicMesh(g.vertices,g.indices)});}
        // 一番広い部位 (壁) を最初にする (体の中に見せるときに使う)
        std::stable_sort(t.pieces.begin(),t.pieces.end(),[&](const RingPiece& a,const RingPiece& c){return b.geometry[size_t(a.material)].indices.size()>b.geometry[size_t(c.material)].indices.size();});
        return templates.emplace(key,std::move(t)).first->second;};
    // 建物を 1 棟ずつ置く: 部位ごとのエンティティ・カメラ用の当たり判定・吸収の対象
    for(size_t i=0;i<ringLots_.size();++i){const auto& lot=ringLots_[i];
        // 正面を ±x に向ける建物は、型を 90 度回して作る
        bool turned=lot.facing%2==1;const auto& t=shape(lot,turned);
        V center=lot.foot.Center();float yaw=float(lot.facing)*1.5707963f;
        entt::entity main=entt::null;std::vector<entt::entity> parts;
        for(const auto& piece:t.pieces){auto e=r.create();r.emplace<NameComponent>(e,"Chrono City ring building");
            // カメラとプレイヤーの間に入ったら透かす (カメラは建物の手前へ寄せない)
            r.emplace<CameraOccluder>(e,V{center.x,t.height*.5f,center.z},V{lot.foot.x1-lot.foot.x0,t.height,lot.foot.z1-lot.foot.z0},1.f);
            auto& tr=r.emplace<TransformComponent>(e);tr.translate=Write(center);tr.rotate=Write(V{0,yaw,0});tr.scale={1,1,1};
            auto& mr=r.emplace<MeshRendererComponent>(e);const auto& skin=tools.surface(piece.material);
            mr.modelHandle=piece.mesh;mr.textureHandle=skin.texture;mr.extraTextureHandles=skin.extra;mr.useCubemap=false;
            mr.shaderName=piece.material==CityGlass?"CityGlass":"CitySurface";mr.color=tools.tint(piece.material);
            if(main==entt::null){main=e;cityVisuals_[uint32_t(e)]={piece.mesh,skin.texture,skin.extra,{1,1,1},{0,yaw,0},{0,t.height*.5f,0},t.height,mr.shaderName};}
            else parts.push_back(e);}
        // カメラが建物にめり込まないための当たり判定
        auto collider=r.create();r.emplace<NameComponent>(collider,"Chrono City building collision");
        r.emplace<TransformComponent>(collider).translate=Write(V{center.x,t.height*.5f,center.z});
        r.emplace<BoxColliderComponent>(collider).size=Write(V{lot.foot.x1-lot.foot.x0,t.height,lot.foot.z1-lot.foot.z0});
        r.emplace<TagComponent>(collider).tag=TagType::Wall;r.emplace<Solid>(collider);parts.push_back(collider);
        // 吸収の対象: 半径は幅と高さの大きい方の半分、壁までの余裕は短い辺の半分
        cityProps_.push_back({main,V{center.x,1.2f,center.z},1,0,LifeMorphItem::None,RingLotRadius(lot),RingFill(RingZones()[size_t(lot.zone)].style),RingLotReach(lot)});
        ringLotProps_[i]=int(cityProps_.size()-1);devourBuildingParts_[cityProps_.size()-1]=std::move(parts);}
    // 小物: 輪ごとの種類 (モデル・幅・吸収の量の割合)。歩ける所にばらまく。
    // (木のモデルは葉が非常に細かく、読み込みと遠景の簡略化に数十秒かかるので使わない)
    struct Kind {const char* id;float width,fill;};
    const std::vector<Kind> kinds[]={
        // 路地裏: ゴミ袋・段ボール・木箱・ケース
        {{"trashbag",3.4f,1},{"cardboard_box_01",3.2f,1},{"plastic_crate_01",3,1},{"plastic_crate_02",3.4f,1},{"metal_jerrycan",2.6f,1},{"WetFloorSign_01",2.4f,1},
         {"wooden_crate_01",4.6f,1},{"industrial_pastic_container",6,1},{"plastic_container",5.6f,1},{"propane_tank",3.6f,1},{"wooden_crate_02",6.8f,1},{"hand_truck",4,1}},
        // 住宅街: 植木鉢・ベンチ・テーブル・椅子・ゴミ箱・猫の像・室外機・車
        {{"planter_box_01",8,1},{"planter_box_02",8,1},{"potted_plant_01",4,.6f},{"potted_plant_02",4,.6f},{"painted_wooden_bench",11,.6f},{"wooden_picnic_table",11,.6f},
         {"plastic_monobloc_chair_01",3.6f,.5f},{"metal_trash_can",4,1},{"concrete_cat_statue",3,1},{"exterior_aircon_unit",5.5f,1},{"covered_car",26,.8f},
         {"trashbag",3.4f,1},{"cardboard_box_01",3.2f,1}},
        // 商店街: 街灯・テーブルセット・コーヒーの屋台・樽・車・公衆電話・配電盤・木箱
        {{"street_lamp_01",8,.2f},{"street_lamp_02",8,.2f},{"outdoor_table_chair_set_01",10,.6f},{"CoffeeCart_01",12,.8f},{"Barrel_01",3.6f,1},{"wooden_barrels_01",9,.9f},
         {"covered_car",26,.8f},{"korean_public_payphone_01",4.4f,1},{"utility_box_01",5.4f,1},{"utility_box_02",5.4f,1},{"wooden_crate_02",6.8f,1},{"metal_trash_can",4,1}},
        // オフィス街: 馬の像・発電機・消火栓・セメント袋・バリア・車・配電盤
        {{"horse_statue_01",18,.8f},{"portable_generator",6,1},{"fire_hydrant",3,1},{"cement_bag",3.6f,1},
         {"concrete_road_barrier",10,1},{"covered_car",26,.8f},{"power_box_01",3.4f,1},{"utility_box_02",5.4f,1}},
        // 高層街: クジラの像・ゴシックの像・バリア・車
        {{"bronze_whale_statue",60,.8f},{"gothic_statue",12,.9f},{"concrete_road_barrier_02",10,1},{"concrete_road_barrier",10,1},{"covered_car",26,.8f}}};
    const auto blockAll=[](size_t){return true;};
    int seed=1;
    for(size_t z=0;z<RingZones().size();++z){const auto& zone=RingZones()[z];float inner=z?RingZones()[z-1].outer:0,outer=zone.outer;
        // 使えるモデルだけ (ダウンロードに失敗したモデルは除く)
        std::vector<Kind> usable;for(const auto& k:kinds[z])if(std::filesystem::exists(std::string("Resources/Models/PolyHaven/")+k.id+"_2k.fbx"))usable.push_back(k);
        if(usable.empty())continue;
        // 路地裏は最初の食べ物なので、狭い所に多く散らかす
        int count=z==0?260:zone.props,placed=0;
        for(int attempt=0;placed<count&&attempt<count*30;++attempt){
            const auto& kind=usable[size_t(RingNoise(++seed)*float(usable.size()))%usable.size()];
            float width=kind.width*(.85f+.3f*RingNoise(++seed));
            V at{(RingNoise(++seed)*2-1)*outer,0,(RingNoise(++seed)*2-1)*outer};
            float d=RingDistance(at);if(d<inner+width||d>outer-width-4)continue;
            // 開始位置のすぐ近くと、建物の中には置かない
            if(LifeDistance(at,RingData().start)<14||!RingWalkable(at,width*.6f,ringLots_,blockAll))continue;
            auto e=tools.place(kind.id,at,width,RingNoise(++seed)*6.283f,1,0,LifeMorphItem::None);
            if(e==entt::null)continue;++placed;
            // 細い物 (街灯・木など) は大きさの割に量を少なくする
            if(!cityProps_.empty()&&cityProps_.back().entity==e)cityProps_.back().fill=kind.fill;}}
    
    // お助け変形アイテム: タイヤ (住宅街の入口)・消火栓 (商店街の火事の手前)・信号機 (オフィス街の入口)
    tools.place("old_tyre",RingData().tireItem,4.6f,.35f,15,12,LifeMorphItem::Tire);
    for(V at:RingData().hydrants){auto e=tools.place("fire_hydrant",at,3.2f,0,15,24,LifeMorphItem::Hydrant);
        if(e!=entt::null&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).color={1.4f,1.1f,1.1f,1};}
    signalItemAt_=RingData().signalItem;
    {CityBuilder local;const float k=SignalItemScale;local.Box(CityMetal,{-1.2f*k,0,-1.2f*k},{1.2f*k,.4f*k,1.2f*k});local.Box(CityMetal,{-.42f*k,.4f*k,-.42f*k},{.42f*k,5.2f*k,.42f*k});
        local.Box(CityMetal,{-2.3f*k,(SignalHousingY-.8f)*k,-.55f*k},{2.3f*k,(SignalHousingY+.8f)*k,.55f*k});auto& g=local.geometry[CityMetal];
        uint32_t mesh=render->CreateDynamicMesh(g.vertices,g.indices);const auto& skin=tools.surface(CityMetal);const float height=SignalHeight*k;
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop traffic signal");
        auto& t=r.emplace<TransformComponent>(e);t.translate=Write(signalItemAt_);t.scale={1,1,1};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=mesh;mr.textureHandle=skin.texture;mr.extraTextureHandles=skin.extra;mr.shaderName="CitySurface";mr.useCubemap=false;
        mr.color={.32f,.34f,.36f,1};
        cityVisuals_[uint32_t(e)]={mesh,skin.texture,skin.extra,{1,1,1},{0,0,0},{0,height*.5f,0},height,"CitySurface"};
        cityProps_.push_back({e,signalItemAt_+V{0,1.2f,0},15,24,LifeMorphItem::Signal,.5f*height});}
}
}
