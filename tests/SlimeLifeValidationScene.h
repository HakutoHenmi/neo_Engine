#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Systems/ChronoSystem.h"
#include "../Game/Chrono/SlimeTentacleRules.h"
#include "../Game/Chrono/DevourMorphRules.h"
#include <chrono>
#include <fstream>
class SlimeLifeValidationScene final:public Game::GameScene {
    Engine::WindowDX* window_=nullptr;
    std::chrono::steady_clock::time_point start_;
    float elapsed_=0;int frames_=0;
    bool noAuto_=false,heldOnce_=false,engulfImage_=false,splitImage_=false,tireImage_=false;
    bool dashed_=false;
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
    bool growthPersistent_=true;
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
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.33f;input.morphForm=2;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);controller.invincible=60;
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        for(auto& enemy:enemies){enemy.attackDelay=60;enemy.shotClock=60;enemy.alerted=false;}
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),1);body.at=CityTireItem()+Vec{-6,1.25f,-8};body.forward=body.liquidForward={0,0,1};controller.velocity={};++life.revision;
            float nearest=1e9f;bool found=false;
            for(size_t i=0;i<life.props.size();++i)if(life.props[i].morphItem==LifeMorphItem::Tire){float d=LifeDistance(body.at,life.props[i].at);if(d<nearest){tireProp_=i;nearest=d;found=true;}}
            tireValid_&=found&&nearest<life.recipe.AbsorbRange();}
        if(tirePhase_==0)input.attack=tireClock_<.1f;
        // 解放前の攻撃はタックルになって少し進むので、離れていれば近づきながら吸収する
        if(tirePhase_==1){input.absorb=true;auto direction=life.props[tireProp_].at-body.at;input.yaw=std::atan2(direction.x,direction.z);Vec flat{direction.x,0,direction.z};if(LifeDistance(body.at,life.props[tireProp_].at)>life.recipe.AbsorbRange()*.6f||Dot(body.forward,Unit(flat))<.95f)input.move={0,0,1};}
        if(tirePhase_==2)input.yaw=2.2f;
        if(tirePhase_==7)input.yaw=1.2f;
        if(tirePhase_==2||tirePhase_==6)input.attack=tireClock_>.35f&&tireClock_<.5f;
        if(tirePhase_==3)input.move={0,0,1};
        if(tirePhase_==4)input.move={1,0,0};
        if(tirePhase_==5)input.returnToSlime=true;
        float before=life.age;Game::GameScene::Update();++frames_;tireClock_+=life.age-before;
        auto next=[&](){trace_<<"tire phase="<<tirePhase_<<" valid="<<tireValid_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" mass="<<body.mass<<'\n';trace_.flush();++tirePhase_;tireClock_=0;};
        if(tirePhase_==0&&tireClock_>.8f){tireValid_&=!body.tireUnlocked&&body.action==LifeAction::Idle;Capture(L"tests/out/slime-life-tire-highlight.png");next();}
        else if(tirePhase_==1&&body.tireUnlocked&&body.action==LifeAction::Idle){tireValid_&=!life.props[tireProp_].active;Capture(L"tests/out/slime-life-tire-unlocked.png");next();}
        else if(tirePhase_==2&&body.action==LifeAction::Tire&&body.weapon.ready){tireValid_&=!body.weapon.active&&body.mass<LifeHammerMass;Capture(L"tests/out/slime-life-tire-form.png");next();}
        else if(tirePhase_==3&&tireClock_>2){tireSpeed_=Length(Vec{controller.velocity.x,0,controller.velocity.z});tireValid_&=tireSpeed_>life.recipe.Speed()*2&&std::abs(body.tireAngle)>.01f;Capture(L"tests/out/slime-life-tire-rolling.png");next();}
        else if(tirePhase_==4&&tireClock_>.2f){tireValid_&=body.forward.z>.8f;next();}
        else if(tirePhase_==5&&body.action==LifeAction::Idle){tireValid_&=body.tireUnlocked;next();}
        else if(tirePhase_==6&&body.action==LifeAction::Tire&&body.weapon.ready){
            tireValid_&=body.tireUnlocked;body.mass=body.health=body.maxHealth=1200;body.at={102,1.25f,-400};controller.velocity={};++life.revision;
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
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.morphForm=3;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        auto& controller=GetComponents().get<Player>(player);auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        constexpr float masses[]={100,905,905,905};constexpr Vec offsets[]={{0,0,14},{0,0,55},{12,12,28},{0,80,80}};
        if(aimClock_==0){
            GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()));
            life.step=2;life.props.clear();life.bodies={};body.at={102,1.25f,-360};body.mass=body.health=body.maxHealth=masses[aimPhase_];body.particles=6600;body.hydrantUnlocked=true;body.forward=body.liquidForward={0,0,1};
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
    int crewPhase_=0;float crewClock_=0,crewDistance_=0;int crewSpread_=0,crewKills_=0;bool crewImages_[3]{};
    // 子分が本体から離れて自分で敵を狩るか (本体は動かさない)
    void UpdateCrew(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.42f;input.yaw=crewPhase_==1?3.1416f:0.f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        life.stepDelay=120;controller.invincible=60;
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),2);enemies.clear();life.step=2;life.props.clear();
            body.at={102,1.25f,-560};body.mass=body.health=body.maxHealth=900;body.forward=body.liquidForward={0,0,1};controller.velocity={};life.absorbedMass=800;
            for(int i=0;i<6;++i)LifeTakeHit(life,1);body.health=body.maxHealth;
            const Vec offsets[]={{-14,0,30},{12,0,34},{-22,0,14},{20,0,18},{0,0,42},{-6,0,-24}};uint32_t id=9001;
            for(auto offset:offsets){Vec at=body.at+offset;enemies.push_back(CitySpawnEnemy({CityEnemy::Rat,at,at},id++,life.step));enemies.back().at.y=CityStats(CityEnemy::Rat).scale;}
            ++life.revision;}
        for(auto& enemy:enemies){enemy.attackDelay=60;enemy.shotClock=60;}
        float before=life.age;Game::GameScene::Update();++frames_;crewClock_+=life.age-before;
        // 本体からの一番遠い子分と、同時に狙っている敵の種類の数
        std::vector<uint32_t> hunts;
        for(size_t i=1;i<life.bodies.size();++i){const auto& c=life.bodies[i];if(c.mass<=0||c.down)continue;crewDistance_=std::max(crewDistance_,LifeDistance(c.at,body.at));
            if(c.hunt!=0xffffffffu&&std::find(hunts.begin(),hunts.end(),c.hunt)==hunts.end())hunts.push_back(c.hunt);}
        crewSpread_=std::max(crewSpread_,int(hunts.size()));
        crewKills_=0;for(const auto& enemy:enemies)if(!SwarmAlive(enemy))++crewKills_;
        if(crewPhase_==0&&crewClock_>2.5f&&!crewImages_[0]){Capture(L"tests/out/slime-life-crew-hunt.png");crewImages_[0]=true;}
        if(crewPhase_==0&&crewClock_>5){Capture(L"tests/out/slime-life-crew-spread.png");crewImages_[1]=true;crewPhase_=1;crewClock_=0;}
        if(crewPhase_==1&&crewClock_>1.5f){Capture(L"tests/out/slime-life-crew-offscreen.png");crewImages_[2]=true;crewPhase_=2;crewClock_=0;}
        if(frames_%60==0){trace_<<"crew phase="<<crewPhase_<<" distance="<<crewDistance_<<" spread="<<crewSpread_<<" hits="<<life.childHits<<" kills="<<crewKills_<<" children="<<life.ChildCount()<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if((crewPhase_==2&&crewClock_>6)||real>120){bool pass=crewPhase_==2&&crewDistance_>14&&crewSpread_>=2&&life.childHits>0&&crewKills_>=2&&crewImages_[2];
            std::ofstream("tests/out/slime-life-crew.txt")<<(pass?"PASS":"FAIL")<<" crew farthest="<<crewDistance_<<" spread="<<crewSpread_<<" child_hits="<<life.childHits<<" kills="<<crewKills_<<" children="<<life.ChildCount();
            PostQuitMessage(pass?0:2);}
    }
    int showPhase_=0,showReadyFrames_=0;float showClock_=0,showSun_=0,showCloseDistance_=1e9f;size_t showProp_=0;bool showValid_=true,showFrozen_=true,showImages_[3]{};
    Game::Chrono::Vec showEnemyAt_{};
    // 太陽の明るさ
    float SunIntensity(){for(auto e:GetComponents().view<Game::NameComponent,Game::DirectionalLightComponent>())if(GetComponents().get<Game::NameComponent>(e).name=="Sun")return GetComponents().get<Game::DirectionalLightComponent>(e).intensity;return -1;}
    // ステージ開始のカメラ演出: 最後まで流れて各カットを写し、やり直した後は長押しでスキップできるか
    int introPhase_=0,introShots_=0,introSkipFrames_=0;bool introValid_=true;
    void UpdateIntro(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.intro=true;
        GetComponents().get<Player>(player).invincible=60;
        auto* chrono=GetChronoSystem();
        // 1 回目: 各カットの途中を写し、最後まで流れきるか
        if(introPhase_==0){
            const float shots[]={1.6f,4.2f,6.8f,9.f,10.7f};
            const wchar_t* images[]={L"tests/out/slime-life-intro-goal.png",L"tests/out/slime-life-intro-fire.png",L"tests/out/slime-life-intro-avenue.png",L"tests/out/slime-life-intro-alley.png",L"tests/out/slime-life-intro-descend.png"};
            Game::GameScene::Update();++frames_;
            if(introShots_<5&&chrono->StageIntroActive()&&chrono->StageIntroAge()>=shots[introShots_]){Capture(images[introShots_]);++introShots_;}
            if(!chrono->StageIntroActive()){introValid_&=introShots_==5&&chrono->StageIntroAge()>=11.f;Capture(L"tests/out/slime-life-intro-end.png");introPhase_=1;}
            else if(frames_>2400){introValid_=false;introPhase_=1;}
            return;
        }
        // やり直して演出を始め直す
        if(introPhase_==1){input.restart=true;Game::GameScene::Update();++frames_;introValid_&=chrono->StageIntroActive();introPhase_=2;return;}
        // 2 回目: 長押しでスキップし、すぐ終わるか
        input.jump=true;
        Game::GameScene::Update();++frames_;++introSkipFrames_;
        if(introSkipFrames_==20)Capture(L"tests/out/slime-life-intro-skip.png");
        if(!chrono->StageIntroActive()||introSkipFrames_>600){
            bool pass=introValid_&&!chrono->StageIntroActive()&&chrono->StageIntroAge()<2.f;
            std::ofstream("tests/out/slime-life-intro.txt")<<(pass?"PASS":"FAIL")<<" shots="<<introShots_<<" skip_age="<<chrono->StageIntroAge()<<" skip_frames="<<introSkipFrames_;
            PostQuitMessage(pass?0:1);
        }
    }
    // 変形アイテムを初めて取ると、周りが止まってカメラが正面へ寄り、変形した姿を見せてから元に戻るか
    void UpdateShowcase(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.33f;input.showcase=true;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];GetComponents().get<Player>(player).invincible=60;
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),1);body.at=CityTireItem()+Vec{0,1.25f,-6};body.forward=body.liquidForward={0,0,1};GetComponents().get<Player>(player).velocity={};++life.revision;
            float nearest=1e9f;bool found=false;
            for(size_t i=0;i<life.props.size();++i)if(life.props[i].morphItem==LifeMorphItem::Tire){float d=LifeDistance(body.at,life.props[i].at);if(d<nearest){showProp_=i;nearest=d;found=true;}}
            showValid_&=found&&nearest<life.recipe.AbsorbRange();
            // 動き回る敵を 1 体置き、見せ場の間に止まるかを見る
            enemies.clear();Vec at=body.at+Vec{-10,0,14};enemies.push_back(CitySpawnEnemy({CityEnemy::Rat,at,at+Vec{0,0,30}},9001,life.step));enemies.back().at.y=CityStats(CityEnemy::Rat).scale;enemies.back().alerted=true;
            showSun_=SunIntensity();}
        for(auto& enemy:enemies){enemy.attackDelay=60;enemy.shotClock=60;}
        if(showPhase_==0){input.absorb=true;auto direction=life.props[showProp_].at-body.at;input.yaw=std::atan2(direction.x,direction.z);}
        float before=life.age;Game::GameScene::Update();++frames_;showClock_+=std::max(life.age-before,GetChronoSystem()->MorphShowcaseActive()?1.f/60:0.f);
        bool active=GetChronoSystem()->MorphShowcaseActive();
        auto camera=GetCamera().Position();float distance=LifeDistance({camera.x,camera.y,camera.z},body.at);
        auto next=[&](){trace_<<"showcase phase="<<showPhase_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" sun="<<SunIntensity()<<" camera="<<distance<<'\n';trace_.flush();++showPhase_;showClock_=0;};
        if(showPhase_==0&&active){showEnemyAt_=enemies.empty()?Vec{}:enemies.front().at;next();}
        else if(showPhase_==1){
            if(!enemies.empty())showFrozen_&=LifeDistance(enemies.front().at,showEnemyAt_)<.001f;
            if(showClock_>.35f&&!showImages_[0]){Capture(L"tests/out/slime-life-showcase-in.png");showImages_[0]=true;}
            if(body.weapon.ready&&body.action==LifeAction::Tire){showCloseDistance_=std::min(showCloseDistance_,distance);
                if(!showImages_[1]&&++showReadyFrames_>20){showValid_&=SunIntensity()<showSun_*.6f;Capture(L"tests/out/slime-life-showcase-close.png");showImages_[1]=true;}}
            if(!active)next();}
        else if(showPhase_==2&&showClock_>.8f){
            showValid_&=body.action==LifeAction::Tire&&body.weapon.ready&&std::abs(SunIntensity()-showSun_)<.001f&&distance>showCloseDistance_*1.3f;
            Capture(L"tests/out/slime-life-showcase-after.png");showImages_[2]=true;next();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(showPhase_==3||real>90){bool pass=showPhase_==3&&showValid_&&showFrozen_&&showImages_[0]&&showImages_[1]&&showImages_[2];
            std::ofstream("tests/out/slime-life-showcase.txt")<<(pass?"PASS":"FAIL")<<" showcase phase="<<showPhase_<<" valid="<<showValid_<<" frozen="<<showFrozen_<<" close="<<showCloseDistance_<<" after="<<distance<<" sun="<<SunIntensity()<<"/"<<showSun_<<" action="<<int(body.action);
            PostQuitMessage(pass?0:2);}
    }
    int signalPhase_=0;float signalClock_=0;bool signalValid_=true,signalImages_[4]{};float signalLineHealth_=0,signalRunnerHealth_=0;std::vector<Game::Chrono::Vec> signalStopped_;
    // 大交差点で信号機を 1 周 (赤 → 青 → 黄) 使い、色ごとの効果と点検の制限を確かめる
    void UpdateSignal(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.42f;input.morphForm=4;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);controller.invincible=60;
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());life.stepDelay=120;
        // 前の 5 体は赤の範囲、遠い 1 体 (最後) は範囲外で走ってくる
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),2);enemies.clear();life.step=6;
            body.at=CityCrossing()+Vec{0,1.25f,-60};body.mass=body.health=body.maxHealth=900;body.signalUnlocked=body.tireUnlocked=true;body.forward=body.liquidForward={0,0,1};controller.velocity={};life.absorbedMass=800;
            const Vec offsets[]={{-12,0,14},{-4,0,18},{6,0,15},{14,0,20},{0,0,25},{-36,0,44}};uint32_t id=9001;
            for(auto offset:offsets){Vec at=body.at+offset,patrol=offset.z>30?body.at+Vec{36,0,44}:at;enemies.push_back(CitySpawnEnemy({CityEnemy::Soldier,at,patrol},id++,life.step));auto& e=enemies.back();e.at.y=CityStats(CityEnemy::Soldier).scale;e.health=e.maxHealth=2000;e.alerted=offset.z<30;}
            ++life.revision;}
        // 最後の 1 体は気付かせず、横に巡回させ続ける (赤の範囲の外を動いている敵)
        for(auto& e:enemies){e.attackDelay=60;e.shotClock=60;}if(enemies.size()==6)enemies[5].alerted=false;
        bool ready=body.action==LifeAction::Signal&&body.weapon.ready;
        // 数字キーと同じく、押した瞬間に信号機へ変形する
        if(body.action==LifeAction::Idle&&signalPhase_==0)input.morphNow=true;
        // 待ちが終わったら 1 回だけ押す
        bool press=ready&&life.signal.wait<=0&&!life.previousFire&&signalClock_>.3f;
        if((signalPhase_==0||signalPhase_==1||signalPhase_==2)&&press)input.attack=true;
        // 点検中に押す / 数字キーの 1 でタイヤへ、3 で信号機へ戻る (入力は更新の前に決める)
        if(signalPhase_==4)input.attack=signalClock_<.1f;
        if(signalPhase_==5||signalPhase_==6){input.morphForm=signalPhase_==5?2:4;input.morphNow=signalClock_<.05f;}
        float before=life.age;Game::GameScene::Update();++frames_;signalClock_+=life.age-before;
        auto next=[&](){trace_<<"signal phase="<<signalPhase_<<" valid="<<signalValid_<<" next="<<int(life.signal.next)<<" wait="<<life.signal.wait<<'\n';trace_.flush();++signalPhase_;signalClock_=0;};
        if(frames_%20==0&&enemies.size()==6){const auto& e=enemies[5];trace_<<"signal runner at="<<e.at.x<<","<<e.at.z<<" phase="<<int(e.phase)<<" alerted="<<e.alerted<<" stop="<<e.signalStop<<" mark="<<e.signalMark<<" hp="<<e.health<<'\n';}
        if(enemies.size()<6){signalValid_=false;signalPhase_=9;}
        // 赤: 5 体が止まり、1 秒たっても動かない。走ってくる 1 体は止まらない
        else if(signalPhase_==0&&input.attack&&ready){signalStopped_.clear();for(int i=0;i<5;++i){signalValid_&=enemies[size_t(i)].signalStop>0;signalStopped_.push_back(enemies[size_t(i)].at);}
            signalValid_&=enemies[5].signalStop==0;next();}
        else if(signalPhase_==1&&!input.attack){
            if(signalClock_>.35f&&!signalImages_[0]){Capture(L"tests/out/slime-life-signal-red.png");signalImages_[0]=true;}
            if(signalClock_>1&&signalClock_<1.1f)for(int i=0;i<5;++i)signalValid_&=LifeDistance(enemies[size_t(i)].at,signalStopped_[size_t(i)])<.05f;}
        else if(signalPhase_==1&&input.attack){signalLineHealth_=0;for(int i=0;i<5;++i)signalLineHealth_+=enemies[size_t(i)].health;next();}
        // 青: 止まっていた 5 体が前に並び、横断歩道の人影でダメージを受ける
        else if(signalPhase_==2&&!input.attack){
            if(signalClock_>.8f&&signalClock_<.9f)for(int i=0;i<5;++i)signalValid_&=enemies[size_t(i)].signalLined&&LifeDistance(enemies[size_t(i)].at,enemies[size_t(i)].signalLine)<1.5f;
            if(signalClock_>.3f&&!signalImages_[3]){Capture(L"tests/out/slime-life-signal-green-lay.png");signalImages_[3]=true;}
            if(signalClock_>1.2f&&!signalImages_[1]){Capture(L"tests/out/slime-life-signal-green.png");signalImages_[1]=true;}}
        else if(signalPhase_==2&&input.attack){float health=0;for(int i=0;i<5;++i)health+=enemies[size_t(i)].health;trace_<<"signal crossing damage="<<signalLineHealth_-health<<'\n';
            signalValid_&=signalLineHealth_-health>5*SignalWalkerDamage(body.mass)*4;signalRunnerHealth_=enemies[5].health;
            // 黄: 走ってくる 1 体にもマーカーが付く
            int marked=0;for(const auto& e:enemies)if(e.signalMark>0)++marked;signalValid_&=marked>=5&&enemies[5].signalMark>0;next();}
        else if(signalPhase_==3){
            // マーカーが付いた後に 1 体を一歩動かす (動いた敵は大爆発を受ける)
            if(signalClock_>.3f&&enemies[5].signalMark>0&&LifeDistance(enemies[5].at,enemies[5].signalMarkAt)<.1f)enemies[5].at=enemies[5].at+Vec{SignalMarkMove+.5f,0,0};
            if(signalClock_>.5f&&!signalImages_[2]){Capture(L"tests/out/slime-life-signal-yellow.png");signalImages_[2]=true;}
            if(signalClock_>SignalMarkTime+.5f){
                // 動いた 1 体は大爆発を受け、マーカーはすべて消えている。点検中は押しても信号が出ない
                trace_<<"signal runner damage="<<signalRunnerHealth_-enemies[5].health<<'\n';
                signalValid_&=signalRunnerHealth_-enemies[5].health>=SignalMarkDamage(body.mass,true)*.99f&&SignalResting(life.signal);
                for(const auto& e:enemies)signalValid_&=e.signalMark<=0;next();}}
        else if(signalPhase_==4){if(signalClock_>.5f){signalValid_&=life.signal.next==SignalColor::Red&&enemies[5].signalStop<=0;next();}}
        else if(signalPhase_==5){if(signalClock_>.2f){signalValid_&=body.action==LifeAction::Tire;next();}}
        else if(signalPhase_==6){if(signalClock_>.2f){signalValid_&=body.action==LifeAction::Signal;next();}}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(signalPhase_>=7||real>120){bool pass=signalPhase_==7&&signalValid_&&signalImages_[0]&&signalImages_[1]&&signalImages_[2]&&signalImages_[3];
            std::ofstream("tests/out/slime-life-signal.txt")<<(pass?"PASS":"FAIL")<<" signal phase="<<signalPhase_<<" valid="<<signalValid_<<" next="<<int(life.signal.next)<<" rest="<<life.signal.wait;
            PostQuitMessage(pass?0:2);}
    }
    int hydrantPhase_=0;float hydrantClock_=0,hydrantHp_=0;size_t hydrantProp_=0,hydrantWaterPeak_=0;
    bool hydrantValid_=true,hydrantAttackImage_=false,hydrantGuardImage_=false,hydrantUnprotected_=false;
    void UpdateHydrant(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.morphForm=3;input.pitch=.2f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);
        auto& enemies=const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies());
        life.stepDelay=120;controller.invincible=hydrantPhase_==4||hydrantPhase_==5?0.f:60.f;
        auto spawn=[&](CityEnemy kind,Vec at){enemies.clear();enemies.push_back(CitySpawnEnemy({kind,at,at},9001,life.step));enemies.back().at.y=CityStats(kind).scale;};
        if(frames_==0){GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),2);enemies.clear();life.step=3;
            body.at={40,1.25f,478};body.mass=body.health=body.maxHealth=320;body.forward=body.liquidForward={0,0,1};controller.velocity={};++life.revision;
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
        if(hydrantPhase_==6)AimHydrantAt(input,body,CityGateCenter()+Vec{0,8,0});
        float before=life.age;Game::GameScene::Update();++frames_;hydrantClock_+=life.age-before;
        auto next=[&](){trace_<<"hydrant valid="<<hydrantValid_<<" phase="<<hydrantPhase_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" hp="<<body.health<<'\n';trace_.flush();++hydrantPhase_;hydrantClock_=0;};
        const auto& water=GetRenderer()->GetHydrantWaterSnapshot();hydrantWaterPeak_=std::max(hydrantWaterPeak_,water.particles.size());
        // 解放前の攻撃は変形せずタックルになる (タックルで消火栓に触れて解放されてもよい)
        if(hydrantPhase_==0&&hydrantClock_>.8f){hydrantValid_&=body.action==LifeAction::Idle;Capture(L"tests/out/slime-life-hydrant-highlight.png");next();}
        else if(hydrantPhase_==1&&body.hydrantUnlocked&&body.action==LifeAction::Idle){hydrantValid_&=!life.props[hydrantProp_].active;next();}
        else if(hydrantPhase_==2&&body.action==LifeAction::Hydrant&&body.weapon.ready&&hydrantClock_>1){Capture(L"tests/out/slime-life-hydrant-form.png");spawn(CityEnemy::Shield,body.at+Vec{0,0,22});next();}
        else if(hydrantPhase_==3){
            if(hydrantClock_>1&&!hydrantAttackImage_){Capture(L"tests/out/slime-life-hydrant-attack.png");hydrantAttackImage_=true;}
            if(life.waterHits>0&&!SwarmAlive(enemies.front())){spawn(CityEnemy::Archer,body.at+Vec{0,0,28});enemies.front().health=enemies.front().maxHealth=1000;hydrantHp_=body.health;next();}}
        else if(hydrantPhase_==4){
            if(hydrantClock_>1&&!hydrantGuardImage_){Capture(L"tests/out/slime-life-hydrant-defense.png");hydrantGuardImage_=true;}
            if(hydrantClock_>4&&life.waterBlocks>=2){hydrantValid_&=body.health==hydrantHp_;hydrantHp_=body.health;next();}}
        else if(hydrantPhase_==5&&body.health<hydrantHp_){hydrantValid_&=body.weapon.ready;hydrantUnprotected_=true;enemies.clear();life.step=4;body.at={102,1.25f,906};controller.velocity={};++life.revision;next();}
        else if(hydrantPhase_==6&&GetChronoSystem()->CityGateState().gateOpen){Capture(L"tests/out/slime-life-hydrant-gate.png");next();}
        else if(hydrantPhase_==7&&body.action==LifeAction::Idle){hydrantValid_&=body.hydrantUnlocked&&!life.waterAttack&&!life.waterGuard;next();}
        else if(hydrantPhase_==8&&body.action==LifeAction::Hydrant&&body.weapon.ready){next();}
        if(frames_%120==0){trace_<<"hydrant tick phase="<<hydrantPhase_<<" clock="<<hydrantClock_<<" samples="<<water.particles.size()<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" hp="<<body.health<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(hydrantPhase_==9||real>160){bool pass=hydrantPhase_==9&&hydrantValid_&&hydrantWaterPeak_>100&&hydrantAttackImage_&&hydrantGuardImage_&&hydrantUnprotected_;
            std::ofstream("tests/out/slime-life-hydrant.txt")<<(pass?"PASS":"FAIL")<<" hydrant phase="<<hydrantPhase_<<" valid="<<hydrantValid_<<" water_peak="<<hydrantWaterPeak_<<" hits="<<life.waterHits<<" blocks="<<life.waterBlocks<<" unprotected_hit="<<hydrantUnprotected_<<" gate="<<GetChronoSystem()->CityGateState().gateOpen;
            PostQuitMessage(pass?0:2);}
    }
    // 通常のスライムと各変形を行き来する間、カメラが 1 フレームで大きく跳ばずになめらかに動くか
    int morphCameraFrame_=0;float morphCameraJump_=0,morphCameraJerk_=0;Game::Chrono::Vec morphCameraLast_{},morphCameraStep_{};bool morphCameraValid_=true;
    void UpdateMorphCamera(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];GetComponents().get<Player>(player).invincible=60;
        if(morphCameraFrame_==0){body.mass=body.health=body.maxHealth=900;life.absorbedMass=800;body.tireUnlocked=body.hydrantUnlocked=body.signalUnlocked=true;life.props.clear();++life.revision;}
        for(auto& enemy:const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies())){enemy.at=body.at+Vec{300,0,300};enemy.attackDelay=60;}
        // 2 秒ごとに ハンマー → 戻る → タイヤ → 戻る → 消火栓 → 戻る → 信号機 → 戻る
        const int forms[]={1,0,2,0,3,0,4,0};int slot=morphCameraFrame_/120-1,local=morphCameraFrame_%120;
        if(slot>=0&&slot<8&&local==0){if(forms[slot]){input.morphForm=forms[slot];input.morphNow=true;}else input.returnToSlime=true;}
        Game::GameScene::Update();++morphCameraFrame_;
        auto at=GetCamera().Position();Vec camera{at.x,at.y,at.z},step=camera-morphCameraLast_;
        // 立ち止まっているので、カメラの動きは変形による構図の変化だけ。1 フレームの移動量とその変化 (急な加速) を測る
        if(morphCameraFrame_>30){float scale=std::max(1.f,LifeScale(body.mass));morphCameraJump_=std::max(morphCameraJump_,Length(step)/scale);morphCameraJerk_=std::max(morphCameraJerk_,Length(step-morphCameraStep_)/scale);}
        morphCameraLast_=camera;morphCameraStep_=step;
        if(morphCameraFrame_>=120*9){// 修正前は 1 フレームで 8 前後跳んでいた。テストは 20fps 前後で動くので、イージング中の 1 フレームの移動はそれなりに大きい
            bool pass=morphCameraValid_&&morphCameraJump_<2.5f&&morphCameraJerk_<1.5f;
            std::ofstream("tests/out/slime-life-morph-camera.txt")<<(pass?"PASS":"FAIL")<<" max_step="<<morphCameraJump_<<" max_jerk="<<morphCameraJerk_;
            PostQuitMessage(pass?0:1);}
    }
    void UpdateCameraFacing(){
        using namespace Game;using namespace Game::Chrono;
        constexpr LifeAction forms[]={LifeAction::Idle,LifeAction::Idle,LifeAction::Tire,LifeAction::Tire,LifeAction::Tire,LifeAction::Tire};
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.35f;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];
        // 最大の大きさは、実際に育ちきって戦う広い大交差点で確かめる
        if(cameraFrame_==0){auto at=cameraPhase_==0?body.at:CityCrossing()+Vec{0,1.25f,0};life.bodies={};body.at=at;body.mass=cameraPhase_==0?100:LifeMaxMass;body.health=body.maxHealth=body.mass;body.particles=6600;body.forward=body.liquidForward={1,0,0};
            body.tireUnlocked=body.hydrantUnlocked=true;if(LifeWeaponForm(forms[cameraPhase_])){life.form=forms[cameraPhase_];LifeEnterWeapon(body,forms[cameraPhase_],{1,0,0});}
            life.props.clear();life.absorbedMass=body.mass-LifeStartMass;GetComponents().get<Player>(player).velocity={};++life.revision;}
        for(auto& enemy:const_cast<std::vector<SwarmEnemy>&>(GetChronoSystem()->LifeEnemies())){enemy.at=body.at+Vec{200,0,200};enemy.attackDelay=60;}
        GetComponents().get<Player>(player).invincible=60;
        // Move sideways first, then orbit the camera while movement is released.
        if(cameraFrame_<25)input.move={1,0,0};
        if(cameraFrame_>=25)input.yaw=float(cameraFrame_-25)*.028f;
        // 最後の段階はタイヤの溜めを押し続け、溜めている間はカメラの前を向き続けるかを見る
        if(cameraPhase_==5)input.attack=cameraFrame_>=30;
        Game::GameScene::Update();++frames_;++cameraFrame_;
        if(cameraFrame_==1){GetRenderer()->fluidMorphSeedPending_=true;GetRenderer()->fluidMorphRetargetMask_=Engine::Renderer::kFluidMorphAllBodies;}
        if(cameraFrame_>65){
            Vec cameraForward{std::sin(input.yaw),0,std::cos(input.yaw)};
            bool facesCamera=LifeFacesCamera(body.action)||(body.action==LifeAction::Tire&&body.weapon.charging);
            cameraValid_&=Dot(body.forward,facesCamera?cameraForward:Vec{1,0,0})>(facesCamera?.97f:.999f);
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
        const wchar_t* images[]={L"tests/out/slime-life-camera-small.png",L"tests/out/slime-life-camera-large.png",L"tests/out/slime-life-camera-tire.png",L"tests/out/slime-life-camera-tire-turn.png",L"tests/out/slime-life-camera-tire-orbit.png",L"tests/out/slime-life-camera-tire-charge.png"};Capture(images[cameraPhase_]);trace_<<"camera phase="<<cameraPhase_<<" valid="<<cameraValid_<<" action="<<int(body.action)<<" ready="<<body.weapon.ready<<" fwd="<<body.forward.x<<","<<body.forward.z<<'\n';trace_.flush();
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
        if(progressionFrame_==0){if(life.step==1)for(auto& enemy:enemies)if(SwarmAlive(enemy)&&enemy.id!=enemies.back().id){enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;}life.stepDelay=0;}
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
        if(progressionFrame_==40&&(life.step==0||life.step==2||life.step==4)){progressionValid_&=life.step==progressionWave_;GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),life.step/2+1);}
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
            body.tireUnlocked=true;LifeEnterWeapon(body,LifeAction::Tire,{0,0,1});absorbPhase_=5;absorbClock_=0;
        }else if(absorbPhase_==5&&life.absorbed==24){
            absorbContact_=body.mass==252&&body.health==252&&body.maxHealth==252&&body.action==LifeAction::Tire&&life.props[3].active&&life.props[4].active;
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
        constexpr LifeAction forms[]={LifeAction::Idle,LifeAction::Idle,LifeAction::Idle,LifeAction::Tire,LifeAction::Tire,LifeAction::Tire,LifeAction::Tire};
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;input.move={.25f,0,1};
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());
        if(motionFrame_==0){if(motionPhase_==0)motionOrigin_=life.bodies[0].at;life.bodies={};auto& body=life.bodies[0];
            // 最大の大きさのタイヤは、実際に育ちきって走る広い大交差点で確かめる (狭い路地では壁がカメラを押し込む)
            body.at=motionPhase_>=4?CityCrossing()+Vec{0,1.25f,0}:motionOrigin_;motionPrevious_=body.at;body.mass=masses[motionPhase_];body.health=body.maxHealth=100;body.particles=6600;body.age=2;
            GetComponents().get<Player>(player).velocity={};motionDistance_=0;
            body.tireUnlocked=true;if(forms[motionPhase_]!=LifeAction::Idle)LifeEnterWeapon(body,forms[motionPhase_],{0,0,1});
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
        if(motionFrame_==150&&motionPhase_==4)Capture(L"tests/out/slime-life-motion-giant-tire.png");
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
            body.tireUnlocked=body.hydrantUnlocked=true;if(stressPhase_==4||stressPhase_==5)LifeEnterWeapon(body,stressPhase_==4?LifeAction::Tire:LifeAction::Hydrant,{0,0,1});
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
        stressReport_<<"phase="<<stressPhase_<<" mass="<<masses[stressPhase_]<<" samples="<<stressTimes_.size()<<" frame_mean="<<total/stressTimes_.size()<<" frame_p95="<<stressTimes_[stressTimes_.size()*95/100]<<" cell="<<renderer->volumePreviousVoxelSize_<<" finite="<<finite<<" substeps="<<renderer->GetFluidProfileStats().substeps<<" selected_triangles="<<renderer->GetSceneryLodStats().selectedIndices/3<<" visible_triangles="<<renderer->GetSceneryLodStats().visibleIndices/3<<" shadow_triangles="<<renderer->GetSceneryLodStats().shadowIndices/3;
        for(size_t i=0;i<stressGpu_.size();++i)stressReport_<<" gpu"<<i<<"="<<stressGpu_[i]/stressTimes_.size();stressReport_<<'\n';stressReport_.flush();
        if(stressPhase_==3)Capture(L"tests/out/slime-life-max-size.png");
        if(!finite){stressReport_<<"FAIL nonfinite/escaped particles\n";stressReport_.flush();PostQuitMessage(2);return;}
        if(++stressPhase_==7){stressReport_<<"PASS all growth/weapon/12-child stress phases\n";stressReport_.flush();PostQuitMessage(0);return;}stressFrame_=0;
    }
    void UpdateGrowth(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        const auto& before=GetChronoSystem()->LifeState();const auto& body=before.bodies[0];
        GetComponents().get<Player>(player).invincible=60; // Observe progression without damage splits changing size.
        if(frames_==0){growthInitial_=before.ChildCount()==0&&before.ChildLimit()==2&&body.mass==LifeStartMass;growthStartScale_=LifeScale(body.mass);
            // 路地の壁はハンマーで壊すもの (別のテスト)。ここでは吸収だけで路地を回れるよう先に開ける
            GetChronoSystem()->OpenCityGates(const_cast<entt::registry&>(GetRegistry()),1);}
        // タイヤを解放する前に攻撃するとタックルになる
        if(frames_==2){input.attack=true;input.morphForm=2;}
        else if(frames_>10&&body.action==LifeAction::Idle){
            const LifeCollectible* target=nullptr;float nearest=1e9f;
            for(const auto& prop:before.props)if(prop.active&&prop.morphItem==LifeMorphItem::None&&LifeCanAbsorb(body.mass,prop.required)&&CityAreaAt(prop.at)==CityAreaAt(body.at)){
                float distance=LifeDistance(body.at,prop.at);if(distance<nearest){nearest=distance;target=&prop;}}
            if(target){auto delta=target->at-body.at;input.yaw=std::atan2(delta.x,delta.z);
                if(nearest>before.recipe.AbsorbRange()*LifeScale(body.mass)*.65f||Dot(body.forward,Unit(delta))<.98f)input.move={0,0,1};
                else input.absorb=!before.previousAbsorb;}
        }
        Game::GameScene::Update();++frames_;
        const auto& after=GetChronoSystem()->LifeState();const auto& grown=after.bodies[0];
        growthTackle_|=after.tackleTime>0&&grown.action==LifeAction::Idle;
        growthPersistent_&=!LifeWeaponForm(grown.action);
        growthInitial_&=after.ChildCount()==0&&after.splits==0;
        if(frames_>120&&!growthEarlyImage_){Capture(L"tests/out/slime-life-growth-early.png");growthEarlyImage_=true;}
        if(grown.mass>=600&&grown.action==LifeAction::Idle&&!growthLateImage_){Capture(L"tests/out/slime-life-growth-late.png");growthLateImage_=true;}
        if(frames_%60==0){trace_<<"growth mass="<<grown.mass<<" children="<<after.ChildCount()<<" absorbed="<<after.absorbedMass<<" action="<<int(grown.action)<<" at="<<grown.at.x<<","<<grown.at.z<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(growthLateImage_||real>200||GetChronoSystem()->Finished()){
            bool pass=growthLateImage_&&growthTackle_&&growthInitial_&&growthPersistent_&&LifeScale(grown.mass)>growthStartScale_*2.1f;
            uint32_t particles=0;for(const auto& b:after.bodies)particles+=b.particles;pass&=particles==Engine::Renderer::kPlayerFluidParticles;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" growth_initial="<<growthInitial_<<" early_tackle="<<growthTackle_<<" no_morph="<<growthPersistent_<<" mass="<<grown.mass<<" children="<<after.ChildCount()<<" scale="<<LifeScale(grown.mass)<<" particles="<<particles<<" collected="<<after.absorbedMass;
            PostQuitMessage(pass?0:2);
        }
    }
    int alleyPhase_=-1,alleyFrame_=0,alleySwings_=0,alleyAbsorbPhase_=0;bool alleyImages_[2]{},alleyOnlyHammer_=true,alleyAbsorbed_=false;
    // 一つ目の区画 (脱出路地) を、ハンマーだけで板塀・ブロック塀・金網を壊して抜けられるか。敵は出ない
    void UpdateAlley(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.25f;input.morphForm=1;
        auto& life=const_cast<SlimeLife&>(GetChronoSystem()->LifeState());auto& body=life.bodies[0];GetComponents().get<Player>(player).invincible=60;
        const auto& gates=GetChronoSystem()->CityGateState();const auto& walls=CityWalls();
        // 次に壊す壁 (無ければすべて壊れた)
        int next=-1;for(size_t i=0;i<walls.size();++i)if(!gates.WallOpen(i)){next=int(i);break;}
        if(next>=0){const auto& wall=walls[size_t(next)];Vec out{wall.Axis().z,0,-wall.Axis().x};
            // 開始位置に近い側から近づく
            if(LifeDistance(wall.Center()+out*11,CityStart())>LifeDistance(wall.Center()-out*11,CityStart()))out=out*-1.f;
            if(next!=alleyPhase_){alleyPhase_=next;alleyFrame_=0;
                // 壁の手前に本体を置き、ハンマーに変形できる大きさにする
                body.mass=body.health=body.maxHealth=std::max(body.mass,220.f);life.absorbedMass=std::max(life.absorbedMass,120.f);
                body.at=wall.Center()+out*11+Vec{0,1.25f,0};body.forward=body.liquidForward=out*-1.f;GetComponents().get<Player>(player).velocity={};++life.revision;}
            input.yaw=std::atan2(-out.x,-out.z);
            // 変形するまでは数字キー、変形したら壁へ寄りながら押し続けて叩く
            if(body.action!=LifeAction::Hammer){input.morphNow=alleyFrame_%20==1&&alleyAbsorbPhase_!=1;}
            // 最初の壁の前で一度だけ、ハンマーのまま吸収を 1 フレーム押し、戻るボタン無しでスライムに戻って吸収を始めるかを見る
            else if(next==0&&alleyAbsorbPhase_==0&&body.weapon.ready){input.absorb=true;alleyAbsorbPhase_=1;alleyFrame_=0;
                // 吸い込める小物を 1 つ正面に置く
                for(auto& prop:life.props)if(prop.active&&LifeCanAbsorb(body.mass,prop.required)){prop.at=body.at+body.forward*(2.5f*LifeScale(body.mass))+Vec{0,-.05f,0};break;}}
            else{input.move={0,0,1};input.attack=true;}
        }
        int before=life.step;Game::GameScene::Update();++frames_;++alleyFrame_;
        if(alleyAbsorbPhase_==1){if(body.action==LifeAction::Engulf){alleyAbsorbed_=true;alleyAbsorbPhase_=2;}else if(alleyFrame_>90)alleyAbsorbPhase_=2;}
        alleyOnlyHammer_&=body.action==LifeAction::Idle||body.action==LifeAction::Restore||body.action==LifeAction::Hammer||body.action==LifeAction::Engulf;
        if(body.weapon.active&&body.weapon.age<.02f)++alleySwings_;
        if(next==0&&body.action==LifeAction::Hammer&&body.weapon.active&&!alleyImages_[0]){Capture(L"tests/out/slime-life-alley-hammer.png");alleyImages_[0]=true;}
        if(life.step>before&&!alleyImages_[1]){Capture(L"tests/out/slime-life-alley-cleared.png");alleyImages_[1]=true;}
        if(frames_%60==0){trace_<<"alley step="<<life.step<<" wall="<<next<<" open="<<gates.wallsOpen<<" action="<<int(body.action)<<" swings="<<alleySwings_<<" at="<<body.at.x<<","<<body.at.z<<'\n';trace_.flush();}
        float real=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        bool cleared=life.step>=1&&gates.AlleyOpen()&&alleyImages_[1];
        if(cleared||real>180){bool pass=cleared&&alleyOnlyHammer_&&alleyImages_[0]&&alleyAbsorbed_&&CityStepSpawns(0).empty();
            std::ofstream("tests/out/slime-life-alley.txt")<<(pass?"PASS":"FAIL")<<" alley step="<<life.step<<" walls="<<gates.wallsOpen<<" only_hammer="<<alleyOnlyHammer_<<" absorb_return="<<alleyAbsorbed_<<" swings="<<alleySwings_<<" seconds="<<life.age;
            PostQuitMessage(pass?0:2);}
    }
    // City stage tour: teleports through the four areas with every enemy role on stage and saves one image per view.
    int tourStop_=0,tourFrame_=0;bool tourSpawned_=false;
    void UpdateCityTour(){
        using namespace Game;using namespace Game::Chrono;
        struct Stop {Vec at;float yaw,pitch,mass;const wchar_t* image;bool open;};
        static const Stop stops[]={
            {{0,0,-1200},0,.22f,100,L"tests/out/city-alley-start.png",false},
            {{0,0,-1150},0,.2f,180,L"tests/out/city-alley-wood-fence.png",false},
            {{20,0,-1046},1.5708f,.2f,200,L"tests/out/city-alley-block-wall.png",false},
            {{102,0,-1000},0,.2f,220,L"tests/out/city-alley-wire-fence.png",false},
            {{102,0,-860},0,.18f,400,L"tests/out/city-avenue-entrance.png",false},
            {{102,0,-200},0,.16f,900,L"tests/out/city-avenue.png",false},
            {{102,0,330},0,.2f,1200,L"tests/out/city-barricade.png",false},
            {{102,0,500},0,.2f,1500,L"tests/out/city-fire-scene.png",true},
            {{60,0,780},.8f,.22f,1800,L"tests/out/city-fire-gate.png",true},
            {{102,0,1030},0,.16f,3000,L"tests/out/city-crossing.png",true},
            {{-60,0,1200},.75f,.3f,5200,L"tests/out/city-crossing-crash.png",true},
            {{354,0,1596},-2.4f,.25f,5200,L"tests/out/city-crossing-corner.png",true},
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
    // 吸収モード: 全身の飛びかかり・押し続けの連続・大きすぎて弾かれる・車と塀を丸のみ・段階で時間が増える・時間切れ
    int devourPhase_=0,devourPounces_=0,devourMaxCombo_=0,devourTarget_=-1;float devourClock_=0,devourMass_=0,devourLeft_=0,devourLastAge_=0,devourBaseY_=0,devourLift_=0;int devourAbsorbed_=0;
    int devourLagStage_=-1;std::array<float,3> devourLag_{};float devourLagWorst_=0;
    int devourGrounded_=0,devourFrames_=0;Game::Chrono::DevourPick devourPrevPick_{},devourTracePick_{};bool devourTraced_=true;
    bool devourValid_=true,devourBounced_=false,devourWasPouncing_=false,devourFresh_=true;std::array<bool,5> devourImages_{};std::string devourNotes_;
    // 部屋のステージ: 時間と開始の大きさ、アイテムを拾って使うと一時的に変形し、最初の目標でクリア
    int roomPhase_=0;float roomClock_=0;bool roomValid_=true,roomMouthPlaced_=false,roomMouthSeen_=false;std::string roomNotes_;
    void UpdateRoom(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        auto* chrono=GetChronoSystem();auto& life=const_cast<SlimeLife&>(chrono->LifeState());auto& body=life.bodies[0];auto& clock=chrono->DevourClockState();const auto& stage=DevourStages()[0];
        auto place=[&](Vec at){body.at=at;body.at.y=1.25f;GetComponents().get<Player>(player).velocity={};auto& t=GetComponents().get<TransformComponent>(player);t.translate={body.at.x,body.at.y,body.at.z};};
        auto step=[&](bool ok,const char* note){roomValid_&=ok;roomNotes_+=std::string(" ")+note+"="+std::to_string(ok);++roomPhase_;roomClock_=0;};
        if(frames_>40&&roomPhase_==0){bool closed=chrono->MoveWithinCity({-200,1,-158},{-200,1,-150},1).z<-152&&chrono->MoveWithinCity({-70,1,144},{-70,1,160},1).z<150;
            step(closed&&chrono->DevourStageIndex()==0&&clock.left>stage.time-10&&clock.left<=stage.time&&body.action==LifeAction::Idle,"start");Capture(L"tests/out/slime-life-room-start.png");}
        else if(roomPhase_==1){input.attack=roomClock_>.1f&&roomClock_<.2f;if(roomClock_>.4f){step(body.action==LifeAction::Idle,"no_item_no_morph");roomPhase_=10;}}
        else if(roomPhase_==10){if(roomClock_<.05f){body.mass=body.health=body.maxHealth=300;++life.revision;}input.move={0,0,1};
            if(roomClock_>2.5f&&roomClock_<2.55f)Capture(L"tests/out/slime-life-room-bite.png");
            if(roomClock_>5){step(life.absorbed>=4,"auto_eat");Capture(L"tests/out/slime-life-room-meals.png");roomPhase_=11;}}
        // 大きい物へ触れると、口を大きく開ける
        else if(roomPhase_==11){input.yaw=1.5708f;input.pitch=.15f;
            if(roomClock_<.05f&&!roomMouthPlaced_){roomMouthPlaced_=true;float best=1e9f,r=DevourBodyRadius(body.mass);int target=-1;for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.sealed||prop.beacon||!DevourCanCover(body.mass,prop.radius)||prop.radius<r*.55f)continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;target=int(i);}}
                roomValid_&=target>=0;if(target>=0)place(life.props[size_t(target)].at+Vec{0,0,-3.2f*r});}
            input.move={roomClock_>.5f?-1.f:0.f,0,0};
            if(roomClock_>.7f&&chrono->DevourBiteAge()>DevourLandAt-.03f&&!roomMouthSeen_){roomMouthSeen_=true;Capture(L"tests/out/slime-life-room-mouth.png");}
            if(roomClock_>2.5f){step(roomMouthSeen_,"mouth");roomPhase_=13;}}
        // 坂を登ってテーブルの上へ行ける
        else if(roomPhase_==13){input.yaw=1.5708f;input.pitch=.3f;if(roomClock_<.05f)place({-70,0,-10});input.move={0,0,roomClock_>.2f?1.f:0.f};
            if(roomClock_>7&&roomClock_<7.05f)Capture(L"tests/out/slime-life-room-table.png");
            if(roomClock_>7.2f){step(body.at.y>26,"table_climb");roomPhase_=2;}}
        else if(roomPhase_==2){if(roomClock_<.05f){place(stage.itemAt+Vec{0,0,-20});body.forward=body.liquidForward={0,0,1};}input.yaw=0;
            if(roomClock_>.1f&&roomClock_<.15f)Capture(L"tests/out/slime-life-room-item.png");if(roomClock_>.15f)input.move={0,0,1};if(roomClock_>1.5f)step(chrono->DevourItemReady(),"item_taken");}
        else if(roomPhase_==3){input.attack=roomClock_>.1f&&roomClock_<.2f;if(roomClock_>1){step(body.action==LifeAction::Vacuum&&!chrono->DevourItemReady(),"morph");Capture(L"tests/out/slime-life-room-vacuum.png");}}
        else if(roomPhase_==4){if(roomClock_<.05f)chrono->DevourSetMorphLeft(.2f);if(roomClock_>1)step(body.action==LifeAction::Idle||body.action==LifeAction::Restore,"morph_ends");}
        else if(roomPhase_==5){if(roomClock_<.05f){body.mass=body.health=body.maxHealth=DevourGoalMass(stage,stage.goals[0])+1;++life.revision;}
            if(roomClock_>.5f){step(chrono->DevourRank()==1&&!chrono->Finished(),"clear_keeps_going");Capture(L"tests/out/slime-life-room-clear.png");clock.left=.05f;}}
        else if(roomPhase_==6&&roomClock_>1){bool ok=chrono->Finished();
            // プレイ記録が書かれ、結果と時刻が入っている
            {std::ifstream log(chrono->DevourLogPath());std::string text((std::istreambuf_iterator<char>(log)),{});bool logged=text.find("\"result\": \"time_up\"")!=std::string::npos&&text.find("\"goalTimes\"")!=std::string::npos&&text.find("\"samples\"")!=std::string::npos&&std::filesystem::exists("tests/out/playlogs/summary.csv");roomValid_&=logged;roomNotes_+=" play_log="+std::to_string(logged);}roomValid_&=ok;Capture(L"tests/out/slime-life-room-result.png");std::ofstream report("tests/out/slime-life-room.txt");report<<(roomValid_?"PASS":"FAIL")<<roomNotes_<<" finished="<<ok;report.flush();PostQuitMessage(roomValid_?0:2);return;}
        float before=life.age;Game::GameScene::Update();++frames_;if(roomPhase_>0)roomClock_+=std::max(life.age-before,1.f/120);
        if(frames_>3000){std::ofstream("tests/out/slime-life-room.txt")<<"FAIL timeout phase="<<roomPhase_<<roomNotes_;PostQuitMessage(2);}
    }
    void UpdateDevour(){
        GetChronoSystem()->DevourUseAllForms();
        using namespace Game;using namespace Game::Chrono;
        // 進み具合 (遅すぎるときの調査用)
        if(frames_%60==0){trace_<<"devour frame="<<frames_<<" phase="<<devourPhase_<<" real="<<std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count()<<'\n';trace_.flush();}
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.33f;
        auto* chrono=GetChronoSystem();auto& life=const_cast<SlimeLife&>(chrono->LifeState());auto& body=life.bodies[0];auto& controller=GetComponents().get<Player>(player);
        auto& clock=chrono->DevourClockState();
        auto setMass=[&](float mass){body.mass=body.health=body.maxHealth=mass;body.action=LifeAction::Idle;body.actionAge=0;++life.revision;};
        // 物の手前 distance に置き、そちらを向かせる
        auto placeBefore=[&](Vec at,float distance){Vec from=at+Vec{0,0,-distance};from.y=at.y-1.2f+1.25f;body.at=from;body.forward=body.liquidForward={0,0,1};controller.velocity={};
            auto& t=GetComponents().get<TransformComponent>(player);t.translate={from.x,from.y,from.z};};
        // 近くの覆える (覆えない) 物で、moreThan より大きい物を探す
        auto nearest=[&](float limit,bool fits,float moreThan=0){int best=-1;float d=limit;
            for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.visual==0xffffffffu||prop.reach>0||prop.radius<=moreThan||DevourCanCover(body.mass,prop.radius)!=fits)continue;
                float distance=LifeDistance(prop.at,body.at);if(distance<d){d=distance;best=int(i);}}return best;};
        bool pouncing=body.action==LifeAction::Engulf;
        if(pouncing&&(!devourWasPouncing_||body.actionAge<devourLastAge_)){devourTracePick_=devourPrevPick_;devourTraced_=false;}
        // 調査用: 飛びかかりごとに、選んだ目標と覆い終えたときの結果を記録する
        if(pouncing&&!devourTraced_&&body.actionAge>DevourCoverEnd){devourTraced_=true;const auto& pick=devourTracePick_;
            trace_<<"pounce phase="<<devourPhase_<<" mass="<<body.mass<<" target="<<pick.index<<" fits="<<pick.fits;
            if(pick.index>=0){const auto& prop=life.props[size_t(pick.index)];trace_<<" radius="<<prop.radius<<" eaten="<<!prop.active<<" gap="<<LifeDistance(prop.at,body.at)<<" dy="<<prop.at.y-body.at.y;}
            trace_<<'\n';}
        devourPrevPick_=DevourChooseTarget(body.at,body.forward,body.mass,life.props);
        if(pouncing&&(!devourWasPouncing_||body.actionAge<devourLastAge_))++devourPounces_;devourWasPouncing_=pouncing;devourLastAge_=body.actionAge;
        devourMaxCombo_=std::max(devourMaxCombo_,clock.combo);
        auto next=[&](){devourNotes_+=" |p"+std::to_string(devourPhase_)+":a"+std::to_string(life.absorbed)+"t"+std::to_string(chrono->DevourRank())+"l"+std::to_string(int(clock.left))+"f"+std::to_string(chrono->Finished())+"m"+std::to_string(int(body.mass));++devourPhase_;devourClock_=0;devourFresh_=true;};
        float before=life.age;const int phaseAtStart=devourPhase_;
        // 実際の遊び方: 開始位置から前へ進みながら吸収ボタンを連打して、ちゃんと飲み込めるか
        if(devourPhase_==0&&frames_>30){if(frames_==31){devourClock_=0;devourAbsorbed_=life.absorbed;devourPounces_=0;devourGrounded_=devourFrames_=0;}
            input.move={0,0,devourClock_<6?1.f:0.f};input.absorb=std::fmod(devourClock_,.45f)<.12f;
            ++devourFrames_;if(controller.grounded)++devourGrounded_;
            if(devourClock_>8){bool ok=life.absorbed>=devourAbsorbed_+2;devourValid_&=ok;
                devourNotes_+=" play_pounces="+std::to_string(devourPounces_)+" play_eaten="+std::to_string(life.absorbed-devourAbsorbed_)+" grounded="+std::to_string(devourGrounded_*100/std::max(1,devourFrames_))+"%";next();devourPhase_=9;}}
        else if(devourPhase_==9){devourValid_&=chrono->DevourMode()&&chrono->LifeEnemies().empty()&&clock.left>DevourStages()[1].time-12;
            devourTarget_=nearest(400,true);devourValid_&=devourTarget_>=0;
            if(devourTarget_>=0){body.action=LifeAction::Idle;body.actionAge=0;placeBefore(life.props[size_t(devourTarget_)].at,8);devourMass_=body.mass;devourAbsorbed_=life.absorbed;}
            devourNotes_+=" no_enemies="+std::to_string(chrono->LifeEnemies().empty());next();devourPhase_=1;}
        // 1 回飛びかかる: 跳ぶ・覆う・満足の画像を撮り、飲み込んで大きくなったか
        // (カメラを横へ回してから跳び、包み込みの形を横から撮る)
        else if(devourPhase_==1){input.yaw=1.5708f;input.pitch=.12f;input.absorb=devourClock_>.8f&&devourClock_<.85f;
            const wchar_t* images[]={L"tests/out/slime-life-devour-leap.png",L"tests/out/slime-life-devour-cover.png",L"tests/out/slime-life-devour-settle.png"};const float at[]={DevourWindup+DevourLeap*.6f,DevourLandAt+.03f,DevourCoverEnd+.12f};
            for(int i=0;i<3;++i)if(pouncing&&body.actionAge>=at[i]&&!devourImages_[size_t(i)]){Capture(images[i]);devourImages_[size_t(i)]=true;}
            if(devourClock_>1.9f){bool ok=devourTarget_>=0&&life.absorbed>devourAbsorbed_&&body.mass>devourMass_&&!life.props[size_t(devourTarget_)].active;devourValid_&=ok;devourNotes_+=" single="+std::to_string(ok);next();}}
        // 押し続けると、近くの物へ次々に飛びかかる
        else if(devourPhase_==2){input.absorb=true;if(devourFresh_){devourAbsorbed_=life.absorbed;devourPounces_=0;}
            int target=nearest(60,true);if(target>=0&&(body.action!=LifeAction::Engulf||body.actionAge>=DevourCoverEnd)){Vec d=life.props[size_t(target)].at-body.at;d.y=0;if(Length(d)>.1f)body.forward=body.liquidForward=Unit(d);}
            if(devourClock_>5){bool ok=devourPounces_>=3&&devourPounces_<=6;devourValid_&=ok;
                devourNotes_+=" held_pounces="+std::to_string(devourPounces_)+" held_eaten="+std::to_string(life.absorbed-devourAbsorbed_)+" max_combo="+std::to_string(devourMaxCombo_);next();devourPhase_=20;}}
        // 連打しても空へ上っていかない (何も無い所へ何度も飛びかかり、高さが変わらないか)
        else if(devourPhase_==20){if(devourFresh_){devourBaseY_=body.at.y;devourLift_=0;}
            input.absorb=(frames_/4)%2==0;devourLift_=std::max(devourLift_,body.at.y-devourBaseY_);
            if(devourClock_>3){bool ok=devourLift_<1.5f;devourValid_&=ok;devourNotes_+=" mash_lift="+std::to_string(devourLift_);next();devourPhase_=26;}}
        // 輪の壁は通れない。門をハンマーで砕くと壁が崩れて通れる
        else if(devourPhase_==26){input.yaw=0;input.morphForm=1;const float wall=RingWallAt(1);
            if(devourFresh_){setMass(300);devourTarget_=-1;float best=1e9f;for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||!prop.beacon||prop.sealed)continue;
                    float d=std::abs(prop.at.x)+std::abs(prop.at.z-wall);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=3;return;}
                bool closed=chrono->MoveWithinCity({60,1,wall-20},{60,1,wall+20},1).z<wall;devourValid_&=closed;devourNotes_+=" wall_closed="+std::to_string(closed);
                placeBefore(life.props[size_t(devourTarget_)].at,6*LifeScale(body.mass));}
            input.morphNow=devourClock_<.05f;input.attack=devourClock_>.5f;
            if(devourClock_>2.5f){bool ok=!life.props[size_t(devourTarget_)].active&&chrono->MoveWithinCity({60,1,wall-20},{60,1,wall+20},1).z>wall;devourValid_&=ok;devourNotes_+=" gate_open="+std::to_string(ok);
                Capture(L"tests/out/slime-life-devour-gate.png");chrono->DevourOpenAllRings();next();devourPhase_=3;}}
        // 大きすぎる車には弾かれる
        else if(devourPhase_==3){if(devourFresh_){setMass(300);devourTarget_=nearest(3000,false,10);devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();return;}
                placeBefore(life.props[size_t(devourTarget_)].at,16);
                // 手前の小物を選ばないよう、近くの他の物は片付けておく
                for(size_t i=0;i<life.props.size();++i)if(int(i)!=devourTarget_&&LifeDistance(life.props[i].at,body.at)<40)life.props[i].active=false;}
            input.absorb=devourClock_>.1f&&devourClock_<.3f;
            if(devourClock_>.1f&&pouncing&&body.actionAge>DevourLandAt+.02f&&body.actionAge<DevourCoverEnd&&!devourImages_[3]){Capture(L"tests/out/slime-life-devour-bounce.png");devourImages_[3]=true;devourBounced_=Dot(Vec{controller.velocity.x,0,controller.velocity.z},body.forward)<0;}
            if(devourClock_>1.2f){bool ok=devourBounced_&&life.props[size_t(devourTarget_)].active;devourValid_&=ok;devourNotes_+=" bounce="+std::to_string(ok);next();}}
        // 大きくなれば車を丸のみにでき、体の中に見える
        else if(devourPhase_==4){if(devourFresh_){setMass(8000);devourTarget_=nearest(3000,true,10);devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();return;}placeBefore(life.props[size_t(devourTarget_)].at,40);}
            input.absorb=devourClock_>.4f&&devourClock_<.6f;
            if(devourClock_>.4f&&pouncing&&body.actionAge>DevourCoverEnd+.15f&&!devourImages_[4]){Capture(L"tests/out/slime-life-devour-car.png");devourImages_[4]=true;}
            if(devourClock_>2){bool ok=!life.props[size_t(devourTarget_)].active;devourValid_&=ok;devourNotes_+=" car="+std::to_string(ok);next();}}
        // 家 (建物) も大きさが足りれば丸のみにでき、跡を通れる
        else if(devourPhase_==5){if(devourFresh_){devourTarget_=-1;float best=1e9f;
                for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.reach<10||prop.radius>40)continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();return;}
                const auto& house=life.props[size_t(devourTarget_)];setMass(DevourMassToCover(house.radius)*1.05f);
                placeBefore(house.at,house.reach+3*LifeScale(body.mass)+4);}
            input.absorb=devourClock_>.4f&&devourClock_<.6f;
            if(devourClock_>1.6f){const auto& house=life.props[size_t(devourTarget_)];
                // 飲み込めて、跡の真ん中を通れる
                bool ok=!house.active&&chrono->MoveWithinCity(house.at,house.at+Vec{1,0,0},1).x>house.at.x+.5f;
                devourValid_&=ok;devourNotes_+=" house="+std::to_string(ok);Capture(L"tests/out/slime-life-devour-house.png");next();devourPhase_=21;}}
        // 建物を背にすると、カメラは手前へ寄らず、間の建物が透ける
        else if(devourPhase_==21){input.yaw=0;
            if(devourFresh_){setMass(150);devourTarget_=-1;float best=1e9f;
                for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.reach<10||prop.radius>40)continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=6;return;}
                // 建物の北の壁のすぐ外に置き、南 (建物の側) から見る
                const auto& house=life.props[size_t(devourTarget_)];Vec at=house.at+Vec{0,0,house.reach+8};at.y=1.25f;body.at=at;body.forward=body.liquidForward={0,0,1};controller.velocity={};
                auto& t=GetComponents().get<TransformComponent>(player);t.translate={at.x,at.y,at.z};}
            if(devourClock_>1.5f){auto camera=GetCamera().Position();float distance=LifeDistance({camera.x,camera.y,camera.z},body.at);
                float faded=1;for(auto e:GetComponents().view<CameraOccluder>())faded=std::min(faded,GetComponents().get<CameraOccluder>(e).opacity);
                bool ok=distance>12&&faded<.5f;devourValid_&=ok;
                devourNotes_+=" see_through="+std::to_string(ok)+" cam_dist="+std::to_string(int(distance))+" faded="+std::to_string(faded);
                Capture(L"tests/out/slime-life-devour-seethrough.png");next();devourPhase_=23;}}
        // ハンマー: 大きすぎる建物を叩くと砕けて、飲める欠片が散らばる
        else if(devourPhase_==23){input.yaw=0;input.morphForm=1;
            if(devourFresh_){devourTarget_=-1;float best=1e9f;
                for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.reach<10||prop.radius>40)continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=24;return;}
                const auto& house=life.props[size_t(devourTarget_)];setMass(DevourMassToShatter(house.radius)*1.1f);placeBefore(house.at,house.reach+2*LifeScale(body.mass));devourAbsorbed_=int(life.props.size());}
            input.morphNow=devourClock_<.05f;input.attack=devourClock_>.5f;
            if(devourClock_>2.5f){const auto& house=life.props[size_t(devourTarget_)];bool ok=!house.active&&int(life.props.size())==devourAbsorbed_+DevourShardCount&&DevourCanCover(body.mass,life.props.back().radius);
                devourValid_&=ok;devourNotes_+=" hammer_shatter="+std::to_string(ok);Capture(L"tests/out/slime-life-devour-shatter.png");next();devourPhase_=24;}}
        // タイヤ: 転がるだけで、普段は沈まない大きさの物を通り道で吸い込む
        else if(devourPhase_==24){input.yaw=0;input.morphForm=2;
            if(devourFresh_){setMass(3000);body.tireUnlocked=true;devourTarget_=-1;float best=1e9f;
                for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.reach>0||prop.visual==0xffffffffu||!DevourCanCover(body.mass,prop.radius))continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=6;return;}
                placeBefore(life.props[size_t(devourTarget_)].at,4*LifeScale(body.mass));}
            input.morphNow=devourClock_<.05f;input.move={0,0,devourClock_>.5f?1.f:0.f};
            if(devourClock_>3){bool ok=!life.props[size_t(devourTarget_)].active;devourValid_&=ok;devourNotes_+=" tire_sink="+std::to_string(ok);next();devourPhase_=27;}}
        // 掃除機: 押している間、前の小物を吸い込む
        else if(devourPhase_==27){input.yaw=devourClock_>1.6f?1.5708f:0;input.pitch=.2f;input.morphForm=3;
            if(devourFresh_){setMass(3000);body.vacuumUnlocked=true;devourTarget_=-1;float best=1e9f;
                for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.sealed||prop.reach>0||!DevourCanCover(body.mass,prop.radius))continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=28;return;}
                placeBefore(life.props[size_t(devourTarget_)].at,14*LifeScale(body.mass));devourAbsorbed_=life.absorbed;}
            input.morphNow=devourClock_<.05f;input.attack=devourClock_>.5f&&devourClock_<1.5f;input.move={0,0,devourClock_>.9f&&devourClock_<1.5f?1.f:0.f};
            if(devourClock_>1.4f&&devourClock_<1.45f)Capture(L"tests/out/slime-life-devour-vacuum-back.png");
            if(devourClock_>1.72f&&devourNotes_.find("vacuum_eaten")==std::string::npos){bool ok=life.absorbed>devourAbsorbed_;devourValid_&=ok;Capture(L"tests/out/slime-life-devour-vacuum-side.png");devourNotes_+=" vacuum_eaten="+std::to_string(life.absorbed-devourAbsorbed_);}
            if(devourClock_>2.2f){next();devourPhase_=28;}}
        // バネ: 溜めて離すと建物を飛び越えて、向こうの道に着地する
        else if(devourPhase_==28){input.yaw=devourClock_>.4f?1.5708f:0;input.pitch=.2f;input.morphForm=4;
            if(devourFresh_){devourTarget_=-1;float best=1e9f;for(size_t i=0;i<life.props.size();++i){const auto& prop=life.props[i];if(!prop.active||prop.sealed||prop.reach<10||prop.radius>40)continue;
                    float d=LifeDistance(prop.at,body.at);if(d<best){best=d;devourTarget_=int(i);}}
                devourValid_&=devourTarget_>=0;if(devourTarget_<0){next();devourPhase_=6;return;}
                setMass(6000);body.springUnlocked=true;const auto& house=life.props[size_t(devourTarget_)];placeBefore(house.at,house.reach+2*LifeScale(body.mass));devourBaseY_=body.at.y;devourLift_=0;devourMass_=body.at.z;}
            input.morphNow=devourClock_<.05f;input.attack=devourClock_>.6f&&devourClock_<1.4f;input.move=devourClock_>1.3f&&devourClock_<1.5f?Vec{-1,0,0}:Vec{};
            if(devourClock_>1.2f&&devourClock_<1.25f)Capture(L"tests/out/slime-life-devour-spring-charge.png");
            devourLift_=std::max(devourLift_,body.at.y-devourBaseY_);
            if(devourClock_>.52f&&devourClock_<.57f)Capture(L"tests/out/slime-life-devour-spring-idle.png");
            if(devourClock_>1.75f&&devourClock_<1.8f)Capture(L"tests/out/slime-life-devour-spring-air.png");
            if(devourClock_>3.4f){const auto& house=life.props[size_t(devourTarget_)];bool ok=devourLift_>DevourSpringRise(6000,DevourSpringMinPower)*.8f&&house.active&&body.at.z>devourMass_+20&&std::abs(body.at.y-devourBaseY_)<2;
                devourValid_&=ok;devourNotes_+=" spring_hop="+std::to_string(ok)+" lift="+std::to_string(int(devourLift_))+" moved="+std::to_string(int(body.at.z-devourMass_));Capture(L"tests/out/slime-life-devour-spring-land.png");next();devourPhase_=6;}}
        // 大きくなっても遠くまで見え、動いても体が見える (撮った画面で確かめる)
        else if(devourPhase_==22){input.yaw=.7f;
            if(devourFresh_){setMass(1.2e6f);Vec at{0,1.25f,0};body.at=at;controller.velocity={};auto& t=GetComponents().get<TransformComponent>(player);t.translate={at.x,at.y,at.z};}
            if(devourClock_>2&&devourClock_<6)input.move={0,0,1};
            if(devourClock_>2.5f&&devourNotes_.find("big_still")==std::string::npos){Capture(L"tests/out/slime-life-devour-big-still.png");devourNotes_+=" big_still";}
            if(devourClock_>5.5f){Capture(L"tests/out/slime-life-devour-big-moving.png");
                auto camera=GetCamera().Position();devourNotes_+=" big_cam="+std::to_string(int(LifeDistance({camera.x,camera.y,camera.z},body.at)))+" big_at="+std::to_string(int(body.at.x))+","+std::to_string(int(body.at.z));
                next();devourPhase_=25;}}
        // 大きくなっても液体の体が中心から置いていかれない: 歩く・飛びかかる・タイヤの突進で、粒子の重心のずれを体の半径で測る
        else if(devourPhase_==25){constexpr float masses[]={8000,120000,1.2e6f};input.yaw=.7f;
            int stage=std::min(2,int(devourClock_/9));float t=devourClock_-float(stage)*9;
            if(stage!=devourLagStage_){if(devourLagStage_>=0)devourNotes_+=" lag"+std::to_string(int(masses[devourLagStage_]))+"="+std::to_string(devourLag_[0])+"/"+std::to_string(devourLag_[1])+"/"+std::to_string(devourLag_[2]);
                devourLagStage_=stage;devourLag_={};setMass(masses[stage]);body.tireUnlocked=true;Vec at{0,1.25f,0};body.at=at;controller.velocity={};auto& tr=GetComponents().get<TransformComponent>(player);tr.translate={at.x,at.y,at.z};}
            input.move={0,0,1};input.morphForm=2;input.absorb=t>2.5f&&t<5.5f;input.morphNow=t>6&&t<6.05f;input.attack=t>6.8f&&t<7.8f;
            const auto& snapshot=GetRenderer()->GetFluidBodySnapshot();
            if(t>.6f&&!snapshot.offsets.empty()){Vec mean{};for(auto o:snapshot.offsets){mean.x+=o.x;mean.z+=o.z;}mean=mean*(1.f/float(snapshot.offsets.size()));
                float lag=std::sqrt(mean.x*mean.x+mean.z*mean.z)/DevourBodyRadius(body.mass);auto& slot=devourLag_[t<2.5f?0:t<6?1:2];slot=std::max(slot,lag);devourLagWorst_=std::max(devourLagWorst_,lag);}
            if(devourClock_>27){devourNotes_+=" lag"+std::to_string(int(masses[2]))+"="+std::to_string(devourLag_[0])+"/"+std::to_string(devourLag_[1])+"/"+std::to_string(devourLag_[2]);
                devourValid_&=devourLagWorst_<1;setMass(masses[0]);next();devourPhase_=7;}}
        // 時間は減り、目標に届くとランクが上がる
        else if(devourPhase_==6){const auto& stage=DevourStages()[1];if(devourFresh_)devourLeft_=clock.left;
            if(devourClock_>1&&devourNotes_.find("ticking")==std::string::npos){bool ticking=clock.left<devourLeft_-.8f&&clock.left>devourLeft_-1.3f;
                setMass(DevourGoalMass(stage,stage.goals[0])+1);devourNotes_+=" ticking="+std::to_string(ticking);devourValid_&=ticking;}
            if(devourClock_>1.4f){bool ok=chrono->DevourRank()>=1;devourValid_&=ok;devourNotes_+=" rank_up="+std::to_string(ok);next();devourPhase_=22;}}
        // S ランクでも終わらず、時間切れで終わって結果を出す
        else if(devourPhase_==7){const auto& stage=DevourStages()[1];if(devourFresh_)setMass(DevourGoalMass(stage,stage.goals.back())+1);
            if(devourClock_>.3f&&devourNotes_.find("keeps_going")==std::string::npos){bool ok=chrono->DevourRank()==int(stage.goals.size())&&!chrono->Finished();devourValid_&=ok;devourNotes_+=" keeps_going="+std::to_string(ok);clock.left=.05f;}
            if(devourClock_>1.5f){bool ok=chrono->Finished();devourValid_&=ok;Capture(L"tests/out/slime-life-devour-timeup.png");devourNotes_+=" timeup="+std::to_string(ok);next();}}
        else if(devourPhase_==8){std::ofstream report("tests/out/slime-life-devour.txt");
            report<<(devourValid_?"PASS":"FAIL")<<devourNotes_<<" mass="<<body.mass<<" absorbed="<<life.absorbed<<" left="<<clock.left<<'\n';report.flush();PostQuitMessage(devourValid_?0:2);return;}
        Game::GameScene::Update();++frames_;if(devourPhase_==phaseAtStart){devourFresh_=false;devourClock_+=std::max(life.age-before,1.f/120);}
        if(frames_>20000){std::ofstream report("tests/out/slime-life-devour.txt");report<<"FAIL timeout phase="<<devourPhase_<<devourNotes_<<'\n';PostQuitMessage(2);}
    }
    // 物差しの UI のスライムの撮影: 本物の GPU 流体を、同じ瞬間に黄と青の背景で 2 回撮る。
    // 差から透明度を取り出す (Docs/UIDesign/chiritsumo/matte_slime.py)。idle は待機、roll は右へ転がる
    int spriteFrame_=0,spritePose_=0,spriteStep_=0;bool spriteHidden_=false;uint32_t spriteCube_=0,spriteWhite_=0;Engine::Vector4 spriteBack_{.96f,.77f,0,1};
    static constexpr int SpriteIdle=12,SpriteRoll=10;
    void UpdateSpriteCapture(){
        using namespace Game;using namespace Game::Chrono;
        auto* render=Engine::Renderer::GetInstance();
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.08f;
        auto* chrono=GetChronoSystem();auto& life=const_cast<SlimeLife&>(chrono->LifeState());auto& body=life.bodies[0];
        chrono->DevourClockState().left=170;GetComponents().get<Player>(player).invincible=60;
        if(!spriteHidden_){spriteHidden_=true;spriteCube_=render->LoadObjMesh("Resources/Models/cube/cube.obj");spriteWhite_=render->LoadTexture2D("Resources/Textures/white1x1.png");
            std::filesystem::create_directories("tests/out/sprite");}
        if(++spriteFrame_%30==1)for(auto [e,mr]:const_cast<entt::registry&>(GetRegistry()).view<MeshRendererComponent>().each())mr.enabled=false;
        // 周りの物に反応して口を開かないよう、飲める物を無くす
        for(auto& prop:life.props)prop.sealed=true;
        const bool rolling=spritePose_>=SpriteIdle;
        if(spritePose_>=SpriteIdle+SpriteRoll){std::ofstream("tests/out/sprite/done.txt")<<"PASS";PostQuitMessage(0);return;}
        // 待機の間は顔をカメラへ向ける。転がる間は右へ動かす
        if(!rolling)body.forward=body.liquidForward={0,0,-1};else input.move={1,0,0};
        const int warm=spritePose_==0?150:spritePose_==SpriteIdle?60:rolling?4:6;
        if(spriteStep_<warm){render->SetRogueWorldFrozen(false);spriteBack_={.96f,.77f,0,1};Game::GameScene::Update();++spriteStep_;
            return;}
        render->SetRogueWorldFrozen(true);const int local=spriteStep_-warm;++spriteStep_;
        wchar_t path[96];const wchar_t* kind=rolling?L"roll":L"idle";const int index=rolling?spritePose_-SpriteIdle:spritePose_;
        if(local==5){swprintf_s(path,L"tests/out/sprite/%s_%02d_a.png",kind,index);Capture(path);spriteBack_={.02f,.2f,.92f,1};}
        if(local==11){swprintf_s(path,L"tests/out/sprite/%s_%02d_b.png",kind,index);Capture(path);++spritePose_;spriteStep_=0;}
    }
    // 終了の演出の撮影: 物を実際に飲ませて育ててから時間切れにして、巻尺で測るまでを連続で撮る
    float resultPlay_=0;bool hudShot_=false;int resultShot_=0;std::chrono::steady_clock::time_point resultFinished_{};
    void UpdateResultCapture(){
        using namespace Game;using namespace Game::Chrono;
        auto player=FindObjectByName("Player");auto& input=GetComponents().get<ControlFrame>(player);input={};input.pitch=.3f;
        auto* chrono=GetChronoSystem();auto& life=const_cast<SlimeLife&>(chrono->LifeState());auto& body=life.bodies[0];auto& clock=chrono->DevourClockState();
        GetComponents().get<Player>(player).invincible=60;
        if(!chrono->Finished()){
            const auto& stage=chrono->DevourStageInfo();
            if(frames_==60){const wchar_t* goal=wcsstr(GetCommandLineW(),L"--result-goal=");const int index=goal?_wtoi(goal+14):0;const float size=goal?stage.goals[std::min<size_t>(size_t(std::max(index,0)),stage.goals.size()-1)]*1.02f:stage.goals.front()*.9f;
                if(index>=0){body.mass=body.health=body.maxHealth=DevourGoalMass(stage,size);++life.revision;}}
            if(frames_>60){input.move={0,0,1};input.yaw=resultPlay_*.5f;resultPlay_+=1.f/60;}
            if(resultPlay_>6&&!hudShot_){hudShot_=true;Capture(L"tests/out/hud-play.png");}
            if(resultPlay_>9)clock.left=.01f;
            if(frames_>40000){PostQuitMessage(2);return;}
            resultFinished_=std::chrono::steady_clock::now();Game::GameScene::Update();++frames_;return;
        }
        Game::GameScene::Update();++frames_;
        const float age=std::chrono::duration<float>(std::chrono::steady_clock::now()-resultFinished_).count();
        const float at[]={.12f,.4f,.75f,1.1f,1.5f,2.f,3.f,4.5f};
        if(resultShot_<8&&age>=at[resultShot_]){wchar_t path[64];swprintf_s(path,L"tests/out/result-seq-%d.png",resultShot_++);Capture(path);}
        if(resultShot_>=8){std::ofstream("tests/out/result-seq.txt")<<"PASS absorbed="<<life.absorbed<<" mass="<<body.mass;
            // 結果からタイトルへの切り替えも見るときは、外から画面を撮るので終わらない
            if(wcsstr(GetCommandLineW(),L"--slime-result-to-title"))Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");else PostQuitMessage(0);}
    }
    void Capture(const wchar_t* path){
        DirectX::ScratchImage image;auto result=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(result))result=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);
        trace_<<"capture="<<result<<'\n';
    }
