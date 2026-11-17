// DevourHose.cpp
// 坂の町の消火栓 (ChronoSystem の一部)。消火栓の置物の横の輪で変形キーを押すと、スライムが消火栓の形になり、
// カメラが戦闘の消火栓と同じ肩越し (体の右後ろの近く) へ寄る。マウス・右スティックで自由に回し、
// 画面の真ん中の照準 (体の右斜め上に見える) が指す地面や建物へ水を撃ち続ける (12 秒。変形キーでやめる)。
// 当たる範囲の輪を地面に描く。水は GPU 流体の消火栓の水 (戦闘モードと同じ粒・速さ・重さ)。
//   平らな所: 水が当たった飲める物は、しぶきと一緒にこちらへ飛んできて飲める (屋根の上や川の向こうの物も取れる)
//   坂の下りへ撃つ: 当たった物は坂を流れ落ち、坂の下の段や塀の前に溜まる (あとで下りてまとめて飲む)
// ノズルからは細かい霧、着弾点からは大きな霧が出て、撃ち続けると霧に虹がかかる。
// 水は 1 つの口から扇に広げて撃ち (目の前に滝のような水の幕ができる)、低い弧が壁や段にふさがれるときは高い弧で越える。
// 物への効果は、GPU の水と同じ初速と重さの放物線を町の地面の上でたどって決める。
// GPU の水には、まわりの町の地面の高さの表 (建物とまだ飲めない物の高さも重ねる) を渡し、水が坂を流れ下り、
// 建物や段で止まるようにする (Renderer::SetHydrantTerrain・FluidSimCS の HydrantTerrain)。
// 数は Game/Chrono/DevourGimmickRules.h の Hose*、輪の場所は slope_town.json の rides.hydrant
#include "ChronoSystemInternal.h"
#include <chrono>

namespace Game {
using Chrono::GimmickKind;
namespace {
constexpr int kPopRise=0,kPopHit=1;
constexpr uint32_t kNone=0xffffffffu;
// 霧の数の上限・1 秒に出す霧 (ノズル・着弾点)
constexpr size_t kMistLimit=220;
constexpr float kMistNozzle=26,kMistImpact=34;
// 虹: 帯の内側の半径 (外側を 1 として)・濃くなる速さ・消える速さ・出始めるまでの時間
constexpr float kRainbowInner=.84f,kRainbowGrow=.45f,kRainbowFade=.7f;
float U(float low,float high){return low+(high-low)*GimmickSeed();}
// 放水の CPU の時間を足し込む (重さの確認用)
struct HoseTimer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~HoseTimer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}};
}

