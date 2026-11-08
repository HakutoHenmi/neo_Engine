// RingCity.cpp
// 吸収モードの町 (ChronoSystem の一部)。中心の路地裏から外へ、住宅街の輪が広がる
// (配置の規則は Game/Chrono/RingCityRules.h、数値は Resources/Data/ring_city.json)。
// 建物は輪ごとに数種類の型を作って使い回し (同じ型は同じメッシュなのでまとめて描ける)、1 棟ずつ別のエンティティにする。
// 建物も吸収の対象で、飲み込むと見た目と当たり判定が消え、通れるようになる。
// 建物の材質は CC0 (Poly Haven・ambientCG。Resources/Credits/CityRing.md)。小物はすべて自作のローポリのモデル
#include "CityBuilder.h"
#include "../Chrono/DevourRules.h"
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
// 輪の門: シャッターの扉と、両側の太い柱と上の梁 (W は幅、D は厚み)
void RingBuildGate(CityBuilder& b,float W,float D,float h){
    float hw=W*.5f,hd=D*.5f,post=std::max(1.5f,W*.07f);
    b.Box(CityRustShutter,{-hw,0,-hd*.6f},{hw,h*.85f,hd*.6f});
    b.Box(CityDarkTiles,{-hw,0,-hd},{-hw+post,h,hd});b.Box(CityDarkTiles,{hw-post,0,-hd},{hw,h,hd});b.Box(CityDarkTiles,{-hw,h*.85f,-hd},{hw,h,hd});
}
// 型の形を作る。W と D は敷地の幅と奥行き (ローカルの x と z。正面は +z)、variant で材質と形を変える
void RingBuildTemplate(CityBuilder& b,int variant,float W,float D,int floors,float fh){
    {
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
    }
}
// 輪ごとの吸収の量の割合 (建物は中が空いているので、大きさの割に少し少ない)
constexpr float kRingFill=.5f;
}
// 輪の町の地面: 町全体のアスファルト・路地裏の床・区画の歩道と縁石・道路の中央線
void ChronoSystem::BuildRingGround(CityBuilder& city){
    using namespace Chrono;
    // (大きくなると遠くまで見えるので、地面は町の外まで広く敷く)
    const float half=RingCityHalf()+4000,alley=RingZones()[0].outer;
    city.Polygon(CityRoad,{{-half,-.2f,-half},{half,-.2f,-half},{half,-.2f,half},{-half,-.2f,half}},{0,1,0});
    city.Polygon(CityAlleyFloor,{{-alley,0,-alley},{alley,0,-alley},{alley,0,alley},{-alley,0,alley}},{0,1,0});
    // 広場の舗装
    for(const auto& plaza:RingPlazas()){float s=plaza.radius;V c=plaza.at;city.Polygon(CityPavingBrick,{{c.x-s,.1f,c.z-s},{c.x+s,.1f,c.z-s},{c.x+s,.1f,c.z+s},{c.x-s,.1f,c.z+s}},{0,1,0});}
    for(const auto& block:RingBlocks()){const auto& w=block.rect;constexpr int floor=CityPaving;
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
    using namespace Chrono;auto* render=Engine::Renderer::GetInstance();BeginLowProps(14);render->SetAmbientColor({.4f,.4f,.45f});
    ringLots_=RingLots();ringLotProps_.assign(ringLots_.size(),-1);devourBuildingParts_.clear();
    // 建物の型 (輪・型の番号・90 度回したか) ごとのメッシュ
    std::map<std::tuple<int,int,bool>,RingTemplate> templates;
    auto shape=[&](const RingLot& lot,bool turned)->const RingTemplate& {
        auto key=std::make_tuple(lot.zone,lot.variant,turned);auto found=templates.find(key);if(found!=templates.end())return found->second;
        const auto& zone=RingZones()[size_t(lot.zone)];float w=lot.foot.x1-lot.foot.x0,d=lot.foot.z1-lot.foot.z0;if(turned)std::swap(w,d);
        CityBuilder b;if(lot.gate)RingBuildGate(b,w,d,lot.height);else RingBuildTemplate(b,lot.variant,w,d,lot.floors,zone.floorHeight);
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
            // カメラとプレイヤーの間に入ったら薄くする
            r.emplace<CameraOccluder>(e,V{center.x,t.height*.5f,center.z},V{lot.foot.x1-lot.foot.x0,t.height,lot.foot.z1-lot.foot.z0},1.f);
            auto& tr=r.emplace<TransformComponent>(e);tr.translate=Write(center);tr.rotate=Write(V{0,yaw,0});tr.scale={1,1,1};
            auto& mr=r.emplace<MeshRendererComponent>(e);const auto flat=FlatCityColor(piece.material);
            mr.modelHandle=piece.mesh;mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;mr.useCubemap=false;mr.shaderName="LowpolySurface";mr.color={flat.x,flat.y,flat.z,1};
            if(main==entt::null){main=e;CityPropVisual visual{piece.mesh,white_,lowMaps_,{1,1,1},{0,yaw,0},{0,t.height*.5f,0},t.height,mr.shaderName};visual.color=flat;cityVisuals_[uint32_t(e)]=visual;}
            else parts.push_back(e);}
        // カメラが建物にめり込まないための当たり判定
        auto collider=r.create();r.emplace<NameComponent>(collider,"Chrono City building collision");
        r.emplace<TransformComponent>(collider).translate=Write(V{center.x,t.height*.5f,center.z});
        r.emplace<BoxColliderComponent>(collider).size=Write(V{lot.foot.x1-lot.foot.x0,t.height,lot.foot.z1-lot.foot.z0});
        r.emplace<TagComponent>(collider).tag=TagType::Wall;r.emplace<Solid>(collider);parts.push_back(collider);
        // 吸収の対象: 半径は幅と高さの大きい方の半分、壁までの余裕は短い辺の半分
        // (門は開けるための物なので量は少なく、手前の輪から届く)
        cityProps_.push_back({main,V{center.x,1.2f,center.z},1,0,LifeMorphItem::None,RingLotRadius(lot),lot.gate?.2f:kRingFill,RingLotReach(lot),lot.gate?lot.zone-1:lot.zone,lot.gate,lot.gate>0});
        ringLotProps_[i]=int(cityProps_.size()-1);devourBuildingParts_[cityProps_.size()-1]=std::move(parts);}
    // 輪の壁: 門を飲み込むか砕くまで次の輪へ通さない。崩れたら隠す
    for(int z=1;z<RingStyleCount;++z){float at=RingWallAt(z),h=RingWallHeight(z),t=2;CityBuilder b;ringWalls_[size_t(z)].clear();
        for(float s:{-at,at}){b.Box(CityConcrete,{-at-t,0,s-t},{at+t,h,s+t});b.Box(CityConcrete,{s-t,0,-at-t},{s+t,h,at+t});
            b.Box(CityDarkTiles,{-at-t,h,s-t-1},{at+t,h+2,s+t+1});b.Box(CityDarkTiles,{s-t-1,h,-at-t},{s+t+1,h+2,at+t});}
        for(int m:{int(CityConcrete),int(CityDarkTiles)}){auto& g=b.geometry[size_t(m)];auto e=r.create();r.emplace<NameComponent>(e,"Chrono City ring wall");r.emplace<TransformComponent>(e).scale={1,1,1};
            auto& mr=r.emplace<MeshRendererComponent>(e);const auto flat=FlatCityColor(m);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;
            mr.useCubemap=false;mr.shaderName="LowpolySurface";mr.color={flat.x,flat.y,flat.z,1};ringWalls_[size_t(z)].push_back(e);}}
    // 小物: 実寸のローポリのモデルをセットにして置く (scripts/lowpoly/build_lowpoly.py)。1cm = 0.06 単位
    constexpr float U=.06f;int seed=1;
    const Engine::Vector4 plain{1,1,1,1},carColors[]={{.92f,.92f,.94f,1},{.86f,.3f,.26f,1},{.3f,.5f,.86f,1},{.98f,.82f,.34f,1},{.5f,.8f,.7f,1}};
    auto noise=[&]{return RingNoise(++seed);};
    // 輪の番号を付けて置く (mult は実寸に対する倍率)
    auto put=[&](const char* id,V at,float yaw,float fill,float mult,int ring,Engine::Vector4 tint={1,1,1,1}){size_t before=cityProps_.size();auto e=LowProp(r,id,at,U*mult,yaw,fill,tint);if(cityProps_.size()>before)cityProps_.back().ring=ring;return e;};
    // 住宅街: 区画の縁に沿って、暮らしのセットを道へはみ出すように置く。along は道に沿う向き、out は道への向き
    auto scene=[&](int kind,V at,V along,V out,float yaw,int ring=1){
        auto spot=[&](float a,float o){return at+along*a+out*o;};
        switch(kind){
        case 0:// ごみ捨て場
            for(int i=0;i<4;++i)put("gomibukuro",spot(float(i)*2.6f-4,1+noise()*2),noise()*6.283f,1,1.1f,ring);
            put("danboru",spot(5,1.5f),yaw+.3f,1,1.2f,ring);put("danboru",spot(7.5f,2),yaw-.4f,1,1.2f,ring);put("gomibako",spot(-7,1.6f),0,1,1,ring);put("karasu",spot(1,4),noise()*6.283f,1,1,ring);break;
        case 1:// 自販機のコーナー
            put("jihanki",spot(0,2.4f),yaw,.8f,1,ring);put("gomibako",spot(5,1.6f),0,1,1,ring);for(int i=0;i<5;++i)put("akikan",spot(3+noise()*5,3+noise()*4),noise()*6.283f,1,2.2f,ring);break;
        case 2:// 玄関先
            put("yubinuke",spot(-6,1),yaw,.6f,1,ring);put("jitensha",spot(0,2),yaw+1.5708f,.6f,1,ring);put("sanrinsha",spot(7,2.4f),yaw+.8f,.8f,1.2f,ring);put("uekibachi",spot(-9,1.4f),0,1,1,ring);put("uekibachi",spot(-3.5f,1),1,1,.9f,ring);put("joro",spot(4,1.2f),yaw,1,1.5f,ring);break;
        case 3:// 路上の軽自動車
            put("kei_car",spot(0,5.5f),yaw+1.5708f,.8f,1,ring,carColors[size_t(noise()*5)%5]);put("cone",spot(-14,3.5f),0,1,1,ring);put("cone",spot(14,3.5f),0,1,1,ring);break;
        case 4:// 犬小屋と洗濯物
            put("inugoya",spot(-6,3),yaw,.8f,1.1f,ring);put("inu",spot(-1,5),yaw+1,1,1,ring);put("monohoshi",spot(9,2),yaw+1.5708f,.5f,1,ring);
            for(int i=0;i<3;++i){V hang=spot(4.5f+float(i)*4.5f,2);hang.y=(150-84)*U;put("sentakumono",hang,yaw+1.5708f,.7f,1,ring);}break;
        case 5:// 電柱とカーブミラーとポスト
            put("denchu",spot(0,1.2f),yaw,.25f,1,ring);put("curve_mirror",spot(5,1.2f),yaw,.4f,1,ring);put("yubin_post",spot(-5,1.8f),yaw,.8f,1,ring);put("cone",spot(9,2.2f),0,1,1,ring);break;
        default:// 室外機と猫
            put("shitsugaiki",spot(-3,1),yaw,1,1,ring);put("shitsugaiki",spot(3,1),yaw,1,1,ring);put("neko",spot(0,2.4f),yaw,1,1.2f,ring);put("uekibachi",spot(7,1.3f),0,1,1,ring);break;}};
    {const float gap=56;int weights[]={0,0,1,2,2,3,3,4,4,5,5,6};
        for(const auto& block:RingBlocks()){const auto& w=block.rect;const float yawOf[]={0,3.14159f,1.5708f,-1.5708f};
            const V starts[]={{w.x0,0,w.z1},{w.x0,0,w.z0},{w.x1,0,w.z0},{w.x0,0,w.z0}},alongs[]={{1,0,0},{1,0,0},{0,0,1},{0,0,1}},outs[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};const float lengths[]={w.x1-w.x0,w.x1-w.x0,w.z1-w.z0,w.z1-w.z0};
            for(int side=0;side<4;++side)for(float a=gap*.5f;a<lengths[side]-10;a+=gap){if(noise()<.48f)continue;scene(weights[size_t(noise()*12)%12],starts[side]+alongs[side]*a,alongs[side],outs[side],yawOf[side]);}}}
    // 路地裏: 壁際にごみ置き場と室外機が続く。真ん中は通り道として空ける
    {const float edge=RingZones()[0].outer-6;const V sides[][3]={{{-edge,0,-edge},{1,0,0},{0,0,1}},{{-edge,0,edge},{1,0,0},{0,0,-1}},{{-edge,0,-edge},{0,0,1},{1,0,0}},{{edge,0,-edge},{0,0,1},{-1,0,0}}};
        for(int side=0;side<4;++side)for(float a=30;a<edge*2-20;a+=44){scene(int(a/44+side)%2?6:0,sides[side][0]+sides[side][1]*a,sides[side][1],sides[side][2],side<2?(side?3.14159f:0.f):(side==2?1.5708f:-1.5708f),0);}}
    // 動く物: 軽自動車と自転車は区画のまわりの道を走り、路地裏を猫が歩いて、カラスが跳ねる
    {const auto blocks=RingBlocks();const auto around=[](const CityRect& w,float out){return std::vector<V>{{w.x0-out,0,w.z0-out},{w.x1+out,0,w.z0-out},{w.x1+out,0,w.z1+out},{w.x0-out,0,w.z1+out}};};
        for(size_t i=0;i<blocks.size();i+=7){const auto path=around(blocks[i].rect,10);auto e=put("kei_car",path[0],0,.8f,1,1,carColors[(i/7)%5]);AddRoute(e,path,30,noise());}
        for(size_t i=3;i<blocks.size();i+=9){const auto path=around(blocks[i].rect,5);auto e=put("jitensha",path[0],0,.6f,1,1);AddRoute(e,path,16,noise());}
        const float loop=RingZones()[0].outer*.45f;const std::vector<V> alley{{-loop,0,-loop},{loop,0,-loop},{loop,0,loop},{-loop,0,loop}};
        for(int i=0;i<4;++i)AddRoute(put("inu",alley[0],0,1,1,0),alley,6,float(i)/4);
        for(int i=0;i<6;++i){const float a=float(i)*1.047f;AddDance(r,put("karasu",V{std::cos(a)*loop*.7f,0,std::sin(a)*loop*.7f},a,1,1,0),7,.12f,.7f);}}
    // 輪の中で育つ体の半径 (門を飲める大きさから、一番大きい物を飲める大きさまでの真ん中)。ごちそうの大きさはこれに合わせる
    auto ringBody=[](size_t z){const auto& zones=RingZones();float in=z?zones[z].gateRadius/DevourCoverRatio:DevourStartRadius,out=z+1<zones.size()?zones[z+1].gateRadius/DevourCoverRatio:DevourBodyRadius(4e4f);return std::sqrt(in*out);};
    // ごちそうを 1 つ置く
    auto food=[&](const char* id,V at,float width,float fill,int ring,bool beacon=false,Engine::Vector4 tint={1,1,1,1}){LowProp(r,id,at,LowScaleFor(id,width),noise()*6.283f,fill,tint);auto& prop=cityProps_.back();prop.ring=ring;prop.beacon=beacon;};
    // 大通りのごみの列: 1 個ずつは小さいが、列全部で大きく育つ。タイヤで転がると一気に吸い込める
    const char* trash[]={"gomibukuro","danboru","gomibako","shitsugaiki","uekibachi","gomibukuro"};
    // (住宅街の列は体が大きいので、実寸で大きい物を並べる)
    const char* large[]={"kei_car","bench","inugoya","jihanki","monohoshi","kei_car"};
    for(size_t z=0;z<RingZones().size();++z){const auto& zone=RingZones()[z];float inner=z?RingZones()[z-1].outer+zone.gateRadius:20,outer=zone.outer-12,body=ringBody(z),lane=z?zone.avenue*.28f:14;
        for(int arm=0;arm<4;++arm)for(int k=0;k<zone.lineFood;++k){float along=inner+(outer-inner)*(float(k)+.5f)/float(zone.lineFood),side=lane*(k%2?1.f:-1.f);
            V at=arm==0?V{side,0,along}:arm==1?V{along,0,side}:arm==2?V{side,0,-along}:V{-along,0,side};
            if(z==0&&LifeDistance(at,CurrentDevourStage().spawn)<14)continue;
            if(z){int pick=int(noise()*6)%6;put(large[pick],at,arm%2?0.f:1.5708f,.4f,1,int(z),pick%5==0?carColors[size_t(noise()*5)%5]:plain);}
            else food(trash[size_t(noise()*6)%6],at,2*body*(.25f+.2f*noise()),1,0);}}
    // 広場は公園: すべり台とベンチとボール。1 つだけ、ステージ 1 の部屋がある家が建っている
    {int index=0;for(const auto& plaza:RingPlazas()){int ring=plaza.zone;float body=ringBody(size_t(ring));bool home=index++==0;
            if(home)food("my_house",plaza.at,2.4f*body,1,ring,true);else{put("suberidai",plaza.at,noise()*6.283f,.5f,1.6f,ring);cityProps_.back().beacon=true;}
            const char* park[]={"bench","soccer_ball","neko","inu","uekibachi","jitensha","sanrinsha"};const float mults[]={1,3,1.2f,1,1.2f,1,1.2f};int count=RingZones()[size_t(ring)].plazaFood;
            for(int k=0;k<count;++k){float a=6.2831853f*float(k)/float(count),d=plaza.radius*.72f;int pick=int(noise()*7)%7;auto e=put(park[pick],plaza.at+V{std::cos(a)*d,0,std::sin(a)*d},-a+1.5708f,.8f,mults[pick],ring);
                if(pick==2||pick==3){const float w=plaza.radius*.8f;const std::vector<V> walk{plaza.at+V{-w,0,-w},plaza.at+V{w,0,-w},plaza.at+V{w,0,w},plaza.at+V{-w,0,w}};AddRoute(e,walk,9,noise());}}}}
    BuildStageItem(r,tools);
}
// ステージの変形アイテムを置く。掃除機とバネのモデルは箱を組み合わせて作る
void ChronoSystem::BuildStageItem(entt::registry& r,const RingCityTools& tools){
    using namespace Chrono;auto* render=Engine::Renderer::GetInstance();const auto& stage=CurrentDevourStage();V at=stage.itemAt;
    auto ringOf=[&]{if(!cityProps_.empty())cityProps_.back().ring=stage.map=="ring"?RingZoneAt(at):0;};
    // 台座: 金色の段の上にアイテムが載っていて、飲むと台座も消える
    auto pedestal=[&]{if(cityProps_.empty())return;constexpr float high=5;auto& item=cityProps_.back();ringOf();CityBuilder b;b.Box(CityMetal,{-6,0,-6},{6,1.6f,6});b.Box(CityMetal,{-4.5f,1.6f,-4.5f},{4.5f,3.4f,4.5f});b.Box(CityMetal,{-3.2f,3.4f,-3.2f},{3.2f,high,3.2f});b.Box(CityMetal,{-1.1f,20,-1.1f},{1.1f,900,1.1f});
        auto& g=b.geometry[CityMetal];const auto& skin=tools.surface(CityMetal);auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop pedestal");r.emplace<TransformComponent>(e).translate=Write(V{at.x,at.y,at.z});r.get<TransformComponent>(e).scale={1,1,1};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=white_;mr.extraTextureHandles=skin.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color={1,.72f,.15f,1};
        devourBuildingParts_[cityProps_.size()-1]={e};auto& t=r.get<TransformComponent>(item.entity);t.translate.y+=high+.6f;devourItemY_=t.translate.y;
        // 遠くからでも形が分かるよう、大きく見せる
        t.scale={t.scale.x*2.2f,t.scale.y*2.2f,t.scale.z*2.2f};cityVisuals_[uint32_t(item.entity)].scale=cityVisuals_[uint32_t(item.entity)].scale*2.2f;};
    if(stage.item==LifeMorphItem::Tire){LowProp(r,"tire",at,LowScaleFor("tire",4.6f),.35f,1);auto& tire=cityProps_.back();tire.required=15;tire.reward=12;tire.morphItem=LifeMorphItem::Tire;pedestal();return;}
    CityBuilder b;float height=4;
    if(stage.item==LifeMorphItem::Vacuum){b.Box(CityMetal,{-.8f,.3f,-2},{.8f,1.7f,0});b.Box(CityMetal,{-.25f,1.4f,0},{.25f,1.9f,1.6f});b.Box(CityMetal,{-.2f,.2f,1.5f},{.2f,1.6f,1.9f});b.Box(CityMetal,{-1.3f,0,1.8f},{1.3f,.35f,2.4f});}
    else{for(int i=0;i<6;++i){float y=float(i)*.7f,w=1.3f;b.Box(CityMetal,{-w,y,-w},{w,y+.3f,-w+.3f});b.Box(CityMetal,{-w,y,w-.3f},{w,y+.3f,w});b.Box(CityMetal,{-w,y,-w},{-w+.3f,y+.3f,w});b.Box(CityMetal,{w-.3f,y,-w},{w,y+.3f,w});}
        b.Box(CityMetal,{-.2f,0,-.2f},{.2f,3.8f,.2f});}
    auto& g=b.geometry[CityMetal];uint32_t mesh=render->CreateDynamicMesh(g.vertices,g.indices);const auto& skin=tools.surface(CityMetal);
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop stage item");auto& t=r.emplace<TransformComponent>(e);t.translate=Write(at);t.scale={1,1,1};
    auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=mesh;mr.textureHandle=skin.texture;mr.extraTextureHandles=skin.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color={.9f,.9f,1,1};
    cityVisuals_[uint32_t(e)]={mesh,skin.texture,skin.extra,{1,1,1},{0,0,0},{0,height*.5f,0},height,"CitySurface"};
    cityProps_.push_back({e,at+V{0,1.2f,0},15,24,stage.item,.5f*height});pedestal();
}
}
