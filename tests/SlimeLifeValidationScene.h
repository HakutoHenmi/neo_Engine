#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Systems/ChronoSystem.h"
#include "../Game/Chrono/SlimeTentacleRules.h"
#include <chrono>
#include <fstream>
class SlimeLifeValidationScene final:public Game::GameScene {
    Engine::WindowDX* window_=nullptr;
    std::chrono::steady_clock::time_point start_;
    float elapsed_=0;int frames_=0;
    bool noAuto_=false,heldOnce_=false,engulfImage_=false,splitImage_=false,hammerImage_=false;
    bool sword_=false,plane_=false;
    bool splitConserved_=false,damageSplit_=false,cursorFree_=true,labClick_=false,labSave_=false,draining_=false;
    bool hpGrowthValid_=true,labFrozen_=true,deathSeen_=false,editorCursorFree_=true;
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
    int absorbPhase_=0,absorbBatches_=0,absorbBeforeRelease_=0;
    float absorbClock_=0,absorbTravel_=0;
    Game::Chrono::Vec absorbOrigin_{};
    bool absorbNoAuto_=true,absorbBatch_=false,absorbFinite_=true,absorbRelease_=false,absorbWaitHeld_=false,absorbContact_=false,absorbOrbImage_=false;
    std::array<bool,3> absorbImages_{};
    int cameraPhase_=0,cameraFrame_=0;
    bool cameraValid_=true;
    float cameraStartDistance_=0,cameraLargestDistance_=0;
    int tirePhase_=0;float tireClock_=0,tireSpeed_=0;bool tireValid_=true;size_t tireProp_=0;
    void UpdateTire(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.33f;input.morphForm=4;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);controller.invincible=60;
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        for(auto& enemy:enemies){enemy.attackDelay=60;enemy.shotClock=60;enemy.alerted=false;}
        if(frames_==0){float nearest=1e9f;bool found=false;
            for(size_t i=0;i<life.props.size();++i)if(life.props[i].morphItem==LifeMorphItem::Tire){float d=LifeDistance(body.at,life.props[i].at);if(d<nearest){tireProp_=i;nearest=d;found=true;}}
            tireValid_&=found&&nearest<life.recipe.AbsorbRange();}
        if(tirePhase_==0)input.attack=tireClock_<.1f;
        if(tirePhase_==1){input.absorb=true;auto direction=life.props[tireProp_].at-body.at;input.yaw=std::atan2(direction.x,direction.z);}
        if(tirePhase_==2)input.yaw=2.2f;
        if(tirePhase_==7)input.yaw=1.2f;
        if(tirePhase_==2||tirePhase_==6)input.attack=tireClock_>.35f&&tireClock_<.5f;
        if(tirePhase_==3)input.move={0,0,1};
        if(tirePhase_==4)input.move={1,0,0};
        if(tirePhase_==5)input.returnToSlime=true;
        float before=life.age;Game::GameScene::Update();++frames_;tireClock_+=life.age-before;
        auto next=[&](){trace_<<"tire phase="<<tirePhase_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" mass="<<body.mass<<'\n';trace_.flush();++tirePhase_;tireClock_=0;};
        if(tirePhase_==0&&tireClock_>.8f){tireValid_&=!body.tireUnlocked&&body.action==LifeAction::Idle;Capture(L"tests/out/slime-life-tire-highlight.png");next();}
        else if(tirePhase_==1&&body.tireUnlocked&&body.action==LifeAction::Idle){tireValid_&=!life.props[tireProp_].active;Capture(L"tests/out/slime-life-tire-unlocked.png");next();}
        else if(tirePhase_==2&&body.action==LifeAction::Tire&&body.weapon.ready){tireValid_&=!body.weapon.active&&body.mass<LifeHammerMass;Capture(L"tests/out/slime-life-tire-form.png");next();}
        else if(tirePhase_==3&&tireClock_>2){tireSpeed_=Length(Vec{controller.velocity.x,0,controller.velocity.z});tireValid_&=tireSpeed_>life.recipe.Speed()*2&&std::abs(body.tireAngle)>.01f;Capture(L"tests/out/slime-life-tire-rolling.png");next();}
        else if(tirePhase_==4&&tireClock_>.2f){tireValid_&=body.forward.z>.8f;next();}
        else if(tirePhase_==5&&body.action==LifeAction::Idle){tireValid_&=body.tireUnlocked;next();}
        else if(tirePhase_==6&&body.action==LifeAction::Tire&&body.weapon.ready){
            tireValid_&=body.tireUnlocked;body.mass=body.health=body.maxHealth=1200;body.at={78,1.25f,-400};controller.velocity={};++life.revision;
            GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()));next();}
        else if(tirePhase_==7&&tireClock_>2&&GetRenderer()->FluidMorphReady(life.revision,0,LifeScale(body.mass))){Capture(L"tests/out/slime-life-tire-large.png");next();}
        for(auto p:GetRenderer()->GetFluidBodySnapshot().offsets)tireValid_&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(tirePhase_==8||real>100){uint32_t particles=0;for(const auto& b:life.bodies)particles+=b.particles;
            bool pass=tirePhase_==8&&tireValid_&&particles==6600;
            std::ofstream("tests/out/slime-life-tire.txt")<<(pass?"PASS":"FAIL")<<" tire phase="<<tirePhase_<<" valid="<<tireValid_<<" speed="<<tireSpeed_<<" particles="<<particles<<" unlocked="<<body.tireUnlocked<<" action="<<int(body.action)<<" ready="<<body.weapon.ready;
            PostQuitMessage(pass?0:2);}
    }
    static void AimHydrantAt(Game::Chrono::ControlFrame& input,const Game::Chrono::LifeBody& body,Game::Chrono::Vec target){
        using namespace Game::Chrono;
        float yaw=std::atan2(target.x-body.at.x,target.z-body.at.z);
        for(int i=0;i<16;++i){Vec flat{std::sin(yaw),0,std::cos(yaw)};
            Vec delta=target-body.at-LifeHydrantCameraOffset(body.mass,flat);yaw=std::atan2(delta.x,delta.z);
            input.pitch=-std::asin(std::clamp(Unit(delta).y,-1.f,1.f));}
        input.yaw=yaw;
    }
    int aimPhase_=0,aimHits_=0;float aimClock_=0;bool aimValid_=true;
    void UpdateHydrantAiming(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.morphForm=5;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        auto& controller=GetComponents().get<Player>(player);auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        constexpr float masses[]={100,905,905,905};constexpr Vec offsets[]={{0,0,14},{0,0,55},{12,12,28},{0,80,80}};
        if(aimClock_==0){
            GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()));
            life.step=2;life.props.clear();life.bodies={};body.at={78,1.25f,-360};body.mass=body.health=body.maxHealth=masses[aimPhase_];body.particles=6600;body.hydrantUnlocked=true;body.forward=body.liquidForward={0,0,1};
            LifeEnterWeapon(body,LifeAction::Hydrant,{0,0,1});controller.velocity={};++life.revision;
            enemies.clear();Vec target=body.at+offsets[aimPhase_];enemies.push_back(CitySpawnEnemy({CityEnemy::Shield,target,target},9001,2));
            enemies.front().health=enemies.front().maxHealth=1000;aimHits_=life.waterHits;
        }
        Vec target=body.at+offsets[aimPhase_];target.y=aimPhase_==2?14.f:CityStats(CityEnemy::Shield).scale;
        enemies.front().at=target;enemies.front().stagger=60;enemies.front().attackDelay=60;controller.invincible=60;life.stepDelay=120;
        AimHydrantAt(input,body,target);input.attack=aimClock_>.6f;
        if(aimPhase_==1&&aimClock_>2)input.absorb=true; // Both independent water rings must survive multiple wraps.
        if(aimPhase_==3){input.yaw=0;input.pitch=-.65f;input.attack=false;}
        float before=life.age;GameScene::Update();++frames_;aimClock_+=life.age-before;
        if(aimPhase_==2)enemies.front().at=target; // Elevated target fixture also stays elevated when drawn.
        if(aimClock_>1){
            const auto& aim=GetChronoSystem()->HydrantAimState();auto camera=GetCamera().Position();Vec flat{std::sin(input.yaw),0,std::cos(input.yaw)},right{flat.z,0,-flat.x};
            Vec offset=Vec{camera.x,camera.y,camera.z}-body.at;float scale=LifeScale(body.mass);
            aimValid_&=aim.active&&offset.y>5*scale-1.25f&&Dot(offset,right)>3.5f*scale;
            aimValid_&=aimPhase_==3?!aim.enemy:aim.enemy&&aim.reachable&&!aim.blocked;
            DirectX::XMFLOAT3 projected;DirectX::XMStoreFloat3(&projected,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(aim.target.x,aim.target.y,aim.target.z,1),GetCamera().View()*GetCamera().Proj()));
            aimValid_&=std::abs(projected.x)<.001f&&std::abs(projected.y)<.001f;
            if(aimPhase_==1&&aimClock_>3){
                std::array<int,8> bins{};Vec origin=LifeHydrantNozzle(body),axis=aim.velocity;axis.y=0;float speed=Length(axis);axis=Unit(axis);
                for(const auto& p:GetRenderer()->GetHydrantWaterSnapshot().particles)if(p.type<1.15f&&p.contactAge<.12f){
                    Vec at{p.at.x,p.at.y,p.at.z};float time=Dot(at-origin,axis)/speed;
                    if(time>=0&&time<aim.flight&&Length(at-LifeHydrantTrajectory(origin,aim.velocity,time))<3){int bin=std::min(7,int(time/aim.flight*8));++bins[size_t(bin)];}}
                for(int bin:bins)aimValid_&=bin>0;
            }
        }
        if(aimClock_>(aimPhase_==1?5.5f:1.5f)&&(aimPhase_==3||life.waterHits>=aimHits_+3)){
            const wchar_t* images[]={L"tests/out/slime-life-aim-close.png",L"tests/out/slime-life-aim-large.png",L"tests/out/slime-life-aim-elevated.png",L"tests/out/slime-life-aim-up.png"};Capture(images[aimPhase_]);
            trace_<<"aim phase="<<aimPhase_<<" valid="<<aimValid_<<" hits="<<life.waterHits-aimHits_<<" health="<<enemies.front().health<<'\n';trace_.flush();
            if(++aimPhase_<4)aimClock_=0;
        }
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(aimPhase_==4||real>120){bool pass=aimPhase_==4&&aimValid_;
            std::ofstream("tests/out/slime-life-aim.txt")<<(pass?"PASS":"FAIL")<<" hydrant aim phases="<<aimPhase_<<" shoulder_and_center="<<aimValid_<<" gpu_water_hits="<<life.waterHits;
            PostQuitMessage(pass?0:2);}
    }
    int hydrantPhase_=0;float hydrantClock_=0,hydrantHp_=0;size_t hydrantProp_=0,hydrantWaterPeak_=0;
    bool hydrantValid_=true,hydrantAttackImage_=false,hydrantGuardImage_=false,hydrantUnprotected_=false;
    void UpdateHydrant(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.morphForm=5;input.pitch=.2f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        life.stepDelay=120;controller.invincible=hydrantPhase_==4||hydrantPhase_==5?0.f:60.f;
        auto spawn=[&](CityEnemy kind,Vec at){enemies.clear();enemies.push_back(CitySpawnEnemy({kind,at,at},9001,life.step));enemies.back().at.y=CityStats(kind).scale;};
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),true);enemies.clear();life.step=2;
            body.at={86,1.25f,-470};body.mass=body.health=body.maxHealth=320;body.forward=body.liquidForward={0,0,1};controller.velocity={};++life.revision;
            bool found=false;float nearest=1e9f;for(size_t i=0;i<life.props.size();++i)if(life.props[i].morphItem==LifeMorphItem::Hydrant){float d=LifeDistance(body.at,life.props[i].at);if(d<nearest){found=true;nearest=d;hydrantProp_=i;}}
            hydrantValid_&=found&&nearest<life.recipe.AbsorbRange()*LifeScale(body.mass);}
        if(hydrantPhase_==0)input.attack=hydrantClock_<.1f;
        if(hydrantPhase_==1)input.absorb=true;
        if(hydrantPhase_==2)input.attack=hydrantClock_>.35f;
        if(hydrantPhase_==3||hydrantPhase_==6)input.attack=true;
        if(hydrantPhase_==4)input.absorb=true;
        if(hydrantPhase_==4||hydrantPhase_==5)for(auto& enemy:enemies)enemy.shotClock=std::min(enemy.shotClock,.25f);
        if(hydrantPhase_==7)input.returnToSlime=true;
        if(hydrantPhase_==8)input.attack=hydrantClock_>.35f;
        if(hydrantPhase_==3&&!enemies.empty())AimHydrantAt(input,body,enemies.front().at);
        if(hydrantPhase_==6)AimHydrantAt(input,body,CityGateCenter+Vec{0,8,0});
        float before=life.age;Game::GameScene::Update();++frames_;hydrantClock_+=life.age-before;
        auto next=[&](){trace_<<"hydrant phase="<<hydrantPhase_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" hp="<<body.health<<'\n';trace_.flush();++hydrantPhase_;hydrantClock_=0;};
        const auto& water=GetRenderer()->GetHydrantWaterSnapshot();hydrantWaterPeak_=std::max(hydrantWaterPeak_,water.particles.size());
        if(hydrantPhase_==0&&hydrantClock_>.8f){hydrantValid_&=!body.hydrantUnlocked&&body.action==LifeAction::Idle;Capture(L"tests/out/slime-life-hydrant-highlight.png");next();}
        else if(hydrantPhase_==1&&body.hydrantUnlocked&&body.action==LifeAction::Idle){hydrantValid_&=!life.props[hydrantProp_].active;next();}
        else if(hydrantPhase_==2&&body.action==LifeAction::Hydrant&&body.weapon.ready&&hydrantClock_>1){Capture(L"tests/out/slime-life-hydrant-form.png");spawn(CityEnemy::Shield,body.at+Vec{0,0,22});next();}
        else if(hydrantPhase_==3){
            if(hydrantClock_>1&&!hydrantAttackImage_){Capture(L"tests/out/slime-life-hydrant-attack.png");hydrantAttackImage_=true;}
            if(life.waterHits>0&&!SwarmAlive(enemies.front())){spawn(CityEnemy::Archer,body.at+Vec{0,0,28});enemies.front().health=enemies.front().maxHealth=1000;hydrantHp_=body.health;next();}}
        else if(hydrantPhase_==4){
            if(hydrantClock_>1&&!hydrantGuardImage_){Capture(L"tests/out/slime-life-hydrant-defense.png");hydrantGuardImage_=true;}
            if(hydrantClock_>4&&life.waterBlocks>=2){hydrantValid_&=body.health==hydrantHp_;hydrantHp_=body.health;next();}}
        else if(hydrantPhase_==5&&body.health<hydrantHp_){hydrantValid_&=body.weapon.ready;hydrantUnprotected_=true;enemies.clear();life.step=4;body.at={78,1.25f,-88};controller.velocity={};++life.revision;next();}
        else if(hydrantPhase_==6&&GetChronoSystem()->CityGateState().gateOpen){Capture(L"tests/out/slime-life-hydrant-gate.png");next();}
        else if(hydrantPhase_==7&&body.action==LifeAction::Idle){hydrantValid_&=body.hydrantUnlocked&&!life.waterAttack&&!life.waterGuard;next();}
        else if(hydrantPhase_==8&&body.action==LifeAction::Hydrant&&body.weapon.ready){next();}
        if(frames_%120==0){trace_<<"hydrant tick phase="<<hydrantPhase_<<" clock="<<hydrantClock_<<" samples="<<water.particles.size()<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" hp="<<body.health<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(hydrantPhase_==9||real>160){bool pass=hydrantPhase_==9&&hydrantValid_&&hydrantWaterPeak_>100&&hydrantAttackImage_&&hydrantGuardImage_&&hydrantUnprotected_;
            std::ofstream("tests/out/slime-life-hydrant.txt")<<(pass?"PASS":"FAIL")<<" hydrant phase="<<hydrantPhase_<<" valid="<<hydrantValid_<<" water_peak="<<hydrantWaterPeak_<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" unprotected_hit="<<hydrantUnprotected_<<" gate="<<GetChronoSystem()->CityGateState().gateOpen;
            PostQuitMessage(pass?0:2);}
    }
    void UpdateCameraFacing(){
        using namespace Game;using namespace Game::Chrono;
        constexpr LifeAction forms[]={LifeAction::Idle,LifeAction::Idle,LifeAction::Sword,LifeAction::Hammer,LifeAction::Plane,LifeAction::Sword};
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.35f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        if(cameraFrame_==0){auto at=body.at;life.bodies={};body.at=at;body.mass=cameraPhase_==0?100:LifeMaxMass;body.health=body.maxHealth=body.mass;body.particles=6600;body.forward=body.liquidForward={1,0,0};
            if(LifeWeaponForm(forms[cameraPhase_])){life.form=forms[cameraPhase_];LifeEnterWeapon(body,forms[cameraPhase_],{1,0,0});}
            life.props.clear();life.absorbedMass=body.mass-LifeStartMass;GetComponents().get<Player>(player).velocity={};++life.revision;}
        for(auto& enemy:const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies())){enemy.at=body.at+Vec{200,0,200};enemy.attackDelay=60;}
        GetComponents().get<Player>(player).invincible=60;
        // Move sideways first, then orbit the camera while movement is released.
        if(cameraFrame_<25)input.move={1,0,0};
        if(cameraFrame_>=25)input.yaw=float(cameraFrame_-25)*.028f;
        if(cameraPhase_==5)input.attack=cameraFrame_==120;
        Game::GameScene::Update();++frames_;++cameraFrame_;
        if(cameraFrame_==1){GetRenderer()->fluidMorphSeedPending_=true;GetRenderer()->fluidMorphRetargetMask_=Engine::Renderer::kFluidMorphAllBodies;}
        if(cameraFrame_>65){
            Vec cameraForward{std::sin(input.yaw),0,std::cos(input.yaw)};
            cameraValid_&=Dot(body.forward,LifeFacesCamera(body.action)?cameraForward:Vec{1,0,0})>(LifeFacesCamera(body.action)?.97f:.999f);
            auto vp=DirectX::XMMatrixMultiply(GetCamera().View(),GetCamera().Proj());const auto& snapshot=GetRenderer()->GetFluidBodySnapshot();
            cameraValid_&=snapshot.offsets.size()==6600;
            for(const auto& offset:snapshot.offsets){auto p=offset+snapshot.core;DirectX::XMFLOAT3 projected{};
                DirectX::XMStoreFloat3(&projected,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x,p.y,p.z,1),vp));
                cameraValid_&=std::isfinite(projected.x)&&std::isfinite(projected.y)&&std::abs(projected.x)<.85f&&std::abs(projected.y)<.75f&&projected.z>0&&projected.z<1;}
        }
        if(cameraFrame_<140)return;
        if(LifeWeaponForm(body.action))cameraValid_&=body.weapon.ready;
        auto cameraAt=GetCamera().Position();float distance=Length(Vec{cameraAt.x,cameraAt.y,cameraAt.z}-body.at);
        if(cameraPhase_==0)cameraStartDistance_=distance;else cameraLargestDistance_=std::max(cameraLargestDistance_,distance);
        const wchar_t* images[]={L"tests/out/slime-life-camera-small.png",L"tests/out/slime-life-camera-large.png",L"tests/out/slime-life-camera-sword.png",L"tests/out/slime-life-camera-hammer.png",L"tests/out/slime-life-camera-plane.png",L"tests/out/slime-life-camera-sword-swing.png"};Capture(images[cameraPhase_]);
        if(++cameraPhase_==6){bool pass=cameraValid_&&cameraLargestDistance_>cameraStartDistance_*4;
            std::ofstream("tests/out/slime-life-camera.txt")<<(pass?"PASS":"FAIL")<<" phases="<<cameraPhase_<<" facing_and_framing="<<cameraValid_<<" small_distance="<<cameraStartDistance_<<" large_distance="<<cameraLargestDistance_;
            PostQuitMessage(pass?0:2);
        }else cameraFrame_=0;
    }
    int progressionWave_=-1,progressionFrame_=0;
    bool progressionValid_=true;
    void UpdateProgression(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& controls=GetComponents().get<ControlFrame>(player);controls={};controls.pitch=.3f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        auto& ink=GetComponents().get<InkPlayer>(player);GetComponents().get<Player>(player).invincible=60;
        life.props.clear();
        if(progressionWave_!=life.step){progressionWave_=life.step;progressionFrame_=0;
            int expected=0,alive=0;for(int w=0;w<=life.step;++w)expected+=int(CityStepSpawns(w).size());
            progressionValid_&=int(enemies.size())==expected&&!GetChronoSystem()->Finished();
            for(size_t i=0;i<enemies.size();++i){const auto& enemy=enemies[i];progressionValid_&=enemy.id==i+1;
                if(SwarmAlive(enemy)){++alive;auto stats=CityStats(CityKind(enemy));
                    progressionValid_&=enemy.step==life.step&&std::abs(enemy.scale-stats.scale)<.001f&&std::abs(enemy.maxHealth-stats.health)<.001f&&
                        CityWalkable({enemy.at.x,0,enemy.at.z},.5f,{true,true});}}
            progressionValid_&=alive==int(CityStepSpawns(life.step).size());
        }
        // Leave one survivor first: a step must not advance while it lives.
        if(progressionFrame_==0){if(life.step==0)for(auto& enemy:enemies)if(SwarmAlive(enemy)&&enemy.id!=enemies.back().id){enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;}life.stepDelay=0;}
        if(progressionFrame_<4)progressionValid_&=life.step==progressionWave_;
        if(progressionFrame_==4){
            body.mass=body.health=body.maxHealth=std::min(LifeMaxMass,100.f+float(life.step+1)*850);life.absorbedMass=body.mass-100;++life.revision;
            if(life.step==CityStepCount-1){body.mass=body.health=body.maxHealth=LifeMaxMass;
                // Check cap at the actual large size without consuming the orb.
                progressionValid_&=!LifeCollectTouch(life,0x80000001u,body.at,15,0,1,12,body.at);}
        }
        // Gate steps wait for their gate even after every enemy falls.
        if(progressionFrame_==30){if(life.step==CityStepCount-1)Capture(L"tests/out/slime-life-final-wave.png");
            for(auto& enemy:enemies)if(SwarmAlive(enemy)){enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;}}
        if(progressionFrame_==40&&(life.step==1||life.step==4)){progressionValid_&=life.step==progressionWave_;GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()));}
        Game::GameScene::Update();++frames_;++progressionFrame_;
        for(auto p:GetRenderer()->GetFluidBodySnapshot().offsets)progressionValid_&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        if(GetChronoSystem()->Finished()||frames_>900){
            bool pass=progressionValid_&&GetChronoSystem()->Finished()&&life.step==CityStepCount-1&&ink.swarmKills==CityEnemyTotal()&&body.mass==LifeMaxMass;
            std::ofstream("tests/out/slime-life-progression.txt")<<(pass?"PASS":"FAIL")<<" steps="<<life.step+1<<" enemies="<<enemies.size()<<" kills="<<ink.swarmKills<<" mass="<<body.mass<<" valid="<<progressionValid_;
            PostQuitMessage(pass?0:2);
        }
    }
    void UpdateAbsorption(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.4f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        if(frames_==0){life.props.clear();absorbOrigin_=body.at;
            for(int i=0;i<12;++i){float angle=(float(i)-5.5f)*.105f,reach=8+float(i%5)*.45f;life.props.push_back({body.at+Vec{std::sin(angle)*reach,-.75f,std::cos(angle)*reach},15,i==0?3.f:2.f,true,0xffffffffu,true});}
            life.props.push_back({body.at+Vec{0,-.75f,-3},15,5});life.props.push_back({body.at+Vec{0,-.75f,3},130,5});
        }
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        for(auto& enemy:enemies){enemy.at=body.at+Vec{150,0,150};enemy.attackDelay=60;}
        GetComponents().get<Player>(player).invincible=60;
        if(absorbPhase_==1)input.absorb=absorbClock_<.55f;
        if(absorbPhase_==2){input.absorb=true;input.move={0,0,1};}
        if(absorbPhase_==5)input.move={0,0,1};
        auto beforeAction=body.action;float beforeAge=life.age;Game::GameScene::Update();++frames_;
        absorbClock_+=life.age-beforeAge;
        if(absorbPhase_==0){absorbNoAuto_&=life.absorbed==0;if(absorbClock_>1.2f){absorbPhase_=1;absorbClock_=0;}}
        else if(absorbPhase_==1){
            if(body.action==LifeAction::Engulf){absorbBatch_|=body.grabs.size()==12;float phase=body.actionAge/life.recipe.AbsorbDuration();
                const float times[]={.3f,.5f,.82f};const wchar_t* images[]={L"tests/out/slime-life-tentacles-reach.png",L"tests/out/slime-life-tentacles-hold.png",L"tests/out/slime-life-tentacles-pull.png"};
                for(int i=0;i<3;++i)if(phase>=times[i]&&!absorbImages_[size_t(i)]){Capture(images[i]);absorbImages_[size_t(i)]=true;}}
            if(life.absorbed==12&&body.action==LifeAction::Idle){
                absorbBatch_&=body.mass==125&&body.health==125&&body.maxHealth==125&&life.props[12].active&&life.props[13].active;
                life.props.clear();absorbOrigin_=body.at;
                for(int row=0;row<3;++row)for(int col=-1;col<=1;++col)life.props.push_back({body.at+Vec{float(col),-.75f,15+float(row)*16},15,2});
                absorbPhase_=2;absorbClock_=0;
            }
        }else if(absorbPhase_==2){
            absorbWaitHeld_|=input.absorb&&body.action==LifeAction::Idle&&life.absorbed==12;
            if(beforeAction==LifeAction::Idle&&body.action==LifeAction::Engulf)++absorbBatches_;
            absorbTravel_=LifeDistance(body.at,absorbOrigin_);
            if(life.absorbed==21&&body.action==LifeAction::Idle){
                Capture(L"tests/out/slime-life-many-hands-moving.png");life.props.push_back({body.at+Vec{0,-.75f,4},15,2});
                absorbBeforeRelease_=life.absorbed;absorbPhase_=3;absorbClock_=0;
            }
        }else if(absorbPhase_==3&&absorbClock_>.7f){
            absorbRelease_=life.absorbed==absorbBeforeRelease_&&body.action==LifeAction::Idle&&body.mass==143;
            body.mass=body.health=body.maxHealth=180;life.absorbedMass=80;life.props.clear();GetComponents().get<Player>(player).velocity={};
            const float rewards[]={12,24,36},required[]={15,15,130};for(int i=0;i<3;++i)life.props.push_back({body.at+Vec{float(i-1)*3,-.25f,8},required[i],rewards[i],true,0xffffffffu,true});
            absorbPhase_=4;absorbClock_=0;
        }else if(absorbPhase_==4&&absorbClock_>.8f){
            Capture(L"tests/out/slime-life-orb-tiers.png");absorbOrbImage_=true;life.props.clear();
            const float rewards[]={12,24,36};for(int i=0;i<3;++i)life.props.push_back({body.at+Vec{0,-.25f,float(i)*5},15,rewards[i],true,0xffffffffu,true});
            life.props.push_back({body.at+Vec{0,-.25f,1},300,36});life.props.push_back({body.at+Vec{0,20,1},15,12});
            LifeEnterWeapon(body,LifeAction::Hammer,{0,0,1});absorbPhase_=5;absorbClock_=0;
        }else if(absorbPhase_==5&&life.absorbed==24){
            absorbContact_=body.mass==252&&body.health==252&&body.maxHealth==252&&body.action==LifeAction::Hammer&&life.props[3].active&&life.props[4].active;
            Capture(L"tests/out/slime-life-contact-pickup.png");absorbPhase_=6;
        }
        for(auto p:GetRenderer()->GetFluidBodySnapshot().offsets)absorbFinite_&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(absorbPhase_==6||real>90){uint32_t particles=0;for(const auto& b:life.bodies)particles+=b.particles;
            bool pass=absorbPhase_==6&&absorbNoAuto_&&absorbBatch_&&absorbWaitHeld_&&absorbBatches_>=3&&absorbTravel_>20&&absorbRelease_&&absorbFinite_&&absorbContact_&&absorbOrbImage_&&particles==6600&&absorbImages_[0]&&absorbImages_[1]&&absorbImages_[2];
            std::ofstream("tests/out/slime-life-absorb.txt")<<(pass?"PASS":"FAIL")<<" multi="<<absorbBatch_<<" no_auto="<<absorbNoAuto_<<" wait_held="<<absorbWaitHeld_<<" held_batches="<<absorbBatches_<<" moving_distance="<<absorbTravel_<<" release="<<absorbRelease_<<" contact="<<absorbContact_<<" orb_tiers="<<absorbOrbImage_<<" finite="<<absorbFinite_<<" mass="<<body.mass<<" absorbed="<<life.absorbed<<" particles="<<particles<<" phase="<<absorbPhase_;
            PostQuitMessage(pass?0:2);
        }
    }
    void UpdateMotion(){
        using namespace Game;using namespace Game::Chrono;
        constexpr float masses[]={100,600,LifeMaxMass,600,LifeMaxMass,LifeMaxMass,LifeMaxMass};
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
        if(motionAge_>2){
            const auto& snapshot=renderer->GetFluidBodySnapshot();auto vp=DirectX::XMMatrixMultiply(GetCamera().View(),GetCamera().Proj());
            for(const auto& offset:snapshot.offsets){auto p=offset+snapshot.core;DirectX::XMFLOAT3 projected{};
                DirectX::XMStoreFloat3(&projected,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x,p.y,p.z,1),vp));
                motionCameraError_=std::max({motionCameraError_,std::abs(projected.x)-.85f,std::abs(projected.y)-.75f});}
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
        constexpr float masses[]={100,600,2400,LifeMaxMass,LifeMaxMass,LifeMaxMass,LifeMaxMass-240};
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
                for(const auto& prop:before.props)if(prop.active&&LifeCanAbsorb(body.mass,prop.required)&&CityAreaAt(prop.at)==CityAreaAt(body.at)){
                    float distance=LifeDistance(body.at,prop.at);if(distance<nearest){nearest=distance;target=&prop;}}
                if(target){auto delta=target->at-body.at;input.yaw=std::atan2(delta.x,delta.z);
                    if(nearest>before.recipe.AbsorbRange()*LifeScale(body.mass)*.65f||Dot(body.forward,Unit(delta))<.98f)input.move={0,0,1};
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
        if(frames_%60==0){trace_<<"growth mass="<<grown.mass<<" children="<<after.ChildCount()<<" absorbed="<<after.absorbedMass<<" forms="<<growthForms_[0]<<growthForms_[1]<<growthForms_[2]<<" action="<<int(grown.action)<<" ready="<<grown.weapon.ready<<" taps="<<growthSwings_[0]<<" sweeps="<<growthSwings_[1]<<" at="<<grown.at.x<<","<<grown.at.z<<" vel="<<GetComponents().get<Player>(player).velocity.x<<","<<GetComponents().get<Player>(player).velocity.z<<" move="<<input.move.z<<" absorb="<<input.absorb<<" cd="<<grown.cooldown<<" y="<<grown.at.y<<" grounded="<<GetComponents().get<Player>(player).grounded;
            {int left=0;float best=1e9f;Vec bestAt{};for(const auto& prop:after.props)if(prop.active&&CityAreaAt(prop.at)==CityAreaAt(grown.at)){++left;
                if(LifeCanAbsorb(grown.mass,prop.required)&&LifeDistance(prop.at,grown.at)<best){best=LifeDistance(prop.at,grown.at);bestAt=prop.at;}}
                trace_<<" left="<<left<<" nearest=("<<bestAt.x<<","<<bestAt.y<<","<<bestAt.z<<") d="<<best<<" dot="<<Dot(grown.forward,Unit(Vec{bestAt.x-grown.at.x,0,bestAt.z-grown.at.z}));}
            trace_<<'\n';trace_.flush();}
        bool complete=growthLateImage_&&growthForms_[0]&&growthForms_[1];
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(complete||real>200||GetChronoSystem()->Finished()){
            bool pass=complete&&growthTackle_&&growthInitial_&&growthPersistent_&&growthManualReturn_&&growthSingleSweep_&&growthSwings_[1]>=3&&growthHammerAttackImage_&&growthSwordAttackImage_&&LifeScale(grown.mass)>growthStartScale_*2.1f;
            uint32_t particles=0;for(const auto& b:after.bodies)particles+=b.particles;pass&=particles==Engine::Renderer::kPlayerFluidParticles;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" growth_initial="<<growthInitial_<<" early_tackle="<<growthTackle_<<" mass="<<grown.mass<<" children="<<after.ChildCount()<<" scale="<<LifeScale(grown.mass)<<" hammer="<<growthForms_[0]<<" sword="<<growthForms_[1]<<" plane="<<growthForms_[2]<<" particles="<<particles<<" collected="<<after.absorbedMass<<" persistent="<<growthPersistent_<<" manual_return="<<growthManualReturn_<<" hammer_taps="<<growthSwings_[0]<<" sword_single_sweeps="<<growthSwings_[1]<<" plane_boosts="<<growthSwings_[2]<<" hammer_hits="<<growthHits_[0]<<" sword_hits="<<growthHits_[1];
            PostQuitMessage(pass?0:2);
        }
    }
    // City stage tour: teleports through the three areas with every enemy role on stage and saves one image per view.
    int tourStop_=0,tourFrame_=0;bool tourSpawned_=false;
    void UpdateCityTour(){
        using namespace Game;using namespace Game::Chrono;
        struct Stop {Vec at;float yaw,pitch,mass;const wchar_t* image;bool open;};
        static const Stop stops[]={
            {{0,0,-680},0,.22f,100,L"tests/out/city-alley-start.png",false},
            {{-4,0,-622},.35f,.18f,140,L"tests/out/city-alley-rats.png",false},
            {{40,0,-562},1.2f,.2f,180,L"tests/out/city-alley-bend.png",false},
            {{78,0,-512},0,.22f,260,L"tests/out/city-barricade.png",false},
            {{78,0,-150},0,.18f,1500,L"tests/out/city-gate.png",false},
            {{78,0,-440},0,.2f,700,L"tests/out/city-street.png",true},
            {{120,0,-265},-1.5708f,.2f,900,L"tests/out/city-side-street.png",true},
            {{78,0,30},0,.16f,3000,L"tests/out/city-crossing.png",true},
            {{-120,0,160},.75f,.3f,5200,L"tests/out/city-guardian.png",true},
            {{330,0,600},-2.4f,.25f,5200,L"tests/out/city-crossing-corner.png",true},
        };
        constexpr int count=int(sizeof(stops)/sizeof(stops[0]));
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        GetComponents().get<Player>(player).invincible=60;
        if(!tourSpawned_){tourSpawned_=true;
            // One of every role, placed where the stage spawns them.
            for(int step=1;step<CityStepCount;++step){auto spawns=CityStepSpawns(step);int n=0;
                for(const auto& spawn:spawns){if(++n>(step==6?0:step==5?6:4))break;auto enemy=CitySpawnEnemy(spawn,uint32_t(enemies.size()+1),0);enemy.alerted=false;enemy.at.y=enemy.scale;enemies.push_back(enemy);}}}
        const auto& stop=stops[tourStop_];
        if(tourFrame_==0){body.mass=body.health=body.maxHealth=stop.mass;life.absorbedMass=stop.mass-100;++life.revision;
            body.at={stop.at.x,1.25f,stop.at.z};body.forward=body.liquidForward={std::sin(stop.yaw),0,std::cos(stop.yaw)};
            GetComponents().get<Player>(player).velocity={};
            if(stop.open&&!GetChronoSystem()->CityGateState().gateOpen)GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()));}
        // Keep enemies on their posts so every view shows the intended layout.
        for(auto& enemy:enemies){enemy.attackDelay=60;enemy.shotClock=60;enemy.alerted=false;enemy.patrol=enemy.home=Vec{enemy.at.x,0,enemy.at.z};}
        input.yaw=stop.yaw;input.pitch=stop.pitch;
        Game::GameScene::Update();++frames_;++tourFrame_;
        body.at.x=stop.at.x;body.at.z=stop.at.z;
        if(tourFrame_==150){Capture(stop.image);tourFrame_=0;
            if(++tourStop_==count){std::ofstream("tests/out/slime-life-smoke.txt")<<"PASS city tour images="<<count;PostQuitMessage(0);}}
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
        if(wcsstr(GetCommandLineW(),L"--slime-life-aim")){UpdateHydrantAiming();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-hydrant")){UpdateHydrant();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-tire")){UpdateTire();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-city-tour")){UpdateCityTour();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-motion")){UpdateMotion();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-camera")){UpdateCameraFacing();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-progression")){UpdateProgression();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-absorb-only")){UpdateAbsorption();return;}
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
        float beforeMass=life.ReservedMass(),beforeHp=body.health,beforeMaxHp=body.maxHealth,beforeAge=life.age;int beforeSplits=life.splits;
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
            if(mixtureStep_==7){trialWrapped_|=after.preview.action==LifeAction::Engulf&&GetRenderer()->fluidMorphTargets_.front().shape.w>0;
                if(after.preview.action==LifeAction::Engulf){float phase=after.preview.actionAge/after.recipe.AbsorbDuration();const float timings[]={.29f,.56f,.77f,.95f};const wchar_t* names[]={L"tests/out/slime-life-hands-open.png",L"tests/out/slime-life-hands-wrap.png",L"tests/out/slime-life-hands-close.png",L"tests/out/slime-life-hands-pull.png"};
                    for(size_t i=0;i<4;++i)if(phase>=timings[i]&&!handImages_[i]){Capture(names[i]);handImages_[i]=true;}
                    float loX=100,hiX=-100,hiY=0;
                    for(size_t g=0;g<after.preview.grabs.size();++g)for(int n=0;n<=LifeTentacleSegments;++n){auto point=LifeTentaclePoint(after.preview,after.recipe,after.preview.grabs[g],g,float(n)/LifeTentacleSegments);
                        loX=std::min(loX,point.x);hiX=std::max(hiX,point.x);hiY=std::max(hiY,point.y-1200);}
                    engulfVolume_|=loX<-2.8f&&hiX>2.8f&&hiY>1.4f&&after.preview.grabs.size()==9;}
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
        hpGrowthValid_&=after.bodies[0].health<=beforeHp+std::max(0.f,after.bodies[0].maxHealth-beforeMaxHp)+.001f;
        if(after.splits>beforeSplits){damageSplit_=GetComponents().get<Player>(player).stats.hitsTaken>beforeHits;splitConserved_=std::abs(after.ReservedMass()-beforeMass)<.01f;}
        if(frames_%30==0){trace_<<"t="<<after.age<<" hp="<<after.bodies[0].health<<" mass="<<after.TotalMass()<<" absorbed="<<after.absorbed<<" action="<<int(after.bodies[0].action)<<" a="<<after.bodies[0].actionAge<<" split="<<after.splits<<" ai="<<after.childHits<<" hammer="<<after.hammerHits<<" drain="<<draining_<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(GetChronoSystem()->Finished()||after.age>70||real>200){
            bool dead=GetChronoSystem()->Finished()&&after.bodies[0].health==0;
            if(dead&&!deathSeen_){deathSeen_=true;return;}
            bool pass=noAuto_&&heldOnce_&&damageSplit_&&splitConserved_&&cursorFree_&&editorCursorFree_&&labClick_&&labSave_&&labFrozen_&&labLive_&&labMaterial_&&labFlight_&&hpGrowthValid_&&dead&&after.childHits>0&&after.hammerHits>0&&engulfImage_&&hammerImage_&&sword_&&plane_;
            Capture(L"tests/out/slime-life-game-over.png");
            auto summary=after;input.restart=true;Game::GameScene::Update();const auto& restarted=GetChronoSystem()->LifeState();
            bool reset=std::abs(restarted.TotalMass()-LifeStartMass)<.01f&&restarted.bodies[1].mass==0&&restarted.bodies[0].health==100&&!GetChronoSystem()->Finished()&&restarted.recipe.mix==summary.recipe.mix&&Length(restarted.recipe.color.ToRGB()-summary.recipe.color.ToRGB())<.001f;
            trace_<<"reset mass="<<restarted.TotalMass()<<" hp="<<restarted.bodies[0].health<<" water="<<restarted.recipe.mix[0]<<" expected="<<summary.recipe.mix[0]<<'\n';trace_.flush();
            pass=pass&&reset;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" manual="<<noAuto_<<" held_once="<<heldOnce_<<" damage_split="<<damageSplit_<<" split_mass="<<splitConserved_<<" cursor_free="<<cursorFree_<<" editor_cursor="<<editorCursorFree_<<" lab_click="<<labClick_<<" lab_save="<<labSave_<<" lab_frozen="<<labFrozen_<<" live="<<labLive_<<" material="<<labMaterial_<<" flight="<<labFlight_<<" hp_growth_valid="<<hpGrowthValid_<<" game_over="<<dead<<" child_hits="<<summary.childHits<<" hammer_hits="<<summary.hammerHits<<" sword="<<sword_<<" plane="<<plane_<<" reset="<<reset<<" frames="<<frames_;
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
