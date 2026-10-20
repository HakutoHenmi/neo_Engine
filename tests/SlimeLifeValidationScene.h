#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Systems/ChronoSystem.h"
#include <chrono>
#include <fstream>
class SlimeLifeValidationScene final:public Game::GameScene {
    Engine::WindowDX* window_=nullptr;
    std::chrono::steady_clock::time_point start_;
    float elapsed_=0;int frames_=0;
    bool noAuto_=false,heldOnce_=false,engulfImage_=false,splitImage_=false,hammerImage_=false;
    bool sword_=false,plane_=false;
    bool splitConserved_=false,damageSplit_=false,cursorFree_=true,labClick_=false,labSave_=false,draining_=false;
    bool hpMonotonic_=true,labFrozen_=true,deathSeen_=false,editorCursorFree_=true;
    bool labLive_=false,labMaterial_=false,labFlight_=false,labFramed_=false;
    float wateryWidth_=0,wateryHeight_=0;bool profileDifference_=false;
    bool freeColor_=false,immediateColor_=true,allFrames_=true,fineSurface_=false;
    int framedSamples_=0;
    bool smoothTurn_=true,flowAligned_=true,stopRelaxed_=false,turnFinite_=true,turnImage_=false;
    int flowSamples_=0;
    bool coreLeads_=false,coreLook_=false,coreLookImage_=false;
    Game::Chrono::Vec stoppedLead_{};
    float maxTurnStep_=0;
    int mixtureStep_=0;
    float stickyDistance_=0,waterDistance_=0,waterHitHp_=0,glueHitHp_=0,mixtureJump_=0,mixtureWait_=0;
    bool trialFrozen_=true,trialWrapped_=false,trialSplit_=true,trialImage_=false;
    bool engulfVolume_=false;
    std::array<bool,4> handImages_{};
    std::ofstream trace_;
    std::array<bool,3> growthForms_{};
    std::array<int,3> growthSwings_{},growthHits_{};
    uint64_t growthLastSwing_=0;
    bool growthSingleSweep_=true,growthHammerAttackImage_=false,growthSwordAttackImage_=false;
    bool growthPersistent_=true,growthManualReturn_=true;
    bool growthTackle_=false,growthInitial_=false,growthEarlyImage_=false,growthLateImage_=false;
    float growthStartScale_=1;
    int stressPhase_=0,stressFrame_=0;
    std::vector<double> stressTimes_;
    std::array<double,Engine::Renderer::kFluidProfileStageCount> stressGpu_{};
    std::chrono::steady_clock::time_point stressPrevious_{};
    std::ofstream stressReport_;
    int motionPhase_=0,motionFrame_=0;
    Game::Chrono::Vec motionOrigin_{},motionPrevious_{};
    float motionDistance_=0;
    float motionAge_=0,motionCameraError_=0,motionClockError_=0,motionMinDt_=1,motionMaxDt_=0;
    void UpdateMotion(){
        using namespace Game;using namespace Game::Chrono;
        constexpr float masses[]={100,544,1200,544,1200,1200,1200};
        constexpr LifeAction forms[]={LifeAction::Idle,LifeAction::Idle,LifeAction::Idle,LifeAction::Hammer,LifeAction::Hammer,LifeAction::Sword,LifeAction::Plane};
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;input.move={.25f,0,1};
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());
        if(motionFrame_==0){if(motionPhase_==0)motionOrigin_=life.bodies[0].at;life.bodies={};auto& body=life.bodies[0];body.at=motionOrigin_;body.mass=masses[motionPhase_];body.health=body.maxHealth=100;body.particles=6600;body.age=2;
            GetComponents().get<Player>(player).velocity={};motionPrevious_=motionOrigin_;motionDistance_=0;
            if(forms[motionPhase_]!=LifeAction::Idle)LifeEnterWeapon(body,forms[motionPhase_],{0,0,1});
            life.absorbedMass=masses[motionPhase_]-100;GetComponents().get<InkPlayer>(player).swarmKills=0;
            life.props.clear();++life.revision;motionAge_=0;motionCameraError_=motionClockError_=0;motionMinDt_=1;motionMaxDt_=0;}
        GetComponents().get<Player>(player).invincible=60;
        float before=life.age;Game::GameScene::Update();float dt=life.age-before;motionAge_+=dt;++motionFrame_;
        motionDistance_+=LifeDistance(motionPrevious_,life.bodies[0].at);motionPrevious_=life.bodies[0].at;
        auto* renderer=GetRenderer();if(motionFrame_==1){renderer->fluidMorphSeedPending_=true;renderer->fluidMorphRetargetMask_=Engine::Renderer::kFluidMorphAllBodies;}
        if(motionAge_>2){float wanted=std::max(0.f,(LifeScale(masses[motionPhase_])-1)*6);
            motionCameraError_=std::max(motionCameraError_,std::abs(GetComponents().get<InkPlayer>(player).cameraPullback-wanted));
            motionClockError_=std::max(motionClockError_,std::abs(renderer->fluidMorphDt_-dt));motionMinDt_=std::min(motionMinDt_,dt);motionMaxDt_=std::max(motionMaxDt_,dt);}
        if(motionFrame_==190&&motionPhase_==3)Capture(L"tests/out/slime-life-shadow-moving.png");
        if(motionFrame_<200){Sleep(motionFrame_%2?3:18);return;}
        bool pass=motionCameraError_<.002f&&motionClockError_<.00001f&&motionDistance_>20;
        std::ofstream report("tests/out/slime-life-motion.txt",motionPhase_?std::ios::app:std::ios::out);
        report<<"phase="<<motionPhase_<<" mass="<<masses[motionPhase_]<<" form="<<int(forms[motionPhase_])<<" distance="<<motionDistance_<<" dt_min="<<motionMinDt_<<" dt_max="<<motionMaxDt_<<" camera_distance_error="<<motionCameraError_<<" fluid_clock_error="<<motionClockError_<<" pass="<<pass<<'\n';
        if(!pass){report<<"FAIL movement camera/solver clocks\n";report.flush();PostQuitMessage(2);return;}
        if(++motionPhase_==7){report<<"PASS variable-frame movement, growth camera and fluid clocks\n";report.flush();PostQuitMessage(0);return;}motionFrame_=0;
    }
    void UpdateStress(){
        using namespace Game;using namespace Game::Chrono;
        constexpr float masses[]={100,300,600,1200,1200,1200,960};
        auto now=std::chrono::steady_clock::now();double ms=stressPrevious_.time_since_epoch().count()?std::chrono::duration<double,std::milli>(now-stressPrevious_).count():0;stressPrevious_=now;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());
        if(stressFrame_==0){
            if(!stressReport_.is_open())stressReport_.open("tests/out/slime-life-stress.txt");
            auto at=life.bodies[0].at;life.bodies={};auto& body=life.bodies[0];body.at=at;body.mass=masses[stressPhase_];body.health=body.maxHealth=100;body.particles=6600;body.age=2;
            if(stressPhase_==4||stressPhase_==5)LifeEnterWeapon(body,stressPhase_==4?LifeAction::Hammer:LifeAction::Sword,{0,0,1});
            if(stressPhase_==6){body.particles=4200;for(size_t i=1;i<LifeBodyCount;++i){auto& child=life.bodies[i];float angle=float(i)*6.2831853f/12;child.at=at+Vec{std::sin(angle)*12,0,std::cos(angle)*12};child.mass=20;child.health=child.maxHealth=20;child.particles=200;child.cooldown=60;}}
            life.absorbedMass=life.TotalMass()-100;life.props.clear();++life.revision;stressTimes_.clear();stressGpu_={};
        }
        GetComponents().get<Player>(player).invincible=60;
        if(stressFrame_>=60&&stressFrame_<120){input.move={.35f,0,1};input.yaw=float(stressFrame_-60)*.012f;}
        Game::GameScene::Update();++stressFrame_;
        auto* renderer=GetRenderer();renderer->SetFluidProfilerEnabled(true);
        if(stressFrame_==1){renderer->fluidMorphSeedPending_=true;renderer->fluidMorphRetargetMask_=Engine::Renderer::kFluidMorphAllBodies;}
        if(stressFrame_>48){stressTimes_.push_back(ms);const auto& gpu=renderer->GetFluidProfileStats();for(size_t i=0;i<stressGpu_.size();++i)stressGpu_[i]+=gpu.lastMs[i];}
        if(stressFrame_<160)return;
        std::sort(stressTimes_.begin(),stressTimes_.end());double total=0;for(auto t:stressTimes_)total+=t;
        bool finite=true;for(auto p:renderer->GetFluidBodySnapshot().offsets)finite&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<100&&std::abs(p.y)<100&&std::abs(p.z)<100;
        stressReport_<<"phase="<<stressPhase_<<" mass="<<masses[stressPhase_]<<" samples="<<stressTimes_.size()<<" frame_mean="<<total/stressTimes_.size()<<" frame_p95="<<stressTimes_[stressTimes_.size()*95/100]<<" cell="<<renderer->volumePreviousVoxelSize_<<" finite="<<finite;
        for(size_t i=0;i<stressGpu_.size();++i)stressReport_<<" gpu"<<i<<"="<<stressGpu_[i]/stressTimes_.size();stressReport_<<'\n';stressReport_.flush();
        if(stressPhase_==3)Capture(L"tests/out/slime-life-max-size.png");
        if(!finite){stressReport_<<"FAIL nonfinite/escaped particles\n";stressReport_.flush();PostQuitMessage(2);return;}
        if(++stressPhase_==7){stressReport_<<"PASS all growth/weapon/12-child stress phases\n";stressReport_.flush();PostQuitMessage(0);return;}stressFrame_=0;
    }
    void UpdateGrowth(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        const auto& before=GetChronoSystem()->LifeState();const auto& body=before.bodies[0];
        auto beforeAction=body.action;float beforeActionAge=body.actionAge;int beforeHits=before.hammerHits;
        GetComponents().get<Player>(player).invincible=60; // Observe progression without damage splits changing size.
        if(LifeWeaponForm(body.action)){
            const SwarmEnemy* target=nullptr;float nearest=1e9f;
            for(const auto& enemy:GetChronoSystem()->LifeEnemies())if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,body.at);if(d<nearest){target=&enemy;nearest=d;}}
            if(target){auto direction=target->at-body.at;input.yaw=std::atan2(direction.x,direction.z)+(body.action==LifeAction::Plane?3.14159265f:0);}
            int form=int(body.action)-int(LifeAction::Hammer);
            if(body.actionAge<1.1f||(body.actionAge>2&&body.actionAge<3.5f))input.move={.2f,0,.6f};
            if(body.actionAge>1.5f){
                input.attack=body.action==LifeAction::Hammer?body.actionAge<4.5f:frames_%6<2;
                if(body.actionAge>4.8f&&growthSwings_[size_t(form)]>=3)input.returnToSlime=true;
            }
        }
        if(frames_==0){growthInitial_=before.ChildCount()==0&&before.ChildLimit()==2&&body.mass==LifeStartMass;growthStartScale_=LifeScale(body.mass);}
        if(frames_==2){input.attack=true;input.morphForm=3;}
        else if(frames_>10&&body.action==LifeAction::Idle&&body.cooldown<=0){
            int test=-1;for(int i=0;i<3;++i)if(!growthForms_[size_t(i)]&&LifeCanMorph(body.mass,LifeAction(int(LifeAction::Hammer)+i))){test=i;break;}
            if(test>=0){input.morphForm=test+1;input.attack=!before.previousFire;}
            else {const LifeCollectible* target=nullptr;float nearest=1e9f;
                for(const auto& prop:before.props)if(prop.active&&LifeCanAbsorb(body.mass,prop.required)){
                    float distance=LifeDistance(body.at,prop.at);if(distance<nearest){nearest=distance;target=&prop;}}
                if(target){auto delta=target->at-body.at;input.yaw=std::atan2(delta.x,delta.z);
                    if(nearest>before.recipe.AbsorbRange()*LifeScale(body.mass)*.65f)input.move={0,0,1};
                    else input.absorb=!before.previousAbsorb;}
            }
        }
        Game::GameScene::Update();++frames_;
        const auto& after=GetChronoSystem()->LifeState();const auto& grown=after.bodies[0];
        growthTackle_|=after.tackleTime>0&&grown.mass<LifeHammerMass&&grown.action!=LifeAction::Plane;
        for(int i=0;i<3;++i)if(grown.action==LifeAction(int(LifeAction::Hammer)+i)){
            if(!LifeCanMorph(grown.mass,grown.action)){std::ofstream("tests/out/slime-life-smoke.txt")<<"FAIL locked weapon entered";PostQuitMessage(2);return;}
            if(grown.weapon.active&&grown.attackSerial!=growthLastSwing_){growthLastSwing_=grown.attackSerial;++growthSwings_[size_t(i)];}
            if(i==1&&grown.weapon.active)growthSingleSweep_&=grown.weapon.age<=LifeWeaponDuration(LifeAction::Sword)+.05f;
            if(grown.weapon.active&&grown.weapon.age>=.29f&&grown.weapon.age<.35f){
                if(i==0&&!growthHammerAttackImage_){Capture(L"tests/out/slime-life-hammer-downswing.png");growthHammerAttackImage_=true;}
                if(i==1&&!growthSwordAttackImage_){Capture(L"tests/out/slime-life-sword-sweep.png");growthSwordAttackImage_=true;}}
            if(grown.actionAge>1.25f&&grown.actionAge<1.5f&&!growthForms_[size_t(i)]){
                const wchar_t* images[]={L"tests/out/slime-life-held-hammer.png",L"tests/out/slime-life-held-sword.png",L"tests/out/slime-life-held-plane.png"};
                if(beforeActionAge<=1.25f)Capture(images[i]);}
        }
        if(LifeWeaponForm(beforeAction)){
            int form=int(beforeAction)-int(LifeAction::Hammer);growthHits_[size_t(form)]+=after.hammerHits-beforeHits;
            growthPersistent_&=grown.action==beforeAction||input.returnToSlime;
        }
        if(input.returnToSlime){
            int selected=int(beforeAction)-int(LifeAction::Hammer);
            if(selected>=0&&selected<3){growthForms_[size_t(selected)]=true;growthManualReturn_&=grown.action==LifeAction::Restore&&!grown.weapon.active;}
        }
        growthInitial_&=after.ChildCount()==0&&after.splits==0;
        if(frames_>120&&!growthEarlyImage_){Capture(L"tests/out/slime-life-growth-early.png");growthEarlyImage_=true;}
        if(grown.mass>=600&&growthForms_[2]&&grown.action==LifeAction::Idle&&!growthLateImage_){Capture(L"tests/out/slime-life-growth-late.png");growthLateImage_=true;}
        if(frames_%60==0){trace_<<"growth mass="<<grown.mass<<" children="<<after.ChildCount()<<" absorbed="<<after.absorbedMass<<" forms="<<growthForms_[0]<<growthForms_[1]<<growthForms_[2]<<" action="<<int(grown.action)<<" ready="<<grown.weapon.ready<<" taps="<<growthSwings_[0]<<" sweeps="<<growthSwings_[1]<<'\n';trace_.flush();}
        bool complete=growthLateImage_&&growthForms_[0]&&growthForms_[1];
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(complete||real>200||GetChronoSystem()->Finished()){
            bool pass=complete&&growthTackle_&&growthInitial_&&growthPersistent_&&growthManualReturn_&&growthSingleSweep_&&growthSwings_[1]>=3&&growthHammerAttackImage_&&growthSwordAttackImage_&&LifeScale(grown.mass)>growthStartScale_*2.1f;
            uint32_t particles=0;for(const auto& b:after.bodies)particles+=b.particles;pass&=particles==Engine::Renderer::kPlayerFluidParticles;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" growth_initial="<<growthInitial_<<" early_tackle="<<growthTackle_<<" mass="<<grown.mass<<" children="<<after.ChildCount()<<" scale="<<LifeScale(grown.mass)<<" hammer="<<growthForms_[0]<<" sword="<<growthForms_[1]<<" plane="<<growthForms_[2]<<" particles="<<particles<<" collected="<<after.absorbedMass<<" persistent="<<growthPersistent_<<" manual_return="<<growthManualReturn_<<" hammer_taps="<<growthSwings_[0]<<" sword_single_sweeps="<<growthSwings_[1]<<" plane_boosts="<<growthSwings_[2]<<" hammer_hits="<<growthHits_[0]<<" sword_hits="<<growthHits_[1];
            PostQuitMessage(pass?0:2);
        }
    }
    void Capture(const wchar_t* path){
        DirectX::ScratchImage image;auto result=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(result))result=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);
        trace_<<"capture="<<result<<'\n';
    }
