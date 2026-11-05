// RoomStage.cpp
// 吸収モードの子ども部屋のステージ (ChronoSystem の一部)。大きくなるほど行ける場所が広がる。
// ベッドの下から始まり、積み木の壁を飲むと床へ、次の積み木を飲むと奥の部屋へ出られる。
// 布団・テーブル・机・棚・ソファは坂で登れる高い場所で、散らかった物が載っている。小物は箱や円柱を組み合わせた形を色違いで使い回す。壁は内側の面だけなので、外のカメラから部屋の中が見える
#include "CityBuilder.h"
#include "../Chrono/RoomRules.h"

namespace Game {
using namespace CityParts;
namespace {
using Chrono::Vec;
// 部屋の区画: ベッドの下 (ここから始まる)・床・奥の部屋の境目
constexpr float kBedX=-140,kBedZ=-150,kBackZ=150,kBackDepth=12;
// 奥の部屋の入口 (棚の切れ目。積み木でふさがれている)
constexpr float kDoors[][2]={{-90,-53},{60,97}};
// 家具: 床の範囲・高さ・材質
struct RoomFurniture {CityRect foot{};float height=0;int material=0;};
const std::vector<RoomFurniture>& RoomFurnitures(){
    static const std::vector<RoomFurniture> items{{{-260,kBackZ,kDoors[0][0],kBackZ+kBackDepth},50,CityWood},{{kDoors[0][1],kBackZ,kDoors[1][0],kBackZ+kBackDepth},50,CityWood},{{kDoors[1][1],kBackZ,260,kBackZ+kBackDepth},50,CityWood},
        {{-70,-140,-30,-105},30,CityPlaster},{{40,-145,105,-125},45,CityWood},{{130,-120,165,-85},35,CityPlaster},{{-20,70,30,100},25,CityWood},{{-230,120,-175,145},40,CityPlaster},{{-90,185,-50,215},30,CityWood},{{70,195,120,215},35,CityPlaster}};
    return items;
}
// 小物の形 (幅を 1 とする。y は 0 から)
enum RoomShape {RoomCube,RoomDisc,RoomBall,RoomBar,RoomSlab,RoomMug,RoomBottle,RoomPlate,RoomShapeCount};
// 円柱 (底の中心は原点の真上)
void RoomPrism(CityBuilder& b,float radius,float y0,float y1){
    constexpr int sides=12;auto at=[&](int i,float y){float a=6.2831853f*float(i)/sides;return Vec{std::cos(a)*radius,y,std::sin(a)*radius};};
    for(int i=0;i<sides;++i){Vec p=at(i,y0),q=at(i+1,y0),r=at(i+1,y1),s=at(i,y1),n=Chrono::Unit(Vec{p.x+q.x,0,p.z+q.z});
        b.Polygon(CityMetal,{q,p,s,r},n);b.Polygon(CityMetal,{Vec{0,y1,0},r,s},{0,1,0});}
}
void RoomBuildShape(CityBuilder& b,RoomShape shape){
    if(shape==RoomCube)b.Box(CityMetal,{-.5f,0,-.5f},{.5f,1,.5f});
    else if(shape==RoomDisc)RoomPrism(b,.5f,0,.14f);
    else if(shape==RoomBall)for(int i=0;i<10;++i){float y0=float(i)/10,y1=float(i+1)/10,mid=(y0+y1)*.5f-.5f;RoomPrism(b,std::sqrt(std::max(.02f,.25f-mid*mid)),y0,y1);}
    else if(shape==RoomBar)b.Box(CityMetal,{-.5f,0,-.06f},{.5f,.12f,.06f});
    else if(shape==RoomSlab)b.Box(CityMetal,{-.5f,0,-.36f},{.5f,.22f,.36f});
    else if(shape==RoomBottle){RoomPrism(b,.5f,0,1.7f);RoomPrism(b,.22f,1.7f,2.4f);}
    else if(shape==RoomPlate){RoomPrism(b,.5f,.03f,.07f);RoomPrism(b,.3f,0,.04f);}
    else{RoomPrism(b,.42f,0,1);b.Box(CityMetal,{.4f,.25f,-.07f},{.7f,.75f,.07f});}
}
// 小物の種類: 区画 (0 ベッドの下・1 床・2 奥の部屋・3 布団・4 テーブル・5 椅子と棚)・形・大きさの範囲 (cm)・色・数・量の割合
struct RoomKind {int area;RoomShape shape;float low,high;Engine::Vector4 color;int count;float fill;};
const RoomKind kRoomKinds[]={
    {0,RoomBall,.8f,1.6f,{.95f,.4f,.55f,1},110,1},{0,RoomBall,.8f,1.6f,{.35f,.7f,.95f,1},110,1},{0,RoomDisc,2,2.6f,{.9f,.72f,.25f,1},40,1},{0,RoomCube,1.5f,2,{.94f,.94f,.9f,1},40,1},
    {0,RoomBall,1.6f,2.6f,{.3f,.8f,.6f,1},30,1},{0,RoomCube,3,3.8f,{.9f,.25f,.2f,1},30,.7f},
    // 床: 小さな物をたくさん散らかす
    {1,RoomMug,8,9,{.92f,.92f,.96f,1},8,.7f},{1,RoomBall,7,10,{.95f,.55f,.15f,1},16,.7f},{1,RoomSlab,7,9,{.85f,.2f,.25f,1},24,.7f},{1,RoomBar,8,10,{.15f,.15f,.18f,1},50,.5f},{1,RoomSlab,4,6,{.96f,.65f,.72f,1},60,.7f},
    {1,RoomCube,3,4.5f,{.9f,.25f,.2f,1},60,.7f},{1,RoomCube,3,4.5f,{.95f,.8f,.2f,1},60,.7f},{1,RoomCube,3,4.5f,{.25f,.6f,.3f,1},60,.7f},{1,RoomDisc,2,2.6f,{.9f,.72f,.25f,1},120,1},{1,RoomBall,1.6f,2.6f,{.3f,.8f,.6f,1},120,1},
    // 奥の部屋: このステージで飲める一番大きい物 (それより大きい物は家具で、飲めない)
    {2,RoomSlab,10,12,{.55f,.45f,.8f,1},10,.6f},{2,RoomBall,8,11,{.85f,.3f,.7f,1},20,.7f},{2,RoomCube,8,11,{.3f,.7f,.75f,1},20,.7f},
    // 低い場所 (布団の上): 靴下・ティッシュ
    {3,RoomSlab,6,8,{.92f,.92f,.95f,1},24,.8f},{3,RoomBall,5,8,{.75f,.75f,.82f,1},24,.8f},{3,RoomBall,1.6f,2.6f,{.95f,.4f,.55f,1},40,1},
    // テーブルの上: 皿・マグカップ・ペットボトル・スプーン
    {4,RoomPlate,11,12,{.9f,.92f,.98f,1},4,.5f},{4,RoomBar,10,12,{.8f,.82f,.85f,1},8,.6f},{4,RoomMug,8,9,{.95f,.95f,.98f,1},8,1},{4,RoomBottle,3,4,{.55f,.75f,.9f,1},16,.7f},{4,RoomDisc,2,2.6f,{.9f,.72f,.25f,1},30,1.5f},
    // 高い場所 (椅子・棚): 高いほど良い物が載っている
    {5,RoomCube,6,8,{.95f,.8f,.2f,1},16,1.5f},{5,RoomBall,6,8,{.9f,.3f,.6f,1},14,1.5f}};
// 区画の中の点か
bool RoomInArea(int area,Vec p){
    if(area==0)return p.x<kBedX-6&&p.z<kBedZ-6;
    if(area==2)return p.z>kBackZ+kBackDepth+6&&Chrono::RoomGround(p)<.1f;
    if(area==1)return p.z<kBackZ-6&&!(p.x<kBedX+6&&p.z<kBedZ+6)&&Chrono::RoomGround(p)<.1f;
    return true;
}
}
// 記録に使う区画の番号: 部屋は 0 ベッドの下・1 床・2 奥の部屋・3 高い場所、輪の町は輪の番号
int ChronoSystem::DevourAreaAt(V p)const{
    if(CurrentDevourStage().map!="room")return Chrono::RingZoneAt(p);
    if(Chrono::RoomGround(p)>1)return 3;
    return p.x<kBedX&&p.z<kBedZ?0:p.z>kBackZ+kBackDepth*.5f?2:1;
}
// 部屋の床・壁・家具
void ChronoSystem::BuildRoomGround(CityBuilder& city){
    using namespace Chrono;const float h=DevourStages()[size_t(devourStage_)].half;
    city.Polygon(CityWood,{{-h,0,-h},{h,0,-h},{h,0,h},{-h,0,h}},{0,1,0});
    city.Polygon(CityPaving,{{-90,.1f,-100},{5,.1f,-100},{5,.1f,-40},{-90,.1f,-40}},{0,1,0});
    city.Polygon(CityPavingBrick,{{-60,.1f,40},{120,.1f,40},{120,.1f,130},{-60,.1f,130}},{0,1,0});
}
// 部屋の小物と変形アイテム
void ChronoSystem::BuildRoomStage(entt::registry& r,const RingCityTools& tools){
    using namespace Chrono;auto* render=Engine::Renderer::GetInstance();const auto& stage=DevourStages()[size_t(devourStage_)];
    // 家具は飲み込めない壁 (敷地だけを作り、吸収の対象の番号は持たない)
    ringLots_.clear();for(const auto& item:RoomFurnitures()){RingLot lot;lot.foot=item.foot;ringLots_.push_back(lot);}
    ringLotProps_.assign(ringLots_.size(),-1);devourBuildingParts_.clear();
    std::array<uint32_t,RoomShapeCount> meshes{};
    for(int i=0;i<RoomShapeCount;++i){CityBuilder b;RoomBuildShape(b,RoomShape(i));auto& g=b.geometry[CityMetal];meshes[size_t(i)]=render->CreateDynamicMesh(g.vertices,g.indices);}
    const auto& skin=tools.surface(CityMetal);const float unit=2*DevourStartRadius/stage.start;const auto blockAll=[](size_t){return true;};int seed=11;
    // 壁と家具: カメラとの間に入ると消えるよう、1 つずつ別のエンティティにする
    auto solid=[&](int material,V lo,V hi){CityBuilder b;b.Box(material,lo,hi);auto& g=b.geometry[size_t(material)];const auto& look=tools.surface(material);
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono Room wall");r.emplace<TransformComponent>(e).scale={1,1,1};r.emplace<CameraOccluder>(e,(lo+hi)*.5f,hi-lo,1.f);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=look.texture;mr.extraTextureHandles=look.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color=tools.tint(material);};
    {const float h=stage.half,top=300,t=4;solid(CityPlaster,{-h-t,0,h},{h+t,top,h+t});solid(CityPlaster,{-h-t,0,-h-t},{h+t,top,-h});solid(CityPlaster,{h,0,-h},{h+t,top,h});solid(CityPlaster,{-h-t,0,-h},{-h,top,h});}
    for(const auto& item:RoomFurnitures())solid(item.material,{item.foot.x0,0,item.foot.z0},{item.foot.x1,item.height,item.foot.z1});
    // 物を 1 つ置く
    auto put=[&](RoomShape shape,V at,float width,Engine::Vector4 color,float fill){
        float yaw=RingNoise(++seed)*6.283f,height=shape==RoomBottle?width*2.4f:shape==RoomCube||shape==RoomBall||shape==RoomMug?width:width*.2f;
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono Room prop");
        // 大きい物はカメラとの間に入ったら透かす
        if(width>12&&height>width*.5f)r.emplace<CameraOccluder>(e,at+V{0,height*.5f,0},V{width,height,width},1.f);
        auto& t=r.emplace<TransformComponent>(e);t.translate=Write(at);t.scale={width,width,width};t.rotate=Write(V{0,yaw,0});
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=meshes[size_t(shape)];mr.textureHandle=white_;mr.extraTextureHandles=skin.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color={color.x,color.y,color.z,color.w};
        CityPropVisual visual{meshes[size_t(shape)],white_,skin.extra,{width,width,width},{0,yaw,0},{0,height*.5f,0},height,"CitySurface"};visual.color=color;
        // 皮で包む形 (丸い物は小さな箱に大きな丸みを付ける)
        const Vec halves[]={{.5f,.5f,.5f},{.5f,.07f,.5f},{.1f,.1f,.1f},{.5f,.06f,.06f},{.5f,.11f,.36f},{.5f,.5f,.42f},{.3f,1.05f,.3f},{.5f,.04f,.5f}};
        visual.half=halves[size_t(shape)]*width;visual.round=shape==RoomBall?.4f*width:shape==RoomBottle?.2f*width:0;cityVisuals_[uint32_t(e)]=visual;
        cityProps_.push_back({e,at+V{0,1.2f,0},1,0,LifeMorphItem::None,.5f*std::max(width,height),fill});};
    // 積み木: 飲める大きさになると飲み込めて、飲むと通れる。敷地の番号と吸収の対象の番号を結びつける
    auto block=[&](float cm,float x,float z,Engine::Vector4 color,bool beacon){float width=2*DevourCoverRatio*DevourBodyRadius(DevourGoalMass(stage,cm))*.98f;
        RingLot lot;lot.foot={x-width*.5f,z-width*.5f,x+width*.5f,z+width*.5f};ringLots_.push_back(lot);ringLotProps_.push_back(int(cityProps_.size()));
        // (斜めに置くと敷地の角から出るので、まっすぐ置く)
        put(RoomCube,{x,0,z},width,color,.3f);auto& seedProp=cityProps_.back();seedProp.beacon=beacon;r.get<TransformComponent>(seedProp.entity).rotate={0,0,0};cityVisuals_[uint32_t(seedProp.entity)].rotate={};return width;};
    const Engine::Vector4 blockColors[]={{.9f,.3f,.25f,1},{.95f,.8f,.25f,1},{.3f,.55f,.9f,1}};
    // ベッドの下から床へ出る積み木の壁
    {float cm=stage.gates[0];int k=0;for(float x=-stage.half+4;x<kBedX+4;){float w=block(cm,x,kBedZ,blockColors[k%3],k==12);x+=w;++k;}
        for(float z=-stage.half+4;z<kBedZ-4;){float w=block(cm,kBedX,z,blockColors[k%3],false);z+=w;++k;}}
    // 奥の部屋の入口の積み木
    {float cm=stage.gates[1];for(const auto& door:kDoors){float width=2*DevourCoverRatio*DevourBodyRadius(DevourGoalMass(stage,cm))*.98f;int count=int(std::ceil((door[1]-door[0])/width));
            for(int k=0;k<count;++k)block(cm,door[0]+(door[1]-door[0])*(float(k)+.5f)/float(count),kBackZ+kBackDepth*.5f,blockColors[k%3],k==count/2);}}
    // 区画ごとの小物
    std::vector<std::pair<V,float>> bigs;
    for(const auto& kind:kRoomKinds)for(int k=0,placed=0;placed<kind.count&&k<kind.count*60;++k){
        float width=(kind.low+(kind.high-kind.low)*RingNoise(++seed))*unit;
        V at{(RingNoise(++seed)*2-1)*(stage.half-width),0,(RingNoise(++seed)*2-1)*(stage.half-width)};
        // 狭い区画は、その中に絞って置く
        if(kind.area==0)at={-stage.half+width+(kBedX-6+stage.half-2*width)*RingNoise(++seed),0,-stage.half+width+(kBedZ-6+stage.half-2*width)*RingNoise(++seed)};
        if(kind.area==2)at.z=kBackZ+kBackDepth+6+width+(stage.half-kBackZ-kBackDepth-6-2*width)*RingNoise(++seed);
        // 高い場所の物は、その面の中に絞って置く
        if(kind.area>=3){std::vector<const RoomSurface*> list;for(const auto& surface:RoomSurfaces)if(surface.area==kind.area)list.push_back(&surface);
            const auto& f=list[size_t(RingNoise(++seed)*float(list.size()))%list.size()]->foot;float m=std::min(width,std::min(f.x1-f.x0,f.z1-f.z0)*.3f);
            at={f.x0+m+(f.x1-f.x0-2*m)*RingNoise(++seed),0,f.z0+m+(f.z1-f.z0-2*m)*RingNoise(++seed)};}
        at.y=RoomGround(at);
        if(!RoomInArea(kind.area,at)||LifeDistance(at,stage.spawn)<10+width*2.5f||LifeDistance(at,stage.itemAt)<16+width||!RingWalkable(at,width*.6f,ringLots_,blockAll))continue;
        // 大きい物は固めない: 他の大きい物から離して置く
        if(width>14){bool crowded=false;for(const auto& other:bigs)crowded|=LifeDistance(at,other.first)<(width+other.second)*.9f+24;if(crowded)continue;bigs.push_back({at,width});}
        put(kind.shape,at,width,kind.color,kind.fill);++placed;}
    // 布団・テーブル・椅子・棚と坂: カメラとの間に入ると消えるよう、1 つずつ別のエンティティにする
    for(const auto& surface:RoomSurfaces){CityBuilder b;const auto& f=surface.foot;float top=std::max(surface.h0,surface.h1);
        if(surface.h0==surface.h1)b.Box(CityPlaster,{f.x0,0,f.z0},{f.x1,top,f.z1});
        else{float a=surface.h0,c=surface.h1;V p00{f.x0,a,f.z0},p10{f.x1,surface.alongZ?a:c,f.z0},p11{f.x1,c,f.z1},p01{f.x0,surface.alongZ?c:a,f.z1};
            b.Polygon(CityPlaster,{p00,p01,p11,p10},{0,1,0});
            b.Polygon(CityPlaster,{{f.x0,0,f.z0},{f.x1,0,f.z0},p10,p00},{0,0,-1});b.Polygon(CityPlaster,{{f.x1,0,f.z1},{f.x0,0,f.z1},p01,p11},{0,0,1});
            b.Polygon(CityPlaster,{{f.x0,0,f.z1},{f.x0,0,f.z0},p00,p01},{-1,0,0});b.Polygon(CityPlaster,{{f.x1,0,f.z0},{f.x1,0,f.z1},p11,p10},{1,0,0});}
        auto& g=b.geometry[CityPlaster];const auto& look=tools.surface(CityPlaster);auto e=r.create();r.emplace<NameComponent>(e,"Chrono Room furniture");r.emplace<TransformComponent>(e).scale={1,1,1};
        r.emplace<CameraOccluder>(e,V{(f.x0+f.x1)*.5f,top*.5f,(f.z0+f.z1)*.5f},V{f.x1-f.x0,top,f.z1-f.z0},1.f);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=look.texture;mr.extraTextureHandles=look.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color={surface.r,surface.g,surface.b,1};}
    // ベッド: 上の板はカメラとの間に入ると消えるので、下にいても体が見える
    {CityBuilder b;b.Box(CityPlaster,{-stage.half,24,-stage.half},{kBedX+6,44,kBedZ+6});for(float x:{-stage.half+6,kBedX-6})for(float z:{-stage.half+6,kBedZ-6})b.Box(CityPlaster,{x-3,0,z-3},{x+3,24,z+3});
        auto& g=b.geometry[CityPlaster];const auto& surface=tools.surface(CityPlaster);auto e=r.create();r.emplace<NameComponent>(e,"Chrono Room bed");r.emplace<TransformComponent>(e).scale={1,1,1};
        V lo{-stage.half,24,-stage.half},hi{kBedX+6,44,kBedZ+6};r.emplace<CameraOccluder>(e,(lo+hi)*.5f,hi-lo,1.f);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=surface.texture;mr.extraTextureHandles=surface.extra;mr.shaderName="CitySurface";mr.useCubemap=false;mr.color={.75f,.8f,1,1};}
    BuildStageItem(r,tools);
}
}
