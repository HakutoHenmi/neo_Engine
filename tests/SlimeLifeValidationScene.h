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
    std::ofstream trace_;
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
        // 測るまでと、体の中へ入って飲みこんだ物を並べるまで。最後の 1 枚は升を 1 つ大きく見せる
        const float at[]={.12f,.4f,.75f,1.1f,1.5f,2.f,2.8f,3.3f,3.9f,4.6f,6.f,9.f,10.5f};constexpr int shots=int(sizeof(at)/sizeof(at[0]));
        if(resultShot_==shots-1&&age>=at[resultShot_]-.6f)chrono->OpenShelfDetailForTest(0);
        if(resultShot_<shots&&age>=at[resultShot_]){wchar_t path[64];swprintf_s(path,L"tests/out/result-seq-%d.png",resultShot_++);Capture(path);}
        if(resultShot_>=shots){std::ofstream("tests/out/result-seq.txt")<<"PASS absorbed="<<life.absorbed<<" mass="<<body.mass<<" shelf="<<chrono->ShelfCellCountForTest();
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
        // 撮影用の起動引数と、部屋のテスト。それ以外は輪の町 (吸収モード) のテスト
        if(wcsstr(GetCommandLineW(),L"--slime-sprite-capture")){UpdateSpriteCapture();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-result-capture")){UpdateResultCapture();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-life-room")){UpdateRoom();return;}
        UpdateDevour();
    }
    void Draw()override {
        // スライムの撮影: 背景の壁と床 (世界の物は隠してある)
        if(spriteCube_){auto* render=Engine::Renderer::GetInstance();auto& body=GetChronoSystem()->LifeState().bodies[0];GetCamera().SetProjection(.34f,1266.f/729.f,.1f,2000);
            Engine::Transform wall;wall.translate={body.at.x,100,body.at.z+60};wall.scale={600,300,1};render->DrawMesh(spriteCube_,spriteWhite_,wall,spriteBack_);
            Engine::Transform floor;floor.translate={body.at.x,-20,body.at.z};floor.scale={600,20.02f,600};render->DrawMesh(spriteCube_,spriteWhite_,floor,spriteBack_);}
        Game::GameScene::Draw();
    }
};
