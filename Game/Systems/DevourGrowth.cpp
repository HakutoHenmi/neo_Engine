// DevourGrowth.cpp
// 大きい物を飲んで体が育ったときの水しぶき (ChronoSystem の一部)。体の流体とは別に用意した粒で、本物の水の飛び散り方に寄せる:
//  - 王冠: 体の縁から急な角度で立ち上がる細いしずくの壁 (水面に物が落ちたときの冠)
//  - しずく: 大きさはばらばら (小さい物ほど多い)。小さいほど速く飛び、空気の抵抗で早く失速する
//  - 細かい飛沫: とても小さく速い粒。すぐ失速して消える
//  - 霧: 足元に広がって薄れる、やわらかい白いもや
//  - 着地: 大きいしずくは地面で砕けて小さな粒を跳ね上げ、平たいぬれ跡を残して乾くように消える
// しずくは透けた水 (SplashWater: 縁の映り込みとつや)、霧はカメラを向いた四角 (SplashMist)。
// 大きさ・速さ・重力は体の半径に合わせる (体が大きくなってカメラが引いても、画面の上で同じ飛び方に見える)。
// 同時に体を上へぐっと伸ばし、cm の表示を大きく跳ねさせる。続けて飲んだときは間を空ける (うるさくしない)
// おまけ: スライムが床を通った所にぬれ跡を残す (WetTrail)。時間がたつと縮みながら薄れ、古い順に消える
#include "ChronoSystemInternal.h"
#include <random>