// ---- 消火栓: 肩越しのカメラの照準 (画面の真ん中) が指す所へ水を撃つ ----
class ChronoSystem::HoseGimmick final : public ChronoSystem::GimmickBehavior {
public:
    const char* SignModel()const override{return "sign_plane";}
    void Begin(ChronoSystem& sys,DevourGimmick& g,GimmickRide& ride,const Chrono::LifeBody& body,float)const override{
        ride.shape=Chrono::GimmickHoseShape;ride.heading={std::sin(sys.yaw_),0,std::cos(sys.yaw_)};ride.look=ride.heading;ride.anchor=g.at;
        // 狙いは、向いている方の少し先から始める (次のコマからは照準の先)
        const float scale=LifeScale(body.mass);ride.aim=g.at+ride.heading*(Chrono::HoseAimStart*std::sqrt(scale));ride.aim.y=sys.InkGround(ride.aim);
        sys.hose_.budget=0;sys.hose_.pullClock=0;
        sys.devourPops_.push_back({g.at+V{0,6,0},GimmickWord("hoseSpray"),0,.5f,kPopRise,GimmickSeed()});}
    void Step(ChronoSystem& sys,GimmickRideStep& st)const override{
        auto& ride=st.ride;const float dt=st.dt,scale=st.scale;
        // 体は輪の真ん中から動かない
        st.next=st.Settle(V{ride.anchor.x,sys.InkGround(ride.anchor),ride.anchor.z});
        // 狙い: カメラの真ん中の視線が最初に当たる地面か建物 (照準の先)。届く範囲 (水が 45 度で飛ぶ距離の少し内側) に収める
        {ride.aim=sys.HoseSight(st.ctx,ride.aim);
            V d=ride.aim-ride.anchor;d.y=0;const float distance=Length(d),least=Chrono::HoseAimLeast*std::sqrt(scale),most=Chrono::HoseAimMost;
            if(distance>most)d=d*(most/distance);else if(distance<least)d=distance>.01f?d*(least/distance):ride.heading*least;
            ride.aim=ride.anchor+d;ride.aim.y=sys.InkGround(ride.aim);
            // ノズルは狙いの印の方を向く
            ride.heading=Unit(d);}
        {V look=ride.look;if(Dot(look,ride.heading)<-.9f)look=look+V{-look.z,0,look.x}*.25f;ride.look=Unit(Lerp(look,ride.heading,1-std::exp(-10*dt)));}
        // やめる・時間切れ: 輪はしばらく休む (地面に落ちた水は消えるまで残る)
        if(ride.release>=0||ride.age>=Chrono::HoseTime){st.end=true;st.g.rest=Chrono::HoseRest;
            sys.devourPops_.push_back({st.body.at+V{0,5*st.scale,0},GimmickWord("gimmickSplash"),0,.4f,kPopHit,GimmickSeed()});return;}
        if(ride.age>=Chrono::GimmickFormTime)sys.HoseSpray(st.r,st.ctx,ride,dt);}
    bool RideInput(ChronoSystem&,DevourGimmick&,GimmickRide& ride,bool pressed,bool)const override{
        if(pressed&&ride.age>=Chrono::GimmickFormTime&&ride.release<0)ride.release=0;return pressed;}
    void RidePrompts(std::vector<UI::Scale::Prompt>& out)const override{out.push_back({UI::Scale::KeyLook,"ねらう"});out.push_back({UI::Scale::KeyMorph,"やめる"});}
    V Facing(const GimmickRide& ride)const override{return ride.look;}
    // 照準 (画面の真ん中。戦闘の消火栓と同じ形) と、水が落ちる所の印
    void DrawMarks(const ChronoSystem& sys,const DevourGimmick&,UI::Canvas& ui,GameContext& ctx)const override{
        {const Engine::Vector4 edge{.015f,.025f,.03f,.9f},color{.65f,1,1,1};
            for(auto bar:{UI::Rect{626,359,8,2},UI::Rect{646,359,8,2},UI::Rect{639,346,2,8},UI::Rect{639,366,2,8}}){ui.Fill({bar.x-1,bar.y-1,bar.w+2,bar.h+2},edge);ui.Fill(bar,color);}
            ui.Dot(640,360,2.8f,edge);ui.Dot(640,360,1.4f,color);}
        if(!sys.hose_.spraying)return;float x,y;if(!GimmickScreen(ctx,sys.hose_.impact,x,y))return;
        const Engine::Vector4 black{0,0,0,.8f},blue{.35f,.75f,1,1};const float r=20+2*std::sin(sys.life_.age*10);
        for(int pass=0;pass<2;++pass){const float w=pass?3.f:6.f;const auto& c=pass?blue:black;
            ui.Line(x-r-10,y,x-r*.45f,y,w,c);ui.Line(x+r*.45f,y,x+r+10,y,w,c);ui.Line(x,y-r-10,x,y-r*.45f,w,c);ui.Line(x,y+r*.45f,x,y+r+10,w,c);}
        ui.Dot(x,y,6,black);ui.Dot(x,y,4,blue);}
};
const ChronoSystem::GimmickBehavior& ChronoSystem::HoseBehavior(){static const HoseGimmick hose;return hose;}

