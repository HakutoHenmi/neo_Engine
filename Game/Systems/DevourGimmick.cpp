// DevourGimmick.cpp
// 吸収モードの部屋のギミック (ChronoSystem の一部)。どれも床の輪の中で変形キーを 1 回押すだけで動く。
//   だるま落とし: 体が水鉄砲になり、上下に動く狙いに合わせて撃つ (押すたびに 1 発)。胴を全部抜くと小物がばらまかれる
//   ドミノ: 体がドミノの 1 枚になって倒れ、並んだドミノが次々に倒れる。最後のバケツが倒れて小物があふれる
//   ボウリング: 体が玉になってレーンを転がる (スティックで左右)。全部倒すとストライクで小物が降る
//   絵の具: しずくになって絵の具へ飛び込み、体がその色になる。同じ色の物を飲むと多く増える
//   液体 (蜂蜜・のり・ジュース): 薄い膜になって、こぼれた液体の跡をつたって家具の上と床を行き来する
//   紙飛行機: 高い所から紙飛行機になって、部屋の向こうまで滑空する (スティックで左右)
// 形と規則は Game/Chrono/DevourGimmickRules.h、置き場所は RoomStage.cpp。
#include "ChronoSystemInternal.h"
#include "../Chrono/RoomRules.h"

namespace Game {
using Chrono::GimmickKind;
namespace {
// 画面の言葉を出す種類 (0 飲む音のように跳ねる・1 ぶつかる音のように集中線つき)
constexpr int kPopRise=0,kPopHit=1;
constexpr uint32_t kNone=0xffffffffu;
}
// 看板 (旗) の土台の形: モデル (scripts/lowpoly の gimmick_sign を 1.2 倍) の台は半径 12・高さ 3.6、柱は半径 1.7
namespace {
constexpr float kSignBaseOuter=12.5f,kSignBaseInner=9.f,kSignBaseTop=3.6f,kSignPole=1.7f;
}
// 土台の上の高さ: 外の縁から内側へなめらかに上がる坂 (どの大きさでも段差として止まらず、そのまま乗り越えられる)
float ChronoSystem::GimmickSignGround(V at)const{
    float height=0;
    for(const auto& sign:gimmickSigns_){const float dx=at.x-sign.x,dz=at.z-sign.z,d=std::sqrt(dx*dx+dz*dz);if(d>=kSignBaseOuter)continue;
        const float u=std::clamp((kSignBaseOuter-d)/(kSignBaseOuter-kSignBaseInner),0.f,1.f);height=std::max(height,sign.y+kSignBaseTop*u*u*(3-2*u));}
    return height;
}
// 柱: 体の中心を柱から丸く押し出す。角が無いので、ぶつからずに柱の横へ滑る (体はやわらかいので、柱が少し体にめり込んでよい)
ChronoSystem::V ChronoSystem::GimmickSignSlide(V at,float bodyRadius)const{
    for(const auto& sign:gimmickSigns_){const float reach=kSignPole+bodyRadius*.35f;V d{at.x-sign.x,0,at.z-sign.z};const float len=Length(d);if(len>=reach)continue;
        const V dir=len>1e-3f?d*(1/len):V{1,0,0};at.x=sign.x+dir.x*reach;at.z=sign.z+dir.z*reach;}
    return at;
}
// ギミックを 1 つ足す: 目印の看板 (signAt の y が負なら立てない) を置く。床に立つ看板は乗り越えられる (GimmickSignGround)。
// 作動範囲は床の輪ではなく、縁から垂直に立つ光の膜で示す (DrawGimmickCurtains)
int ChronoSystem::AddGimmick(entt::registry& r,GimmickKind kind,const std::string& name,V at,float radius,float minCm,float signYaw,V signAt){
    if(gimmicks_.empty()){
        // 膜の形: 半径 1・高さ 1 の円筒の側面 (uv.y が高さ)
        std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;constexpr int segments=48;
        for(int i=0;i<=segments;++i){float a=6.2831853f*float(i)/float(segments),c=std::cos(a),s=std::sin(a);
            for(int k=0;k<2;++k)vertices.push_back({{c,float(k),s,1},{float(i)/float(segments),float(k)},{c,0,s}});}
        for(uint32_t i=0;i<uint32_t(segments);++i){uint32_t a=i*2;indices.insert(indices.end(),{a,a+1,a+3,a,a+3,a+2});}
        gimmickCurtain_=Engine::Renderer::GetInstance()->CreateDynamicMesh(vertices,indices);}
    DevourGimmick g;g.kind=kind;g.name=name;g.at=at;g.radius=radius;g.minCm=minCm;
    if(signAt.y>=0){g.sign=uint32_t(LowProp(r,GimmickBehaviorFor(kind).SignModel(),signAt,1.2f,signYaw,0,{1,1,1,1},0));
        if(signAt.y<1)gimmickSigns_.push_back(signAt);}
    gimmicks_.push_back(std::move(g));return int(gimmicks_.size())-1;
}
// 動かす物を止めておく (飲めないが体は止まる) / 放す (普通に飲める)。止めている間は大きさを覚えておく
void ChronoSystem::HoldGimmickProp(DevourGimmick& g,size_t part,bool hold){
    if(part>=g.partProps.size())return;int k=g.partProps[part];if(k<0||size_t(k)>=life_.props.size())return;auto& prop=life_.props[size_t(k)];
    if(hold){if(prop.radius<900){g.partRadius[part]=prop.radius;prop.radius=999;}}
    else if(prop.radius>=900)prop.radius=g.partRadius[part];
}
// 作り直した後: 見た目と物の番号を結び、動かす物を止め、ばらまく物を隠す
void ChronoSystem::ResetDevourGimmicks(entt::registry& r){
    gimmickRide_={};gimmickNear_=-1;gimmickJets_.clear();devourPaint_={0,0,0,0};devourPaintFlash_=0;gimmickMatchCool_=0;
    std::map<uint32_t,int> byVisual;for(size_t i=0;i<life_.props.size();++i)if(life_.props[i].visual!=kNone)byVisual[life_.props[i].visual]=int(i);
    auto find=[&](uint32_t visual){auto f=byVisual.find(visual);return f==byVisual.end()?-1:f->second;};
    for(auto& g:gimmicks_){g.state=0;g.hits=0;g.pending=-1;g.clock=0;g.wobble=0;g.done=false;
        g.partProps.clear();g.partRadius.assign(g.parts.size(),1);g.partAge.assign(g.parts.size(),-1);
        for(size_t i=0;i<g.parts.size();++i){g.partProps.push_back(find(g.parts[i]));HoldGimmickProp(g,i,true);}
        g.rewardProps.clear();for(auto visual:g.rewards){int k=find(visual);g.rewardProps.push_back(k);if(k<0)continue;
            life_.props[size_t(k)].active=false;auto e=entt::entity(visual);if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;}}
    ClearGimmickAreas(r);
}
// 作動範囲の中にある他の物 (散らかした物・家具の上の物) を、範囲の外の同じ高さの床へ押し出す。
// ギミック自身の物 (動かす物・ばらまく物) はそのまま。押し出せる所が無ければ見えなくする
void ChronoSystem::ClearGimmickAreas(entt::registry& r){
    std::set<int> own;for(const auto& g:gimmicks_){for(int k:g.partProps)own.insert(k);for(int k:g.rewardProps)own.insert(k);}
    for(size_t k=0;k<life_.props.size();++k){auto& prop=life_.props[k];if(!prop.active||own.count(int(k)))continue;
        const int g=GimmickAreaAt(prop.at,prop.radius+1);if(g<0)continue;const auto& area=gimmicks_[size_t(g)];
        V away{prop.at.x-area.at.x,0,prop.at.z-area.at.z};if(Length(away)<.01f)away={1,0,0};away=Unit(away);
        const float level=prop.at.y-Chrono::RoomGround(prop.at+V{0,1,0});bool moved=false;
        for(int tries=0;tries<24&&!moved;++tries){const float a=float((tries+1)/2)*.35f*(tries%2?1.f:-1.f),c=std::cos(a),s=std::sin(a);
            const V dir{away.x*c-away.z*s,0,away.x*s+away.z*c};const float reach=area.radius+prop.radius+3+float(tries/8)*6;
            V to=area.at+dir*reach;to.y=prop.at.y;const float ground=Chrono::RoomGround(to+V{0,1,0});
            if(std::abs(ground-Chrono::RoomGround(prop.at+V{0,1,0}))>1||!DevourWalkable(V{to.x,ground,to.z},prop.radius*.5f)||GimmickAreaAt(to,prop.radius+1)>=0)continue;
            to.y=ground+level;const V delta=to-prop.at;prop.at=to;moved=true;
            auto e=entt::entity(prop.visual);if(r.valid(e))if(auto* t=r.try_get<TransformComponent>(e)){t->translate.x+=delta.x;t->translate.y+=delta.y;t->translate.z+=delta.z;}}
        if(!moved){prop.active=false;auto e=entt::entity(prop.visual);if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;}}
}
// その点 (半径 margin の物) が入っている作動範囲 (同じ高さの輪)。無ければ -1
int ChronoSystem::GimmickAreaAt(V at,float margin)const{
    for(size_t i=0;i<gimmicks_.size();++i){const auto& g=gimmicks_[i];if(std::abs(at.y-g.at.y)>18)continue;
        if(Length(V{at.x-g.at.x,0,at.z-g.at.z})<g.radius+margin)return int(i);}
    return -1;
}
// ばらまく: 隠しておいた物が from から弧を描いて周りへ飛び、着地すると飲める
void ChronoSystem::BurstGimmick(entt::registry& r,GameContext& ctx,DevourGimmick& g,V from,float spread){
    int count=0;
    for(int k:g.rewardProps){if(k<0)continue;auto& prop=life_.props[size_t(k)];if(prop.active)continue;
        V to=from;for(int tries=0;tries<8;++tries){float a=GimmickSeed()*6.283f,d=spread*(.25f+.75f*std::sqrt(GimmickSeed()));to=from+V{std::cos(a)*d,0,std::sin(a)*d};if(DevourWalkable(to,prop.radius*.5f)&&GimmickAreaAt(to,prop.radius+1)<0)break;to=from;}
        to.y=Chrono::RoomGround(V{to.x,from.y+1,to.z})+std::min(1.2f,prop.height*.5f);
        auto e=entt::entity(prop.visual);DevourFly fly;fly.index=size_t(k);fly.from=from;fly.to=to;fly.last=prop.at;fly.time=.6f+.4f*GimmickSeed();fly.height=14+spread*.35f*GimmickSeed();fly.spin=(GimmickSeed()<.5f?-1.f:1.f)*(5+6*GimmickSeed());
        if(r.valid(e)){if(r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=true;if(auto* t=r.try_get<TransformComponent>(e))fly.rotate=Read(t->rotate);}
        prop.at=to;prop.active=true;prop.sealed=true;devourFlies_.push_back(fly);++count;}
    if(count){devourPops_.push_back({from+V{0,10,0},GimmickWord("gimmickBurst"),0,.9f,kPopHit,GimmickSeed()});PlayDevourSound(devourSounds_.shatter,.6f,.9f);LifeImpact(ctx,ImpactDevourTier,from,{0,1,0},.8f);}
}
// ---- ギミックの種類ごとの動き (Strategy Pattern。インターフェースは DevourGimmickBehaviors.h) ----
namespace {
// 仕掛けの見た目を動かす
void MoveGimmickPart(entt::registry& r,uint32_t visual,Vec at,Vec rotate){auto e=entt::entity(visual);if(!r.valid(e))return;if(auto* t=r.try_get<TransformComponent>(e)){t->translate=Write(at);t->rotate=Write(rotate);}}
}

// だるま落とし: 体が水鉄砲になり、上下に動く狙いに合わせて撃つ。胴を全部抜くと小物がばらまかれる
class ChronoSystem::ShootGimmick final : public ChronoSystem::GimmickBehavior {
public:
    const char* SignModel()const override{return "sign_shoot";}
    void Begin(ChronoSystem&,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody&,float)const override{
        ride.shape=Chrono::GimmickGunShape;ride.anchor=g.at;g.clock=0;g.pending=-1;}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        auto& ride=st.ride;st.next=st.Settle(ride.anchor);st.body.forward=ride.heading;
        // カメラをだるまの方へ回す (構図は CameraSubject で引いて広げる)
        {float want=std::atan2(ride.heading.x,ride.heading.z),turn=std::remainder(want-sys.yaw_,6.2831853f);sys.yaw_+=turn*(1-std::exp(-5*st.dt));}
        if(ride.release>=0){ride.release+=st.dt;st.end=ride.release>=(st.g.done?1.f:.15f);}}
    bool RideInput(ChronoSystem& sys,DevourGimmick& g,GimmickRide& ride,bool pressed,bool cancel)const override{
        auto& body=sys.life_.bodies[0];
        if(cancel&&ride.release<0){ride.release=0;return true;}
        // 水鉄砲を撃つ: 撃った水が届くまでは次を撃てない
        if(pressed&&ride.age>Chrono::GimmickFormTime&&g.pending<0&&ride.release<0&&!g.done){
            const float aim=Chrono::GimmickAim(g.clock);int left=Chrono::GimmickDarumaDiscs-g.hits;g.pending=Chrono::GimmickAimTarget(aim,left);g.state=1;g.wobble=std::max(g.wobble,0.f);
            const float scale=LifeScale(body.mass);V muzzle=body.at+ride.heading*(5.4f*scale)+V{0,3.2f*scale-1.25f,0};
            V stand=g.spots.empty()?g.at:g.spots[0];const float discH=4*1.2f*g.scale,standTop=2*1.2f*g.scale;
            float y=standTop+(float(g.pending)+.5f)*discH;if(g.pending>=left)y=standTop+float(left)*discH+5*1.2f*g.scale;
            V hit=stand+V{0,y,0}-ride.heading*(6*1.2f*g.scale);sys.gimmickJets_.push_back({muzzle,hit,0});
            sys.devourPops_.push_back({muzzle+V{0,2*scale,0},GimmickWord("gimmickShot"),0,.35f,kPopRise,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.pounce,.45f,1.7f);
            body.aspectVelocity-=3;}
        return pressed;}
    void TickWorld(ChronoSystem& sys,entt::registry& r,GameContext& ctx,size_t gi,float dt)const override{
        auto& g=sys.gimmicks_[gi];auto& ride=sys.gimmickRide_;
        const bool aiming=ride.index==int(gi)&&ride.age>Chrono::GimmickFormTime&&ride.release<0&&!g.done;if(aiming&&g.pending<0)g.clock+=dt;
        V stand=g.spots.empty()?g.at:g.spots[0];const float discH=4*1.2f*g.scale,standTop=2*1.2f*g.scale;int left=0;
        // 撃った水が届いた: 胴なら抜けて飛び、頭ならぐらつくだけ
        if(g.pending>=0&&sys.gimmickJets_.empty()){int target=g.pending;g.pending=-1;g.state=0;int n=Chrono::GimmickDarumaDiscs-g.hits;
            if(target<n){int seen=0;for(size_t i=0;i<std::min(g.parts.size(),size_t(Chrono::GimmickDarumaDiscs));++i){if(g.partAge[i]>=0)continue;if(seen++!=target)continue;
                    g.partAge[i]=0;++g.hits;sys.HoldGimmickProp(g,i,false);int k=g.partProps[i];
                    if(k>=0){auto& prop=sys.life_.props[size_t(k)];V face=g.face;V side{face.z,0,-face.x};V to=stand+face*(30+18*GimmickSeed())+side*((GimmickSeed()-.5f)*40);to.y=std::min(1.2f,prop.height*.5f);
                        if(!sys.DevourWalkable(to,3))to=stand+face*24;
                        DevourFly fly;fly.index=size_t(k);fly.from=g.partBase[i];fly.to=to;fly.last=prop.at;fly.time=.55f;fly.height=12;fly.spin=9;fly.rotate={1.5708f,GimmickSeed()*6.283f,0};
                        prop.at=to;prop.sealed=true;sys.devourFlies_.push_back(fly);}
                    sys.devourPops_.push_back({g.partBase[i]+V{0,discH,0},GimmickWord("gimmickHit"),0,.7f,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.hit,.6f,1.3f);sys.LifeImpact(ctx,ImpactDevourBounce,g.partBase[i]);break;}}
            else{g.wobble=.9f;sys.devourPops_.push_back({stand+V{0,standTop+float(n)*discH+16*1.2f*g.scale,0},GimmickWord("gimmickMiss"),0,.6f,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.bounce,.6f,.8f);}
            if(g.hits>=Chrono::GimmickDarumaDiscs&&!g.done){g.done=true;ride.release=0;
                sys.devourPops_.push_back({stand+V{0,standTop+18*1.2f*g.scale,0},GimmickWord("gimmickWin"),0,1,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.rankUp,.7f);
                sys.BurstGimmick(r,ctx,g,stand+V{0,standTop+6,0},70);}}
        // 残った胴は下へ詰め、頭 (最後の部品) はその上に乗る。頭は成功するまで飲めない
        const size_t discs=std::min(g.parts.size(),size_t(Chrono::GimmickDarumaDiscs));
        for(size_t i=0;i<discs;++i){if(g.partAge[i]>=0)continue;float want=stand.y+standTop+float(left++)*discH;g.partBase[i].y+=(want-g.partBase[i].y)*std::min(1.f,dt*16);MoveGimmickPart(r,g.parts[i],g.partBase[i],g.partRotate[i]);
            int k=g.partProps[i];if(k>=0)sys.life_.props[size_t(k)].at=g.partBase[i]+V{0,std::min(1.2f,discH*.5f),0};}
        if(g.parts.size()>discs){const size_t h=discs;float want=stand.y+standTop+float(left)*discH;g.partBase[h].y+=(want-g.partBase[h].y)*std::min(1.f,dt*14);g.wobble=std::max(0.f,g.wobble-dt);
            MoveGimmickPart(r,g.parts[h],g.partBase[h],g.partRotate[h]+V{0,0,std::sin(g.wobble*22)*.18f*g.wobble});
            int k=g.partProps[h];if(k>=0)sys.life_.props[size_t(k)].at=g.partBase[h]+V{0,1.2f,0};
            if(g.done&&g.partAge[h]<0){g.partAge[h]=0;sys.HoldGimmickProp(g,h,false);}}}
    void RidePrompts(std::vector<UI::Scale::Prompt>& out)const override{namespace S=UI::Scale;out.push_back({S::KeyMorph,"撃つ"});out.push_back({S::KeyAbsorb,"やめる"});}
    // 体とだるまの全体が入るように、両方の間を中心にして広く引く
    bool CameraSubject(const ChronoSystem&,const DevourGimmick& g,Chrono::LifeCameraSubject& subject)const override{
        const V stand=g.spots.empty()?g.at:g.spots[0];const float tall=(2+4*float(Chrono::GimmickDarumaDiscs)+8)*1.2f*g.scale;
        const V top=stand+V{0,tall*.5f,0},from=subject.center;
        subject.center=Lerp(from,top,.42f);subject.radius=std::max({subject.radius,Length(top-from)*.52f,tall*.62f});return true;}
    // 狙いの印: 上下に動く照準を、だるまの横に出す
    void DrawMarks(const ChronoSystem& sys,const DevourGimmick& g,UI::Canvas& ui,GameContext& ctx)const override{
        if(g.done||sys.gimmickRide_.age<Chrono::GimmickFormTime)return;
        V stand=g.spots.empty()?g.at:g.spots[0];const float discH=4*1.2f*g.scale,standTop=2*1.2f*g.scale;const int left=Chrono::GimmickDarumaDiscs-g.hits;
        const float aim=Chrono::GimmickAim(g.clock);const int target=Chrono::GimmickAimTarget(aim,left);
        float y=standTop+(float(target)+.5f)*discH;if(target>=left)y=standTop+float(left)*discH+5*1.2f*g.scale;
        float x,sy;if(!GimmickScreen(ctx,stand+V{0,y,0}-g.face*(7*1.2f*g.scale),x,sy))return;
        const Engine::Vector4 black{0,0,0,.85f},red{.95f,.22f,.18f,1},gold{1,.82f,.2f,1};const Engine::Vector4& color=target>=left?red:gold;
        const float r=26+3*std::sin(sys.life_.age*12);
        for(int pass=0;pass<2;++pass){const float w=pass?3.f:7.f;const auto& c=pass?color:black;
            ui.Line(x-r-12,sy,x-r*.45f,sy,w,c);ui.Line(x+r*.45f,sy,x+r+12,sy,w,c);ui.Line(x,sy-r-12,x,sy-r*.45f,w,c);ui.Line(x,sy+r*.45f,x,sy+r+12,w,c);}
        ui.Dot(x,sy,7,black);ui.Dot(x,sy,5,color);
        if(target>=left)UI::Scale::Notice(ui,"頭に当てるとぐらつくだけ。胴をねらおう",664,522);}
    // だるまの台のまわりと、輪から台までの間
    bool KeepsClear(const DevourGimmick& g,V at,float margin)const override{
        auto flat=[](V a){a.y=0;return a;};
        for(const auto& spot:g.spots)if(Length(flat(at-spot))<26+margin)return true;
        V a=flat(g.at),b=g.spots.empty()?a:flat(g.spots[0]);return Chrono::LifeSegmentDistance(flat(at),a,b)<10+margin;}
};

// 地面の上の道 (ドミノ・レーン・液体の道) から corridor 以内か
static bool GimmickNearFloorPath(const std::vector<Vec>& path,Vec at,float corridor,float margin){
    auto flat=[](Vec a){a.y=0;return a;};
    for(size_t i=1;i<path.size();++i)if(path[i].y<1&&path[i-1].y<1&&Chrono::LifeSegmentDistance(flat(at),flat(path[i-1]),flat(path[i]))<corridor+margin)return true;
    return false;
}

// ドミノ: 体がドミノの 1 枚になって倒れ、並んだドミノが次々に倒れる。最後のバケツが倒れて小物があふれる
class ChronoSystem::DominoGimmick final : public ChronoSystem::GimmickBehavior {
public:
    const char* SignModel()const override{return "sign_domino";}
    void Begin(ChronoSystem&,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody&,float)const override{
        ride.shape=Chrono::GimmickDominoShape;V start=g.path.size()>1?g.path[1]-g.path[0]:g.face;start.y=0;ride.heading=Unit(start);ride.anchor=g.path.front()-ride.heading*(5*1.2f*g.scale);ride.anchor.y=g.at.y;}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        // ドミノの 1 枚になって立ち、前へ倒れて最初のドミノを押す
        auto& ride=st.ride;auto& g=st.g;
        st.next=st.Settle(ride.anchor);const float tip=std::clamp((ride.age-.45f)/.32f,0.f,1.f);ride.pose.tip=1.3f*tip*tip;
        if(tip>=1&&!ride.fired){ride.fired=true;g.state=1;g.clock=0;sys.PlayDevourSound(sys.devourSounds_.hit,.4f,1.4f);
            sys.devourPops_.push_back({ride.anchor+ride.heading*8+V{0,8,0},GimmickWord("gimmickDomino"),0,.6f,kPopRise,GimmickSeed()});}
        st.end=ride.age>=1.05f;}
    void TickWorld(ChronoSystem& sys,entt::registry& r,GameContext& ctx,size_t gi,float dt)const override{
        auto& g=sys.gimmicks_[gi];if(g.state!=1)return;
        // ドミノが前から順に倒れる。倒れたドミノは飲める
        g.clock+=dt;const size_t n=g.parts.size();
        for(size_t i=0;i<n;++i){float a=std::clamp((g.clock-float(i)*Chrono::GimmickDominoStep)/Chrono::GimmickDominoFall,0.f,1.f);if(a<=0)continue;
            float rest=i+1==n?1.5708f:Chrono::GimmickDominoRest;MoveGimmickPart(r,g.parts[i],g.partBase[i],g.partRotate[i]+V{rest*a*a,0,0});
            if(a>=1&&g.partAge[i]<0){g.partAge[i]=0;sys.HoldGimmickProp(g,i,false);int k=g.partProps[i];V forward{std::sin(g.partRotate[i].y),0,std::cos(g.partRotate[i].y)};
                if(k>=0)sys.life_.props[size_t(k)].at=g.partBase[i]+forward*(5*1.2f*g.scale)+V{0,1,0};
                if(i%5==0){sys.PlayDevourSound(sys.devourSounds_.note,.25f,1+.04f*float(i));sys.devourPops_.push_back({g.partBase[i]+V{0,14*g.scale,0},GimmickWord("gimmickDomino"),0,.4f,kPopRise,GimmickSeed()});}}}
        // 最後のドミノが倒れたら、バケツが倒れて中の物があふれる
        float tip=std::clamp((g.clock-float(n)*Chrono::GimmickDominoStep-Chrono::GimmickDominoFall)/.45f,0.f,1.f);
        if(g.extra!=kNone)MoveGimmickPart(r,g.extra,g.extraBase,g.extraRotate+V{1.45f*tip*tip,0,0});
        if(tip>=.5f&&!g.done){g.done=true;V mouth=g.extraBase+g.face*12+V{0,6,0};sys.PlayDevourSound(sys.devourSounds_.hit,.7f,.8f);sys.BurstGimmick(r,ctx,g,mouth,55);}
        if(tip>=1)g.state=2;}
    // 並んだドミノの道と、最後のバケツのまわり
    bool KeepsClear(const DevourGimmick& g,V at,float margin)const override{
        if(GimmickNearFloorPath(g.path,at,12,margin))return true;
        V d=at-g.extraBase;d.y=0;return g.extra!=kNone&&Length(d)<22+margin;}
};

// ボウリング: 体が玉になってレーンを転がる (スティックで左右)。全部倒すとストライクで小物が降る
class ChronoSystem::BowlingGimmick final : public ChronoSystem::GimmickBehavior {
public:
    const char* SignModel()const override{return "sign_bowling";}
    void Begin(ChronoSystem&,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody&,float)const override{
        ride.shape=Chrono::GimmickBallShape;ride.anchor=g.path.front();ride.length=Chrono::GimmickPathLength(g.path);}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        auto& ride=st.ride;auto& g=st.g;auto& r=st.r;const float dt=st.dt,scale=st.scale;V& right=st.right;
        // 玉になってレーンを転がる。スティックで左右へ寄せられる
        if(ride.age<Chrono::GimmickFormTime+.1f){st.next=st.Settle(ride.anchor);return;}
        const float speed=Chrono::GimmickBallSpeed*std::min(1.f,(ride.age-Chrono::GimmickFormTime)/.5f),ballR=Chrono::GimmickBallRadius*scale;
        V dir;Chrono::GimmickPathPoint(g.path,ride.s,&dir);ride.heading=Unit(V{dir.x,0,dir.z});right={ride.heading.z,0,-ride.heading.x};
        const float half=std::max(0.f,11*1.2f*g.scale-ballR);ride.lateral=std::clamp(ride.lateral+Dot(st.wish,right)*40*dt,-half,half);
        ride.s+=speed*dt;ride.pose.spin+=speed*dt/std::max(ballR,.1f);
        V at=Chrono::GimmickPathPoint(g.path,ride.s)+right*ride.lateral;st.next={at.x,at.y+1.25f,at.z};
        // ピンに届いたら、当たったピンと、それに押されたピンを倒す
        if(g.state==0&&!g.partBase.empty()){float first=1e9f;for(const auto& pin:g.partBase)first=std::min(first,Dot(pin-g.path.front(),ride.heading));
            if(ride.s+ballR>=first){g.state=1;std::vector<V> local;std::vector<bool> down;const float pinR=2.6f*1.2f*g.scale,spacing=8*1.2f*g.scale;
                for(const auto& pin:g.partBase){V d=pin-g.path.front();local.push_back({Dot(d,ride.heading),0,Dot(d,right)});}
                down=Chrono::GimmickPinsDown(local,ride.lateral,ballR,pinR,spacing);int fallen=0;
                for(size_t i=0;i<down.size();++i){if(!down[i])continue;++fallen;sys.HoldGimmickProp(g,i,false);int k=g.partProps[i];if(k<0)continue;auto& prop=sys.life_.props[size_t(k)];
                    float side=local[i].z-ride.lateral;V away=Unit(ride.heading*(.7f+.4f*GimmickSeed())+right*((side>=0?1.f:-1.f)*(.3f+.6f*GimmickSeed())));V to=prop.at+away*(18+30*GimmickSeed());
                    if(!sys.DevourWalkable(to,2))to=prop.at+ride.heading*12;to.y=prop.at.y;
                    auto e=entt::entity(prop.visual);DevourFly fly;fly.index=size_t(k);fly.from=prop.at;fly.to=to;fly.last=prop.at;fly.time=.5f+.3f*GimmickSeed();fly.height=10+14*GimmickSeed();fly.spin=(GimmickSeed()<.5f?-1.f:1.f)*(8+6*GimmickSeed());
                    fly.rotate={1.5708f,GimmickSeed()*6.283f,0};if(r.valid(e))if(auto* t=r.try_get<TransformComponent>(e))t->rotate=Write(fly.rotate);
                    prop.at=to;prop.sealed=true;sys.devourFlies_.push_back(fly);}
                V deck=g.partBase.front();for(const auto& pin:g.partBase)deck=deck+pin*(1.f/float(g.partBase.size()));deck=deck-g.partBase.front()*(1.f/float(g.partBase.size()));
                if(fallen==int(g.partBase.size())){sys.devourPops_.push_back({deck+V{0,22,0},GimmickWord("gimmickStrike"),0,1,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.rankTop,.7f);sys.BurstGimmick(r,st.ctx,g,deck+V{0,6,0},60);}
                else if(fallen>0){sys.devourPops_.push_back({deck+V{0,16,0},GimmickWord("gimmickPins"),0,.4f+.6f*float(fallen)/float(g.partBase.size()),kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.hit,.6f,1.2f);}
                sys.LifeImpact(st.ctx,ImpactDevourTier,deck,{0,1,0},.4f+.06f*float(fallen));}}
        if(ride.s>=ride.length){st.end=true;g.done=true;for(size_t i=0;i<g.parts.size();++i)sys.HoldGimmickProp(g,i,false);}}
    void RidePrompts(std::vector<UI::Scale::Prompt>& out)const override{out.push_back({UI::Scale::KeyMove,"左右によける"});}
    bool KeepsClear(const DevourGimmick& g,V at,float margin)const override{return GimmickNearFloorPath(g.path,at,20,margin);}
};

// 絵の具: しずくになって絵の具へ飛び込み、体がその色になる。同じ色の物を飲むと多く増える
class ChronoSystem::PaintGimmick final : public ChronoSystem::GimmickBehavior {
public:
    const char* SignModel()const override{return "sign_paint";}
    void Begin(ChronoSystem&,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody& body,float floorY)const override{
        // いちばん近い色の場所へ飛び込む
        ride.shape=Chrono::GimmickDropShape;float nearest=1e9f;
        for(size_t k=0;k<g.spots.size();++k){V d=g.spots[k]-body.at;d.y=0;if(Length(d)<nearest){nearest=Length(d);g.pending=int(k);ride.anchor=g.spots[k];}}
        ride.anchor.y=floorY;}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        // しずくになって跳び上がり、絵の具へ飛び込む。落ちた所で体の色が変わる
        auto& ride=st.ride;auto& g=st.g;const float scale=st.scale;
        const float t=ride.age,hop=t<.3f?0:t<.62f?std::sin((t-.3f)/.32f*3.1415927f)*7*scale:0;st.next=st.Settle(ride.anchor)+V{0,hop,0};
        ride.pose.squash=t<.3f?.15f*LifeSmooth(t/.3f):t<.62f?-.1f:std::max(0.f,.45f*(1-(t-.62f)/.3f));
        if(t>=.62f&&!ride.fired){ride.fired=true;if(g.pending>=0&&size_t(g.pending)<g.colors.size())sys.devourPaint_=g.colors[size_t(g.pending)];sys.devourPaintFlash_=1;
            sys.devourPops_.push_back({ride.anchor+V{0,6*scale,0},GimmickWord("gimmickSplash"),0,.8f,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.splat,.7f);sys.LifeImpact(st.ctx,ImpactDevour,ride.anchor,{0,1,0},.8f);}
        st.end=t>=1;}
};

// 道をたどる: 液体 (蜂蜜・のり・ジュース) は薄い膜になってこぼれた跡をつたい、
// 紙飛行機は高い所から部屋の向こうまで滑空する (スティックで左右)。動きの大部分が同じなので 1 つのクラスで扱う
class ChronoSystem::PathGimmick final : public ChronoSystem::GimmickBehavior {
public:
    explicit PathGimmick(bool plane):plane_(plane){}
    const char* SignModel()const override{return plane_?"sign_plane":"sign_liquid";}
    void Begin(ChronoSystem&,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody&,float)const override{
        ride.shape=plane_?Chrono::GimmickPlaneShape:Chrono::GimmickSheetShape;ride.anchor=g.path.front();ride.length=Chrono::GimmickPathLength(g.path);ride.heading=Chrono::GimmickPathHeading(g.path,0);}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        // 膜 / 紙飛行機になって道をたどる。紙飛行機は左右へ少し寄せられる (入口の近くでは寄せられない)
        auto& ride=st.ride;auto& g=st.g;const float dt=st.dt,scale=st.scale;V& right=st.right;
        if(ride.age<Chrono::GimmickFormTime){st.next=st.Settle(ride.anchor);ride.heading=Chrono::GimmickPathHeading(g.path,0);return;}
        const bool plane=plane_;const float speed=plane?Chrono::GimmickPlaneSpeed:Chrono::GimmickFlowSpeed;
        ride.s=std::min(ride.length,ride.s+speed*dt);V dir;V at=Chrono::GimmickPathPoint(g.path,ride.s,&dir);
        V heading=Chrono::GimmickPathHeading(g.path,ride.s);ride.heading=Unit(Lerp(ride.heading,heading,std::min(1.f,dt*10)));right={ride.heading.z,0,-ride.heading.x};
        float flat=std::sqrt(dir.x*dir.x+dir.z*dir.z),pitch=std::atan2(dir.y,flat);ride.pose.pitch+=(pitch-ride.pose.pitch)*std::min(1.f,dt*(plane?6.f:14.f));
        if(plane){float room=Chrono::GimmickPlaneSteer;for(size_t k=1;k+1<g.path.size();++k){float sk=Chrono::GimmickPathLength({g.path.begin(),g.path.begin()+std::ptrdiff_t(k)+1});if(g.spots.size()>0&&Length(g.path[k]-g.spots[0])<1)room=std::min(room,Chrono::GimmickPlaneSteer*std::clamp(std::abs(ride.s-sk)/70.f,0.f,1.f));}
            float steer=Dot(st.wish,right);ride.lateral=std::clamp(ride.lateral+steer*30*dt,-room,room);ride.pose.roll+=(steer*-.6f-ride.pose.roll)*std::min(1.f,dt*5);
            at=at+right*ride.lateral;if(ride.s<ride.length-1&&std::fmod(ride.age,.5f)<dt)sys.devourPops_.push_back({at+V{0,4*scale,0}-ride.heading*6,GimmickWord("gimmickFly"),0,.3f,kPopRise,GimmickSeed()});}
        else if(std::fmod(ride.age,.6f)<dt)sys.devourPops_.push_back({at+V{0,4*scale,0},GimmickWord("gimmickFlow"),0,.3f,kPopRise,GimmickSeed()});
        st.next={at.x,at.y+1.25f,at.z};
        if(ride.s>=ride.length){st.end=true;at.y=Chrono::RoomGround(V{at.x,at.y+2,at.z});st.next={at.x,at.y+1.25f,at.z};
            if(plane){sys.devourPops_.push_back({at+V{0,5*scale,0},GimmickWord("gimmickSplash"),0,.6f,kPopHit,GimmickSeed()});sys.PlayDevourSound(sys.devourSounds_.splat,.6f);sys.LifeImpact(st.ctx,ImpactDevour,at,{0,1,0},.7f);}}}
    void RidePrompts(std::vector<UI::Scale::Prompt>& out)const override{if(plane_)out.push_back({UI::Scale::KeyMove,"左右によける"});}
    // 液体の道 (床の上の区間) と、紙飛行機の着地点のまわり
    bool KeepsClear(const DevourGimmick& g,V at,float margin)const override{
        if(!plane_)return GimmickNearFloorPath(g.path,at,10,margin);
        if(g.path.empty())return false;V d=at-g.path.back();d.y=0;return Length(d)<16+margin;}
private:
    bool plane_;
};

// 種類から動きを選ぶ (データを持たない共有の不変オブジェクト)。GimmickKind の順の表を引く
const ChronoSystem::GimmickBehavior& ChronoSystem::GimmickBehaviorFor(GimmickKind kind){
    static const ShootGimmick shoot;static const DominoGimmick domino;static const BowlingGimmick bowling;
    static const PaintGimmick paint;static const PathGimmick liquid(false),plane(true);
    static const GimmickBehavior* const table[]={&shoot,&domino,&bowling,&paint,&liquid,&plane};
    const size_t index=size_t(kind);return *table[index<std::size(table)?index:size_t(GimmickKind::Paint)];
}

// 輪の中で変形キーを押したか調べて、乗り始める。乗っている間の撃つ・やめるもここで受ける。押した入力を使ったら true
bool ChronoSystem::TickGimmickInput(entt::registry& r,Player& p,GameContext& ctx,bool pressed,bool cancel){
    (void)r;(void)p;auto& body=life_.bodies[0];gimmickNear_=-1;
    if(GimmickRiding()){auto& ride=gimmickRide_;auto& g=gimmicks_[size_t(ride.index)];
        return GimmickBehaviorFor(g.kind).RideInput(*this,g,ride,pressed,cancel);}
    if(finished_||body.action!=LifeAction::Idle||devourHop_.active||devourStun_>0)return false;
    const float floorY=body.at.y-1.25f;float best=1e9f;
    for(size_t i=0;i<gimmicks_.size();++i){const auto& g=gimmicks_[i];if(g.done)continue;V d=body.at-g.at;d.y=0;float distance=Length(d);
        if(distance<g.radius&&std::abs(floorY-g.at.y)<8&&distance<best){best=distance;gimmickNear_=int(i);}}
    if(gimmickNear_<0||!pressed)return false;
    auto& g=gimmicks_[size_t(gimmickNear_)];const auto& stage=CurrentDevourStage();
    if(DevourSizeCm(stage,body.mass)+1e-3f<g.minCm){life_.message=g.name+"は "+DevourSizeText(g.minCm)+" から遊べる";life_.messageAge=1.6f;PlayDevourSound(devourSounds_.bounce,.35f,1.3f);return true;}
    // 乗り始める: 体の形と、最初の位置・向きは種類ごとに決める
    auto& ride=gimmickRide_;ride={};ride.index=gimmickNear_;ride.anchor=body.at;ride.heading=g.face;
    GimmickBehaviorFor(g.kind).Begin(*this,g,ride,body,floorY);
    ride.from=body.at;++life_.revision;PlayDevourSound(devourSounds_.morph,.55f);LifeImpact(ctx,ImpactDevour,body.at,{0,1,0},.5f);
    return true;
}
// 乗っている間の体の位置: 種類ごとの決まった動き。終わったら乗るのをやめる
ChronoSystem::V ChronoSystem::GimmickStep(entt::registry& r,Player& p,GameContext& ctx,float dt,V wish){
    auto& body=life_.bodies[0];auto& ride=gimmickRide_;auto& g=gimmicks_[size_t(ride.index)];const float scale=LifeScale(body.mass);
    ride.previous=ride.pose;ride.age+=dt;const float form=LifeSmooth(ride.age/Chrono::GimmickFormTime);
    GimmickRideStep step{r,ctx,g,ride,body,dt,scale,form,wish,V{ride.heading.z,0,-ride.heading.x},body.at};
    GimmickBehaviorFor(g.kind).Step(*this,step);
    body.forward=ride.heading;
    if(step.end){EndGimmickRide(false);p.velocity={};body.aspectVelocity+=4;}
    return step.next;
}
// 乗るのをやめて、液体の体へ戻る
void ChronoSystem::EndGimmickRide(bool){gimmickRide_={};++life_.revision;}
// 毎フレームの仕掛けの動き: だるま落としの輪・ドミノの連鎖・バケツ・水鉄砲の水・絵の具の光
void ChronoSystem::TickDevourGimmicks(entt::registry& r,GameContext& ctx,float dt){
    devourPaintFlash_=std::max(0.f,devourPaintFlash_-dt*1.5f);gimmickMatchCool_=std::max(0.f,gimmickMatchCool_-dt);
    for(auto& jet:gimmickJets_)jet.age+=dt;
    gimmickJets_.erase(std::remove_if(gimmickJets_.begin(),gimmickJets_.end(),[](const GimmickJet& j){return j.age>Chrono::GimmickShotTime+.18f;}),gimmickJets_.end());
    for(size_t gi=0;gi<gimmicks_.size();++gi)GimmickBehaviorFor(gimmicks_[gi].kind).TickWorld(*this,r,ctx,gi,dt);
}
// 乗っている間の構図 (だるま落としは体とだるまの全体が入るように広く引く)
bool ChronoSystem::GimmickCameraSubject(Chrono::LifeCameraSubject& subject)const{
    if(!GimmickRiding())return false;const auto& g=gimmicks_[size_t(gimmickRide_.index)];
    return GimmickBehaviorFor(g.kind).CameraSubject(*this,g,subject);
}
// 作動範囲の膜: 輪の縁から垂直に立ち、上へ消えていく光の膜。範囲に入ると濃く光り、小さすぎるときは赤くなる。
// 終わったギミックと、乗っている間のそのギミック (と液体の道の反対側の端) は出さない
void ChronoSystem::DrawGimmickCurtains(GameContext& ctx){
    if(!gimmickCurtain_||life_.bodies.empty())return;
    const bool tooSmall=gimmickNear_>=0&&DevourSizeCm(CurrentDevourStage(),life_.bodies[0].mass)+1e-3f<gimmicks_[size_t(gimmickNear_)].minCm;
    const int riding=gimmickRide_.index,linked=riding>=0?gimmicks_[size_t(riding)].link:-1;
    for(size_t i=0;i<gimmicks_.size();++i){const auto& g=gimmicks_[i];if(g.done||int(i)==riding||int(i)==linked)continue;
        const bool inside=int(i)==gimmickNear_;const float height=Chrono::GimmickCurtainHeight(g.radius);
        Engine::Transform t;t.translate=EV(g.at+V{0,.05f,0});t.scale={g.radius,height,g.radius};
        Engine::Vector4 color=inside&&tooSmall?Engine::Vector4{.95f,.2f,.12f,.85f}:inside?Engine::Vector4{1,.66f,.1f,.95f}:Engine::Vector4{1,.52f,.05f,.8f};
        if(inside)color.w*=.85f+.15f*std::sin(life_.age*7);
        ctx.renderer->DrawMeshInstanced(gimmickCurtain_,white_,t,color,"GimmickCurtain");}
}
// 水鉄砲の水を、体と同じ液体の管として描く (撃った瞬間に伸びて、届くと細くなって消える)
void ChronoSystem::GimmickJets(std::vector<Engine::Renderer::FluidTendril>& arms)const{
    const float scale=life_.bodies.empty()?1:LifeScale(life_.bodies[0].mass);
    for(const auto& jet:gimmickJets_){float u=std::clamp(jet.age/Chrono::GimmickShotTime,0.f,1.f),fade=jet.age>Chrono::GimmickShotTime?1-(jet.age-Chrono::GimmickShotTime)/.18f:1;
        V tip=Lerp(jet.from,jet.to,u);const float radius=.55f*scale*std::max(.2f,fade);const int segments=6;V previous=jet.from;
        for(int k=1;k<=segments;++k){float t=float(k)/float(segments);V at=Lerp(jet.from,tip,t);at.y+=std::sin(t*3.1415927f)*1.5f*scale*u;arms.push_back({EV(previous),radius,EV(at),radius*(1-.2f*t)});previous=at;}}
}
// 絵の具の色と同じ色の物は多く増える
float ChronoSystem::GimmickFoodBonus(const Chrono::LifeCollectible& prop)const{
    if(devourPaint_.w<=0||prop.visual==kNone)return 1;auto found=cityVisuals_.find(prop.visual);if(found==cityVisuals_.end())return 1;const auto& c=found->second.color;
    return Chrono::GimmickSameColor(c.x,c.y,c.z,devourPaint_.x,devourPaint_.y,devourPaint_.z)?Chrono::GimmickPaintBonus:1.f;
}
// 乗っている間の操作 (下の操作の札)
void ChronoSystem::GimmickRidePrompts(std::vector<UI::Scale::Prompt>& out)const{
    if(!GimmickRiding())return;GimmickBehaviorFor(gimmicks_[size_t(gimmickRide_.index)].kind).RidePrompts(out);
}
// 作動範囲の中で押せる操作: 「変形キー ギミックの名前」。小さすぎるときは薄くして、何 cm から使えるかを添える
bool ChronoSystem::GimmickNearPrompt(UI::Scale::Prompt& out)const{
    if(GimmickRiding()||gimmickNear_<0)return false;const auto& g=gimmicks_[size_t(gimmickNear_)];
    const bool tooSmall=DevourSizeCm(CurrentDevourStage(),life_.bodies[0].mass)+1e-3f<g.minCm;
    out={UI::Scale::KeyMorph,g.name,"",tooSmall?DevourSizeText(g.minCm)+" から":"",tooSmall};return true;
}
// 乗っている間の画面の印 (だるま落としの照準など)
void ChronoSystem::DrawGimmickMarks(UI::Canvas& ui,GameContext& ctx){
    if(!GimmickRiding())return;const auto& g=gimmicks_[size_t(gimmickRide_.index)];
    GimmickBehaviorFor(g.kind).DrawMarks(*this,g,ui,ctx);
}
}
