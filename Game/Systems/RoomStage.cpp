// RoomStage.cpp
// 吸収モードの部屋のステージ (ChronoSystem の一部)。大きくなるほど行ける場所が広がる。
// ベッドの下から始まり、8cm まで育つと出口の収納ケース (ベッドの下から見て前の右端。ほかは跳び越えられない本の壁) を段差ジャンプで越えて子ども部屋へ出られる。奥の和室へは入口から入れる。
// 物は実寸 (1cm = 1.2 単位) のローポリのモデルで、関連する物はセットにして置く。
// モデルは scripts/lowpoly/build_lowpoly.py で作り、高い場所の高さは Game/Chrono/RoomRules.h に置く
#include "CityBuilder.h"
#include "../Chrono/RoomRules.h"
#include <stdexcept>

namespace Game {
using namespace CityParts;
namespace {
using Chrono::Vec;
// 1cm の大きさ
constexpr float kUnit=1.2f;
// 区画の境目: ベッドの下 (ここから始まる)・和室との間の壁
constexpr float kBedX=-72,kBedZ=-192,kWallZ=40,kWallDepth=12;
// 和室の入口
constexpr float kDoors[][2]={{20,80},{220,280}};
// ベッドの下を囲む本の壁: 前の列 (出口の収納ケースの左) と右の列。飲めず、跳び越えもできない
constexpr CityRect kBookWalls[]={{-300,-192,Chrono::RoomExitX0,-180},{-72,-300,-60,-192}};
// 飲めない家具の足元: おもちゃ箱・椅子・本棚・間の壁・ベッドの下の本の壁
const std::vector<CityRect>& RoomLots(){
    static const std::vector<CityRect> lots{{-268,-44,-196,4},{211,-219,259,-171},{264,-110,300,-10},
        {-300,kWallZ,kDoors[0][0],kWallZ+kWallDepth},{kDoors[0][1],kWallZ,kDoors[1][0],kWallZ+kWallDepth},{kDoors[1][1],kWallZ,300,kWallZ+kWallDepth},
        kBookWalls[0],kBookWalls[1]};
    return lots;
}
// 色
constexpr Engine::Vector4 kPlain{1,1,1,1},kRed{.92f,.28f,.24f,1},kYellow{.98f,.8f,.25f,1},kGreen{.32f,.72f,.4f,1},kBlue{.3f,.55f,.92f,1},kPink{.96f,.5f,.66f,1},kSky{.45f,.75f,.95f,1};
}
// 記録に使う区画の番号: 部屋は 0 ベッドの下・1 床・2 和室・3 高い場所、輪の町は輪の番号
int ChronoSystem::DevourAreaAt(V p)const{
    if(CurrentDevourStage().map!="room")return Chrono::RingZoneAt(p);
    if(Chrono::RoomGround(p)>1)return 3;
    return p.x<kBedX&&p.z<kBedZ?0:p.z>kWallZ+kWallDepth*.5f?2:1;
}
// ローポリのモデルを読み、大きさの範囲を求める (同じモデルは使い回す)
const ChronoSystem::LowModel& ChronoSystem::LowModelOf(const std::string& id){
    auto found=lowModels_.find(id);if(found!=lowModels_.end())return found->second;
    auto* render=Engine::Renderer::GetInstance();LowModel m;m.mesh=render->LoadObjMesh("Resources/Models/Lowpoly/"+id+".obj");
    auto* source=render->GetModel(m.mesh);if(!source)throw std::runtime_error("missing lowpoly model: "+id);
    m.lo={1e9f,1e9f,1e9f};m.hi={-1e9f,-1e9f,-1e9f};
    for(const auto& v:source->GetData().vertices){m.lo={std::min(m.lo.x,v.position.x),std::min(m.lo.y,v.position.y),std::min(m.lo.z,v.position.z)};m.hi={std::max(m.hi.x,v.position.x),std::max(m.hi.y,v.position.y),std::max(m.hi.z,v.position.z)};}
    return lowModels_.emplace(id,m).first->second;
}
// 上から見た長い辺が width になる拡大率
float ChronoSystem::LowScaleFor(const std::string& id,float width){const auto& m=LowModelOf(id);return width/std::max({m.hi.x-m.lo.x,m.hi.z-m.lo.z,.001f});}
// モデルを置く準備: 色の画像と平らな材質 (ステージを作るたびに読み直す)
void ChronoSystem::BeginLowProps(float occludeHeight){
    auto* render=Engine::Renderer::GetInstance();lowModels_.clear();lowOcclude_=occludeHeight;
    devourMovers_.clear();gimmicks_.clear();gimmickSigns_.clear();devourSpills_.clear();
    lowPalette_=render->LoadTexture2D("Resources/Textures/Lowpoly/palette.png");
    render->LoadTexture2D("Resources/Textures/UI/Comic/atlas.png");
    lowMaps_={render->LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png",false),white_,white_};
}
// モデルを 1 つ置く。at はモデルの原点の足元、scale は 1cm の大きさ、fill が 0 なら飲めない家具、tilt は倒す角度
entt::entity ChronoSystem::LowProp(entt::registry& r,const std::string& id,V at,float scale,float yaw,float fill,Engine::Vector4 tint,float tilt){
    const auto& m=LowModelOf(id);V size=(m.hi-m.lo)*scale,mid=(m.lo+m.hi)*.5f;float width=std::max(size.x,size.z),height=size.y,cs=std::cos(yaw),sn=std::sin(yaw);
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono Lowpoly "+id);
    auto& t=r.emplace<TransformComponent>(e);t.scale=Write(V{scale,scale,scale});t.rotate=Write(V{tilt,yaw,0});t.translate=Write(V{at.x,at.y-m.lo.y*scale,at.z});
    auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=m.mesh;mr.textureHandle=lowPalette_;mr.extraTextureHandles=lowMaps_;mr.shaderName="LowpolySurface";mr.useCubemap=false;mr.color={tint.x,tint.y,tint.z,tint.w};
    // 上から見た見た目の中心 (モデルの原点からずれている物がある)
    V offset{(mid.x*cs+mid.z*sn)*scale,0,(-mid.x*sn+mid.z*cs)*scale},center=at+offset+V{0,height*.5f,0};
    // カメラとの間に入った物は全体を薄くする (小さな物はそのまま)
    // (判定の範囲は向きに合わせて、物の形に近づける)
    if(height>lowOcclude_)r.emplace<CameraOccluder>(e,center,V{std::abs(cs)*size.x+std::abs(sn)*size.z,height,std::abs(sn)*size.x+std::abs(cs)*size.z},1.f);
    if(fill<=0)return e;
    CityPropVisual visual{m.mesh,lowPalette_,lowMaps_,{scale,scale,scale},{tilt,yaw,0},center-Read(t.translate),height,"LowpolySurface"};visual.color=tint;
    // 皮で包む形: 小さな箱に大きな丸みを付けて、物の形に近い丸い皮にする
    V half=size*.5f;float round=.8f*std::min({half.x,half.y,half.z});visual.half=half-V{round,round,round};visual.round=round;cityVisuals_[uint32_t(e)]=visual;
    cityProps_.push_back({e,at+offset+V{0,std::min(1.2f,height*.5f),0},1,0,LifeMorphItem::None,.5f*std::max(width,height),fill});
    // 飲めないときに体を止める足元 (細長い物も丸で扱う)
    cityProps_.back().foot=.5f*std::sqrt(size.x*size.z);cityProps_.back().height=height;
    return e;
}
// 部屋のギミック: どれも同じ看板と床の輪を目印にし、輪の中で変形キーを押すと動く
void ChronoSystem::BuildRoomGimmicks(entt::registry& r){
    using namespace Chrono;using Chrono::GimmickKind;auto* render=Engine::Renderer::GetInstance();int seed=907;
    auto real=[&](const std::string& id,V at,float yaw,float fill,float mult=1,Engine::Vector4 tint=kPlain,float tilt=0){return LowProp(r,id,at,kUnit*mult,yaw,fill,tint,tilt);};
    auto sized=[&](const char* id,V at,float cm,float yaw,float fill,Engine::Vector4 tint=kPlain){return LowProp(r,id,at,LowScaleFor(id,cm*kUnit),yaw,fill,tint,0);};
    auto spin=[&]{return RingNoise(++seed)*6.283f;};
    // 成功したときにばらまく物 (最初は隠す)
    struct Reward {const char* id;float low,high;Engine::Vector4 tint;};
    auto rewards=[&](DevourGimmick& g,V at,std::initializer_list<Reward> kinds,int count){std::vector<Reward> list(kinds);
        for(int i=0;i<count;++i){const auto& k=list[size_t(i)%list.size()];g.rewards.push_back(uint32_t(sized(k.id,at,k.low+(k.high-k.low)*RingNoise(++seed),spin(),1,k.tint)));}};
    // 液体の跡 (床・家具の壁・上の面を通る平らな帯)
    auto streak=[&](const char* name,const std::vector<std::array<V,2>>& list,Engine::Vector4 color){CityBuilder b;
        for(const auto& box:list)b.Box(CityMetal,V{std::min(box[0].x,box[1].x),std::min(box[0].y,box[1].y),std::min(box[0].z,box[1].z)},V{std::max(box[0].x,box[1].x),std::max(box[0].y,box[1].y),std::max(box[0].z,box[1].z)});auto& geometry=b.geometry[CityMetal];
        auto e=r.create();r.emplace<NameComponent>(e,name);r.emplace<TransformComponent>(e).scale={1,1,1};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(geometry.vertices,geometry.indices);mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;mr.shaderName="LowpolySurface";mr.useCubemap=false;mr.color={color.x,color.y,color.z,1};};
    const Engine::Vector4 marbles[]={kPink,kSky,kGreen,kYellow};

    // ---- 絵の具: ベッドの足元の床に置いた大きなパレット。4 色のたまりのどれかに飛び込むと、体がその色になる ----
    {const V palette{-12,0,-262};real("paint_palette",palette,0,0);
        const int g=AddGimmick(r,GimmickKind::Paint,"絵の具に飛び込む",palette,44,5,.5f,{-50,0,-228});auto& paint=gimmicks_[size_t(g)];
        const float spots[][2]={{-18,-8},{-4,-13},{12,-10},{22,3}};const Engine::Vector4 colors[]={kRed,kYellow,kGreen,kBlue};
        for(int i=0;i<4;++i){paint.spots.push_back(palette+V{spots[i][0]*kUnit,0,spots[i][1]*kUnit});paint.colors.push_back(colors[i]);}
        // 絵の具のチューブがパレットのまわりに転がっている
        for(int i=0;i<10;++i){float a=spin(),d=52+18*RingNoise(++seed);V at=palette+V{std::cos(a)*d,0,std::sin(a)*d*.7f};if(std::abs(at.z)>290||RoomTop(at)>.1f)continue;real("paint_tube",at,spin(),.8f,1.2f,colors[i%4]);}}

    // ---- ドミノ: じゅうたんと机の間をくねって並ぶ。最後のバケツが倒れて、中のおもちゃがあふれる ----
    {std::vector<V> path;for(int i=0;i<=24;++i){float u=float(i)/24;path.push_back({40+135*u,0,-210+18*std::sin(u*6.283f)});}
        const int g=AddGimmick(r,GimmickKind::Domino,"ドミノを倒す",path.front()+V{-14,0,0},16,8,1.5708f,{22,0,-232});auto& domino=gimmicks_[size_t(g)];domino.path=path;domino.scale=1.2f;
        const Engine::Vector4 colors[]={kRed,kYellow,kBlue,kGreen,kPink,kSky};const float length=GimmickPathLength(path),step=9;int k=0;
        for(float s=0;s<=length;s+=step,++k){V dir;V at=GimmickPathPoint(path,s,&dir);const float yaw=std::atan2(dir.x,dir.z);
            domino.parts.push_back(uint32_t(real("domino",at,yaw,.7f,1.2f,colors[k%6])));domino.partBase.push_back(at);domino.partRotate.push_back({0,yaw,0});}
        V dir;V end=GimmickPathPoint(path,length,&dir);dir=Unit(V{dir.x,0,dir.z});domino.face=dir;const float yaw=std::atan2(dir.x,dir.z);
        V bucket=end+dir*13;domino.extra=uint32_t(real("toy_bucket",bucket,yaw,0));domino.extraBase=bucket;domino.extraRotate={0,yaw,0};
        rewards(domino,bucket+V{0,4,0},{{"tsumiki",3,4.5f,kRed},{"marble",1.6f,2.4f,kSky},{"toy_ball",6,8,kPlain},{"minicar",6,7,kBlue},{"tsumiki",3,4.5f,kYellow},{"marble",1.6f,2.4f,kPink},{"candy",2.4f,3,kPlain}},28);}

    // ---- ボウリング: 和室の壁沿いに敷いたレーン。玉になって転がり、全部倒すとストライクで小物が降る ----
    {const V lane{-175,0,17};real("bowling_lane",lane,0,0);
        const int g=AddGimmick(r,GimmickKind::Bowling,"ボウリング",lane+V{18,0,0},16,10,.4f,{-186,0,-8});auto& bowl=gimmicks_[size_t(g)];bowl.face={1,0,0};
        bowl.path={lane+V{18,0,0},lane+V{278*kUnit,0,0}};
        for(int row=0;row<4;++row)for(int k=0;k<=row;++k){V at=lane+V{(246+8*float(row))*kUnit,0,(float(k)-float(row)*.5f)*6.4f*kUnit};
            bowl.parts.push_back(uint32_t(real("bowling_pin",at,spin(),.8f)));bowl.partBase.push_back(at);bowl.partRotate.push_back({0,0,0});}
        rewards(bowl,lane+V{262*kUnit,2,0},{{"candy",2.4f,3.2f,kPlain},{"coin",2.2f,2.8f,kPlain},{"marble",1.6f,2.4f,kYellow},{"tsumiki",3,4,kGreen},{"marble",1.6f,2.4f,kPink}},26);}

    // ---- 液体: こぼれた蜂蜜 (机)・のり (本棚)・ジュース (おもちゃ箱) の跡をつたって、床と家具の上を行き来する ----
    {struct Spill {const char* name;const char* item;V bottom,wall,top;float minCm;Engine::Vector4 color;V sign;V item_at;float itemYaw;float itemLift;};
        const Spill spills[]={
            // 机の左の縁からベッド側へ流す。上下の輪も同じ流れの両端に置く。
            {"蜂蜜をつたって机へ","honey_jar",{148,0,-264},{168.6f,0,-264},{186,86.4f,-264},8,{1,.62f,.1f,1},{140,0,-240},{198,86.4f+6.2f,-266},-1.5708f,6.2f},
            {"のりをつたって本棚へ","glue_bottle",{236,0,-40},{262.6f,0,-40},{274,RoomShelfTop,-40},10,{.92f,.95f,1,1},{232,0,-12},{296,RoomShelfTop+3.8f,-40},-1.5708f,3.8f},
            {"ジュースをつたっておもちゃ箱へ","juice_cup",{-232,0,28},{-232,0,5.4f},{-232,RoomToyboxTop,-14},8,{1,.48f,.14f,1},{-258,0,26},{-232,RoomToyboxTop+4.8f,-26},0,4.8f}};
        for(const auto& s:spills){const V up{s.wall.x,s.top.y,s.wall.z};
            // 下から上へ、上から下へ。どちらの端の輪でも押せる (上の端には看板を立てない)
            const int a=AddGimmick(r,GimmickKind::Liquid,s.name,s.bottom,13,s.minCm,std::atan2(s.wall.x-s.bottom.x,s.wall.z-s.bottom.z),s.sign);
            gimmicks_[size_t(a)].path={s.bottom,s.wall,up,s.top};
            std::string down=s.name;down=down.substr(0,down.find("つたって"))+"をつたって床へ";
            const int b=AddGimmick(r,GimmickKind::Liquid,down,s.top,12,s.minCm,0,{0,-1,0});gimmicks_[size_t(b)].path={s.top,up,s.wall,s.bottom};gimmicks_[size_t(a)].link=b;gimmicks_[size_t(b)].link=a;
            // 倒れた入れ物と、こぼれたたまり・壁をつたう跡
            real(s.item,s.item_at,s.itemYaw,0,1,kPlain,1.5708f);
            // 入れ物の口から流れ続ける液体の粒子 (粘りは種類で変わる)
            {V mouth=s.item_at;mouth.y=s.top.y;V toward=up-mouth;toward.y=0;mouth=mouth+Unit(toward)*std::min(5.f,Length(toward)*.4f);
                const auto kind=std::string(s.item)=="honey_jar"?Chrono::SpillKind::Honey:std::string(s.item)=="glue_bottle"?Chrono::SpillKind::Glue:Chrono::SpillKind::Juice;
                AddDevourSpill(kind,s.color,mouth,up,s.wall,s.bottom);}
            LowProp(r,"spill",s.bottom+V{0,.05f,0},1.5f,spin(),0,s.color,0);LowProp(r,"spill",s.top+V{0,.05f,0},1.1f,spin(),0,s.color,0);
            V side=Unit(V{s.bottom.z-s.wall.z,0,s.wall.x-s.bottom.x});V out=Unit(V{s.bottom.x-s.wall.x,0,s.bottom.z-s.wall.z});
            V w0=s.wall-side*3,w1=s.wall+side*3;
            streak("Chrono Room spill",{{V{std::min(w0.x,w1.x)-std::abs(out.x)*.5f,0,std::min(w0.z,w1.z)-std::abs(out.z)*.5f},V{std::max(w0.x,w1.x)+std::abs(out.x)*.5f,s.top.y+.4f,std::max(w0.z,w1.z)+std::abs(out.z)*.5f}},
                {Lerp(s.bottom,s.wall,.3f)-side*4+V{0,.02f,0},s.wall+side*4+V{0,.3f,0}}},s.color);
            // 上の面の小物 (登ったご褒美)
            for(int i=0;i<10;++i){V at=s.top+V{(RingNoise(++seed)-.5f)*24,0,(RingNoise(++seed)-.5f)*24};at.y=RoomTop(at);if(at.y<s.top.y-1)continue;
                const char* ids[]={"marble","coin","candy","marble"};sized(ids[i%4],at,i%4==1?2.6f:2.2f,spin(),1,i%4==0?marbles[i/4%4]:kPlain);}}
        // 本棚の上: 読み終えた漫画と、置き場に困ったおもちゃ。おもちゃ箱の上: 入りきらなかったおもちゃ
        for(int i=0;i<4;++i)real("manga",{282+(RingNoise(++seed)-.5f)*10,RoomShelfTop+float(i)*2*kUnit,-92},.1f*float(i),.6f);
        real("nuigurumi",{280,RoomShelfTop,-70},-1.5708f,.8f,.7f);real("toy_ball",{282,RoomShelfTop,-22},0,.7f,.7f);
        real("minicar",{-250,RoomToyboxTop,-34},.6f,.7f,1,kBlue);real("toy_ball",{-212,RoomToyboxTop,-30},0,.7f,.6f);}

    // ---- だるま落とし: 和室の入口の先。水鉄砲になって、上下に動く狙いに合わせて胴を撃ち抜く ----
    {const V shooter{40,0,82},stand{118,0,82};const float m=1.6f,discH=4*kUnit*m,standTop=2*kUnit*m;
        const int g=AddGimmick(r,GimmickKind::Shoot,"だるま落とし",shooter,16,8,0,{52,0,58});auto& daruma=gimmicks_[size_t(g)];daruma.face={1,0,0};daruma.scale=m;daruma.spots={stand};
        real("daruma_stand",stand,0,0,m);const Engine::Vector4 colors[]={kRed,kBlue,kYellow,kGreen,kPink};
        for(int i=0;i<GimmickDarumaDiscs;++i){V at=stand+V{0,standTop+float(i)*discH,0};daruma.parts.push_back(uint32_t(real("daruma_disc",at,0,.8f,m,colors[i])));daruma.partBase.push_back(at);daruma.partRotate.push_back({0,0,0});}
        V head=stand+V{0,standTop+float(GimmickDarumaDiscs)*discH,0};const float face=std::atan2(shooter.x-stand.x,shooter.z-stand.z);
        daruma.parts.push_back(uint32_t(real("daruma",head,face,.9f,m)));daruma.partBase.push_back(head);daruma.partRotate.push_back({0,face,0});
        rewards(daruma,stand+V{0,4,0},{{"candy",2.4f,3.2f,kPlain},{"coin",2.2f,2.8f,kPlain},{"marble",1.6f,2.4f,kRed},{"mikan",5,5.5f,kPlain},{"marble",1.6f,2.4f,kSky},{"koma_fu",3,3.4f,kPlain}},26);}

    // ---- 紙飛行機: 机の上から飛び立って、和室の入口を抜けて座布団の上へ降りる ----
    {const V launch{232,86.4f,-240};
        const int g=AddGimmick(r,GimmickKind::Plane,"紙飛行機で飛ぶ",launch,14,10,.3f,{184,86.4f,-284});auto& plane=gimmicks_[size_t(g)];
        plane.path={launch,{234,98,-216},{238,104,-186},{242,94,-110},{248,76,-30},{250,62,46},{251,40,100},{252,14,140},{254,0,156}};plane.spots={{250,62,46}};
        for(int i=0;i<3;++i)real("kami_hikoki",{200+float(i)*14,86.4f,-282+float(i%2)*8},spin(),.6f,1.1f);
        for(int i=0;i<4;++i)real("kami_hikoki",{236+(RingNoise(++seed)-.5f)*50,0,120+RingNoise(++seed)*40},spin(),.6f,1.1f);}
}
// 散らかした物を置かない場所: ギミックの輪と、ドミノ・レーン・液体の道の上、だるま落としの台のまわり
bool ChronoSystem::GimmickClear(V at,float margin)const{
    auto flat=[](V a){a.y=0;return a;};
    // 看板の土台の上には物を散らかさない
    for(const auto& sign:gimmickSigns_)if(Length(flat(at-sign))<14+margin)return true;
    for(const auto& g:gimmicks_){V d=flat(at-g.at);if(Length(d)<g.radius+margin)return true;
        for(const auto& spot:g.spots)if(g.kind==Chrono::GimmickKind::Shoot&&Length(flat(at-spot))<26+margin)return true;
        if(g.kind==Chrono::GimmickKind::Shoot){V a=flat(g.at),b=g.spots.empty()?a:flat(g.spots[0]);if(Chrono::LifeSegmentDistance(flat(at),a,b)<10+margin)return true;}
        const float corridor=g.kind==Chrono::GimmickKind::Bowling?20.f:g.kind==Chrono::GimmickKind::Domino?12.f:g.kind==Chrono::GimmickKind::Liquid?10.f:0.f;
        if(corridor>0)for(size_t i=1;i<g.path.size();++i)if(g.path[i].y<1&&g.path[i-1].y<1&&Chrono::LifeSegmentDistance(flat(at),flat(g.path[i-1]),flat(g.path[i]))<corridor+margin)return true;
        if(g.kind==Chrono::GimmickKind::Domino&&g.extra!=0xffffffffu&&Length(flat(at-g.extraBase))<22+margin)return true;
        if(g.kind==Chrono::GimmickKind::Plane&&!g.path.empty()&&Length(flat(at-g.path.back()))<16+margin)return true;}
    return false;
}
// 部屋の床 (見た目はモデルと同じ平らな色の板を上に敷く)
void ChronoSystem::BuildRoomGround(CityBuilder& city){
    const float h=Chrono::DevourStages()[size_t(devourStage_)].half;
    city.Polygon(CityWood,{{-h,-1,-h},{h,-1,-h},{h,-1,h},{-h,-1,h}},{0,1,0});
}
// 部屋の家具・小物・変形アイテム
void ChronoSystem::BuildRoomStage(entt::registry& r,const RingCityTools& tools){
    using namespace Chrono;auto* render=Engine::Renderer::GetInstance();const auto& stage=DevourStages()[size_t(devourStage_)];
    BeginLowProps(6);
    // 家具は飲み込めない壁 (敷地だけを作り、吸収の対象の番号は持たない)
    ringLots_.clear();gimmickSigns_.clear();for(const auto& foot:RoomLots()){RingLot lot;lot.foot=foot;ringLots_.push_back(lot);}
    ringLotProps_.assign(ringLots_.size(),-1);devourBuildingParts_.clear();
    // 平らな色の箱の集まり (床の板や壁)
    auto boxes=[&](const char* name,const std::vector<std::array<V,2>>& list,Engine::Vector4 color){CityBuilder b;for(const auto& box:list)b.Box(CityMetal,box[0],box[1]);auto& g=b.geometry[CityMetal];
        auto e=r.create();r.emplace<NameComponent>(e,name);r.emplace<TransformComponent>(e).scale={1,1,1};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;mr.shaderName="LowpolySurface";mr.useCubemap=false;mr.color={color.x,color.y,color.z,color.w};return e;};
    const float h=stage.half,top=300,t=4,z1=kWallZ+kWallDepth;
    // 壁と壁の飾りは、カメラとの間に入ると壁ごと薄くする
    auto wall=[&](const char* name,const std::vector<std::array<V,2>>& list,Engine::Vector4 color,V lo,V hi){r.emplace<CameraOccluder>(boxes(name,list,color),(lo+hi)*.5f,hi-lo,1.f);};
    auto plain=[&](const std::vector<std::array<V,2>>& list,Engine::Vector4 color){wall("Chrono Room wall",list,color,list[0][0],list[0][1]);};
    // 床: 子ども部屋はフローリングとじゅうたん、和室は畳
    boxes("Chrono Room floor",{{V{-h,-.6f,-h},V{h,-.3f,kWallZ}}},{.62f,.42f,.26f,1});
    {std::vector<std::array<V,2>> planks;for(int i=0;i<10;++i){float x=-h+2*h*float(i)/10;planks.push_back({V{x+.6f,-.3f,-h},V{x+h/5-.6f,-.2f,kWallZ}});}boxes("Chrono Room flooring",planks,{.74f,.53f,.33f,1});}
    boxes("Chrono Room rug",{{V{-50,-.2f,-170},V{170,-.14f,-40}}},{.32f,.62f,.72f,1});boxes("Chrono Room rug edge",{{V{-42,-.14f,-162},V{162,-.07f,-48}}},{.96f,.88f,.66f,1});boxes("Chrono Room rug middle",{{V{-34,-.07f,-154},V{154,0,-56}}},{.32f,.62f,.72f,1});
    boxes("Chrono Room tatami edge",{{V{-h,-.6f,kWallZ},V{h,-.2f,h}}},{.42f,.5f,.3f,1});
    {std::vector<std::array<V,2>> mats;for(int i=0;i<6;++i)for(int k=0;k<2;++k){float x=-h+2*h*float(i)/6,z=z1+(h-z1)*float(k)/2;mats.push_back({V{x+1.2f,-.2f,z+1.2f},V{x+h/3-1.2f,0,z+(h-z1)/2-1.2f}});}boxes("Chrono Room tatami",mats,{.86f,.9f,.46f,1});}
    // 壁: 1 面ずつ別のエンティティにして、カメラとの間に入ると消す
    const Engine::Vector4 child{1.15f,1.25f,1.32f,1},washi{1.45f,1.34f,1.08f,1};
    plain({{V{-h-t,0,-h-t},V{h+t,top,-h}}},child);plain({{V{-h-t,0,h},V{h+t,top,h+t}}},washi);
    plain({{V{h,0,-h},V{h+t,top,kWallZ}}},child);plain({{V{-h-t,0,-h},V{-h,top,kWallZ}}},child);
    plain({{V{h,0,kWallZ},V{h+t,top,h}}},washi);plain({{V{-h-t,0,kWallZ},V{-h,top,h}}},washi);
    // 間の壁と入口の上の鴨居
    {const float d0=kDoors[0][0],d1=kDoors[0][1],d2=kDoors[1][0],d3=kDoors[1][1],lintel=120;
        plain({{V{-h,0,kWallZ},V{d0,top,z1}}},washi);plain({{V{d1,0,kWallZ},V{d2,top,z1}}},washi);plain({{V{d3,0,kWallZ},V{h,top,z1}}},washi);
        plain({{V{d0,lintel,kWallZ},V{d1,top,z1}}},washi);plain({{V{d2,lintel,kWallZ},V{d3,top,z1}}},washi);}
    // 実寸で置く物 (mult は実寸に対する倍率)
    auto real=[&](const std::string& id,V at,float yaw,float fill,float mult=1,Engine::Vector4 tint=kPlain,float tilt=0){return LowProp(r,id,at,kUnit*mult,yaw,fill,tint,tilt);};
    // 家具
    // (ベッドと机は上の板の範囲で判定して、上に乗っている間は薄くしない)
    {auto& o=r.get<CameraOccluder>(real("bed",{-180,0,-240},0,0));o.center={-180,44,-240};o.size={240,28,120};}
    {auto& o=r.get<CameraOccluder>(real("desk",{235,0,-264},0,0));o.center={235,84,-264};o.size={130,6,72};}
    real("chair",{235,0,-195},3.14159f,0);real("bookshelf",{282,0,-60},-1.5708f,0);real("toybox",{-232,0,-20},.0f,0);
    // 本の階段・積んだ本
    real("book_stairs",{-180,0,-180},0,0);real("book_steps",{148.4f,0,190},-1.5708f,0);
    // ベッドの下の出口: ベッドの下から見て前の列の右端に収納ケースを 1 つだけ置く (飲めない。8cm まで育つと段差ジャンプで越えられる)
    {const V at{(Chrono::RoomExitX0-60)*.5f,0,-186};r.emplace_or_replace<CameraOccluder>(real("storage_case",at,0,0,1,{1,.95f,.86f,1}),at+V{0,RoomCaseHeight*.5f,0},V{-60-Chrono::RoomExitX0,RoomCaseHeight,12},1.f);}
    // 出口のほかは、飲めない本を立てたり横に積んだりして並べた壁 (kBookWalls)。跳び越えられないので、出口が 1 つに決まる
    {const Engine::Vector4 covers[]={kRed,kBlue,kGreen,kYellow,kSky,kPink,kPlain};int bookSeed=7000;
        const auto& m=LowModelOf("manga");const float thick=(m.hi.y-m.lo.y)*kUnit,length=(m.hi.z-m.lo.z)*kUnit,width=(m.hi.x-m.lo.x)*kUnit;
        auto book=[&](V at,float yaw,float mult,int color,bool standing,V center,V size){
            auto e=LowProp(r,"manga",at,kUnit*mult,yaw,0,covers[size_t(color)%7],standing?1.5708f:0.f);r.emplace_or_replace<CameraOccluder>(e,center,size,1.f);};
        for(const auto& f:kBookWalls){const bool alongX=f.x1-f.x0>f.z1-f.z0;const float run=alongX?f.x1-f.x0:f.z1-f.z0,mid=alongX?(f.z0+f.z1)*.5f:(f.x0+f.x1)*.5f,start=alongX?f.x0:f.z0,yaw=alongX?1.5708f:0.f;
            // 列の向きの位置 u と高さ y を部屋の座標へ。size は列の向き・高さ・奥行きの大きさ
            auto place=[&](float u,float y){return alongX?V{start+u,y,mid}:V{mid,y,start+u};};
            auto box=[&](float along,float tall,float deep){return alongX?V{along,tall,deep}:V{deep,tall,along};};
            float u=0;for(int group=0;u<run-1;++group){
                if(group%3==2&&u+length<=run){
                    // 横に積んだ本: 長い辺を列に沿わせて 6〜8 冊
                    const int count=6+int(RingNoise(++bookSeed)*3);
                    for(int i=0;i<count;++i)book(place(u+length*.5f,float(i)*thick),yaw+(RingNoise(++bookSeed)-.5f)*.12f,1,group+i,false,place(u+length*.5f,(float(i)+.5f)*thick),box(length,thick,width));
                    u+=length+.6f;}
                else{
                    // 立てた本: 背を外へ向けて 5〜9 冊。高さを少しずつ変える
                    const int count=5+int(RingNoise(++bookSeed)*5);
                    for(int i=0;i<count;++i){const float mult=.9f+.25f*RingNoise(++bookSeed),step=thick*mult;if(u+step>run)break;
                        book(place(u,length*.5f*mult),yaw,mult,group*3+i,true,place(u+step*.5f,length*.5f*mult),box(step,length*mult,width*mult));u+=step;}
                    u+=.4f;}}}}
    // 本棚とおもちゃ箱は上に乗れる (こぼれた液体をつたって登る)
    ringLots_[0].top=Chrono::RoomToyboxTop;ringLots_[2].top=Chrono::RoomShelfTop;
    BuildRoomGimmicks(r);BuildRoomSets(r);
    // 天井・窓・照明: 部屋の中から照らす
    {const Engine::Vector4 lampWhite{2.6f,2.5f,2.2f,1},lampWarm{2.8f,2.2f,1.4f,1};
        auto noShadow=[&](entt::entity e){r.get<MeshRendererComponent>(e).shaderName="LowpolyNoShadow";};
        boxes("Chrono Room ceiling",{{V{-h-t,top,-h-t},V{h+t,top+4,kWallZ}}},{1.25f,1.25f,1.2f,1});boxes("Chrono Room ceiling",{{V{-h-t,top,kWallZ},V{h+t,top+4,h+t}}},{1.4f,1.26f,1,1});
        noShadow(LowProp(r,"ceiling_lamp",{40,top-9*kUnit,-130},kUnit,0,0,lampWhite));noShadow(LowProp(r,"washitsu_lamp",{-120,top-70*kUnit,176},kUnit,0,0,lampWarm));
        // 窓とカーテン (子ども部屋) と障子 (和室): 外の光で明るい
        const Engine::Vector4 glass{3.2f,3.3f,3.4f,1},paper{3,2.7f,2.2f,1},frame{.72f,.5f,.3f,1};
        wall("Chrono Room window",{{V{-h+.1f,70,-150},V{-h+1,220,-10}}},glass,V{-h-t,0,-h},V{-h,top,kWallZ});wall("Chrono Room window frame",{{V{-h+1,66,-154},V{-h+2,70,-6}},{V{-h+1,220,-154},V{-h+2,224,-6}},{V{-h+1,70,-154},V{-h+2,220,-150}},{V{-h+1,70,-10},V{-h+2,220,-6}},{V{-h+1,70,-82},V{-h+2,220,-78}}},frame,V{-h-t,0,-h},V{-h,top,kWallZ});
        wall("Chrono Room curtain",{{V{-h+2,40,-176},V{-h+5,232,-140}}},{.42f,.62f,.9f,1},V{-h-t,0,-h},V{-h,top,kWallZ});wall("Chrono Room curtain",{{V{-h+2,40,-20},V{-h+5,232,16}}},{.42f,.62f,.9f,1},V{-h-t,0,-h},V{-h,top,kWallZ});
        wall("Chrono Room shoji",{{V{-110,10,h-1},V{120,190,h-.1f}}},paper,V{-h,0,h},V{h,top,h+t});
        {std::vector<std::array<V,2>> bars;for(int i=0;i<=8;++i){float x=-110+230*float(i)/8;bars.push_back({V{x-1,10,h-2},V{x+1,190,h-1}});}for(int i=0;i<=5;++i){float y=10+180*float(i)/5;bars.push_back({V{-110,y-1,h-2},V{120,y+1,h-1}});}wall("Chrono Room shoji frame",bars,{.44f,.28f,.16f,1},V{-h,0,h},V{h,top,h+t});}
        auto lamp=[&](V at,DirectX::XMFLOAT3 color,float intensity,float range){auto e=r.create();r.emplace<NameComponent>(e,"Chrono Room light");r.emplace<TransformComponent>(e).translate=Write(at);auto& p=r.emplace<PointLightComponent>(e);p.color=color;p.intensity=intensity;p.range=range;};
        lamp({40,270,-130},{1,.97f,.9f},1.6f,560);lamp({-60,240,176},{1,.92f,.78f},1.9f,600);lamp({-190,14,-240},{.95f,.95f,1},.9f,190);lamp({160,170,270},{1,.94f,.84f},1.4f,420);
        // 太陽の代わりに、真上からの光で物の足元に影を落とす。部屋全体は明るめにする
        render->SetAmbientColor({1.6f,1.55f,1.45f});
        for(auto e:r.view<NameComponent,TransformComponent,DirectionalLightComponent>())if(r.get<NameComponent>(e).name=="Sun"){r.get<TransformComponent>(e).rotate={DirectX::XMConvertToRadians(82.f),DirectX::XMConvertToRadians(20.f),0};auto& sun=r.get<DirectionalLightComponent>(e);sun.intensity=.95f;sun.color={1,.96f,.88f};}}
    BuildStageItem(r,tools);
}
// セットの物: 土台のまわりに、整った物と散らかった物を置く
void ChronoSystem::BuildRoomSets(entt::registry& r){
    using namespace Chrono;int seed=211;constexpr float mikan=.7f;
    const auto& stage=Chrono::DevourStages()[size_t(devourStage_)];
    auto real=[&](const std::string& id,V at,float yaw,float fill,float mult=1,Engine::Vector4 tint=kPlain,float tilt=0){return LowProp(r,id,at,kUnit*mult,yaw,fill,tint,tilt);};
    auto spin=[&]{return RingNoise(++seed)*6.283f;};
    // その場所の一番上の面に置く
    auto ground=[&](float x,float z){return V{x,RoomTop({x,0,z}),z};};
    // 幅 (cm) を決めて置く
    auto sized=[&](const char* id,V at,float cm,float yaw,float fill,Engine::Vector4 tint=kPlain){return LowProp(r,id,at,LowScaleFor(id,cm*kUnit),yaw,fill,tint,0);};
    // 積み上げる
    auto stack=[&](const char* id,V at,int count,float height,float fill,float mult=1){for(int i=0;i<count;++i)real(id,at+V{0,float(i)*height*kUnit*mult,0},float(i%2)*.14f-.07f,fill,mult);};
    // こたつ: 天板にかごのみかん・急須・湯呑・リモコン。みかんは布団の上を転がって床まで落ちている
    {const V k{-120,0,176};const float top=42;real("kotatsu",k,0,0);
        real("zabuton",{-262,0,176},0,0);real("zabuton",{37,0,120},0,0);real("zabuton",{37,0,232},0,0);
        V basket=k+V{-14,top,-12};real("kago",basket,.3f,.7f,.75f);
        for(int i=0;i<4;++i){float a=.8f+1.5708f*float(i);real("mikan",basket+V{std::cos(a)*5,2.2f,std::sin(a)*5},a,1,mikan);}real("mikan",basket+V{0,7,0},2,1,mikan);
        real("kyusu",k+V{18,top,16},2.4f,.8f,.8f);real("yunomi",k+V{22,top,-16},0,.9f,.9f);real("yunomi",k+V{-22,top,22},1,.9f,.9f);real("remocon",k+V{-2,top,24},.5f,.8f);
        real("mikan",k+V{6,top,2},4,1,mikan);real("mikan",k+V{-30,top,8},1,1,mikan);
        const float rolled[][2]={{-60,150},{-40,190},{-78,250},{-170,262},{-196,120},{-150,84},{-52,96},{-14,70},{10,176},{-22,282},{-250,262},{-262,92}};
        for(const auto& p:rolled)real("mikan",ground(p[0],p[1]),spin(),1,mikan);
        real("yunomi",{-24,3.4f,276},.9f,.9f,.9f,kPlain,1.5708f);}
    // 将棋: 盤の上は対局の途中で、駒は盤のまわりにも落ちている
    {const V s{170,0,190};const float w=21.6f,d=23.4f,edge=1.68f,top=22.9f,cellX=2*(w-edge)/9,cellZ=2*(d-edge)/9;real("shogi_board",s,0,0);
        real("zabuton",{170,0,130},0,0);real("zabuton",{246,0,190},0,0);
        struct Piece {const char* id;int col,row;};
        const Piece mine[]={{"ou",4,0},{"kin",3,0},{"kin",5,1},{"gin",2,1},{"kaku",1,1},{"hi",7,1},{"kei",1,0},{"kyo",0,0},{"kyo",8,0},{"fu",0,2},{"fu",1,2},{"fu",3,2},{"fu",5,3},{"fu",6,2},{"fu",8,2}};
        const Piece theirs[]={{"ou",4,8},{"kin",3,8},{"kin",5,8},{"gin",6,7},{"hi",1,7},{"kaku",7,7},{"kei",7,8},{"kyo",0,8},{"kyo",8,8},{"fu",0,6},{"fu",2,6},{"fu",3,6},{"fu",4,5},{"fu",7,6},{"fu",8,6}};
        auto piece=[&](const Piece& p,float yaw){real(std::string("koma_")+p.id,s+V{-(w-edge)+(float(p.col)+.5f)*cellX,top,-(d-edge)+(float(p.row)+.5f)*cellZ},yaw,1);};
        for(const auto& p:mine)piece(p,0);
        for(const auto& p:theirs)piece(p,3.14159f);
        const char* spilled[]={"fu","fu","fu","gin","kei","fu","kaku","fu","kin","fu","kyo","fu","fu","gin"};
        for(int i=0,placed=0;placed<14&&i<300;++i){float a=spin(),dist=30+34*RingNoise(++seed)*RingNoise(++seed);V at=s+V{std::cos(a)*dist,0,std::sin(a)*dist};
            if(at.z<kWallZ+kWallDepth+6||at.z>294||at.x>294||RoomTop(at)>.1f)continue;real(std::string("koma_")+spilled[placed],at,spin(),1);++placed;}
        real("yunomi",{132,0,226},0,.9f,.9f);real("mikan",{120,0,240},1,1,mikan);}
    // トランプ: 床に配った手札と、飛び散ったカード
    {const V c{120,0,272};const char* cards[]={"trump_red","trump_black","trump_back"};
        for(int i=0;i<5;++i)real("trump_back",c+V{float(i)*1.6f-8,float(i)*.2f,0},(float(i)-2)*.25f,.7f);
        for(int i=0;i<24;++i){float a=spin(),dist=10+36*RingNoise(++seed);V at=c+V{std::cos(a)*dist,0,std::sin(a)*dist*.5f};real(cards[i%3],at,spin(),.7f);}}
    // 床の間: 招き猫・だるま・ティッシュ
    // 床の間: 招き猫が寄り集まって踊り、だるまは転がって揺れている
    {const float maneki[][3]={{288,272,1.1f},{268,262,.9f},{282,248,1},{252,280,1.2f},{262,242,.85f}};for(const auto& m:maneki)AddDance(r,real("maneki",{m[0],0,m[1]},3.14f+(RingNoise(++seed)-.5f)*1.2f,.8f,m[2]),5+RingNoise(++seed)*2,.22f,2.5f);
        const float daruma[][3]={{200,282,1.3f},{178,270,.9f},{214,262,1},{160,284,.7f},{226,288,1.1f}};for(const auto& d:daruma)AddDance(r,real("daruma",{d[0],0,d[1]},3.14f+(RingNoise(++seed)-.5f)*1.6f,.8f,d[2]),3,.2f,0);}
    real("tissue",{86,0,240},.3f,.7f);
    // 子ども部屋: 積み木の山
    {const V c{70,0,-110};const Engine::Vector4 colors[]={kRed,kYellow,kBlue,kGreen};constexpr float m=1.4f,side=4*kUnit*m;int k=0;
        for(int level=0;level<3;++level){int n=3-level;for(int x=0;x<n;++x)for(int z=0;z<n;++z)real("tsumiki",c+V{(float(x)-float(n-1)*.5f)*side,float(level)*side,(float(z)-float(n-1)*.5f)*side},0,.7f,m,colors[k++%4]);}}
    // ぬいぐるみのお茶会: 3 匹が輪になってお菓子を囲む
    {const V c{-5,0,-65};for(int i=0;i<3;++i){float a=1.2f+2.094f*float(i);AddDance(r,real("nuigurumi",c+V{std::cos(a)*22,0,std::sin(a)*22},-a-1.5708f,.8f,.75f),5,.12f,1.5f);}
        for(int i=0;i<6;++i){float a=float(i)*1.05f;real("candy",c+V{std::cos(a)*7,0,std::sin(a)*7},a,1);}}
    // ミニカーの渋滞: じゅうたんの端に一列に並ぶ
    // ミニカーの周回: じゅうたんの縁の道を走る。止まったミニカーは駐車場に並ぶ
    {const Engine::Vector4 colors[]={kPlain,kYellow,kBlue,kGreen};const std::vector<V> track{{-26,0,-146},{146,0,-146},{146,0,-64},{-26,0,-64}};
        for(int i=0;i<7;++i)AddRoute(real("minicar",track[0],0,.7f,1,colors[i%4]),track,34,float(i)/7);
        for(int i=0;i<6;++i){const float a=RingNoise(++seed)*6.283f,d=60*RingNoise(++seed);real("minicar",{60+std::cos(a)*d*1.4f,0,-104+std::sin(a)*d*.7f},spin(),.7f,1,colors[(i+1)%4]);}}
    // おもちゃ箱: ひっくり返したように、ボールとミニカーと積み木が広がる
    {const V c{-232,0,-20};const char* toys[]={"toy_ball","minicar","tsumiki","nuigurumi","tsumiki","minicar"};const float mult[]={.6f,1,1,.6f,1,1};const Engine::Vector4 colors[]={kPlain,kBlue,kYellow,kPlain,kRed,kPlain};
        for(int i=0;i<30;++i){float a=-.6f+2.4f*RingNoise(++seed),dist=46+44*RingNoise(++seed);V at=c+V{std::cos(a)*dist,0,std::sin(a)*dist};if(at.z>kWallZ-6||at.x<-294||GimmickClear(at,4))continue;real(toys[i%6],at,spin(),.7f,mult[i%6],colors[i%6]);}}
    // 漫画の山と、読みかけで広げた物
    {const V c{-20,0,-215};for(int i=0;i<6;++i)real("manga",c+V{float(i%2)*.6f,float(i)*2*kUnit,0},float(i)*.11f-.3f,.6f);real("manga",c+V{30,0,14},1.2f,.6f);real("manga",c+V{-28,0,20},2.4f,.6f);}
    // 本棚の前に落ちた漫画
    for(int i=0;i<4;++i)real("manga",{240-float(i)*12,0,-90+float(i)*18},spin(),.6f);
    // 机の上: ノートと筆記用具。机の下には鉛筆と消しゴムが落ちている
    {const float y=86.4f;real("note",{205,y,-264},.1f,.6f);real("manga",{270,y,-250},.4f,.7f);real("enpitsu",{196,y,-246},.7f,.8f);real("enpitsu",{232,y,-280},2.4f,.8f);real("keshigomu",{222,y,-250},1,1);
        for(int i=0;i<8;++i)real(i%2?"keshigomu":"enpitsu",{176+float(i)*15,0,-262+float(i%3)*22},spin(),.6f);}
    // ベッドの上: 枕元のぬいぐるみ・リモコン・靴下
    real("nuigurumi",ground(-210,-214),1.5f,.9f,.8f);real("remocon",ground(-170,-236),.8f,.8f);real("sock",ground(-130,-260),2,.8f);
    real("tissue",{40,0,-292},.3f,.7f);
    // ---- 散らかし方: 遊んだ跡やこぼれた跡が分かるように、まとまりの中で向きも間隔も崩す ----
    const auto blocked=[](size_t){return true;};
    // その場所に置けるか (家具や収納ケースの中・部屋の外には置かない)
    auto free=[&](V at,float margin){return std::abs(at.x)<294-margin&&std::abs(at.z)<294-margin&&RingWalkable(at,margin,ringLots_,blocked)&&!GimmickClear(at,margin);};
    // 範囲 (x0, z0, x1, z1) に一面に散らす。床の高さ (ベッドの上なら y) に置く
    auto dust=[&](const char* id,float x0,float z0,float x1,float z1,int count,float low,float high,float fill,Engine::Vector4 tint,float y=-1){
        for(int i=0,tries=0;i<count&&tries<count*8;++tries){V at{x0+(x1-x0)*RingNoise(++seed),0,z0+(z1-z0)*RingNoise(++seed)};at.y=y<0?0:y;
            const float cm=low+(high-low)*RingNoise(++seed);if(y<0&&(RoomGround(at)>.1f||!free(at,cm*.6f)))continue;sized(id,at,cm,spin(),fill,tint);++i;}};
    // まわりへ散らばった山: 真ん中ほど多い
    auto scatter=[&](const char* id,V center,float radius,int count,float low,float high,float fill,Engine::Vector4 tint=kPlain){
        for(int i=0,tries=0;i<count&&tries<count*8;++tries){const float a=RingNoise(++seed)*6.283f,d=radius*(RingNoise(++seed)+RingNoise(++seed))*.5f;V at=center+V{std::cos(a)*d,0,std::sin(a)*d};
            const float cm=low+(high-low)*RingNoise(++seed);at.y=RoomTop(at);if(at.y<.1f&&!free(at,cm*.6f))continue;sized(id,at,cm,spin(),fill,tint);++i;}};
    // 転がって続いた跡: 曲がりながら、間隔も揃えない
    auto trail=[&](const char* id,V from,V to,int count,float low,float high,float fill,Engine::Vector4 tint=kPlain,float wiggle=18){
        const V side=Unit(V{-(to.z-from.z),0,to.x-from.x});for(int i=0;i<count;++i){const float u=(float(i)+RingNoise(++seed)*.8f)/float(count);
            V at=Lerp(from,to,u)+side*(std::sin(u*7.5f+float(seed%7))*wiggle+(RingNoise(++seed)-.5f)*wiggle*.6f);at.y=RoomTop(at);const float cm=low+(high-low)*RingNoise(++seed);
            if(at.y<.1f&&!free(at,cm*.6f))continue;sized(id,at,cm,spin(),fill,tint);}};
    const Engine::Vector4 marbleColors[]={kPink,kSky,kGreen,kYellow};
    // ベッドの下 (5cm〜8cm): 開始の場所のまわりから一面に、なくした小物がほこりのように積もっている。出口の収納ケースへ近いほど少し大きい
    for(int k=0;k<4;++k)dust("marble",-294,-294,-76,-198,48,1,1.8f,1,marbleColors[k]);
    dust("coin",-294,-294,-76,-198,40,2.2f,2.8f,1,kPlain);dust("candy",-294,-294,-76,-198,30,2.4f,3.2f,1,kPlain);
    scatter("marble",stage.spawn,26,40,1,1.6f,1,kPink);scatter("coin",stage.spawn+V{18,0,8},22,14,2.2f,2.6f,1);
    dust("tsumiki",-294,-232,-76,-198,22,3,3.8f,.7f,kRed);dust("tsumiki",-110,-294,-76,-198,10,3,3.8f,.7f,kYellow);
    // 収納ケースを越えた先: ビー玉と硬貨が曲がりながら、崩れた積み木の塔へ続く
    trail("marble",{-84,0,-172},{40,0,-130},26,1.6f,2.6f,1,kSky);trail("coin",{-96,0,-170},{-40,0,-60},22,2.2f,2.8f,1,kPlain,26);
    // 崩れた積み木の塔: 塔の残りのまわりへ、色も大きさもまちまちに崩れている
    for(int k=0;k<4;++k)scatter("tsumiki",{70,0,-110},90,26,3,5,.7f,k==0?kRed:k==1?kYellow:k==2?kGreen:kBlue);
    // ジャーからこぼれたビー玉 (本棚の前) と、壁際まで転がった物
    scatter("marble",{230,0,-30},46,70,1.6f,2.6f,1,kSky);scatter("marble",{230,0,-30},70,20,1.6f,2.6f,1,kPink);
    // 脱ぎ散らかした靴下 (ベッドの足元) と、転がって部屋の隅に寄ったボール
    scatter("sock",{-90,0,-120},60,12,8,10,.7f);scatter("toy_ball",{-262,0,-96},40,5,7,10,.7f);scatter("toy_ball",{170,0,10},30,3,7,9,.7f);
    // 机のまわり: 鉛筆と消しゴムが机の下から椅子のまわりへ転がっている
    scatter("enpitsu",{230,0,-200},70,14,16,18,.4f);scatter("keshigomu",{220,0,-190},60,18,4,6,.7f);scatter("coin",{250,0,-170},40,16,2.2f,2.8f,1);
    // 和室へ続く跡: 入口からトランプとみかんが中へ散らばる
    trail("trump_back",{50,0,30},{110,0,250},14,7,8,.7f,kPlain,22);trail("mikan",{-10,0,50},{-110,0,120},6,5,5.5f,1,kPlain,14);
    // 和室: 壁際の漫画の山は崩れかけ、ティッシュは丸めて散らかっている
    for(int i=0;i<4;++i){const V at{112+float(i)*44+(RingNoise(++seed)-.5f)*14,0,66+(RingNoise(++seed)-.5f)*10};stack("manga",at,2+i%3,2,.6f);scatter("manga",at,22,1+i%2,10,12,.6f);}
    scatter("tissue",{40,0,90},26,3,11,13,.6f);scatter("mikan",{-270,0,100},30,6,5,5.5f,1);scatter("daruma",{120,0,110},40,3,8,10,.7f);
    // ベッドの上: 枕のまわりにぬいぐるみと靴下、布団の上にビー玉
    scatter("nuigurumi",{-250,0,-240},40,4,8,10,.8f);scatter("sock",{-130,0,-245},50,6,6,8,.8f);scatter("marble",{-170,0,-230},40,30,1.6f,2.6f,1,kPink);
    // 机の上: 宿題の途中の散らかり
    scatter("enpitsu",{220,0,-262},30,6,12,14,1);scatter("keshigomu",{260,0,-250},26,6,5,7,1.5f);scatter("coin",{200,0,-280},20,20,2.2f,2.8f,1.5f);
}
}