public:
    void Initialize(Engine::WindowDX* window,const Engine::SceneParameters&)override {
        window_=window;Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";Game::GameScene::Initialize(window,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));start_=std::chrono::steady_clock::now();trace_.open("tests/out/slime-life-trace.txt");
        auto* renderer=Engine::Renderer::GetInstance();auto settings=renderer->GetGraphicsSettings();settings.rtShadows=settings.rtReflections=settings.rtIndirect=false;renderer->ApplyGraphicsSettings(settings);
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        if(wcsstr(GetCommandLineW(),L"--slime-life-motion")){UpdateMotion();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-stress")){UpdateStress();return;}
        // The complete gameplay smoke follows the new size-gated progression.
        // Lab, mixture and turn diagnostics keep their independent scenarios.
        if(!wcsstr(GetCommandLineW(),L"--slime-life-lab-only")&&!wcsstr(GetCommandLineW(),L"--slime-life-turn-only")&&!wcsstr(GetCommandLineW(),L"--slime-life-mixture-only")){UpdateGrowth();return;}
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        const auto& life=GetChronoSystem()->LifeState();const auto& body=life.bodies[0];elapsed_=life.age;
        if(elapsed_>.6f&&elapsed_<.8f)noAuto_=life.absorbed==0;
        if(elapsed_>.8f&&elapsed_<2.5f)input.absorb=true;
        if(body.action==LifeAction::Engulf&&body.actionAge>.42f&&!engulfImage_){Capture(L"tests/out/slime-life-engulf.png");engulfImage_=true;}
        if(elapsed_>2.6f&&elapsed_<2.9f)heldOnce_=life.absorbed==1;
        if(life.splits>0&&elapsed_>3.8f&&!splitImage_){Capture(L"tests/out/slime-life-split.png");splitImage_=true;}
        bool turnOnly=wcsstr(GetCommandLineW(),L"--slime-life-turn-only")!=nullptr;
        bool mixtureOnly=wcsstr(GetCommandLineW(),L"--slime-life-mixture-only")!=nullptr;
        bool labOnly=wcsstr(GetCommandLineW(),L"--slime-life-lab-only")!=nullptr||turnOnly||mixtureOnly;
        if(frames_==20||(frames_==90&&!labOnly))input.lab=true;
        if(turnOnly&&frames_==271)input.lab=true;
        if(frames_==78)Capture(L"tests/out/slime-life-lab.png");
        if(frames_==34)Capture(L"tests/out/slime-life-lab-rest.png");
        if(frames_==33)Capture(L"tests/out/slime-life-lab-color.png");
        if(frames_==60)Capture(L"tests/out/slime-life-lab-jump.png");
        if(frames_==85)Capture(L"tests/out/slime-life-lab-land.png");
        if(labOnly&&(frames_==150||frames_==240)){
            const auto& snapshot=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();float loX=1e9f,hiX=-1e9f,loY=1e9f,hiY=-1e9f;
            for(auto p:snapshot.offsets){loX=std::min(loX,p.x);hiX=std::max(hiX,p.x);loY=std::min(loY,p.y);hiY=std::max(hiY,p.y);}
            float width=hiX-loX,height=hiY-loY;
            if(frames_==150){wateryWidth_=width;wateryHeight_=height;Capture(L"tests/out/slime-life-lab-watery.png");}
            else {profileDifference_=wateryWidth_>width*1.2f&&wateryHeight_<height*.9f;Capture(L"tests/out/slime-life-lab-bouncy.png");}
            trace_<<"profile="<<(frames_==150?"water":"bouncy")<<" width="<<width<<" height="<<height<<'\n';trace_.flush();
        }
        if(frames_==60){const auto& snapshot=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();float low=1e9f;
            float top=1e9f,bottom=-1e9f,left=1e9f,right=-1e9f;auto viewProjection=DirectX::XMMatrixMultiply(GetCamera().View(),GetCamera().Proj());
            for(auto point:snapshot.offsets){low=std::min(low,point.y+snapshot.core.y);auto world=point+snapshot.core;DirectX::XMFLOAT3 ndc;
                DirectX::XMStoreFloat3(&ndc,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(world.x,world.y,world.z,1),viewProjection));
                float x=(1+ndc.x)*640,y=(1-ndc.y)*360;top=std::min(top,y);bottom=std::max(bottom,y);left=std::min(left,x);right=std::max(right,x);}
            labFlight_=life.preview.air>.8f&&low<life.preview.at.y-1.5f;
            labFramed_=top>208&&bottom<521&&left>328&&right<952;
            trace_<<"preview frame="<<left<<","<<top<<","<<right<<","<<bottom<<'\n';
            trace_<<"preview flight air="<<life.preview.air<<" underside="<<low-1201.25f<<" height="<<life.preview.at.y-1201.25f<<'\n';}
        if(life.splits>0){
            // Protect the real, already split body while exercising the three forms.
            // End that protection for the subsequent real-enemy game-over check.
            draining_=life.hammerHits>0&&sword_&&plane_&&life.childHits>0;
            GetComponents().get<Player>(player).invincible=draining_?0.f:60.f;
        }
        if(elapsed_>3&&!turnOnly){
            const SwarmEnemy* target=nullptr;float distance=1e9f;
            for(const auto& enemy:GetChronoSystem()->LifeEnemies())if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,body.at);if(d<distance){distance=d;target=&enemy;}}
            if(target){auto delta=target->at-body.at;input.yaw=std::atan2(delta.x,delta.z);input.move=distance>(draining_?2.f:5.f)?Vec{0,0,1}:Vec{};}
            input.morphForm=life.hammerHits==0?1:!sword_?2:3;
            if(life.splits>0&&!draining_&&body.action==LifeAction::Idle&&body.cooldown<=0&&frames_%2==0)input.attack=true;
            if(body.action==LifeAction::Hammer&&body.actionAge>.35f&&!hammerImage_){Capture(L"tests/out/slime-life-hammer.png");hammerImage_=true;}
            if(body.action==LifeAction::Sword&&body.actionAge>.7f)sword_=true;
            if(body.action==LifeAction::Plane&&body.actionAge>.7f)plane_=true;
        }
        if(!turnOnly&&elapsed_>7&&elapsed_<7.1f)input.jump=true;
        if(!turnOnly&&elapsed_>7.25f&&elapsed_<7.35f)input.jump=true;
        if(turnOnly&&!life.lab&&frames_>271){input.absorb=false;input.move=elapsed_<7.2f?Vec{0,0,1}:Vec{};input.yaw=elapsed_>8?3.9269908f:elapsed_>5.8f?.7853982f:float(int(std::max(0.f,elapsed_-1)/1.2f))*1.5707963f;GetComponents().get<Player>(player).invincible=60;
            if(elapsed_>7.8f&&elapsed_<8)stoppedLead_=body.coreLead;
            if(elapsed_>8.25f&&!coreLookImage_){Capture(L"tests/out/slime-life-core-look.png");coreLookImage_=true;}
            if(elapsed_>2.45f&&!turnImage_){Capture(L"tests/out/slime-life-watery-turn.png");turnImage_=true;}}
        auto previousHeading=body.liquidForward;
        float beforeMass=life.ReservedMass(),beforeHp=body.health,beforeAge=life.age;int beforeSplits=life.splits;
        int beforeHits=GetComponents().get<Player>(player).stats.hitsTaken;
        POINT cursor{};bool cursorCheck=frames_>=22&&frames_<=24;
        if(cursorCheck){RECT rect{};GetClientRect(window_->GetHwnd(),&rect);cursor={rect.right/4,rect.bottom/3};ClientToScreen(window_->GetHwnd(),&cursor);BOOL moved=SetCursorPos(cursor.x,cursor.y);trace_<<"cursor set="<<moved<<" error="<<GetLastError()<<" expected="<<cursor.x<<","<<cursor.y;}
        if(frames_==23){SetPaused(true);SetPaused(false);}
        Game::GameScene::Update();++frames_;
        const auto& after=GetChronoSystem()->LifeState();
        if(after.lab&&frames_>=32&&frames_%10==0){
            const auto& snapshot=GetRenderer()->GetFluidBodySnapshot();auto matrix=DirectX::XMMatrixMultiply(GetCamera().View(),GetCamera().Proj());
            for(auto point:snapshot.offsets){auto p=point+snapshot.core;DirectX::XMFLOAT3 clip;DirectX::XMStoreFloat3(&clip,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x,p.y,p.z,1),matrix));
                float x=(1+clip.x)*640,y=(1-clip.y)*360;allFrames_&=x>338&&x<942&&y>213&&y<515;}
            ++framedSamples_;}
        if(frames_==31){auto rgb=after.recipe.color.ToRGB();const auto& tint=GetRenderer()->fluidMorphTargets_.front().color;
            freeColor_=std::abs(rgb.x-tint.x)<.001f&&std::abs(rgb.z-tint.z)<.001f&&rgb.z>rgb.y&&rgb.y>rgb.x;}
        if(frames_==149)fineSurface_=GetRenderer()->volumePreviousVoxelSize_<.25f;
        if(frames_==32){auto* renderer=Engine::Renderer::GetInstance();const auto& targets=renderer->fluidMorphTargets_;
            labMaterial_=targets.size()==6600&&targets[0].shape.w==1&&std::abs(targets[0].material.y-after.recipe.Viscosity())<.001f&&std::abs(after.recipe.Viscosity()-SlimeRecipe{}.Viscosity())>.1f;
            labLive_=after.labAge>0&&renderer->GetFluidBodySnapshot().offsets.size()==6600&&GetCamera().GetPosition().y>1000;}
        if(cursorCheck){POINT actual{};BOOL read=GetCursorPos(&actual);CURSORINFO info{};info.cbSize=sizeof(info);GetCursorInfo(&info);trace_<<" read="<<read<<" actual="<<actual.x<<","<<actual.y<<" flags="<<info.flags<<" pad="<<Engine::Input::GetInstance()->UsingGamepad()<<'\n';trace_.flush();
            // The user can move the physical mouse during this native check.
            // Small movement is fine; a forced return to the centre is hundreds
            // of pixels from this deliberately off-centre test position.
            cursorFree_&=read&&std::abs(actual.x-cursor.x)<=12&&std::abs(actual.y-cursor.y)<=12&&(info.flags&CURSOR_SHOWING)!=0;labFrozen_&=after.lab&&after.age==beforeAge;}
        if(turnOnly&&!after.lab&&frames_>272){const auto& moving=after.bodies[0];float step=std::abs(std::atan2(moving.liquidForward.x,moving.liquidForward.z)-std::atan2(previousHeading.x,previousHeading.z));step=std::abs(std::remainder(step,6.2831853f));maxTurnStep_=std::max(maxTurnStep_,step);smoothTurn_&=step<=12*(after.age-beforeAge)+.003f;
            float speed=Length(Vec{moving.velocity.x,0,moving.velocity.z});float phase=std::fmod(std::max(0.f,after.age-1),1.2f);
            if(after.age>1&&after.age<7&&phase>.7f&&speed>2){Vec d={moving.velocity.x/speed,0,moving.velocity.z/speed};Vec wanted{d.x*d.x-d.z*d.z,0,2*d.x*d.z};flowAligned_&=Dot(moving.flowShape,wanted)>.025f;++flowSamples_;}
            if(after.age>8.5f){stopRelaxed_=Length(moving.flowShape)<.002f;coreLook_=Length(moving.coreLead-stoppedLead_)>.6f&&Length(moving.fluidOffset)<.01f;}
            if(speed>5&&phase>.7f){Vec mean{};const auto& particles=GetRenderer()->GetFluidBodySnapshot().offsets;for(auto p:particles)mean=mean+Vec{p.x,0,p.z};if(!particles.empty())mean=mean*(1.f/particles.size());Vec direction=Unit(Vec{moving.velocity.x,0,moving.velocity.z});coreLeads_|=Dot(moving.coreLead-mean,direction)>.25f;}
            for(auto p:GetRenderer()->GetFluidBodySnapshot().offsets)turnFinite_&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<18&&std::abs(p.z)<18;
            if(after.age>9){bool pass=smoothTurn_&&flowAligned_&&flowSamples_>10&&stopRelaxed_&&coreLeads_&&coreLook_&&turnFinite_&&freeColor_&&immediateColor_;std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" smooth_turn="<<smoothTurn_<<" flow_aligned="<<flowAligned_<<" samples="<<flowSamples_<<" stop_relaxed="<<stopRelaxed_<<" core_leads="<<coreLeads_<<" stationary_look="<<coreLook_<<" finite="<<turnFinite_<<" max_step_radians="<<maxTurnStep_<<" free_color="<<freeColor_<<" immediate_color="<<immediateColor_;PostQuitMessage(pass?0:2);return;}}
        if(mixtureOnly&&frames_>271){
            trialFrozen_&=after.lab&&after.age==beforeAge&&after.bodies[0].health==beforeHp&&after.ReservedMass()==beforeMass;
            if(mixtureStep_==2&&after.trial==SlimeTrial::None){stickyDistance_=after.trialDistance;mixtureStep_=3;}
            if(mixtureStep_==5){if(!trialImage_&&after.trialAge>.25f){Capture(L"tests/out/slime-life-trial-move.png");trialImage_=true;}if(after.trial==SlimeTrial::None){waterDistance_=after.trialDistance;mixtureStep_=6;}}
            if(mixtureStep_==7){trialWrapped_|=after.preview.action==LifeAction::Engulf&&GetRenderer()->fluidMorphTargets_.front().shape.w==0;
                if(after.preview.action==LifeAction::Engulf){float phase=after.preview.actionAge/after.recipe.AbsorbDuration();const float timings[]={.29f,.56f,.77f,.95f};const wchar_t* names[]={L"tests/out/slime-life-hands-open.png",L"tests/out/slime-life-hands-wrap.png",L"tests/out/slime-life-hands-close.png",L"tests/out/slime-life-hands-pull.png"};
                    for(size_t i=0;i<4;++i)if(phase>=timings[i]&&!handImages_[i]){Capture(names[i]);handImages_[i]=true;}
                    float loX=100,hiX=-100,hiY=0;const auto& snapshot=GetRenderer()->GetFluidBodySnapshot();for(auto p:snapshot.offsets){auto world=p+snapshot.core;loX=std::min(loX,world.x);hiX=std::max(hiX,world.x);hiY=std::max(hiY,world.y-1200);}
                    engulfVolume_|=loX<-2.8f&&hiX>2.8f&&hiY>2.4f;}
                if(after.trialAge>.3f&&after.trialAge<.36f)Capture(L"tests/out/slime-life-trial-absorb.png");if(after.trial==SlimeTrial::None)mixtureStep_=8;}
            if(mixtureStep_==9||mixtureStep_==12){if(after.previewChild.mass>0){trialSplit_&=std::abs(after.preview.mass+after.previewChild.mass-100)<.001f&&after.preview.particles+after.previewChild.particles==6600;
                    if(mixtureStep_==9)waterHitHp_=after.preview.health;else glueHitHp_=after.preview.health;}
                if(after.trialAge>.25f&&after.trialAge<.31f)Capture(L"tests/out/slime-life-trial-hit.png");if(after.trial==SlimeTrial::None)++mixtureStep_;}
            if(mixtureStep_==15){mixtureJump_=std::max(mixtureJump_,after.preview.at.y-1201.25f);if(after.labAge-mixtureWait_>2.5f){
                bool pass=waterDistance_>stickyDistance_*2&&glueHitHp_>waterHitHp_+15&&mixtureJump_>4&&trialWrapped_&&engulfVolume_&&trialSplit_&&trialFrozen_&&allFrames_&&freeColor_;
                std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" sticky_distance="<<stickyDistance_<<" water_distance="<<waterDistance_<<" water_hit_hp="<<waterHitHp_<<" glue_hit_hp="<<glueHitHp_<<" jump_height="<<mixtureJump_<<" wrapping="<<trialWrapped_<<" hands_3d="<<engulfVolume_<<" split_conserved="<<trialSplit_<<" stage_frozen="<<trialFrozen_<<" framed="<<allFrames_;PostQuitMessage(pass?0:2);return;}}
        }
        if(frames_==270&&labOnly&&!turnOnly&&!mixtureOnly){
            bool pass=cursorFree_&&editorCursorFree_&&labClick_&&labSave_&&labFrozen_&&labLive_&&labMaterial_&&labFlight_&&labFramed_&&profileDifference_&&freeColor_&&immediateColor_&&allFrames_&&fineSurface_;std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" cursor_free="<<cursorFree_<<" editor_cursor="<<editorCursorFree_<<" lab_click="<<labClick_<<" lab_save="<<labSave_<<" lab_frozen="<<labFrozen_<<" live="<<labLive_<<" material="<<labMaterial_<<" flight="<<labFlight_<<" framed="<<labFramed_<<" all_frames="<<allFrames_<<" samples="<<framedSamples_<<" free_color="<<freeColor_<<" immediate_color="<<immediateColor_<<" fine_surface="<<fineSurface_<<" mixture_shape="<<profileDifference_;PostQuitMessage(pass?0:2);return;
        }
        hpMonotonic_&=after.bodies[0].health<=beforeHp+.001f;
        if(after.splits>beforeSplits){damageSplit_=GetComponents().get<Player>(player).stats.hitsTaken>beforeHits;splitConserved_=std::abs(after.ReservedMass()-beforeMass)<.01f;}
        if(frames_%30==0){trace_<<"t="<<after.age<<" hp="<<after.bodies[0].health<<" mass="<<after.TotalMass()<<" absorbed="<<after.absorbed<<" action="<<int(after.bodies[0].action)<<" a="<<after.bodies[0].actionAge<<" split="<<after.splits<<" ai="<<after.childHits<<" hammer="<<after.hammerHits<<" drain="<<draining_<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(GetChronoSystem()->Finished()||after.age>70||real>200){
            bool dead=GetChronoSystem()->Finished()&&after.bodies[0].health==0;
            if(dead&&!deathSeen_){deathSeen_=true;return;}
            bool pass=noAuto_&&heldOnce_&&damageSplit_&&splitConserved_&&cursorFree_&&editorCursorFree_&&labClick_&&labSave_&&labFrozen_&&labLive_&&labMaterial_&&labFlight_&&hpMonotonic_&&dead&&after.childHits>0&&after.hammerHits>0&&engulfImage_&&hammerImage_&&sword_&&plane_;
            Capture(L"tests/out/slime-life-game-over.png");
            auto summary=after;input.restart=true;Game::GameScene::Update();const auto& restarted=GetChronoSystem()->LifeState();
            bool reset=std::abs(restarted.TotalMass()-LifeStartMass)<.01f&&restarted.bodies[1].mass==0&&restarted.bodies[0].health==100&&!GetChronoSystem()->Finished()&&restarted.recipe.mix==summary.recipe.mix&&Length(restarted.recipe.color.ToRGB()-summary.recipe.color.ToRGB())<.001f;
            trace_<<"reset mass="<<restarted.TotalMass()<<" hp="<<restarted.bodies[0].health<<" water="<<restarted.recipe.mix[0]<<" expected="<<summary.recipe.mix[0]<<'\n';trace_.flush();
            pass=pass&&reset;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" manual="<<noAuto_<<" held_once="<<heldOnce_<<" damage_split="<<damageSplit_<<" split_mass="<<splitConserved_<<" cursor_free="<<cursorFree_<<" editor_cursor="<<editorCursorFree_<<" lab_click="<<labClick_<<" lab_save="<<labSave_<<" lab_frozen="<<labFrozen_<<" live="<<labLive_<<" material="<<labMaterial_<<" flight="<<labFlight_<<" hp_monotonic="<<hpMonotonic_<<" game_over="<<dead<<" child_hits="<<summary.childHits<<" hammer_hits="<<summary.hammerHits<<" sword="<<sword_<<" plane="<<plane_<<" reset="<<reset<<" frames="<<frames_;
            PostQuitMessage(pass?0:2);
        }
    }
    void Draw()override {
#ifdef USE_IMGUI
        bool turnOnly=wcsstr(GetCommandLineW(),L"--slime-life-turn-only")!=nullptr;
        bool mixtureOnly=wcsstr(GetCommandLineW(),L"--slime-life-mixture-only")!=nullptr;
        bool labOnly=wcsstr(GetCommandLineW(),L"--slime-life-lab-only")!=nullptr||turnOnly||mixtureOnly;
        bool profileClick=labOnly&&(frames_==95||frames_==100||(!turnOnly&&frames_==190)||frames_==104||frames_==106||frames_==108||frames_==110);
        bool tintClick=frames_==26||frames_==27||frames_==28;
        bool mixtureClick=mixtureOnly&&frames_>271&&(mixtureStep_==0||mixtureStep_==1||mixtureStep_==3||mixtureStep_==4||mixtureStep_==6||mixtureStep_==8||mixtureStep_==10||mixtureStep_==11||mixtureStep_==13||mixtureStep_==14);
        if((frames_==23||frames_==24||frames_==35||frames_==40||profileClick||tintClick||mixtureClick)&&GetChronoSystem()->SlimeLabOpen()&&ImGui::GetCurrentContext()){
            float x=frames_==23?286.f:frames_==24?167.f:frames_==35?439.f:639.f,y=frames_==23?298.f:frames_==24?650.f:542.f;
            if(profileClick){x=frames_==95?839.f:frames_==100?106.f:244.f;y=frames_==95?542.f:546.f;}
            if(frames_==104||frames_==106){x=239;y=222;}
            if(frames_==108||frames_==110){x=239;y=374;}
            if(tintClick){x=frames_==26?1131.f:frames_==27?1165.f:167.f;y=frames_==26?353.f:frames_==27?242.f:650.f;}
            if(mixtureClick){x=439;y=576;switch(mixtureStep_){case 0:case 10:x=106;y=510;break;case 3:x=106;y=546;break;case 6:x=639;break;case 8:case 11:x=839;break;case 13:x=244;y=546;break;case 14:x=639;y=542;mixtureWait_=GetChronoSystem()->LifeState().labAge;break;}}
            float scale=float(Engine::WindowDX::kW)/1280;SetEditorViewport({0,0},{float(Engine::WindowDX::kW),float(Engine::WindowDX::kH)},x*scale,y*scale);
            auto& io=ImGui::GetIO();bool down=io.MouseDown[0];float duration=io.MouseDownDuration[0];io.MouseDown[0]=true;io.MouseDownDuration[0]=0;
            int before=GetChronoSystem()->LifeState().recipe.mix[0];Game::GameScene::Draw();io.MouseDown[0]=down;io.MouseDownDuration[0]=duration;
            if(mixtureClick)++mixtureStep_;
            if(frames_==23)labClick_=GetChronoSystem()->LifeState().recipe.mix[0]==before+5;
            else if(frames_==24)labSave_=std::string(GetChronoSystem()->LifeState().message)=="RECIPE SAVED";
            if(frames_==26||frames_==27){auto rgb=GetChronoSystem()->LifeState().recipe.color.ToRGB();auto tint=GetRenderer()->volumeColors_[0];immediateColor_&=std::abs(rgb.x-tint.x)<.001f&&std::abs(rgb.y-tint.y)<.001f&&std::abs(rgb.z-tint.z)<.001f;}
            return;
        }
#endif
        Game::GameScene::Draw();
    }
    void DrawEditor()override{
        Game::GameScene::DrawEditor();
#ifdef USE_IMGUI
        if(GetChronoSystem()->SlimeLabOpen())editorCursorFree_&=ImGui::GetMouseCursor()!=ImGuiMouseCursor_None;
#endif
    }
};
