#pragma once
#include "../Game/UI/RogueliteUI.h"
#include "../Game/Scenes/GameScene.h"
#include "../Game/Chrono/ChronoComponents.h"
#include "../Game/Chrono/InkRules.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <fstream>
#include <sstream>
#include <chrono>

// Opt-in integration playback through the production input/action code.
// It never overwrites scenes or personal records and exits without interaction.
class ChronoValidationScene final : public Game::GameScene {
    bool inkTest_=false,inkPaint_=false,inkSwim_=false,inkRamp_=false,inkBird_=false,inkClosed_=true;
    bool inkTerrainClear_=true,inkJetCapture_=false,inkStopped_=false;
    int inkStage_=0;float inkTime_=0;bool inkCapture_=false;int inkFinishFrames_=0;
    #include "SlimeValidation.inl"
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
    bool creatureTest_=false,creatureSnake_=false,creatureMorph_=false,creatureBird_=false,creatureAirHit_=false,creatureDodge_=false;
    unsigned creatureEnd_=0; int snakeCounters_=0; bool relayHit_=false,floorOpaque_=true,creatureWing_=false; float maxDrive_=1;
    bool recordMotion_=false;int motionFrame_=0;float nextMotion_=0;
    bool routeClean_=true,wasHeld_=false;int holdStart_=0,maxHeldHits_=0;float bossTravel_=0;
    bool snakeTest_=false,snakeDamaged_=false,snakeGround_=false,snakeAir_=false;
    int snakeRound_=0,snakePrevious_=0;bool snakeDodged_=false;int snakeHits_=0,snakePattern_=0;
    void UpdateSnakeTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& input=r.get<ControlFrame>(player);input={};input.attackPattern=snakePattern_;
        if(frames_++==0){input.snake=true;Game::GameScene::Update();return;}
        auto& p=r.get<Player>(player);auto& t=r.get<TransformComponent>(player);auto& c=r.get<CreatureBoss>(boss);
        if(c.sweepPhase==1&&snakePrevious_!=1){++snakeRound_;snakeHits_=p.stats.hitsTaken;snakeDodged_=false;}
        if(c.sweepPhase==0&&snakePrevious_!=0){t.translate={0,1.3f,-12};p.velocity={};}
        bool escapeNow=snakePattern_==2?(c.sweepPhase==1&&c.sweepTime>.5f):(c.sweepPhase==2&&c.sweepTime>.035f);
        if(snakeRound_==2&&escapeNow&&!snakeDodged_){input.dodge=true;snakeDodged_=true;}
        if(snakeRound_==2&&snakeDodged_&&c.sweepPhase<=2)input.move=snakePattern_==0?V{0,0,-1}:V{1,0,0};
        auto center=[&](entt::entity e){const auto& a=r.get<TransformComponent>(e).translate;return V{a.x,a.y,a.z}+r.get<Target>(e).offset;};
        V pos{t.translate.x,t.translate.y,t.translate.z};
        if((snakeRound_>=3&&(c.sweepPhase==1||c.sweepPhase==2))||c.sweepPhase==3){
            entt::entity chosen=entt::null;float best=1e9f;
            for(auto e:r.view<Target,TransformComponent>()){
                auto& tg=r.get<Target>(e);if(!tg.active)continue;
                if(c.sweepPhase==3?tg.kind!=Kind::Weakpoint:!r.all_of<SerpentRelay>(e))continue;
                float d=Length(center(e)-pos);if(d<best&&d<Reach(p.mass)+tg.radius){chosen=e;best=d;}
            }
            if(r.valid(chosen)){V dir=Unit(center(chosen)-(pos+V{0,2.8f,0}));input.cameraInput=true;input.yaw=std::atan2(dir.x,dir.z);input.pitch=-std::asin(std::clamp(dir.y,-1.f,1.f));
                input.attack=p.action==Action::Free&&frames_%2==0;}
        }
        if(c.sweepPhase==3&&snakePrevious_!=3){
            if(snakeRound_==1){snakeDamaged_=p.stats.hitsTaken==snakeHits_+1&&p.mass>0;Capture(L"tests/out/snake-hit.png");}
            if(snakeRound_==2)snakeGround_=snakeDodged_&&p.stats.hitsTaken==snakeHits_;
            if(snakeRound_==3)snakeAir_=p.stats.hitsTaken==snakeHits_&&pos.y>4.5f;
        }
        if(c.sweepPhase==1&&c.sweepTime>.6f&&c.sweepTime<.63f)Capture((L"tests/out/snake-warning-"+std::to_wstring(snakeRound_)+L".png").c_str());
        snakePrevious_=c.sweepPhase;Game::GameScene::Update();
        if(frames_%60==0){trace_<<"snake round="<<snakeRound_<<" phase="<<c.sweepPhase<<" pos="<<t.translate.x<<','<<t.translate.y<<','<<t.translate.z<<" hits="<<p.stats.hitsTaken<<" counters="<<p.stats.counters<<'\n';trace_.flush();}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if((snakeRound_>=3&&c.sweepPhase==4)||elapsed>60){
            bool pass=snakeDamaged_&&snakeGround_&&snakeAir_&&p.stats.counters>0;
            std::ofstream("tests/out/snake-smoke.txt")<<(pass?"PASS":"FAIL")<<" hit_recovery="<<snakeDamaged_<<" ground_dodge="<<snakeGround_<<" chain_evade="<<snakeAir_<<" counters="<<p.stats.counters;
            PostQuitMessage(pass?0:2);
        }
    }
    void UpdateCreatureTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& input=r.get<ControlFrame>(player);input={};
        if(frames_==0){input.creature=true;Game::GameScene::Update();++frames_;return;}
        auto& p=r.get<Player>(player);auto& t=r.get<TransformComponent>(player);auto& c=r.get<CreatureBoss>(boss);
        if(c.stage==CreatureStage::Snake)snakeCounters_=p.stats.counters;
        if(c.stage==CreatureStage::Opening&&c.sweepPhase==3&&!creatureWing_){creatureWing_=true;Capture(L"tests/out/creature-wing.png");}
        input.cameraInput=true;input.pitch=-.5f;p.automatic=true;
        V pos{t.translate.x,t.translate.y,t.translate.z};
        auto center=[&](entt::entity e){auto at=r.get<TransformComponent>(e).translate;return V{at.x,at.y,at.z}+r.get<Target>(e).offset;};
        entt::entity selected=entt::null;float best=1e9f;
        for(auto e:r.view<Target,TransformComponent>()){
            const auto& target=r.get<Target>(e);if(!target.active||(target.kind!=Kind::Projectile&&target.kind!=Kind::Weakpoint&&!(c.stage==CreatureStage::Snake&&r.all_of<SerpentRelay>(e))))continue;
            if(c.stage==CreatureStage::Snake&&target.kind==Kind::Weakpoint)continue;
            if(target.kind==Kind::Weakpoint&&!creatureWing_)continue;
            float d=Length(center(e)-pos);if(d>Reach(p.mass)+target.radius)continue;
            if(target.kind==Kind::Weakpoint)d-=40;
            if(d<best){best=d;selected=e;}
        }
        if(r.valid(selected)&&creatureEnd_==0){
            V dir=Unit(center(selected)-(pos+V{0,2.8f,0}));input.yaw=std::atan2(dir.x,dir.z);input.pitch=-std::asin(std::clamp(dir.y,-1.f,1.f));
            input.attack=true; // Keep LMB held across extension, pull, recovery and the next hop.
        }
        if(creatureEnd_==0&&(c.stage==CreatureStage::Opening||(c.stage==CreatureStage::Snake&&c.sweepPhase>0)))input.attack=true;
        if((p.stats.counters>snakeCounters_&&c.stage!=CreatureStage::Snake&&p.stats.projectiles>=3)||creatureEnd_>0){
            if(creatureEnd_==0){creatureAirHit_=pos.y>10&&p.stats.projectiles>=3;Capture(L"tests/out/creature-core.png");}
            input.dodge=creatureEnd_==0;input.move={1,0,0};++creatureEnd_;
        }
        if(input.attack&&!wasHeld_)holdStart_=p.stats.automatic;
        wasHeld_=input.attack;
        Game::GameScene::Update();++frames_;
        if(wasHeld_)maxHeldHits_=std::max(maxHeldHits_,p.stats.automatic-holdStart_);
        bossTravel_=std::max(bossTravel_,Length(c.offset-V{0,0,65}));
        if(c.stage==CreatureStage::Assemble||c.stage==CreatureStage::Descend){
            for(auto e:r.view<SerpentRelay,Target,MeshRendererComponent>())if(e!=p.target)routeClean_&=!r.get<Target>(e).active&&!r.get<MeshRendererComponent>(e).enabled;
            for(auto e:r.view<Feather>())if(e!=p.target)routeClean_=false;
        }
        maxDrive_=std::max(maxDrive_,p.chainDrive);
        for(auto e:r.view<SerpentRelay,Anchor>())relayHit_=relayHit_||r.get<Anchor>(e).cooldown>0;
        auto floor=FindObjectByName("Chrono Creature floor");floorOpaque_=floorOpaque_&&r.valid(floor)&&!r.all_of<CameraOccluder>(floor);
        if(!creatureSnake_&&c.stage==CreatureStage::Snake&&c.timer>1){Capture(L"tests/out/creature-snake.png");creatureSnake_=true;}
        if(!creatureMorph_&&c.form>.4f&&c.form<.6f){Capture(L"tests/out/creature-morph.png");creatureMorph_=true;}
        if(!creatureBird_&&c.stage==CreatureStage::Volley&&c.timer>.6f){Capture(L"tests/out/creature-bird.png");creatureBird_=true;}
        if(creatureEnd_>0&&p.action==Action::Dodge)creatureDodge_=true;
        if(frames_%60==0){trace_<<"creature frame="<<frames_<<" stage="<<static_cast<int>(c.stage)<<" form="<<c.form<<" pos="<<t.translate.x<<','<<t.translate.y<<','<<t.translate.z
            <<" phase="<<c.sweepPhase<<" core_dist="<<Length(center(FindObjectByName("Chrono Weakpoint"))-pos)<<" action="<<static_cast<int>(p.action)<<" feathers="<<p.stats.projectiles<<" counters="<<p.stats.counters<<" failure="<<static_cast<int>(p.failure)<<'\n';trace_.flush();}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(recordMotion_&&c.stage==CreatureStage::Assemble&&elapsed>=nextMotion_){
            nextMotion_=elapsed+.08f;Capture((L"tests/out/motion-"+std::to_wstring(motionFrame_++)+L".png").c_str(),true);}
        if((creatureEnd_>20&&c.stage==CreatureStage::Snake)||elapsed>60||p.mass<=0){
            bool pass=creatureSnake_&&creatureMorph_&&creatureBird_&&creatureWing_&&creatureAirHit_&&creatureDodge_&&c.stage==CreatureStage::Snake;
            int parts=0;for(auto e:r.view<CreaturePart>()){(void)e;++parts;}pass=pass&&parts==224&&relayHit_&&floorOpaque_&&maxDrive_>=1.3f&&routeClean_&&maxHeldHits_>=3&&bossTravel_>20;
            std::ofstream("tests/out/creature-smoke.txt")<<(pass?"PASS":"FAIL")<<" snake="<<creatureSnake_<<" morph="<<creatureMorph_<<" bird="<<creatureBird_
                <<" clean="<<routeClean_<<" held_hits="<<maxHeldHits_<<" travel="<<bossTravel_<<" wing="<<creatureWing_<<" relay="<<relayHit_<<" opaque_floor="<<floorOpaque_<<" max_drive="<<maxDrive_<<" airborne_core="<<creatureAirHit_<<" dodge="<<creatureDodge_<<" parts="<<parts<<" counters="<<p.stats.counters<<" feathers="<<p.stats.projectiles<<" elapsed="<<elapsed;
            PostQuitMessage(pass?0:2);
        }
    }
    void Capture(const wchar_t* name,bool thumbnail=false){
        DirectX::ScratchImage image;
        HRESULT hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        DirectX::ScratchImage resized;
        if(SUCCEEDED(hr)&&thumbnail)hr=DirectX::Resize(*image.GetImage(0,0,0),640,360,DirectX::TEX_FILTER_DEFAULT,resized);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*(thumbnail?resized:image).GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),name);
        trace_<<"capture="<<hr<<'\n';
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);
        auto player=FindObjectByName("Player");GetRegistry().emplace<Game::Chrono::ControlFrame>(player);
        Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true);
        start_=std::chrono::steady_clock::now();trace_.open("tests/out/chrono-trace.txt");
        inkTest_=wcsstr(GetCommandLineW(),L"--ink-smoke")!=nullptr;
        if(inkTest_&&!wcsstr(GetCommandLineW(),L"--horde-smoke")&&!wcsstr(GetCommandLineW(),L"--rogue-smoke"))GetRegistry().get<Game::Chrono::InkPlayer>(player).hordeEnabled=false;
        if(inkTest_){GetRegistry().get<Game::Chrono::InkPlayer>(player).rogue.enabled=wcsstr(GetCommandLineW(),L"--rogue-smoke")!=nullptr;return;}
        snakeTest_=wcsstr(GetCommandLineW(),L"--snake-smoke")!=nullptr;
        snakePattern_=wcsstr(GetCommandLineW(),L"--attack-pattern=2")?2:wcsstr(GetCommandLineW(),L"--attack-pattern=1")?1:0;
        creatureTest_=wcsstr(GetCommandLineW(),L"--creature-smoke")!=nullptr;
        recordMotion_=wcsstr(GetCommandLineW(),L"--record-motion")!=nullptr;
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
        if(inkTest_){UpdateInkTest();return;}
        if(snakeTest_){UpdateSnakeTest();return;}
        if(creatureTest_){UpdateCreatureTest();return;}
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
            input.attack=p.action==Action::Free&&p.lastChainHit>=.10f&&frames_%2==0;
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