public:
    void Initialize(Engine::WindowDX* window,const Engine::SceneParameters&)override {
        window_=window;Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();Game::GameScene::Initialize(window,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));start_=std::chrono::steady_clock::now();trace_.open("tests/out/slime-life-trace.txt");
        auto* renderer=Engine::Renderer::GetInstance();auto settings=renderer->GetGraphicsSettings();settings.rtShadows=settings.rtReflections=settings.rtIndirect=false;renderer->ApplyGraphicsSettings(settings);
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        if(wcsstr(GetCommandLineW(),L"--slime-sprite-capture")){UpdateSpriteCapture();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-result-capture")){UpdateResultCapture();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-room")){UpdateRoom();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-devour")){UpdateDevour();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-intro")){UpdateIntro();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-morph-camera")){UpdateMorphCamera();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-crew")){UpdateCrew();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-signal")){UpdateSignal();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-alley")){UpdateAlley();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-showcase")){UpdateShowcase();return;}
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
            // 分裂した本体を守りながらタイヤの溜め突進を試し、その後は本物の敵で負けるまで確かめる
            draining_=dashed_&&life.childHits>0;
            GetComponents().get<Player>(player).invincible=draining_?0.f:60.f;
        }
        if(elapsed_>3&&!turnOnly){
            const SwarmEnemy* target=nullptr;float distance=1e9f;
            for(const auto& enemy:GetChronoSystem()->LifeEnemies())if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,body.at);if(d<distance){distance=d;target=&enemy;}}
            if(target){auto delta=target->at-body.at;input.yaw=std::atan2(delta.x,delta.z);input.move=distance>(draining_?2.f:5.f)?Vec{0,0,1}:Vec{};}
            input.morphForm=2;const_cast<LifeBody&>(body).tireUnlocked=true;
            if(life.splits>0&&!draining_&&body.action==LifeAction::Idle&&body.cooldown<=0&&frames_%2==0)input.attack=true;
            // タイヤになったら溜めきるまで押し、離して突進する
            if(life.splits>0&&!draining_&&body.action==LifeAction::Tire&&body.weapon.ready)input.attack=!(body.weapon.charging&&body.weapon.charge>=1)&&!(life.previousFire&&!body.weapon.charging);
            if(body.action==LifeAction::Tire&&body.weapon.charging&&body.weapon.charge>.5f&&!tireImage_){Capture(L"tests/out/slime-life-tire-charge.png");tireImage_=true;}
            if(body.action==LifeAction::Tire&&body.weapon.dash>0)dashed_=true;
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
            bool pass=noAuto_&&heldOnce_&&damageSplit_&&splitConserved_&&cursorFree_&&editorCursorFree_&&labClick_&&labSave_&&labFrozen_&&labLive_&&labMaterial_&&labFlight_&&hpGrowthValid_&&dead&&after.childHits>0&&engulfImage_&&tireImage_&&dashed_;
            Capture(L"tests/out/slime-life-game-over.png");
            auto summary=after;input.restart=true;Game::GameScene::Update();const auto& restarted=GetChronoSystem()->LifeState();
            bool reset=std::abs(restarted.TotalMass()-LifeStartMass)<.01f&&restarted.bodies[1].mass==0&&restarted.bodies[0].health==100&&!GetChronoSystem()->Finished()&&restarted.recipe.mix==summary.recipe.mix&&Length(restarted.recipe.color.ToRGB()-summary.recipe.color.ToRGB())<.001f;
            trace_<<"reset mass="<<restarted.TotalMass()<<" hp="<<restarted.bodies[0].health<<" water="<<restarted.recipe.mix[0]<<" expected="<<summary.recipe.mix[0]<<'\n';trace_.flush();
            pass=pass&&reset;
            std::ofstream("tests/out/slime-life-smoke.txt")<<(pass?"PASS":"FAIL")<<" manual="<<noAuto_<<" held_once="<<heldOnce_<<" damage_split="<<damageSplit_<<" split_mass="<<splitConserved_<<" cursor_free="<<cursorFree_<<" editor_cursor="<<editorCursorFree_<<" lab_click="<<labClick_<<" lab_save="<<labSave_<<" lab_frozen="<<labFrozen_<<" live="<<labLive_<<" material="<<labMaterial_<<" flight="<<labFlight_<<" hp_growth_valid="<<hpGrowthValid_<<" game_over="<<dead<<" child_hits="<<summary.childHits<<" hammer_hits="<<summary.hammerHits<<" dashed="<<dashed_<<" reset="<<reset<<" frames="<<frames_;
            PostQuitMessage(pass?0:2);
        }
    }
    void Draw()override {
        // スライムの撮影: 背景の壁と床 (世界の物は隠してある)
        if(spriteCube_){auto* render=Engine::Renderer::GetInstance();auto& body=GetChronoSystem()->LifeState().bodies[0];GetCamera().SetProjection(.34f,1266.f/729.f,.1f,2000);
            Engine::Transform wall;wall.translate={body.at.x,100,body.at.z+60};wall.scale={600,300,1};render->DrawMesh(spriteCube_,spriteWhite_,wall,spriteBack_);
            Engine::Transform floor;floor.translate={body.at.x,-20,body.at.z};floor.scale={600,20.02f,600};render->DrawMesh(spriteCube_,spriteWhite_,floor,spriteBack_);}
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