// 乗っている間の放水: 狙い → 放物線 → GPU の水 → 水の床 → 当たった物 → 霧
void ChronoSystem::HoseSpray(entt::registry& r,GameContext& ctx,GimmickRide& ride,float dt){
    HoseTimer timer{hoseCpuMs_};
    auto& h=hose_;auto& body=life_.bodies[0];const float scale=LifeScale(body.mass);
    h.spraying=true;h.floorHold=6;
    auto ground=[&](V p){return InkGround(p);};
    auto gradient=[&](V p){constexpr float e=3;return V{(ground(p+V{e,0,0})-ground(p-V{e,0,0}))/(2*e),0,(ground(p+V{0,0,e})-ground(p-V{0,0,e}))/(2*e)};};
    // 建物の中か (飲まれた建物は通り抜ける)
    auto solid=[&](V p){for(const auto& s:townSolids_){if(s.prop>=0&&!life_.props[size_t(s.prop)].active)continue;if(p.x>s.x0&&p.x<s.x1&&p.z>s.z0&&p.z<s.z1&&p.y<s.top)return true;}return false;};
    // 狙い: 地面の狙いの印 (乗っている間にスティックで動かす)
    const V target=ride.aim,origin=LifeHydrantNozzle(body);
    // 放物線を町の地面の上でたどって、落ちる所を求める (途中の建物や段にも当たる)。points には道の点を入れる
    auto trace=[&](V velocity,std::vector<V>* points){V impact=origin;if(points){points->clear();points->push_back(origin);}
        for(int i=1;i<=360;++i){const V p=Chrono::LifeHydrantTrajectory(origin,velocity,float(i)/60);impact=p;
            if(p.y<=ground(p)||solid(p)){impact.y=std::max(p.y,ground(p));break;}
            if(points&&i%8==0)points->push_back(p);}
        if(points)points->push_back(impact);return impact;};
    // 弧を選ぶ: 低い弧が途中の壁や段にふさがれるなら、高い弧で越える (どちらも届かなければ、狙いの向きへ一番遠くまで飛ぶ 45 度)
    V velocity{};h.lob=false;
    {const float close=3+2*scale;auto missed=[&](V v){V e=trace(v,nullptr)-target;e.y=0;return Length(e);};
        V low{},high{};float t0=0,t1=0;const bool reachLow=Chrono::HoseLaunch(origin,target,false,low,t0),reachHigh=Chrono::HoseLaunch(origin,target,true,high,t1);
        if(reachLow&&missed(low)<=close)velocity=low;
        else if(reachHigh&&(!reachLow||missed(high)<missed(low))){velocity=high;h.lob=true;}
        else if(reachLow)velocity=low;
        else{V flat{target.x-origin.x,0,target.z-origin.z};flat=Length(flat)>.01f?Unit(flat):body.forward;velocity=(flat+V{0,1,0})*(Chrono::HydrantJetSpeed*.70710678f);}}
    const V impact=trace(velocity,&h.arc);h.nozzle=origin;h.impact=impact;
    {V flow{velocity.x,0,velocity.z};h.flow=Length(flow)>.01f?Unit(flow):body.forward;}
    // 扇: 1 つの口から水平に広げた筋。外の筋ほど少し遅く、手前に落ちる (目の前に滝のような水の幕ができる)
    auto lane=[&](float u){const float a=u*Chrono::HoseFanHalf,c=std::cos(a),sn=std::sin(a);const float slow=1-Chrono::HoseFanSlow*u*u;
        return V{(velocity.x*c+velocity.z*sn)*slow,velocity.y*(1-.5f*Chrono::HoseFanSlow*u*u),(-velocity.x*sn+velocity.z*c)*slow};};
    h.lanes.clear();for(float u:{-1.f,-.5f,0.f,.5f,1.f})h.lanes.push_back(u==0?impact:trace(lane(u),nullptr));
    trace(lane(-1),&h.arcLeft);trace(lane(1),&h.arcRight);
    // GPU の水が流れる町の地面 (高さの表)。体のまわりから狙いまでを覆う。
    // 水を出す前に作る (表があると、水は水専用の流体の大きな範囲に入り、箱庭のデモと同じ水で描かれる)
    h.terrainClock-=dt;BuildHoseTerrain((ride.anchor+target)*.5f,false);
    // GPU 流体の水を出す (筋ごとに分けて同じ口から)。色は箱庭のデモの水と同じ
    {const Engine::Vector4 water{.18f,.62f,.9f,.94f};h.budget+=Chrono::HoseRate*dt;const int count=int(h.budget);h.budget-=float(count);
        for(int k=0;k<Chrono::HoseFanLanes&&count>0;++k){const int share=count/Chrono::HoseFanLanes+(k<count%Chrono::HoseFanLanes?1:0);if(!share)continue;
            const float u=Chrono::HoseFanLanes>1?float(k)/float(Chrono::HoseFanLanes-1)*2-1:0;
            ctx.renderer->EmitGPUFluid(EV(origin),EV(lane(u)*.2f),water,share,Chrono::HydrantAttackWater,false,std::clamp(.4f*scale,.45f,1.2f));}}
    // 当たった物: 坂の下りへ撃っていれば押し流し、それ以外は引き寄せる
    const V slope=gradient(impact);V downhill{-slope.x,0,-slope.z};const float steep=Length(downhill);if(steep>1e-4f)downhill=downhill*(1/steep);
    const bool wash=steep>Chrono::HoseWashSlope&&Dot(downhill,h.flow)>Chrono::HoseWashAlign&&ground(impact)<=ground(ride.anchor)+.5f;h.washing=wash;
    const float reach=Chrono::HoseSplash*std::sqrt(scale),cover=DevourBodyRadius(body.mass)*DevourCoverRatio;
    h.pullClock-=dt;int pulled=0,washed=0;
    for(size_t i=0;i<life_.props.size();++i){auto& prop=life_.props[i];
        if(!prop.active||prop.sealed||prop.orb||prop.morphItem!=LifeMorphItem::None||prop.visual==kNone)continue;
        // 当たる範囲: 着弾点のまわり。押し流すときは、水が流れていく坂の下りへ長く伸ばす
        // (扇の幅: 扇の落ちる所のうち一番近い所から測る)
        V d=prop.at-impact;if(std::abs(d.y)>reach*1.5f)continue;
        {float best=1e9f;for(const auto& at:h.lanes){V e=prop.at-at;e.y=0;if(Length(e)<best){best=Length(e);d=e;}}}d.y=0;
        if(wash){const float along=Dot(d,downhill);if(along>0)d=d-downhill*std::min(along,reach*1.5f);}
        if(Length(d)>reach+prop.radius*.5f)continue;
        bool mover=false;for(const auto& m:devourMovers_)if(m.visual==prop.visual&&m.speed!=0){mover=true;break;}
        if(wash&&!mover&&prop.radius<=cover*Chrono::HoseWashSize&&h.washes.size()<size_t(Chrono::HoseWashLimit)){
            bool already=false;for(const auto& w:h.washes)if(w.prop==int(i)){already=true;break;}
            if(!already){HoseWash w;w.prop=int(i);w.velocity=(downhill*.7f+h.flow*.3f)*(Chrono::HoseWashMax*.6f);h.washes.push_back(w);++washed;}
            continue;}
        if(!wash&&DevourCanCover(body.mass,prop.radius)&&h.pullClock<=0&&pulled<Chrono::HosePullCount){
            const float distance=LifeDistance(prop.at,body.at);DevourPull(r,i,std::max(.25f,distance/(Chrono::HosePullSpeed*std::sqrt(scale))),true);++pulled;}}
    if(pulled)h.pullClock=Chrono::HosePullEvery;
    // 描き文字と音 (間を空ける)
    h.talk-=dt;h.sound-=dt;
    if(h.talk<=0&&(pulled||washed)){h.talk=.6f;devourPops_.push_back({impact+V{0,4*scale,0},GimmickWord(washed?"hoseWash":"hoseSpray"),0,.45f,kPopRise,GimmickSeed()});}
    if(h.sound<=0){h.sound=.22f;PlayDevourSound(devourSounds_.splat,.12f+.1f*GimmickSeed(),1.25f+.2f*GimmickSeed());}
    // 霧: ノズルのまわりに細かく、着弾点にふわっと大きく
    auto add=[&](V at,V v,float size,float life){if(h.mist.size()>=kMistLimit)return;HoseMist m;m.at=at;m.velocity=v;m.size=size;m.life=life;m.seed=GimmickSeed();h.mist.push_back(m);};
    for(float n=kMistNozzle*dt+GimmickSeed();n>=1;n-=1)add(origin+velocity*(U(.02f,.12f)),velocity*U(.05f,.12f)+V{U(-2,2),U(0,2),U(-2,2)},U(.6f,1.1f)*std::sqrt(scale),U(.7f,1.2f));
    for(float n=kMistImpact*dt+GimmickSeed();n>=1;n-=1){const float a=U(0,6.2831853f);
        add(impact+V{std::cos(a)*U(0,3),U(.5f,2.f),std::sin(a)*U(0,3)}*std::sqrt(scale),V{std::cos(a)*U(1,4),U(2,6),std::sin(a)*U(1,4)},U(1.6f,2.8f)*std::sqrt(scale),U(1.1f,1.9f));}
}

