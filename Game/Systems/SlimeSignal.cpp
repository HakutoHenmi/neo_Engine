// SlimeSignal.cpp
// 信号機に変形したスライム (ChronoSystem の一部)。
// 赤で範囲の敵の足を止め、青で止めた敵を前に並べて横断歩道を渡る人影でダメージを与え、黄で敵にマーカーを付ける。
// 色は赤 → 青 → 黄の順にしか進まず、黄の後は長く点検する (SlimeSignalRules.h)。
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
// 体の前方向から、横・上を含むローカルの点をワールドへ移す
Chrono::Vec SignalWorld(Chrono::Vec base,Chrono::Vec forward,Chrono::Vec local,float scale){
    Chrono::Vec side{forward.z,0,-forward.x};return base+side*(local.x*scale)+Chrono::Vec{0,local.y*scale,0}+forward*(local.z*scale);
}
}
// 信号を出す: 色ごとの効果を始め、知らせと音を出す
void ChronoSystem::FireSignal(entt::registry& r,Player& p,GameContext& ctx,SignalColor color){
    (void)r;(void)p;
    auto& body=life_.bodies[0];float scale=LifeScale(body.mass);
    V forward=body.forward;forward.y=0;forward=Length(forward)>.01f?Unit(forward):V{0,0,1};V side{forward.z,0,-forward.x};
    auto* audio=Engine::Audio::GetInstance();
    audio->Play(coreSound_,false,.45f*audio->GetMasterSEVolume(),color==SignalColor::Red?.8f:color==SignalColor::Green?1.25f:1.05f);
    if(color==SignalColor::Red){
        // 赤: 周りの敵の足を止める
        float radius=SignalStopRadius(body.mass);int stopped=SignalStopEnemies(swarm_,body.at,radius);
        signalPulses_.push_back({body.at,0,radius,color});LifeImpact(ctx,ImpactSignalPulse,body.at);
        life_.message="赤信号！ "+std::to_string(stopped)+"体の足を止めた / 次は青で並べる";life_.messageAge=2;
    }else if(color==SignalColor::Green){
        // 青: 止まっている敵を、横の並びの順を保って前に横一列に並べる (守護者は大きすぎて並ばない)
        float size=SignalReach(body.mass);std::vector<SwarmEnemy*> lined;float spacing=5*size;
        for(auto& e:swarm_)if(SwarmAlive(e)&&e.signalStop>0&&CityKind(e)!=CityEnemy::Guardian){lined.push_back(&e);spacing=std::max(spacing,e.scale*2.4f);}
        std::sort(lined.begin(),lined.end(),[&](const SwarmEnemy* a,const SwarmEnemy* b){return Dot(a->at-body.at,side)<Dot(b->at-body.at,side);});
        V center=body.at+forward*(5*scale+14);center.y=InkGround(center);
        auto positions=SignalLinePositions(center,side,int(lined.size()),spacing);
        for(size_t i=0;i<lined.size();++i){auto& e=*lined[i];e.signalLine=positions[i];e.signalLined=true;e.signalStop=std::max(e.signalStop,SignalCrossDuration()+.6f);}
        // 並んだ列の上に横断歩道を敷き、人影を渡らせる (誰も止まっていなければダメージは半分)
        // 敷く向きは毎回左右を入れ替える
        float length=std::max(14*size,spacing*float(lined.size())),dir=(signalWalkCount_++%2)?-1.f:1.f;
        signalWalks_.push_back({center,side,forward,length,0,lined.empty()?.5f:1.f,size,dir,{}});
        life_.message=lined.empty()?std::string("青信号！ 止まっている敵がいない / 赤で止めてから使うと強い"):"青信号！ "+std::to_string(lined.size())+"体を並べて横断歩道へ";life_.messageAge=2;
    }else{
        // 黄: 前方の敵にマーカーを付ける (時間切れで爆発、その間に動けば大爆発)
        int marked=0;float range=SignalMarkReach(body.mass);
        for(auto& e:swarm_)if(SwarmAlive(e)&&SignalInCone(body.at,forward,e.at,range)){e.signalMark=SignalMarkTime;e.signalMarkAt=e.at;++marked;}
        signalPulses_.push_back({body.at+forward*(range*.5f),0,range*.5f,color});
        life_.message="黄信号！ "+std::to_string(marked)+"体にマーカー / 動くと大爆発 → 信号の点検 "+std::to_string(int(SignalRestTime))+"秒";life_.messageAge=2.4f;
    }
}
// 横断歩道の人影と、広がる輪を進める
void ChronoSystem::UpdateSignalEffects(entt::registry& r,Player& p,GameContext& ctx,float dt){
    const auto& body=life_.bodies[0];
    for(auto& walk:signalWalks_){walk.age+=dt;
        for(int i=0;i<SignalWalkers;++i){V at;if(!SignalWalkerAt(walk.center,walk.side,walk.forward,walk.length,walk.size,walk.dir,i,walk.age,at))continue;
            // 人影が通った敵に、人影ごとに 1 回だけダメージ
            for(auto& e:swarm_){if(!SwarmAlive(e)||LifeDistance(at,e.at)>SignalWalkerReach(e.scale,walk.size))continue;
                uint64_t key=(uint64_t(i)<<32)|e.id;if(std::find(walk.hits.begin(),walk.hits.end(),key)!=walk.hits.end())continue;
                walk.hits.push_back(key);LifeHit(r,p,ctx,e.id,SignalWalkerDamage(body.mass)*walk.damage,LifeAction::Signal,at);
                if(swarmEffects_.size()<128)swarmEffects_.push_back({e.at,0,1.2f+e.scale*.4f,2});}}}
    signalWalks_.erase(std::remove_if(signalWalks_.begin(),signalWalks_.end(),[](const SignalWalk& w){return w.age>SignalCrossDuration()+.3f;}),signalWalks_.end());
    for(auto& pulse:signalPulses_)pulse.age+=dt;
    signalPulses_.erase(std::remove_if(signalPulses_.begin(),signalPulses_.end(),[](const SignalPulse& s){return s.age>.8f;}),signalPulses_.end());
}
// 信号機の描画: 体と変形アイテムの灯器・広がる輪・止まった敵とマーカー・横断歩道と人影
void ChronoSystem::DrawSignal(GameContext& ctx){
    const auto& body=life_.bodies[0];const auto& signal=life_.signal;
    auto ring=[&](V at,float radius,Vec color,float alpha){Engine::Transform t;t.translate={at.x,InkGround(at)+.2f,at.z};t.scale={radius,1,radius};
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,t,{color.x,color.y,color.z,alpha},"HordeEffect");};
    auto glow=[&](V at,float radius,Vec color,float bright,const char* shader){Engine::Transform t;t.translate=EV(at);t.scale={radius,radius,radius};
        ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{color.x*bright,color.y*bright,color.z*bright,shader[0]=='S'?.7f:1.f},shader);};
    // 体の灯器: 次に出せる色は点灯、出した直後の色は点滅、それ以外は消灯 (前後の両面)
    if(body.action==LifeAction::Signal&&!life_.lab){float scale=LifeScale(body.mass);V forward=body.forward;forward.y=0;forward=Unit(forward);V base=body.at-V{0,1.25f,0};
        for(auto color:{SignalColor::Green,SignalColor::Yellow,SignalColor::Red}){
            bool lit=(body.weapon.ready&&signal.wait<=0&&signal.next==color)||(signal.flash>0&&signal.flashed==color);
            bool pending=!lit&&signal.wait>0&&!SignalResting(signal)&&signal.next==color&&std::fmod(life_.age,.5f)<.25f;
            for(float face:{1.f,-1.f}){V at=SignalWorld(base,forward,SignalLampLocal(color,face),scale);
                glow(at,.5f*scale,SignalColorRGB(color),lit?3.f:pending?1.2f:.12f,"Default");
                if(lit)glow(at,.95f*scale,SignalColorRGB(color),1.6f,"SlimeChildGlow");}}}
    // 変形アイテムの信号機: 3 色を順に光らせて目を引く
    for(const auto& prop:life_.props)if(prop.active&&prop.morphItem==LifeMorphItem::Signal){
        int lit=int(life_.age*1.5f)%3;
        for(int c=0;c<3;++c){SignalColor color=c==0?SignalColor::Red:c==1?SignalColor::Green:SignalColor::Yellow;
            for(float face:{1.f,-1.f}){V at=SignalWorld(signalItemAt_,{0,0,1},SignalLampLocal(color,face),SignalItemScale);
                glow(at,.5f*SignalItemScale,SignalColorRGB(color),c==lit?3.f:.15f,"Default");}}}
    // 赤と黄の広がる輪
    for(const auto& pulse:signalPulses_){float t=std::min(1.f,pulse.age/.5f);ring(pulse.at,pulse.radius*(.2f+.8f*t),SignalColorRGB(pulse.color),.55f*(1-pulse.age/.8f));}
    // 止まっている敵には赤い輪、マーカーの付いた敵には黄色い光 (残りが少ないほど速く点滅)
    for(const auto& e:swarm_){if(!SwarmAlive(e))continue;
        if(e.signalStop>0)ring(e.at,e.scale*1.6f,SignalColorRGB(SignalColor::Red),.35f+.15f*std::sin(life_.age*8));
        if(e.signalMark>0){float blink=std::sin(life_.age*(6+18*(1-e.signalMark/SignalMarkTime)))>0?1.f:.45f;
            glow(e.at+V{0,e.scale*1.6f+1.2f+.2f*std::sin(life_.age*5),0},.55f+.1f*e.scale,SignalColorRGB(SignalColor::Yellow),2.5f*blink,"Default");
            ring(e.at,e.scale*1.4f,SignalColorRGB(SignalColor::Yellow),.4f*blink);}}
    // 横断歩道: 白い縞を左右どちらかの端から流れるように敷き (先頭は光る)、緑に光る人影を同じ端から歩かせる
    for(const auto& walk:signalWalks_){if(walk.age>SignalCrossDuration())continue;
        float yaw=std::atan2(walk.forward.x,walk.forward.z),half=SignalCrossHalf(walk.length,walk.size),gap=2.2f*walk.size;int bars=int(2*half/gap);
        for(int k=0;k<bars;++k){float laid=walk.age-SignalStripeTime(k,bars);if(laid<0)break;
            // 敷いた直後は細く短く、すぐに元の大きさへ伸びる
            float grow=std::min(1.f,laid/.12f);V at=walk.center+walk.side*(walk.dir*(-half+gap*.5f+float(k)*gap))-walk.forward*(SignalCrossNear*walk.size);
            Engine::Transform t;t.translate={at.x,InkGround(at)+.06f,at.z};t.rotate={0,yaw,0};t.scale={.25f*gap*(.4f+.6f*grow),1,SignalCrossWidth*.5f*walk.size*grow};
            ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,t,{1.3f,1.3f,1.3f,1},"Default");
            if(grow<1){Engine::Transform head;head.translate={at.x,InkGround(at)+.6f,at.z};head.scale={.9f*walk.size,.9f*walk.size,.9f*walk.size};
                ctx.renderer->DrawMeshInstanced(sphere_,white_,head,{1,1,1,.8f*(1-grow)},"SlimeChildGlow");}}
        for(int i=0;i<SignalWalkers;++i){V at;if(!SignalWalkerAt(walk.center,walk.side,walk.forward,walk.length,walk.size,walk.dir,i,walk.age,at))continue;
            // 歩行者信号の人のような緑の人影 (成長に合わせて大きくし、敵の列の上でも見えるよう光の縁を付ける)
            float ground=InkGround(at),swing=std::sin(walk.age*14+float(i)),bob=.2f*std::abs(swing),k=2.2f+.6f*walk.size;const Vec green{.4f,3.f,1.5f};
            auto part=[&](V offset,V size){Engine::Transform t;t.translate={at.x+offset.x*k,ground+(offset.y+bob)*k,at.z+offset.z*k};t.scale=EV(size*k);
                ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{green.x,green.y,green.z,1},"Default");
                ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.2f,1,.55f,.6f},"SlimeChildXRay");};
            part({0,4.6f,0},{.45f,.45f,.45f});part({0,3.2f,0},{.55f,1.05f,.4f});
            for(float leg:{-1.f,1.f}){V step=walk.side*(.55f*swing*leg);part({step.x,1.1f,step.z},{.22f,.95f,.22f});part({-step.x*.8f,3.2f,-step.z*.8f},{.17f,.75f,.17f});}}}
}
// マーカーの残り秒数を敵の頭上に、信号の待ち時間を信号機の頭上に出す
void ChronoSystem::DrawSignalUI(GameContext& ctx){
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);auto viewProj=ctx.camera->View()*ctx.camera->Proj();
    const auto& body=life_.bodies[0];const auto& signal=life_.signal;
    if(body.action==LifeAction::Signal&&body.weapon.ready){
        V top=body.at+V{0,-1.25f+(SignalHeight+1.2f)*LifeScale(body.mass),0};DirectX::XMFLOAT4 clip;
        DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(top.x,top.y,top.z,1),viewProj));
        if(clip.w>.1f){float x=std::clamp((clip.x/clip.w*.5f+.5f)*1280,80.f,1200.f),y=std::clamp((.5f-clip.y/clip.w*.5f)*720,60.f,520.f);
            bool resting=SignalResting(signal);auto rgb=SignalColorRGB(resting?SignalColor::Red:signal.next);Engine::Vector4 tint{rgb.x,rgb.y,rgb.z,1};
            const char* name=signal.next==SignalColor::Red?"赤":signal.next==SignalColor::Green?"青":"黄";
            if(signal.wait>0&&signal.waitTotal>0){
                // 歩行者信号の残り時間のように、減っていく 10 本の棒と秒数
                int bars=int(std::ceil(signal.wait/signal.waitTotal*10));
                for(int b=0;b<10;++b){float bx=x-60+float(b)*12;ui.Fill({bx-1,y-1,10,16},{0,0,0,.75f});ui.Fill({bx,y,8,14},b<bars?tint:Engine::Vector4{1,1,1,.15f});}
                char seconds[48];
                if(resting)snprintf(seconds,sizeof(seconds),"点検 %.1f",signal.wait);else snprintf(seconds,sizeof(seconds),"%s まで %.1f",name,signal.wait);
                std::string label=seconds;float w=ui.Width(label,15,UI::JapaneseFont);
                ui.Text(label,x-w*.5f+1,y-22,15,{0,0,0,.85f},UI::JapaneseFont);ui.Text(label,x-w*.5f,y-23,15,tint,UI::JapaneseFont);}
            else{std::string label=std::string(name)+" OK";float w=ui.Width(label,16,UI::JapaneseFont);
                ui.Dot(x-w*.5f-12,y+8,7,{0,0,0,.7f});ui.Dot(x-w*.5f-12,y+8,5.5f,tint);
                ui.Text(label,x-w*.5f+1,y+1,16,{0,0,0,.85f},UI::JapaneseFont);ui.Text(label,x-w*.5f,y,16,tint,UI::JapaneseFont);}}}
    for(const auto& e:swarm_){if(!SwarmAlive(e)||e.signalMark<=0)continue;
        V head=e.at+V{0,e.scale*1.6f+3.2f,0};DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(head.x,head.y,head.z,1),viewProj));
        if(clip.w<=.1f)continue;float x=(clip.x/clip.w*.5f+.5f)*1280,y=(.5f-clip.y/clip.w*.5f)*720;if(x<0||x>1280||y<0||y>720)continue;
        std::string count=std::to_string(int(std::ceil(e.signalMark)));
        ui.Center(count,x+1.5f,y+2.5f,26,{0,0,0,.85f},UI::JapaneseFont);ui.Center(count,x,y,26,{1,.82f,.2f,1},UI::JapaneseFont);}
}
}
