// DevourSpill.cpp
// 部屋のこぼれた液体 (蜂蜜・のり・ジュース) の粒子の液体 (ChronoSystem の一部)。
// 倒れた入れ物の口から粒を出し、家具の上を縁まで流れ、縁で少し溜まってから壁をつたって落ち、床のたまりに広がって溶け込む。
// 蜂蜜とのりは粘く (ゆっくり落ち、前後の粒へ速さが引き寄せられて 1 本の糸につながる)、ジュースはサラサラ (速く落ちて床で跳ねる)。
// 遠い (カメラから kSpillSeeDistance より遠い) こぼれは、動かしも描きもしない。規則は Game/Chrono/DevourSpillRules.h。
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
// この距離より遠いこぼれは動かさず、描かない。1 つのこぼれの粒の上限
constexpr float kSpillSeeDistance=230;
constexpr size_t kSpillMaxDrops=170;
// 作った直後に先に流しておく時間 (始めから縁から床まで流れている)
constexpr float kSpillWarmTime=24;
enum SpillPhase {SpillTop=0,SpillHang=1,SpillWall=2,SpillFloor=3,SpillSplash=4};
float SpillNoise(uint32_t& seed){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;}
}
// こぼれを 1 つ足す: 入れ物の口 (source)・家具の縁 (edge)・壁の足元 (foot)・床のたまり (pool)
void ChronoSystem::AddDevourSpill(Chrono::SpillKind kind,Engine::Vector4 color,V source,V edge,V foot,V pool){
    DevourSpillFlow s;s.kind=kind;s.color=color;s.source=source;s.edge=edge;s.foot=foot;s.pool=pool;
    s.out=Unit(V{pool.x-foot.x,0,pool.z-foot.z});s.side={-s.out.z,0,s.out.x};
    s.low={std::min({source.x,edge.x,pool.x}),0,std::min({source.z,edge.z,pool.z})};s.high={std::max({source.x,edge.x,pool.x}),edge.y,std::max({source.z,edge.z,pool.z})};
    s.seed=uint32_t(devourSpills_.size()*7919+13);devourSpills_.push_back(std::move(s));
}
// 粒を進める。見えないほど遠いこぼれは止めておく (戻ってきたら続きから流れる)
void ChronoSystem::TickDevourSpills(GameContext& ctx,float frameDt){
    if(devourSpills_.empty()||frameDt<=0)return;const V eye=Read(ctx.camera->Position());
    for(auto& s:devourSpills_){
        const V nearest{std::clamp(eye.x,s.low.x,s.high.x),std::clamp(eye.y,s.low.y,s.high.y),std::clamp(eye.z,s.low.z,s.high.z)};
        s.visible=Length(eye-nearest)<kSpillSeeDistance;const bool warm=!s.warmed;s.warmed=true;if(!s.visible&&!warm)continue;
        const int steps=warm?int(kSpillWarmTime/.05f):1;
        for(int stepIndex=0;stepIndex<steps;++stepIndex){const float dt=warm?.05f:frameDt;
        const auto& k=Chrono::SpillParamsOf(s.kind);const float top=s.edge.y;
        V along=s.edge-s.source;along.y=0;const float run=Length(along);along=run>.01f?along*(1/run):s.out*-1.f;
        // 入れ物の口から粒を出す (上限まで)
        s.emit+=k.rate*dt;
        while(s.emit>=1){s.emit-=1;if(s.drops.size()>=kSpillMaxDrops)continue;DevourSpillFlow::Drop d;d.serial=++s.serial;
            d.lane=(SpillNoise(s.seed)-.5f)*k.lane;d.radius=k.radius*(.8f+.4f*SpillNoise(s.seed));
            d.at=s.source+s.side*d.lane+V{0,d.radius*.5f,0};d.speed=k.topSpeed*(.85f+.3f*SpillNoise(s.seed));s.drops.push_back(d);}
        // 壁を落ちる粒の速さ (粘性で前後の粒へ寄せるため、先に全部を集める)
        std::vector<float> falling(s.drops.size(),-1);for(size_t i=0;i<s.drops.size();++i)if(s.drops[i].phase==SpillWall)falling[i]=s.drops[i].speed;
        for(size_t i=0;i<s.drops.size();++i){auto& d=s.drops[i];d.age+=dt;
            switch(d.phase){
            case SpillTop:{// 家具の上を縁まで流れる
                V flat=d.at-s.source;flat.y=0;const float done=Dot(flat,along);d.at=d.at+along*(d.speed*dt);d.at.y=top+d.radius*.5f;
                if(done+d.speed*dt>=run){d.phase=SpillHang;d.age=0;d.at=s.edge+s.side*d.lane+s.out*(d.radius*.4f)+V{0,d.radius*.3f,0};}break;}
            case SpillHang:// 縁で少し溜まってふくらみ、重さで垂れ始める
                d.at.y-=d.radius*.25f*dt;if(d.age>=k.hang*(.7f+.6f*std::fmod(float(d.serial)*.618034f,1.f))){d.phase=SpillWall;d.speed=0;d.age=0;}break;
            case SpillWall:{// 壁をつたって落ちる。重力で速くなり、終端速度で頭打ち。前後の粒の速さへ寄せる (粘性)
                float speed=Chrono::SpillWallSpeed(k,d.speed,dt);float sum=0;int n=0;
                for(int o:{-1,1}){const int j=int(i)+o;if(j<0||j>=int(falling.size())||falling[size_t(j)]<0)continue;
                    if(k.strand>0&&std::abs(s.drops[size_t(j)].at.y-d.at.y)>k.strand*1.6f)continue;sum+=falling[size_t(j)];++n;}
                if(n)speed=Chrono::SpillViscous(k,speed,sum/float(n),dt);d.speed=speed;
                d.at.y-=speed*dt;const float wobble=std::sin(d.age*3+float(d.serial))*.25f;
                const V wall=s.foot+s.side*(d.lane+wobble)+s.out*(d.radius*.55f);d.at.x=wall.x;d.at.z=wall.z;
                if(d.at.y<=d.radius*.45f){d.at.y=d.radius*.45f;d.phase=SpillFloor;d.age=0;d.speed=k.spread*(.7f+.6f*SpillNoise(s.seed));
                    // サラサラの液体は床で跳ねて飛び散る
                    for(int c=0;c<k.splash&&s.drops.size()<kSpillMaxDrops+20;++c){DevourSpillFlow::Drop f;f.serial=++s.serial;f.phase=SpillSplash;f.radius=d.radius*.55f;f.at=d.at+V{0,.3f,0};
                        const float a=SpillNoise(s.seed)*6.283f;f.velocity=s.out*(6+10*SpillNoise(s.seed))+V{std::cos(a)*7,9+8*SpillNoise(s.seed),std::sin(a)*7};s.drops.push_back(f);}}
                break;}
            case SpillFloor:{// 床のたまりへ広がりながら溶け込む (粘い液体はすぐ止まる)
                const float u=std::clamp(d.age/k.floorLife,0.f,1.f);V toward=s.pool-d.at;toward.y=0;const V dir=Length(toward)>.5f?Unit(toward):s.out;
                d.at=d.at+(dir+s.side*(d.lane*.15f))*(d.speed*(1-u)*dt);d.at.y=d.radius*.45f*(1-.6f*u);break;}
            case SpillSplash:// 跳ねた粒: 放物線で落ちて消える
                d.velocity.y-=160*dt;d.at=d.at+d.velocity*dt;if(d.at.y<0)d.age=1e9f;break;}}
        // 溶け込んだ粒と、遠くへ跳ねた粒を消す
        s.drops.erase(std::remove_if(s.drops.begin(),s.drops.end(),[&](const DevourSpillFlow::Drop& d){return (d.phase==SpillFloor&&d.age>=k.floorLife)||d.age>=1e8f||(d.phase==SpillSplash&&d.age>2);}),s.drops.end());}}
}
// 粒と、前後の粒をつなぐ糸を描く (粘い液体だけ糸でつなぐ)。速い粒は動く向きへ伸ばす
void ChronoSystem::DrawDevourSpills(GameContext& ctx){
    for(const auto& s:devourSpills_){if(!s.visible||s.drops.empty())continue;const auto& k=Chrono::SpillParamsOf(s.kind);Engine::Vector4 color=s.color;color.w=k.alpha;
        auto aim=[](V axis){return Engine::Vector3{-std::asin(std::clamp(axis.y,-1.f,1.f)),std::atan2(axis.x,axis.z),0};};
        for(size_t i=0;i<s.drops.size();++i){const auto& d=s.drops[i];float r=d.radius;
            if(d.phase==SpillFloor){const float u=std::clamp(d.age/k.floorLife,0.f,1.f);r*=1+.6f*u;}
            Engine::Transform t;t.translate=EV(d.at);
            const V motion=d.phase==SpillSplash?d.velocity:d.phase==SpillWall?V{0,-d.speed,0}:V{};const float speed=Length(motion);
            if(speed>3){const V axis=motion*(1/speed);t.rotate=aim(axis);t.scale={r,r,r*(1+std::min(1.5f,speed*.03f))};}
            else if(d.phase==SpillFloor){const float u=std::clamp(d.age/k.floorLife,0.f,1.f);t.scale={r*1.4f,r*(.55f-.3f*u),r*1.4f};}
            else if(d.phase==SpillHang){const float sag=std::min(1.f,d.age/std::max(.05f,k.hang));t.scale={r*(1+.15f*sag),r*(1+.5f*sag),r*(1+.15f*sag)};t.translate.y-=r*.3f*sag;}
            else t.scale={r,r*.8f,r};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,color,"SpillLiquid");
            // 糸: 1 つ前の粒と近ければ、2 つの粒の細い方に合わせた管でつなぐ (家具の上から壁まで 1 本に見える)
            if(k.strand<=0||i==0)continue;const auto& b=s.drops[i-1];if(b.phase>SpillWall||d.phase>SpillWall)continue;
            V delta=d.at-b.at;const float length=Length(delta);if(length<.05f||length>k.strand)continue;
            Engine::Transform tube;tube.translate=EV(b.at);tube.rotate=aim(delta*(1/length));const float thick=std::min(d.radius,b.radius)*.62f;tube.scale={thick,thick,length};
            ctx.renderer->DrawMeshInstanced(slimeBeamMesh_,white_,tube,color,"SpillLiquid");}}
}
}