// 毎フレーム: 流れ落ちる物・霧・虹・水の床
void ChronoSystem::TickHose(entt::registry& r,float dt){
    HoseTimer timer{hoseCpuMs_};
    auto& h=hose_;
    h.rainbow=h.spraying?std::min(1.f,h.rainbow+dt*kRainbowGrow):std::max(0.f,h.rainbow-dt*kRainbowFade);
    // 地面に落ちた水が消えるまで町の地面の表を残す (建物の高さなどが変わるので時々作り直す)。消えたら平らな床へ戻す
    auto* render=Engine::Renderer::GetInstance();
    if(!h.spraying&&h.floorHold>0){h.floorHold-=dt;h.terrainClock-=dt;
        if(h.floorHold<=0){render->ClearHydrantTerrain();render->EnableHydrantWaterReadback(false);h.arc.clear();h.arcLeft.clear();h.arcRight.clear();h.water.clear();}
        else BuildHoseTerrain(h.terrainCenter,false);}
    // 水がある所: GPU から読み戻した消火栓の水の粒を囲む箱 (h.water に最小と最大の角)。流体を描く細かい升をこの箱に合わせる。
    // (前は 24 の升ごとに半径 20 の球を入れていて、描く範囲が水よりずっと広くなり、升が粗くなっていた)
    // 水を画面の上の面で描くとき (Renderer::HydrantScreenWater) は体積の範囲に入れないので、読み戻さない
    if(h.floorHold>0&&render->HydrantScreenWater()){render->EnableHydrantWaterReadback(false);h.water.clear();}
    else if(h.floorHold>0){render->EnableHydrantWaterReadback(true);h.water.clear();V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& p:render->GetHydrantWaterSnapshot().particles){if(p.at.y<-500)continue;
            lo={std::min(lo.x,p.at.x),std::min(lo.y,p.at.y),std::min(lo.z,p.at.z)};hi={std::max(hi.x,p.at.x),std::max(hi.y,p.at.y),std::max(hi.z,p.at.z)};}
        if(lo.x<=hi.x){h.water.push_back(lo);h.water.push_back(hi);}}
    h.spraying=false;
    // 霧は上へ漂いながら遅くなる
    for(auto& m:h.mist){m.age+=dt;m.at=m.at+m.velocity*dt;m.velocity=m.velocity*(1-1.6f*dt)+V{0,1.2f*dt,0};}
    h.mist.erase(std::remove_if(h.mist.begin(),h.mist.end(),[](const HoseMist& m){return m.age>=m.life;}),h.mist.end());
    // 押し流している物: 坂を下りへ速くなり、平らな所で止まる。登れない段・町の端・飲めない物で止まる
    auto ground=[&](V p){return InkGround(p);};
    for(auto& w:h.washes){
        if(w.prop<0||size_t(w.prop)>=life_.props.size()){w.prop=-1;continue;}auto& prop=life_.props[size_t(w.prop)];
        if(!prop.active||prop.sealed){w.prop=-1;continue;}
        w.age+=dt;constexpr float e=3;const V slope{(ground(prop.at+V{e,0,0})-ground(prop.at-V{e,0,0}))/(2*e),0,(ground(prop.at+V{0,0,e})-ground(prop.at-V{0,0,e}))/(2*e)};
        w.velocity=w.velocity*(1-1.4f*dt)-slope*(Chrono::HoseWashAccel*dt);
        float speed=Length(w.velocity);if(speed>Chrono::HoseWashMax){w.velocity=w.velocity*(Chrono::HoseWashMax/speed);speed=Chrono::HoseWashMax;}
        if(speed<Chrono::HoseWashStop&&w.age>.6f){w.prop=-1;continue;}
        V to=prop.at+w.velocity*dt,normal;to.y=prop.at.y;
        const float rise=ground(to)-ground(prop.at);
        if(rise>std::max(.3f,prop.radius*.6f)||!DevourWalkable(to,prop.radius*.5f)||TownRideHit(prop.at,to,std::max(.5f,prop.radius*.7f),normal)){w.velocity={};w.prop=-1;continue;}
        V delta=to-prop.at;delta.y=rise;prop.at=prop.at+delta;
        auto e2=entt::entity(prop.visual);if(r.valid(e2))if(auto* t=r.try_get<TransformComponent>(e2)){t->translate.x+=delta.x;t->translate.y+=delta.y;t->translate.z+=delta.z;
            // 転がって見えるように、進む向きに合わせて少し回す
            w.roll+=speed*dt/std::max(prop.radius,1.f);t->rotate.x=std::sin(w.roll)*.35f;}}
    h.washes.erase(std::remove_if(h.washes.begin(),h.washes.end(),[](const HoseWash& w){return w.prop<0;}),h.washes.end());
}
// 照準の先: カメラの真ん中の視線を、町の地面 (InkGround) と建物の箱 (townSolids_) に当たるまで進める。
// 何にも当たらなければ、届く一番遠くの真下の地面。最初に当たった所は二分探索で詰める
Chrono::Vec ChronoSystem::HoseSight(GameContext& ctx,V fallback){
    if(!ctx.camera)return fallback;
    auto inverse=DirectX::XMMatrixInverse(nullptr,ctx.camera->View());DirectX::XMFLOAT3 eye{},axis{};
    DirectX::XMStoreFloat3(&eye,DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(),inverse));
    DirectX::XMStoreFloat3(&axis,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0,0,1,0),inverse));
    const V from=Read(eye),ray=Unit(Read(axis));
    auto blocked=[&](V p){if(p.y<=InkGround(p))return true;
        for(const auto& s:townSolids_){if(s.prop>=0&&!life_.props[size_t(s.prop)].active)continue;if(p.x>s.x0&&p.x<s.x1&&p.z>s.z0&&p.z<s.z1&&p.y<s.top)return true;}return false;};
    const float range=Chrono::HoseAimMost*1.5f,step=1.5f;float previous=0;
    for(float t=step;t<=range;t+=step){if(!blocked(from+ray*t)){previous=t;continue;}
        float lo=previous,hi=t;for(int i=0;i<10;++i){const float mid=(lo+hi)*.5f;if(blocked(from+ray*mid))hi=mid;else lo=mid;}
        V hit=from+ray*hi;hit.y=std::max(hit.y,InkGround(hit));return hit;}
    V beyond=from+ray*range;beyond.y=InkGround(beyond);return beyond;
}

