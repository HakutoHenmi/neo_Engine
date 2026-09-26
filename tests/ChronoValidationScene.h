#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Chrono/ChronoComponents.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <fstream>
#include <sstream>
#include <chrono>

// Opt-in integration playback through the production input/action code.
// It never overwrites scenes or personal records and exits without interaction.
class ChronoValidationScene final : public Game::GameScene {
    using V=Game::Chrono::Vec;
    Engine::WindowDX* window_=nullptr;
    unsigned frames_=0,phaseFrame_=0;
    int phase_=-4;
    bool arenaChecked_=false,dodgeQueued_=false,dodgePriority_=false,releaseCancels_=false,pullCancels_=false;
    bool sweepChecked_=false,chargeChecked_=false,openingChecked_=false;
    unsigned resultFrames_=0;
    bool stairs_=false,miss_=false,slow_=false,pull_=false,captured_=false;
    bool tetherCaptured_=false;
    bool manualTetherCaptured_=false;
    bool shoulderChecked_=false,aimSlowChecked_=false,releaseChecked_=false;
    bool landingChecked_=false,hitStopChecked_=false,postChecked_=false,feedbackChecked_=false;
    bool landingCaptured_=false;
    bool recoveryRouteChecked_=true;
    bool anchorConsumed_=false,hiddenBossTarget_=true;
    std::chrono::steady_clock::time_point start_;
    std::ofstream trace_;
    void Capture(const wchar_t* name){
        DirectX::ScratchImage image;
        HRESULT hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),name);
        trace_<<"capture="<<hr<<'\n';
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);
        auto player=FindObjectByName("Player");GetRegistry().emplace<Game::Chrono::ControlFrame>(player);
        Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true);
        start_=std::chrono::steady_clock::now();trace_.open("tests/out/chrono-trace.txt");
        // Check the shortcut route with the actual stage colliders and a finite body.
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();V from{-25,1.21f,0};
        for(int i=5;i<9;++i){
            auto e=FindObjectByName("Chrono Anchor "+std::to_string(i));
            const auto& t=r.get<TransformComponent>(e);const auto& target=r.get<Target>(e);
            V center=V{t.translate.x,t.translate.y,t.translate.z}+target.offset;
            V to=center+V{0,1.7f,0};
            recoveryRouteChecked_&=Length(center-from)<=Reach(100)+target.radius;
            for(auto solid:r.view<Solid,BoxColliderComponent,TransformComponent>()){
                const auto& st=r.get<TransformComponent>(solid);const auto& box=r.get<BoxColliderComponent>(solid);
                V c=V{st.translate.x,st.translate.y,st.translate.z}+V{box.center.x,box.center.y,box.center.z};
                V half=V{box.size.x,box.size.y,box.size.z}*.5f;float f;V normal;
                if(Sweep(from,to-from,{c-half,c+half},{.9f,1.2f,.9f},f,normal)){
                    recoveryRouteChecked_=false;trace_<<"recovery obstruction="<<i<<" solid="<<r.get<NameComponent>(solid).name<<'\n';
                }
            }
            from=to;
        }
    }
    void DrawEditor()override{}
    void Update()override{
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");auto boss=FindObjectByName("Boss");
        if(!r.valid(player)||!r.all_of<Player>(player)){std::ofstream("tests/out/chrono-smoke.txt")<<"FAIL missing player";PostQuitMessage(2);return;}
        auto& p=r.get<Player>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input=ControlFrame{};
        input.cameraInput=phase_!=-4;
        auto center=[&](entt::entity e){const auto& a=r.get<TransformComponent>(e).translate;return V{a.x,a.y,a.z}+r.get<Target>(e).offset;};
        V pos{t.translate.x,t.translate.y,t.translate.z};
        if(phase_==-4){
            if(phaseFrame_==0){input.arena=true;Game::GameScene::Update();++phaseFrame_;return;}
            if(phaseFrame_==1){
                unsigned living=0,anchors=0;for(auto e:r.view<Hopper,Target>())if(r.get<Target>(e).active)++living;
                for(auto e:r.view<Anchor>())if(r.get<Anchor>(e).reusable)++anchors;
                arenaChecked_=living==3&&anchors==4&&r.get<Boss>(boss).engaged;
                p.hitStop=.15f;p.action=Action::Recovery;p.timer=.2f;p.buffer=.3f;
                input.attack=true;input.dodge=true;input.move={-1,0,0};
                Game::GameScene::Update();dodgeQueued_=p.dodgeBuffer>0&&p.buffer==0;
            }else if(phaseFrame_==2){
                p.hitStop=0;Game::GameScene::Update();
                dodgePriority_=p.action==Action::Dodge&&p.dodgeDirection.x<-.99f&&p.buffer==0;
            }else if(phaseFrame_==3){
                p.action=Action::Recovery;p.timer=.3f;input.attack=true;Game::GameScene::Update();
            }else if(phaseFrame_==4){
                Game::GameScene::Update();releaseCancels_=p.buffer==0&&p.action==Action::Recovery;
            }else if(phaseFrame_==5){
                p.action=Action::Pulling;p.target=FindObjectByName("Chrono Arena anchor 0");p.cooldown=0;
                input.attack=true;input.dodge=true;input.move={1,0,0};Game::GameScene::Update();
                pullCancels_=p.action==Action::Dodge&&p.target==entt::null&&p.buffer==0&&p.dodgeDirection.x>.99f;
            }else if(phaseFrame_==6){
                auto& b=r.get<Boss>(boss);b.wave=1;b.phase=1;b.clock=.9f;b.attackDirection={0,0,-1};
                b.attackHit=false;p.action=Action::Free;p.invincible=0;p.hitStop=0;p.velocity={};t.translate={0,1.3f,6};
                int hits=p.stats.hitsTaken;Game::GameScene::Update();
                sweepChecked_=b.phase==2&&p.stats.hitsTaken==hits+1&&std::string(p.damageReason)=="HIT - BOSS SWEEP";
            }else if(phaseFrame_==7){
                auto& b=r.get<Boss>(boss);b.clock=.3f;Game::GameScene::Update();
                openingChecked_=b.phase==3&&r.get<Target>(FindObjectByName("Chrono Weakpoint")).active;
            }else if(phaseFrame_==8){
                auto& b=r.get<Boss>(boss);b.wave=2;b.phase=1;b.clock=1.2f;b.attackDirection={1,0,0};b.attackHit=false;
                V beforeBoss=center(boss);p.invincible=5;p.velocity={};t.translate={0,1.3f,-12};
                Game::GameScene::Update();chargeChecked_=b.phase==2&&center(boss).x>beforeBoss.x;
            }else if(phaseFrame_<75){
                input.pitch=.28f;p.invincible=5;Game::GameScene::Update();
                if(phaseFrame_==74)Capture(L"tests/out/chrono-arena.png");
            }else if(phaseFrame_==75){input.arena=true;Game::GameScene::Update();}
            else {phase_=-3;phaseFrame_=0;return;}
            ++phaseFrame_;return;
        }
        if(phase_==-3){
            auto anchor=FindObjectByName("Chrono Anchor 5");
            if(phaseFrame_==0){t.translate={-25,1.21f,0};p.automatic=true;pos={-25,1.21f,0};}
            V dir=Unit(center(anchor)-(pos+V{0,2.8f,0}));
            input.yaw=std::atan2(dir.x,dir.z);input.pitch=-std::asin(std::clamp(dir.y,-1.0f,1.0f));
            input.attack=p.action==Action::Free&&frames_%2==0;
            if(!r.get<Target>(anchor).active){
                anchorConsumed_=!r.get<MeshRendererComponent>(anchor).enabled&&Length(pos-center(anchor))<3;
                // Isolate this shortcut fixture from the crowd/boss clocks.
                SetIsPlaying(false);SetIsPlaying(true);
                r.emplace_or_replace<ControlFrame>(FindObjectByName("Player"));
                phase_=-2;phaseFrame_=0;return;
            }
        }
        if(phase_==-2){
            if(phaseFrame_==0)t.translate={0,10,-27};
            if(p.grounded&&phaseFrame_>20&&p.landingAge>.25f){phase_=-1;phaseFrame_=0;}
        }
        if(phase_==-1){
            if(phaseFrame_==0)t.translate={31,1.3f,-4};
            input.move={0,0,1};
            if(t.translate.z>=46){
                stairs_=t.translate.y>=19;phase_=0;phaseFrame_=0;
                t.translate={0,1.3f,-27};p.velocity={};input.move={};p.mass=185;
            }
        }else if(phase_==0){
            input.aim=true;input.pitch=phaseFrame_<120?0.05f:-0.65f;input.attack=phaseFrame_==150;
            if(phaseFrame_==180){miss_=std::abs(t.translate.x)<0.1f&&std::abs(t.translate.z+27)<0.1f&&p.stats.manual==0;phase_=1;phaseFrame_=0;Capture(L"tests/out/chrono-start.png");}
        }else if(phase_>0){
            if(phase_==1 && p.stats.absorbed>=18){phase_=2;phaseFrame_=0;}
            if(phase_==2&&p.flow.maxCombo>=3&&p.stats.automatic>0){
                phase_=3;phaseFrame_=0;t.translate={0,1.3f,12};pos={0,1.3f,12};
                p.action=Action::Free;p.target=entt::null;p.velocity={};p.instability=0;p.mass=100;p.collisionScale=1;
                r.get<Boss>(boss).engaged=true;
                // Separate the boss fixture from the crowd-combat fixture.
                for(auto e:r.view<Hopper,Target,MeshRendererComponent>()){
                    r.get<Target>(e).active=false;r.get<MeshRendererComponent>(e).enabled=false;
                }
            }
            entt::entity selected=entt::null;float best=1e9f;
            for(auto e:r.view<Target,TransformComponent>()){
                const auto& tg=r.get<Target>(e);if(!tg.active)continue;
                if(phase_<3&&tg.kind!=Kind::Enemy)continue;
                if(phase_>=3&&tg.kind!=Kind::Projectile&&tg.kind!=Kind::Weakpoint)continue;
                float d=Length(center(e)-pos);if(d>Reach(p.mass)+tg.radius)continue;
                if(tg.kind==Kind::Projectile)d-=center(e).y*0.55f;
                if(tg.kind==Kind::Weakpoint)d-=30;
                if(d<best){best=d;selected=e;}
            }
            input.aim=phase_==1||(phase_>=3&&p.stats.counters==2);
            input.toggle=phase_==2&&phaseFrame_==0;
            if(r.valid(selected)){
                auto cam=GetCamera().Position();
                V origin=input.aim?V{cam.x,cam.y,cam.z}:pos+V{0,2.8f,0};
                V dir=Unit(center(selected)-origin);
                input.yaw=std::atan2(dir.x,dir.z);input.pitch=-std::asin(std::clamp(dir.y,-1.0f,1.0f));
                input.attack=p.action==Action::Free&&(frames_%2==0);
            }
        }
        V before{t.translate.x,t.translate.y,t.translate.z};float bossClock=r.get<Boss>(boss).clock;
        bool stopped=p.hitStop>.03f;
        Game::GameScene::Update();++frames_;++phaseFrame_;
        hiddenBossTarget_&=!r.get<MeshRendererComponent>(FindObjectByName("Chrono Weakpoint")).enabled;
        if(stopped&&p.hitStop>0)hitStopChecked_=Length(V{t.translate.x,t.translate.y,t.translate.z}-before)<.001f&&r.get<Boss>(boss).clock==bossClock;
        if(phase_==-2&&p.grounded&&t.scale.y<.9f){landingChecked_=t.translate.y>=1.19f;
            if(!landingCaptured_&&p.landingAge>.09f){Capture(L"tests/out/chrono-landing.png");landingCaptured_=true;}}
        if(p.worldScale<.3f&&Engine::Renderer::GetInstance()->GetPostProcessParams().san>.5f)postChecked_=true;
        if(phase_==0&&p.failureTime>0&&p.failure!=ChainFailure::None)feedbackChecked_=true;
        if(phase_==0&&phaseFrame_==110){
            auto cam=GetCamera().Position();
            shoulderChecked_=cam.x-t.translate.x>4&&Length(V{cam.x,cam.y,cam.z}-V{t.translate.x,t.translate.y,t.translate.z})>8;
            aimSlowChecked_=p.worldScale<.23f&&p.flow.remaining==0;
            Capture(L"tests/out/chrono-shoulder-aim.png");
        }
        if(phase_>=2&&!p.aiming&&p.aimScale>.95f)releaseChecked_=true;
        // Use the real gameplay camera, including during extension.
        if(phase_==0&&phaseFrame_>151&&phaseFrame_<180){
            if(!manualTetherCaptured_&&p.action==Action::Extending&&p.shotDistance>10){
                Capture(L"tests/out/chrono-liquid-side.png");manualTetherCaptured_=true;
            }
        }
        slow_=slow_||p.flow.remaining>0;pull_=pull_||p.action==Action::Pulling;
        if(!tetherCaptured_&&phase_>=3&&p.action==Action::Extending&&Length(p.hand-V{t.translate.x,t.translate.y,t.translate.z})>8){
            Capture(L"tests/out/chrono-liquid-arm.png");tetherCaptured_=true;
        }
        if(p.stats.counters>0&&!captured_){Capture(L"tests/out/chrono-counter.png");captured_=true;}
        if(frames_%30==0){
            trace_<<frames_<<" phase="<<phase_<<" act="<<static_cast<int>(p.action)<<" pos="<<t.translate.x<<','<<t.translate.y<<','<<t.translate.z
                <<" mass="<<p.mass<<" strain="<<p.instability<<" max="<<p.flow.maxCombo<<" auto="<<p.automatic<<" counter="<<p.stats.counters
                <<" bossphase="<<r.get<Boss>(boss).phase<<" breaks="<<r.get<Boss>(boss).waveBreaks
                <<" target="<<(r.valid(p.target)?r.get<NameComponent>(p.target).name:"none")<<" hand="<<p.hand.x<<','<<p.hand.y<<','<<p.hand.z<<'\n';trace_.flush();
        }
        bool win=r.get<Target>(boss).hp<=0;
        if(win&&++resultFrames_<12)return; // Render the result before capture.
        // Real-time boss waves must get the same budget on fast and slow GPUs.
        if(win||std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count()>=65.0f||p.mass<=0){
            Capture(L"tests/out/chrono-end.png");
            const auto& profile=Engine::Renderer::GetInstance()->GetFluidProfileStats();
            bool pass=win&&stairs_&&miss_&&slow_&&pull_&&p.stats.manual>0&&p.stats.automatic>0&&p.stats.counters>=3&&shoulderChecked_&&aimSlowChecked_&&releaseChecked_&&landingChecked_&&hitStopChecked_&&postChecked_&&feedbackChecked_;
            std::ostringstream report;
            report<<" frames="<<frames_<<" stairs="<<stairs_<<" miss_stays_put="<<miss_<<" slow="<<slow_<<" pull="<<pull_
                <<" shoulder="<<shoulderChecked_<<" aim_slow="<<aimSlowChecked_<<" aim_release="<<releaseChecked_
                <<" landing="<<landingChecked_<<" hit_stop="<<hitStopChecked_<<" slow_post="<<postChecked_<<" failure_feedback="<<feedbackChecked_
                <<" manual="<<p.stats.manual<<" auto="<<p.stats.automatic<<" counters="<<p.stats.counters<<" max_chain="<<p.flow.maxCombo
                <<" mass="<<p.mass<<" boss_hp="<<r.get<Target>(boss).hp<<" fluid_slots="<<profile.particleSlots
                <<" elapsed="<<std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count()<<'\n';
            for(unsigned i=0;i<Engine::Renderer::kFluidProfileStageCount;++i)report<<"gpu_stage_"<<i<<"="<<profile.averageMs[i]<<" ms\n";
            // Exercise the same STOP/PLAY lifecycle used by the editor.
            SetIsPlaying(false);
            bool postReset=!Engine::Renderer::GetInstance()->GetPostProcessEnabled()&&Engine::Renderer::GetInstance()->GetPostProcessParams().san==0;
            SetIsPlaying(true);
            auto fresh=FindObjectByName("Player");unsigned living=0;
            for(auto e:r.view<Hopper,Target>())if(r.get<Target>(e).active)++living;
            bool reset=r.valid(fresh)&&r.all_of<Player>(fresh)&&r.get<Player>(fresh).mass==100&&
                r.get<Player>(fresh).flow.combo==0&&!r.get<Player>(fresh).automatic&&living==9&&
                r.get<Target>(FindObjectByName("Boss")).hp==360;
            pass=pass&&reset&&postReset&&recoveryRouteChecked_&&anchorConsumed_&&hiddenBossTarget_&&arenaChecked_&&dodgeQueued_&&dodgePriority_&&releaseCancels_&&pullCancels_&&sweepChecked_&&chargeChecked_&&openingChecked_;
            std::ofstream("tests/out/chrono-smoke.txt")<<(pass?"PASS":"FAIL")<<report.str()<<"restart="<<reset<<" post_reset="<<postReset<<" recovery_route="<<recoveryRouteChecked_
                <<" anchor_consumed="<<anchorConsumed_<<" invisible_boss_target="<<hiddenBossTarget_
                <<" arena="<<arenaChecked_<<" dodge_queued="<<dodgeQueued_<<" dodge_priority="<<dodgePriority_
                <<" release_cancels="<<releaseCancels_<<" pull_cancels="<<pullCancels_
                <<" boss_sweep="<<sweepChecked_<<" boss_charge="<<chargeChecked_<<" boss_opening="<<openingChecked_<<'\n';
            PostQuitMessage(pass?0:2);
        }
    }
};