namespace Game {
namespace {
// 飲んでから飛び散るまでの間 (体がむくっと膨らむのに合わせる)・次の飛び散りまでの間・粒の上限
constexpr float kGrowDelay=.1f,kGrowCooldown=.8f;
constexpr size_t kGrowMaxDrops=3000;
enum GrowKind {GrowDropKind=0,GrowSpray=1,GrowSplat=2,GrowMist=3};
std::mt19937& GrowRandom(){static std::mt19937 random{std::random_device{}()};return random;}
float U(float low,float high){return std::uniform_real_distribution<float>(low,high)(GrowRandom());}
// ぬれ跡: 残る時間・薄れ始めてから消えるまで・跡の数の上限 (超えたら古い物から消す)・跡を置く間隔 (体の半径に対して)
constexpr float kWetLife=8,kWetDry=3,kWetSpacing=.5f;
constexpr size_t kWetMax=400;
}

// ぬれ跡を置く: 床にいて、前の跡から体の半径の半分進んだら 1 つ置く。古い跡は時間で消す
void ChronoSystem::TickDevourTrail(const Player& p,float dt){
    for(auto& m:wetMarks_)m.age+=dt;
    // (古い順に並んでいるので、先頭から消えた分を取り除く)
    auto alive=std::find_if(wetMarks_.begin(),wetMarks_.end(),[](const WetMark& m){return m.age<kWetLife;});wetMarks_.erase(wetMarks_.begin(),alive);
    const auto& body=life_.bodies[0];
    if(finished_||!p.grounded||body.mass<=0){wetHasLast_=false;return;}
    const float R=LifeRadii(body,life_.recipe).x;const V at{body.at.x,0,body.at.z};
    if(wetHasLast_&&Length(at-wetLast_)<R*kWetSpacing)return;
    wetLast_=at;wetHasLast_=true;
    WetMark m;m.at={at.x,InkGround(body.at),at.z};m.radius=R*U(.8f,1.f);m.yaw=U(0,6.2831853f);m.seed=U(0,60);
    wetMarks_.push_back(m);if(wetMarks_.size()>kWetMax)wetMarks_.erase(wetMarks_.begin());
}
void ChronoSystem::DrawDevourTrail(GameContext& ctx){
    if(wetMarks_.empty()||!growQuad_)return;
    for(const auto& m:wetMarks_){
        // 終わりの kWetDry 秒で、縁から乾くように縮みながら薄れる
        const float dry=std::clamp((m.age-(kWetLife-kWetDry))/kWetDry,0.f,1.f),fade=1-dry*dry,size=m.radius*(1-.35f*dry);
        Engine::Transform t;t.translate={m.at.x,m.at.y+.03f+m.radius*.004f,m.at.z};t.rotate={0,m.yaw,0};t.scale={size,1,size};
        ctx.renderer->DrawMeshInstanced(growQuad_,white_,t,{m.seed,0,0,.5f*fade},"WetTrail");}
}

// 大きい物を飲んだ: size は 0〜1 (限界に近いほど大きい)。少し後に飛び散らせる
void ChronoSystem::DevourGrowBurst(float size){
    if(devourGrow_.cool>0||devourGrow_.wait>=0){devourGrow_.size=(std::max)(devourGrow_.size,size);return;}
    devourGrow_.wait=kGrowDelay;devourGrow_.size=size;
}

void ChronoSystem::TickDevourGrowth(float dt){
    auto& g=devourGrow_;g.cool=(std::max)(0.f,g.cool-dt);
    if(g.wait>=0&&(g.wait-=dt)<0){
        g.cool=kGrowCooldown;auto& body=life_.bodies[0];const float s=g.size;g.size=0;
        // 体を上へぐっと伸ばし (ばねで戻る)、cm の表示を大きく跳ねさせる
        body.aspectVelocity=(std::max)(body.aspectVelocity,6+5*s);devourSizePulse_=(std::max)(devourSizePulse_,1+.6f*s);
        const float R=LifeRadii(body,life_.recipe).x,ground=InkGround(body.at),gravity=20*R;
        const V base{body.at.x,ground,body.at.z},up{0,1,0};
        auto add=[&](const GrowDrop& d){if(g.drops.size()<kGrowMaxDrops)g.drops.push_back(d);};
        // 高さ h まで上がり、地面で水平に reach 進む速さで投げる (抵抗が無いときの弾道)
        auto launch=[&](GrowDrop& d,V dir,float h,float reach){const float vy=std::sqrt(2*gravity*h),drop=(std::max)(0.f,d.at.y-ground);
            const float t=(vy+std::sqrt(vy*vy+2*gravity*drop))/gravity;d.velocity=dir*(reach/(std::max)(t,.01f))+up*vy;};
        auto ring=[](float a){return V{std::cos(a),0,std::sin(a)};};
        const float turn=U(0,6.2831853f);
        // 王冠: 縁から急な角度で立ち上がる細いしずく (少し時間をずらして、輪が立ち上がって見えるように)
        const int crown=int(120+60*s);
        for(int i=0;i<crown;++i){GrowDrop d;d.kind=GrowDropKind;const V dir=ring(turn+6.2831853f*(float(i)+U(-.35f,.35f))/float(crown));
            d.at=base+dir*(R*U(.85f,1.f))+up*(R*U(.05f,.2f));d.radius=R*U(.08f,.15f);d.gravity=gravity;d.drag=.6f;d.ground=ground;d.phase=U(0,6.3f);
            launch(d,dir,R*U(.7f,1.25f)*(1+.35f*s),R*U(.2f,.55f));d.age=-U(0,.05f);add(d);}
        // しずく: 小さい物ほど多い大きさの分布。小さいほど遠く低く、空気の抵抗で早く失速する
        const int drops=int(220+140*s);
        for(int i=0;i<drops;++i){GrowDrop d;d.kind=GrowDropKind;const V dir=ring(U(0,6.2831853f));
            const float r=(std::min)(R*.28f,R*.06f*std::pow(U(.03f,1.f),-.5f)),big=r/(R*.28f);
            d.at=base+dir*(R*U(.7f,1.f))+up*(R*U(.05f,.45f));d.radius=r;d.gravity=gravity;d.ground=ground;d.phase=U(0,6.3f);
            d.drag=.35f*(R*.03f/r);launch(d,dir,R*U(.25f,.95f)*(1.15f-.45f*big),R*U(.8f,2.1f)*(1.2f-.5f*big)*(.85f+.3f*s));d.age=-U(0,.08f);add(d);}
        // 細かい飛沫: とても小さく速い。すぐ失速して消える
        const int spray=int(260+180*s);
        for(int i=0;i<spray;++i){GrowDrop d;d.kind=GrowSpray;const V dir=ring(U(0,6.2831853f));
            d.at=base+dir*(R*U(.8f,1.f))+up*(R*U(.05f,.3f));d.radius=R*U(.025f,.05f);d.gravity=gravity;d.ground=ground;d.drag=2.4f;d.life=U(.3f,.6f);
            launch(d,dir,R*U(.2f,.8f),R*U(1.6f,3.2f));d.age=-U(0,.06f);add(d);}
        // 霧: 足元から外へ広がり、少し上がって薄れる
        const int mist=int(26+14*s);
        for(int i=0;i<mist;++i){GrowDrop d;d.kind=GrowMist;const V dir=ring(turn+6.2831853f*(float(i)+U(-.4f,.4f))/float(mist));
            d.at=base+dir*(R*U(.8f,1.1f))+up*(R*U(.1f,.35f));d.velocity=dir*(R*U(1.f,1.8f))+up*(R*U(.2f,.5f));d.radius=R*U(.45f,.7f);
            d.gravity=0;d.drag=2.2f;d.life=U(.55f,.85f);d.phase=U(0,1);d.age=-U(0,.08f);add(d);}
    }
    // 動かす
    const size_t count=g.drops.size();
    for(size_t i=0;i<count;++i){auto& d=g.drops[i];d.age+=dt;if(d.age<0)continue;
        if(d.kind==GrowSplat){d.velocity=d.velocity*std::exp(-8*dt);d.at=d.at+d.velocity*dt;continue;}
        d.velocity.y-=d.gravity*dt;d.velocity=d.velocity*std::exp(-d.drag*dt);d.at=d.at+d.velocity*dt;
        if(d.kind==GrowMist||d.at.y>d.ground+d.radius*.2f||d.velocity.y>=0)continue;
        // 着地: 細かい飛沫は消える。しずくは平たいぬれ跡になり、大きいものは砕けて小さな粒を跳ね上げる
        if(d.kind==GrowSpray){d.age=d.life;continue;}
        const V hit=d.at;const V slide{d.velocity.x*.25f,0,d.velocity.z*.25f};const float r=d.radius,gravity=d.gravity,ground=d.ground;
        d.kind=GrowSplat;d.age=0;d.life=U(.4f,.7f);d.at.y=ground+r*.15f;d.velocity=slide;
        // (重力は体の半径の 20 倍なので、体の半径の 12% より大きいしずくが砕ける)
        if(r>gravity/20*.12f){const int pieces=int(U(2,4.99f));
            for(int k=0;k<pieces&&g.drops.size()<kGrowMaxDrops;++k){GrowDrop p;p.kind=GrowSpray;p.at=hit+V{0,r*.5f,0};p.radius=r*U(.25f,.45f);
                const float a=U(0,6.2831853f);p.velocity=slide*2.f+V{std::cos(a),0,std::sin(a)}*(gravity*U(.025f,.06f))+V{0,gravity*U(.04f,.09f),0};
                p.gravity=gravity;p.ground=ground;p.drag=1.5f;p.life=U(.25f,.45f);g.drops.push_back(p);}}}
    g.drops.erase(std::remove_if(g.drops.begin(),g.drops.end(),[](const GrowDrop& d){return (d.kind!=GrowDropKind&&d.age>=d.life)||d.age>4;}),g.drops.end());
}

void ChronoSystem::DrawDevourGrowth(GameContext& ctx){
    if(devourGrow_.drops.empty()||!ctx.camera)return;
    const auto rgb=life_.recipe.color.ToRGB();
    // 水はわずかにスライムの色を帯びる
    const Engine::Vector4 water{.75f+.25f*rgb.x,.82f+.18f*rgb.y,.9f+.1f*rgb.z,1};
    const V eye=Read(ctx.camera->Position());
    auto aim=[](V axis){return Engine::Vector3{-std::asin(std::clamp(axis.y,-1.f,1.f)),std::atan2(axis.x,axis.z),0};};
    for(const auto& d:devourGrow_.drops){if(d.age<0)continue;Engine::Transform t;t.translate=EV(d.at);
        switch(d.kind){
        case GrowDropKind:case GrowSpray:{
            // 進む向きへ伸ばす (1/60 秒に進む長さぶん)。遅いときは、ぷるぷると形が揺れる
            const float speed=Length(d.velocity),r=d.radius;const float fade=d.kind==GrowSpray?1-std::clamp((d.age-d.life*.6f)/(d.life*.4f),0.f,1.f):1.f;
            const float wobble=.12f*std::sin(d.age*38+d.phase)*std::exp(-d.age*3);
            if(speed>.001f)t.rotate=aim(d.velocity*(1/speed));
            t.scale={r*(1+wobble),r*(1-wobble),(std::min)(r*4.f,r+speed*.018f)};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{water.x,water.y,water.z,(d.kind==GrowSpray?.8f:1.f)*fade},"SplashWater");break;}
        case GrowSplat:{// 平たく広がり、乾くように薄れる
            const float u=std::clamp(d.age/d.life,0.f,1.f),wide=d.radius*(1.5f+1.3f*std::sqrt(u));
            t.scale={wide,d.radius*(.22f-.12f*u),wide};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{water.x,water.y,water.z,.9f*(1-u)*(1-u)},"SplashWater");break;}
        case GrowMist:{// カメラへ向けた四角。広がりながら薄れる
            if(!growQuad_)break;const float u=std::clamp(d.age/d.life,0.f,1.f),size=d.radius*(1+1.2f*u);
            const V to=eye-d.at;const float len=Length(to);if(len<.01f)break;const V n=to*(1/len);
            t.rotate={std::atan2(std::sqrt(n.x*n.x+n.z*n.z),n.y),std::atan2(n.x,n.z),0};t.scale={size,size,size};
            ctx.renderer->DrawMeshInstanced(growQuad_,white_,t,{1,1,d.phase,.28f*std::sin(3.14159f*std::sqrt(u))},"SplashMist");break;}}}
}
}