// GPU の水が流れる町の地面の高さの表を作る: 地面の高さに、建物と、まだ飲めない物 (足元の丸) の高さを重ねる。
// 中心が大きく動いたときと、決まった間隔で作り直す (force ならすぐ)
void ChronoSystem::BuildHoseTerrain(V center,bool force){
    auto& h=hose_;constexpr int n=Chrono::HoseTerrainCells;constexpr float cell=Chrono::HoseTerrainCell;
    V snapped{std::round(center.x/(cell*8))*cell*8,0,std::round(center.z/(cell*8))*cell*8};V moved=snapped-h.terrainCenter;moved.y=0;
    if(!force&&h.terrainClock>0&&Length(moved)<Chrono::HoseTerrainShift*cell)return;
    h.terrainClock=Chrono::HoseTerrainEvery;h.terrainCenter=snapped;
    const float x0=snapped.x-n*cell*.5f,z0=snapped.z-n*cell*.5f;std::vector<float> heights(size_t(n)*n);
    for(int z=0;z<n;++z)for(int x=0;x<n;++x)heights[size_t(z)*n+x]=InkGround(V{x0+(float(x)+.5f)*cell,0,z0+(float(z)+.5f)*cell});
    auto raise=[&](float ax,float az,float bx,float bz,float top){const int i0=std::max(0,int((ax-x0)/cell)),i1=std::min(n-1,int((bx-x0)/cell)),j0=std::max(0,int((az-z0)/cell)),j1=std::min(n-1,int((bz-z0)/cell));
        for(int j=j0;j<=j1;++j)for(int i=i0;i<=i1;++i){auto& v=heights[size_t(j)*n+i];v=std::max(v,top);}};
    for(const auto& sd:townSolids_){if(sd.prop>=0&&!life_.props[size_t(sd.prop)].active)continue;raise(sd.x0,sd.z0,sd.x1,sd.z1,sd.top);}
    const float mass=life_.bodies[0].mass;
    for(const auto& prop:life_.props){if(!prop.active||prop.foot<=0||DevourCanCover(mass,prop.radius))continue;
        const float top=prop.at.y-std::min(1.2f,prop.height*.5f)+prop.height;
        if(prop.feet.empty()){raise(prop.at.x-prop.foot,prop.at.z-prop.foot,prop.at.x+prop.foot,prop.at.z+prop.foot,top);continue;}
        for(const auto& f:prop.feet)raise(prop.at.x+f.x-f.y,prop.at.z+f.z-f.y,prop.at.x+f.x+f.y,prop.at.z+f.z+f.y,top);}
    Engine::Renderer::GetInstance()->SetHydrantTerrain(x0,z0,cell,uint32_t(n),std::move(heights));
}
void ChronoSystem::ResetHose(){
    auto* render=Engine::Renderer::GetInstance();if(render){render->ClearHydrantTerrain();render->EnableHydrantWaterReadback(false);}
    const uint32_t mesh=rainbowMesh_;hose_={};rainbowMesh_=mesh;
}

// 霧と虹を描く
void ChronoSystem::DrawHose(GameContext& ctx){
    auto& h=hose_;
    // 乗っている間: 地面の狙いの輪 (当たる範囲) と、水が飛ぶ道。押し流すときは青で、坂の下りへ矢印を出す。引き寄せるときは金色
    if(GimmickRidingKind()==int(GimmickKind::Hose)){
        const float scale=LifeScale(life_.bodies[0].mass),reach=Chrono::HoseSplash*std::sqrt(scale),pulse=1+.06f*std::sin(life_.age*6);
        const V center=h.spraying?h.impact:gimmickRide_.aim;const Engine::Vector4 color=h.spraying&&h.washing?Engine::Vector4{.3f,.7f,1,1}:Engine::Vector4{1,.8f,.2f,1};
        auto onGround=[&](V p){p.y=InkGround(p)+.4f;return p;};
        // (上空からは線では細くて見えないので、輪と同じ光の膜を低く立てる)
        if(gimmickCurtain_){Engine::Transform t;t.translate=EV(onGround(center)-V{0,.4f,0});t.scale={reach*pulse,3*std::sqrt(scale),reach*pulse};ctx.renderer->DrawMeshInstanced(gimmickCurtain_,white_,t,{color.x,color.y,color.z,.9f},"GimmickCurtain");}
        constexpr int segments=48;{V previous=onGround(center+V{reach*pulse,0,0});
            for(int i=1;i<=segments;++i){const float a=6.2831853f*float(i)/segments;const V p=onGround(center+V{std::cos(a),0,std::sin(a)}*(reach*pulse));ctx.renderer->DrawLine3D(EV(previous),EV(p),color);previous=p;}}
        // (狙いは画面の真ん中の照準で示す。水の飛ぶ道の線は、近くから見ると水の上に重なって見づらいので描かない)
        // 押し流す向き: 着弾点から坂の下りへ矢印
        if(h.spraying&&h.washing){constexpr float e=3;const V c=h.impact;V down{-(InkGround(c+V{e,0,0})-InkGround(c-V{e,0,0})),0,-(InkGround(c+V{0,0,e})-InkGround(c-V{0,0,e}))};
            if(Length(down)>1e-4f){down=Unit(down);const V side{down.z,0,-down.x};const V from=onGround(c+down*(reach*.6f)),tip=onGround(c+down*(reach*1.9f));
                ctx.renderer->DrawLine3D(EV(from),EV(tip),color);ctx.renderer->DrawLine3D(EV(tip),EV(onGround(tip-down*(reach*.35f)+side*(reach*.25f))),color);ctx.renderer->DrawLine3D(EV(tip),EV(onGround(tip-down*(reach*.35f)-side*(reach*.25f))),color);}}}
    if(h.mist.empty()&&h.rainbow<=0)return;
    const V eye=Read(ctx.camera->Position());
    // 霧: カメラへ向けた四角に、ふちのやわらかい丸 (SplashMist)。広がりながら薄れる
    if(growQuad_)for(const auto& m:h.mist){const float u=std::clamp(m.age/m.life,0.f,1.f),size=m.size*(1+1.2f*u);
        const V to=eye-m.at;const float len=Length(to);if(len<.01f)continue;const V n=to*(1/len);
        Engine::Transform t;t.translate=EV(m.at);t.rotate={std::atan2(std::sqrt(n.x*n.x+n.z*n.z),n.y),std::atan2(n.x,n.z),0};t.scale={size,size,size};
        ctx.renderer->DrawMeshInstanced(growQuad_,white_,t,{1,1,m.seed,.24f*std::sin(3.14159f*std::sqrt(u))},"SplashMist");}
    // 虹: ノズルと着弾点の間の霧にかかる半円の帯 (Rainbow)。カメラへ向けて立て、少し地面に沈めて弧だけを見せる
    if(h.rainbow<=0||h.arc.empty())return;
    if(!rainbowMesh_){std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;constexpr int segments=64;
        for(int i=0;i<=segments;++i){const float u=float(i)/segments,a=3.1415927f*u,c=std::cos(a),sn=std::sin(a);
            for(int k=0;k<2;++k){const float radius=k?1.f:kRainbowInner;vertices.push_back({{c*radius,sn*radius,0,1},{u,float(k)},{0,0,1}});}}
        for(uint32_t i=0;i<uint32_t(segments);++i){const uint32_t a=i*2;indices.insert(indices.end(),{a,a+1,a+3,a,a+3,a+2,a,a+3,a+1,a,a+2,a+3});}
        rainbowMesh_=ctx.renderer->CreateDynamicMesh(vertices,indices);}
    V span=h.impact-h.nozzle;span.y=0;const float length=Length(span),scale=LifeScale(life_.bodies[0].mass);
    // 水が遠くまで飛んでいるときだけ (足元へ撃っているときは体を囲む輪に見えるので出さない)
    const float reach=std::clamp((length-8*std::sqrt(scale))/(10*std::sqrt(scale)),0.f,1.f);if(reach<=0)return;
    const float radius=std::clamp(length*.5f,6*std::sqrt(scale),70.f);
    V center=h.nozzle+span*.55f;center.y=InkGround(center)-radius*.2f;
    V toward=eye-center;toward.y=0;if(Length(toward)<.01f)return;toward=Unit(toward);
    Engine::Transform t;t.translate=EV(center);t.rotate={0,std::atan2(toward.x,toward.z),0};t.scale={radius,radius,radius};
    ctx.renderer->DrawMeshInstanced(rainbowMesh_,white_,t,{1,1,1,.38f*LifeSmooth(h.rainbow)*reach},"Rainbow");
}
}
