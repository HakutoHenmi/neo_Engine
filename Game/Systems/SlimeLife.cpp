// SlimeLife.cpp
// スライムの生活モード (現在のゲームの本編。ChronoSystem の一部)。
// 吸収で成長し、タイヤと消火栓に変形して街を進む。調合室 (研究所) の画面もここにある。
#include "ChronoSystemInternal.h"
#include "../Chrono/RoomRules.h"
#include "../Chrono/DevourAnimation.h"
#include <map>

namespace Game {
// このファイルの中だけで使う補助の関数
namespace {
// 調合室 (研究所) を使うか。コア部分ができるまでは無効にして、すぐ街から始める
constexpr bool kSlimeLabEnabled=false;
// スライムの色を描画用の色にする / 数値を小数 1 桁の文字列にする
Engine::Vector4 LifeColor(const SlimeColor& color){auto rgb=color.ToRGB();return {rgb.x,rgb.y,rgb.z,1};}
std::string LifeNumber(float value){char text[32];snprintf(text,sizeof(text),"%.1f",value);return text;}
// 子分の縁取りと印の色 (元気なら水色、体力が減るほど赤くなる)
Engine::Vector4 LifeChildTint(const LifeBody& child){
    float health=std::clamp(child.health/std::max(1.f,child.maxHealth)*1.5f-.2f,0.f,1.f);
    return {1+(.35f-1)*health,.3f+(1-.3f)*health,.2f+(.95f-.2f)*health,1};
}
// 武器の形のメッシュ (1: ハンマー, 2: 剣, それ以外: 飛行機)
const Engine::FluidShape::Mesh& LifeWeaponMesh(int shape){
    // 武器のメッシュは一度だけ読み込んで使い回す
    static const Engine::FluidShape::Mesh hammer("Resources/Models/SlimeLife/hammer.obj"),sword("Resources/Models/SlimeLife/sword.obj"),plane("Resources/Models/SlimeLife/plane.obj");
    return shape==1?hammer:shape==2?sword:plane;
}
// 形ごとの粒子の配置 (0: 球、4: タイヤ、5: 消火栓、それ以外は武器のメッシュ) を作って使い回す
const std::vector<Engine::FluidShape::Point>& LifeCloud(int shape,uint32_t count){
    using namespace Engine::FluidShape;
    // 形と粒子数ごとの点の配置のキャッシュ
    static std::map<std::pair<int,uint32_t>,std::vector<Point>> cache;
    auto key=std::make_pair(shape,count);auto found=cache.find(key);if(found!=cache.end())return found->second;
    // 20〜25: 部屋のギミックの形 (ボール・水鉄砲・ドミノ・紙飛行機・流れる膜・しずく)
    if(Chrono::GimmickShape(shape)){std::vector<Point> points;points.reserve(count);const auto box=Chrono::GimmickShapeBounds(shape);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*box.x,Halton(i,3)*box.y,(Halton(i,5)*2-1)*box.z};
            if(Chrono::GimmickInside(shape,{p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    // 0: 楕円体の中に置く
    if(shape==0)return cache.emplace(key,Sample(nullptr,count)).first->second;
    // 4: タイヤの形の中に Halton 列で点を置き、Morton 順に並べる
    if(shape==4){
        std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*LifeTireTube,Halton(i,3)*LifeTireRadius*2,(Halton(i,5)*2-1)*LifeTireRadius};
            if(LifeInsideTire({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});
        return cache.emplace(key,std::move(points)).first->second;
    }
    // 5: 消火栓の形の中に点を置く
    if(shape==5){std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*2.2f,Halton(i,3)*5.1f,(Halton(i,5)*2-1)*2.2f};
            if(LifeInsideHydrant({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    // 6: 信号機の形の中に点を置く
    // 7: 掃除機、8: バネの形の中に点を置く
    if(shape==7){std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*DevourVacuumHalf,Halton(i,3)*DevourVacuumTop,DevourVacuumBack+Halton(i,5)*(DevourVacuumFront-DevourVacuumBack)};
            if(DevourInsideVacuum({p.x,p.y,p.z})&&(!DevourVacuumBody({p.x,p.y,p.z})||Halton(i,7)<DevourVacuumBodyDensity))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    if(shape==8){std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*DevourSpringPlateRadius,Halton(i,3)*DevourSpringHeight,(Halton(i,5)*2-1)*DevourSpringPlateRadius};
            if(DevourInsideSpring({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    if(shape==6){std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*2.3f,Halton(i,3)*SignalHeight,(Halton(i,5)*2-1)*1.2f};
            if(LifeInsideSignal({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    // それ以外: 武器のメッシュの中に点を置く
    return cache.emplace(key,Sample(&LifeWeaponMesh(shape),count)).first->second;
}
void LifeInterruptTopology(SlimeLife& life){
    // 持ち主が変わった後は正しい武器を作り直す。小さすぎる武器と、
    // 途中で止めた取り込みだけが液体に戻る。確定した成長は残す
    // 武器の形の体だけを調べる
    for(auto& b:life.bodies)if(b.mass>0&&b.action!=LifeAction::Idle){
        if(LifeCanMorph(b,b.action))LifeRetargetWeapon(b);
        else LifeReturnToSlime(b);
    }
}
// 現在の行動 (吸収と回復) にかかる時間
float LifeDuration(const LifeBody& body,const SlimeRecipe& recipe){return body.action==LifeAction::Engulf?recipe.AbsorbDuration():body.restoreDuration;}
}
// カメラが映す対象 (中心と半径)。液体のときは体の大きさ、武器のときは攻撃の動き全体を包む範囲
LifeCameraSubject ChronoSystem::GetLifeCameraSubject()const{
    // 本体の大きさの倍率
    const auto& body=life_.bodies[0];float scale=LifeScale(body.mass);
    if(!LifeWeaponForm(body.action)){
        // 液体のときは、動きで伸びた分も含めた体の半径
        auto radii=LifeRadii(body,life_.recipe);
        float radius=std::max({radii.x*(1+.55f*body.motion),radii.y,radii.z*(1+.55f*body.motion)})*1.25f+.8f*scale;
        return {body.at+body.fluidOffset+V{0,-1.25f+radii.y*.5f+.8f*body.air,0},radius};
    }
    // 攻撃の動き全体の範囲を使い、振りやカメラの回り込みでもズームを安定させる
    // タイヤと消火栓は形が決まっているので、その大きさを使う
    if(body.action==LifeAction::Tire)return {body.at+V{0,-1.25f+LifeTireRadius*scale,0},(LifeTireRadius+.9f)*scale};
    if(body.action==LifeAction::Hydrant)return {body.at+V{0,-1.25f+2.5f*scale,0},4.2f*scale};
    if(body.action==LifeAction::Signal)return {body.at+V{0,-1.25f+3.4f*scale,0},4.8f*scale};
    if(body.action==LifeAction::Vacuum)return {body.at+body.forward*scale+V{0,-1.25f+2.8f*scale,0},6.4f*scale};
    if(body.action==LifeAction::Spring)return {body.at+V{0,-1.25f+4.9f*scale,0},6.2f*scale};
    // 元の頂点はシミュレーションの点群と同じ正規化を使う
    // ハンマー・剣・飛行機それぞれの攻撃の全フレームの頂点から、包む球を一度だけ求める
    static const auto envelopes=[](){std::array<LifeCameraSubject,3> result{};
        for(int shape=1;shape<=3;++shape){const auto& mesh=LifeWeaponMesh(shape);auto form=LifeAction(int(LifeAction::Hammer)+shape-1);
            auto& subject=result[size_t(shape-1)];subject.center={0,(shape==1?2.7f:shape==2?.8f:.9f)*mesh.scale,0};subject.radius=0;
            for(int frame=0;frame<=96;++frame)for(const auto& vertex:mesh.vertices){V point{vertex.x*mesh.scale,vertex.y*mesh.scale,vertex.z*mesh.scale};
                auto posed=LifeWeaponPoseAt(form,point,true,LifeWeaponDuration(form)*float(frame)/96,float(frame)*.13f);
                subject.radius=std::max(subject.radius,Length(posed-subject.center));}
            subject.radius=subject.radius*1.05f+.9f;
        }return result;
    }();
    // 今の武器の範囲を、体の位置と大きさに合わせる
    auto subject=envelopes[size_t(int(body.action)-int(LifeAction::Hammer))];
    subject.center=body.at+V{0,-1.25f,0}+subject.center*scale;subject.radius*=scale;return subject;
}
// 調合室のレシピを JSON に保存する (一時ファイルに書いてから置き換える)
bool ChronoSystem::SaveSlimeLab(){
    // 自動テスト中は保存しない
    if(slimePlayerDemo_ || wcsstr(GetCommandLineW(),L"--slime-life-smoke"))return true;
    // 保存先 (記録と同じフォルダ) に、版・選んだスロット・材料・クリア数・レシピを書く
    try{auto path=RecordPath().parent_path()/L"slime-lab-v1.json";std::filesystem::create_directories(path.parent_path());
        nlohmann::json data{{"version",2},{"slot",lifeRecipeSlot_},{"materials",lifeMaterials_},{"clears",lifeClears_},{"devourBests",devourBests_},{"devourStage",devourStage_}};
        // 図鑑の記録 (Collection::Add が書く) は残す
        try{std::ifstream old(path);nlohmann::json previous;if(old&&(old>>previous)&&previous.contains("collection"))data["collection"]=previous["collection"];}catch(...){}
        data["recipes"]=nlohmann::json::array();for(const auto& recipe:lifeRecipes_){auto rgb=recipe.color.ToRGB();data["recipes"].push_back({{"mix",recipe.mix},{"color",{rgb.x,rgb.y,rgb.z}},{"colorHSV",{recipe.color.h,recipe.color.s,recipe.color.v}}});}
        // 一時ファイルに書いてから置き換え、途中で壊れないようにする
        auto temp=path;temp+=L".tmp";{std::ofstream file(temp);if(!file)throw std::runtime_error("Recipe save failed");file<<data.dump(2);if(!file)throw std::runtime_error("Recipe save failed");}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Recipe replace failed");
        return true;
    // 保存に失敗したらメッセージを出す
    }catch(...){life_.message="SAVE FAILED / PLEASE CHECK STORAGE";life_.messageAge=4;return false;}
}
// スライムの生活モードを作る: 保存したレシピを読み込み、体・敵・取り込める物・門・音を初期化する
void ChronoSystem::BuildSlimeLife(entt::registry& r){
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::BuildSlimeLife");
    // 最初の 1 回だけ保存を読み込む (自動テスト中は読まない)
    if(!lifeLoaded_){lifeLoaded_=true;
        if(!slimePlayerDemo_ && !wcsstr(GetCommandLineW(),L"--slime-life-smoke"))try{
            std::ifstream file(RecordPath().parent_path()/L"slime-lab-v1.json");nlohmann::json data;if(file){file>>data;
                // スロット・材料の数・クリア数を読み、範囲に収める
                lifeRecipeSlot_=std::clamp(data.value("slot",0),0,2);lifeMaterials_=std::clamp(data.value("materials",3),3,4);lifeClears_=std::max(0,data.value("clears",0));if(data.contains("devourBests")&&data["devourBests"].is_array())for(size_t i=0;i<devourBests_.size()&&i<data["devourBests"].size();++i)devourBests_[i]=std::max(0.f,data["devourBests"][i].get<float>());
                // 各レシピの配合と色 (新しい HSV・古い RGB・さらに古い番号の形式に対応)
                const auto& recipes=data.at("recipes");for(size_t i=0;i<lifeRecipes_.size()&&i<recipes.size();++i){auto& out=lifeRecipes_[i];out.mix=recipes[i].at("mix").get<std::array<int,4>>();
                    for(int& n:out.mix)n=std::clamp(n,0,100);if(lifeMaterials_<4)out.mix[3]=0;if(out.mix[0]+out.mix[1]+out.mix[2]+out.mix[3]==0)out.mix[0]=40;
                    if(recipes[i].contains("colorHSV")){auto c=recipes[i].at("colorHSV").get<std::array<float,3>>();for(float& v:c)if(!std::isfinite(v))v=0;out.color={std::clamp(c[0],0.f,1.f),std::clamp(c[1],0.f,1.f),std::clamp(c[2],0.f,1.f)};}
                    else if(recipes[i].contains("color")&&recipes[i]["color"].is_array()){auto c=recipes[i]["color"].get<std::array<float,3>>();out.color=SlimeColor::FromRGB({c[0],c[1],c[2]});}
                    else out.color=SlimeColor::Legacy(recipes[i].value("color",0));}}
        // 保存が壊れていたら既定のレシピを使う
        }catch(...){OutputDebugStringA("Slime Lab: using default recipe after invalid save.\n");}
    }
    // 生活の状態を作り直し、選んでいるレシピを使う
    life_={};life_.revision=GetTickCount64();life_.recipe=lifeRecipes_[size_t(lifeRecipeSlot_)];lifeLastAction_.fill(-1);lifeLastCounts_={};
    // 消火栓の水の読み戻しを止める
    Engine::Renderer::GetInstance()->EnableHydrantWaterReadback(false);hydrantWaterSerial_=0;
    // 調合室は使わない
    life_.lab=false;
    lifeWasLab_=life_.lab;lifeReseed_=true;lifePreviewEmitted_=false;
    // 調合室の見本のスライム (地上から離れた高い所に置く)
    life_.preview.at={0,1201.25f,0};life_.preview.mass=LifeStartMass;life_.preview.particles=Engine::Renderer::kPlayerFluidParticles;life_.preview.forward={0,0,-1};
    // プレイヤーを街の開始位置に置く
    auto& p=r.get<Player>(player_);p.mass=LifeStartMass;p.grounded=true;p.velocity={};p.collisionScale=1;
    auto& t=r.get<TransformComponent>(player_);t.translate=slimePlayerDemo_?DirectX::XMFLOAT3{0,1.25f,0}:DirectX::XMFLOAT3{CurrentDevourStage().spawn.x,1.25f+std::max(0.f,CurrentDevourStage().spawn.y),CurrentDevourStage().spawn.z};t.scale={1,1,1};
    // 本体と子の体を初期化する
    LifeInitializeBodies(life_,Engine::Renderer::kPlayerFluidParticles);
    auto& body=life_.bodies[0];body.at=Read(t.translate);
    if(slimePlayerDemo_){
        body.mass=body.health=body.maxHealth=600;body.tireUnlocked=body.hydrantUnlocked=body.signalUnlocked=body.vacuumUnlocked=body.springUnlocked=true;
        life_.absorbedMass=500;p.mass=600;life_.recipe.color={.23f,.8f,.9f};
        const float scale=LifeScale(body.mass);t.scale={scale,scale,scale};
        for(int i=0;i<24;++i){LifeCollectible prop;prop.at={-20.f+float(i%6)*8,1.f,18.f+float(i/6)*9};
            prop.required=1;prop.reward=25;prop.orb=true;life_.props.push_back(prop);}
    }
    // 体力を 100 にする
    auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(slimePlayerDemo_?600.f:100.f);health.SetHp(slimePlayerDemo_?600.f:100.f);health.SetDead(false);
    // 強化メニューを止め、群れのモードで戦う
    auto& ink=r.get<InkPlayer>(player_);ink.rogue.enabled=false;ink.rogue.menu=false;ink.battlePhase=BattlePhase::Horde;
    ink.hordeEnabled=true;ink.swarmKills=0;
    // 旧モードのムービーは使わず、ステージ開始のカメラ演出から始める
    lifeHitStop_=0;lifeAbsorbBuffer_=0;cameraBlend_={};cameraHydrant_=0;
    // 最初の段階の敵を出す
    swarm_.clear();swarmEffects_.clear();life_.signal={};life_.signalNotice=0;signalWalks_.clear();signalPulses_.clear();
    ink.swarmSpawned=ink.swarmAlive=int(swarm_.size());
    // 取り込める物は街の景観から作る。鉄の門は騎士団が倒れた後に加わる
    // 街の小物を取り込める物として登録する
    // 吸収モードでは、増える量を物の大きさから決める
    devourGates_.clear();ringOpen_={};ringOpen_[0]=true;devourRingOpened_=0;if(devour_&&CurrentDevourStage().map!="ring")ringOpen_.fill(true);
    for(const auto& seed:cityProps_){LifeCollectible prop;prop.ring=seed.ring;prop.foot=seed.foot;prop.height=seed.height;prop.sealed=devour_&&seed.ring>0;prop.beacon=seed.beacon;if(seed.gate)devourGates_[life_.props.size()]=seed.gate;prop.at=seed.at;prop.required=seed.required;prop.reward=devour_?DevourReward(seed.radius)*seed.fill:seed.reward;
        prop.visual=uint32_t(seed.entity);prop.morphItem=seed.morphItem;prop.radius=seed.radius;prop.reach=seed.reach;life_.props.push_back(prop);}
    // 部屋のギミック: 動かす物を止め、ばらまく物を隠す
    if(devour_)ResetDevourGimmicks(r);
    // 吸収モード: 路地の塀とバリケードも、大きくなれば覆って飲み込める
    devourWallProps_.clear();devourBarricadeProp_=-1;
    devourClock_={};devourHeli_={};devourPounce_={};devourMeals_.clear();devourEaten_.clear();shelf_={};Engine::Renderer::GetInstance()->SetFluidInterior(0,0);devourSplashes_.clear();devourDigest_=0;devourGulp_={};devourPounceWait_=0;devourBiteAge_=-1;devourChew_=0;devourSizePulse_=0;devourPops_.clear();devourNewBest_=false;devourRank_=0;if(devour_)devourClock_.left=CurrentDevourStage().time;StartDevourTutorial();
#ifndef NDEBUG
    // 確認用: --devour-time=秒 で制限時間を差し替える
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-time="))devourClock_.left=float(_wtof(arg+14));
    // 確認用: --devour-cm=大きさ で体の大きさを差し替える
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-cm=");arg&&devour_&&!life_.bodies.empty())life_.bodies[0].mass=Chrono::DevourGoalMass(CurrentDevourStage(),float(_wtof(arg+12)));
    // 確認用: --devour-no-tutorial でベッドの下のチュートリアルを飛ばす (ランクの知らせなど、本番の表示を見るため)
    if(wcsstr(GetCommandLineW(),L"--devour-no-tutorial"))devourTutorial_.step=-1;
    // 確認用: --devour-at=x,z,向き(度) で開始の位置と視点の向きを差し替える (ギミックの看板の乗り越えなどを見るため)
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-at=");arg&&devour_&&!life_.bodies.empty()){float x=0,z=0,deg=0;
        if(swscanf_s(arg+12,L"%f,%f,%f",&x,&z,&deg)>=2){V at{x,InkGround(V{x,1,z})+1.25f,z};life_.bodies[0].at=at;r.get<TransformComponent>(player_).translate=Write(at);yaw_=deg*3.1415927f/180;}}
#endif
    devourItemReady_=false;devourMorphLeft_=0;devourSucks_.clear();devourSuckClock_=0;devourSucking_=false;devourHop_={};devourHopCooldown_=0;devourStepPush_=0;devourStun_=0;devourBumpCool_=0;devourHeavy_=0;devourHeavyPeak_=0;devourHeavyKick_=0;devourGrow_={};wetMarks_.clear();wetHasLast_=false;devourBumpMemory_=0;devourFlies_.clear();
    StartDevourLog();if(devour_)devourLog_.bestBefore=devourBests_[size_t(devourStage_)];
    // 門・バリケード・弾などの状態を初期化する
    cityGates_={};if(slimePlayerDemo_)cityGates_=CityGates::AllOpen();cityGateUnlocked_=false;cityBarricadeHealth_=CityBarricadeHealth();cityGateHealth_=CityGateHealth();cityAnnounced_=-1;cityHintClock_=0;
    ResetCityHazards();life_.form=slimePlayerDemo_?LifeAction::Tire:LifeForms()[0];
    if(r.valid(cityGateEntity_))if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=true;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();
    // 前回取り込んで隠した小物を表示し直す
    for(auto e:r.view<NameComponent,MeshRendererComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono City prop",0)==0)r.get<MeshRendererComponent>(e).enabled=true;
    // 流体の見本と変形を初期化する
    scene_->ResetFluidPreview();
    auto* render=Engine::Renderer::GetInstance();render->SetRogueWorldFrozen(false);render->SetFluidMorphTargets({},0);
    render->SetFluidTendrils({});
    // 分裂と飲み込みの音を一度だけ読み込む
    if(lifeEngulfSound_==0xffffffffu){auto* audio=Engine::Audio::GetInstance();
        for(int i=0;i<3;++i)lifeSplitSounds_[i]=audio->Load("Resources/Sound/slime-life-split-"+std::to_string(i)+".wav");
        lifeEngulfSound_=audio->Load("Resources/Sound/slime-life-engulf.wav");}
    LoadDevourSounds();
}
// スライムが被弾したとき: 体力を減らし、分裂した子を外へ飛ばす。武器の形なら攻撃を止める
void ChronoSystem::DamageSlimeLife(entt::registry& r,Player& p,float amount,GameContext& ctx,V source){
    // 無敵中・終了後は受けない
    if(amount<=0||p.invincible>0||finished_)return;
    // 吸収モード: 体力は無く、体の一部が剥がれて小さくなる
    if(devour_){(void)r;DevourShedBody(p,ctx,amount,source);return;}
    // 本体の体力を減らし、分裂した子の番号を受け取る
    auto& body=life_.bodies[0];float previousHp=body.health;int childIndex=LifeTakeHit(life_,amount);
    // 無敵時間と被弾の記録
    p.invincible=.7f;p.damageAge=0;p.damageSource=source;++p.stats.hitsTaken;
    // 子が分かれたら、攻撃の反対側へ飛ばし、変形を整え直して音とメッセージを出す
    if(childIndex>0){
        auto& child=life_.bodies[size_t(childIndex)];V away=body.at-source;away.y=0;
        if(Length(away)<.01f)away={body.forward.z,0,-body.forward.x};else away=Unit(away);
        child.at=body.at+away*(2.55f*(LifeScale(body.mass)+LifeScale(child.mass))+.7f);
        child.at.y=InkGround(child.at)+1.25f;
        LifeInterruptTopology(life_);
        auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[life_.splits%3],false,.65f*audio->GetMasterSEVolume(),1+.05f*life_.recipe.Ratio(1));
        life_.message="被弾分裂：本体HP -"+LifeNumber(previousHp-body.health)+" / 子の大きさ "+LifeNumber(child.mass);life_.messageAge=2;
    // 武器の形なら攻撃を止め (大きさが足りなければ戻る)、それ以外の行動はスライムに戻る
    }else if(LifeWeaponForm(body.action)){LifeStopWeaponAttack(body);if(!LifeCanMorph(body,body.action))LifeReturnToSlime(body);}
    else if(body.action!=LifeAction::Idle)LifeReturnToSlime(body);
    ++life_.revision;
    // 泡の配合なら攻撃の反対側へ押される
    if(life_.recipe.Ratio(3)>0){V away=body.at-source;away.y=0;if(Length(away)>.01f)p.velocity=p.velocity+Unit(away)*(14*life_.recipe.Ratio(3));}
    // 体力の表示を更新し、画面を揺らす
    auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(body.maxHealth);health.SetHp(body.health);health.SetDead(body.health<=0);
    {V push=body.at-source;push.y=0;push=Length(push)>.01f?Unit(push)*.6f+V{0,.8f,0}:V{0,1,0};LifeImpact(ctx,ImpactPlayerHurt,body.at,push,std::clamp(amount/10,.8f,1.6f));}
    // 体力が尽きたら負け
    if(body.health<=0)Finish(p,false);
}
// 子分の自律行動: 本体から離れて敵を狩り、物を集め、弱ったら本体のそばへ戻って回復する
void ChronoSystem::TickLifeChild(entt::registry& r,Player& p,GameContext& ctx,size_t index,float dt){
    auto& b=life_.bodies[index];const auto& body=life_.bodies[0];V previousAt=b.at;
    float distance=LifeDistance(b.at,body.at),scale=LifeScale(b.mass),speed=life_.recipe.ChildSpeed();
    // 目標へ向かう (stop の距離を残して止まる)
    auto moveTo=[&](V goal,float stop,float pace){V direction=goal-b.at;direction.y=0;float length=Length(direction);
        if(length>stop+.05f){b.forward=direction*(1/length);b.at=b.at+b.forward*std::min(length-stop,dt*pace);}};
    if(b.down){ /* 倒れた子はここに残る。近づいても本体の体力は回復しない */ }
    else if(distance>LifeChildLostRange){
        // 壁や門の向こうではぐれた: 本体の横へ戻る
        float angle=float(index)*2.399963f;b.at=ConstrainLifeMovement(body.at,body.at+V{std::sin(angle)*6,0,std::cos(angle)*6},2);b.hunt=0xffffffffu;}
    else{
        // 体力が減ったら撤退し、本体のそばで回復してから戦いに戻る
        if(b.health<b.maxHealth*LifeChildRetreatHealth)b.retreating=true;
        if(b.retreating&&b.health>=b.maxHealth*.9f)b.retreating=false;
        if(b.retreating||distance>LifeChildLeash){
            b.hunt=0xffffffffu;moveTo(body.at,5*LifeScale(body.mass)+3,speed*1.35f);
            if(b.retreating&&distance<14)b.health=std::min(b.maxHealth,b.health+b.maxHealth*.18f*dt);
        }else if(b.action==LifeAction::Idle){
            // 獲物: 今の獲物を追い続け、倒すか本体から離れすぎたら探し直す
            SwarmEnemy* prey=nullptr;
            for(auto& enemy:swarm_)if(enemy.id==b.hunt&&SwarmAlive(enemy)&&LifeDistance(enemy.at,body.at)<LifeChildHuntRange+10){prey=&enemy;break;}
            if(!prey){float best=1e9f;
                for(auto& enemy:swarm_){if(!SwarmAlive(enemy))continue;float d=LifeDistance(enemy.at,b.at);
                    if(d>LifeChildSight||LifeDistance(enemy.at,body.at)>LifeChildHuntRange)continue;
                    // 他の子が狙っている敵は避けて、子分を敵の群れに散らす
                    int claimed=0;for(size_t other=1;other<life_.bodies.size();++other){const auto& o=life_.bodies[other];if(other!=index&&o.mass>0&&!o.down&&o.hunt==enemy.id)++claimed;}
                    float score=d+12.f*float(claimed);if(score<best){best=score;prey=&enemy;}}
                b.hunt=prey?prey->id:0xffffffffu;}
            if(prey){
                // 体当たりの間合いまで詰める。待ち時間の間は獲物の周りを回り込む
                float reach=3.1f*scale+prey->scale;
                if(b.cooldown>0&&LifeDistance(prey->at,b.at)<reach+4){V around=b.at-prey->at;around.y=0;around=Unit(around);V side{-around.z,0,around.x};if(index%2)side=side*-1.f;
                    moveTo(prey->at+Unit(around+side*.8f)*(reach+1.5f),0,speed*.8f);}
                else moveTo(prey->at,reach*.6f,speed);
            }else{
                // 獲物がいなければ、取り込める物を集めるか、本体の周りを広く巡回する
                const LifeCollectible* food=nullptr;float best=LifeChildSight;
                for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];
                    if(!prop.active||!LifeCanAbsorb(b.mass,prop.required)||LifeTargetReserved(life_,0x80000000u|uint32_t(i)))continue;
                    float d=LifeDistance(prop.at,b.at);if(d<best&&LifeDistance(prop.at,body.at)<LifeChildHuntRange){best=d;food=&prop;}}
                if(food)moveTo(food->at,scale,speed);
                else{b.patrolClock-=dt;
                    if(b.patrolClock<=0||LifeDistance(b.patrol,b.at)<2){float seed=float(index)*2.399963f+b.age*.37f;
                        float angle=seed+1.7f*std::sin(seed*3.1f),radius=14+12*(.5f+.5f*std::sin(seed*1.9f));
                        b.patrol=body.at+V{std::sin(angle)*radius,0,std::cos(angle)*radius};b.patrolClock=2.5f+1.5f*(.5f+.5f*std::sin(seed*5.3f));}
                    moveTo(b.patrol,0,speed*.7f);}
            }
            // 正面に取り込める物があれば取り込み、無ければ獲物に体当たりして少し跳ね返る
            if(!StartLifeAbsorb(int(index))&&prey&&LifeDistance(prey->at,b.at)<3.1f*scale+prey->scale&&b.cooldown<=0){
                V back=b.at-prey->at;back.y=0;
                LifeHit(r,p,ctx,prey->id,life_.recipe.ChildDamage()*scale,LifeAction::Idle,b.at);b.cooldown=life_.recipe.ChildCooldown();++life_.childHits;
                b.at=b.at+Unit(back)*(1.2f*scale);}
        }
        // 子同士は重ならないように押し合う
        for(size_t other=1;other<life_.bodies.size();++other){const auto& o=life_.bodies[other];if(other==index||o.mass<=0||o.down)continue;
            V apart=b.at-o.at;apart.y=0;float d=Length(apart),minimum=2.2f*(scale+LifeScale(o.mass));
            if(d>.01f&&d<minimum)b.at=b.at+apart*((minimum-d)/d*.5f*std::min(1.f,dt*8));}
    }
    // 通れる範囲に収め、地面に合わせて動きを更新する
    if(!b.down)b.at=ConstrainLifeMovement(previousAt,b.at,2.4f*scale*.7f);
    b.at.y=InkGround(b.at)+1.25f;
    LifeTickMotion(b,life_.recipe,dt>0?(b.at-previousAt)*(1/dt):V{},true,false,dt);
}
// 取り込みを始める: 口の前の取り込める物や敵を近い順に選び、触手でつかむ
bool ChronoSystem::StartLifeAbsorb(int bodyIndex){
    // 待機中の体だけが取り込みを始められる
    auto& b=life_.bodies[size_t(bodyIndex)];if(b.mass<=0||b.down||b.action!=LifeAction::Idle)return false;
    b.grabs.clear();
    // 取り込みの候補 (近く、正面に近いほど優先)
    struct Candidate {uint32_t id;V at;float required,hp,maxHp,reward,score;};std::vector<Candidate> candidates;
    // 取り込める大きさで口の範囲にあれば候補に加える
    auto candidate=[&](uint32_t id,V point,float required,float hp,float maxHp,float reward){
        if(!LifeCanAbsorb(b.mass,required,hp,maxHp)||!LifeInMouth(b.at,b.forward,point,b.mass,0,life_.recipe))return;
        V delta=point-b.at;delta.y=0;float score=LifeDistance(point,b.at)+3*(1-Dot(Unit(delta),b.forward));
        candidates.push_back({id,point,required,hp,maxHp,reward,score});};
    // 小物と敵から候補を集める
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(prop.active)candidate(0x80000000u|uint32_t(i),prop.at,prop.required,0,1,prop.reward);}
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto drop=CityEnemyDrop(enemy);candidate(enemy.id,enemy.at,drop.required,enemy.health,enemy.maxHealth,drop.reward);}
    // 近い順に触手でつかむ
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& c){return a.score==c.score?a.id<c.id:a.score<c.score;});
    for(const auto& c:candidates)LifeAddGrab(life_,b,c.id,c.at,c.required,c.hp,c.maxHp,c.reward);
    if(b.grabs.empty())return false;
    // 最初につかんだ対象を目標にする
    b.target=b.grabs.front().target;b.engulfPoint=b.grabs.front().at;
    ++life_.revision;return true;
}
// スライムの攻撃が敵に当たったとき (盾持ちと守護者の装甲は特定の武器でだけ崩せる)
// 手応え: 本体の近くで起きたものほど強く揺らして振動させ、すぐ近くならヒットストップも掛ける
void ChronoSystem::LifeImpact(GameContext& ctx,const LifeImpactSpec& spec,V at,V direction,float scale){
    const auto& body=life_.bodies[0];float inner=6+2.5f*LifeScale(body.mass),outer=inner+80;
    float k=std::clamp(1-(LifeDistance(at,body.at)-inner)/(outer-inner),0.f,1.f);if(k<=0)return;
    // 止めるのは本体のすぐ近くだけ。重ねても長くなりすぎないよう、一番長いものに合わせる
    if(spec.stop>0&&k>.75f)lifeHitStop_=std::min(.12f,std::max(lifeHitStop_,spec.stop));
    if(spec.shake>0&&ctx.camera)ctx.camera->StartImpactShake(spec.shakeTime,spec.shake*k*scale,Write(direction),spec.frequency);
    Rumble::Play(spec.low*k*scale,spec.high*k*scale,spec.rumble);
}
void ChronoSystem::LifeHit(entt::registry& r,Player& p,GameContext& ctx,uint32_t id,float damage,LifeAction source,V from){
    for(auto& enemy:swarm_)if(enemy.id==id&&SwarmAlive(enemy)){
        // 触れた敵は気付く
        auto kind=CityKind(enemy);enemy.alerted=true;
        // 盾持ちはタイヤの突進 (と消火栓) 以外の攻撃を正面で防ぐ。突進は構えを崩す
        if(CityShieldBlocks(enemy,from,source==LifeAction::Tire||source==LifeAction::Hydrant||source==LifeAction::Signal)){damage*=.25f;
            if(life_.messageAge<=0){life_.message="盾に防がれた！ タイヤの突進か放水で体勢を崩そう";life_.messageAge=1.5f;}}
        else if(kind==CityEnemy::Shield&&(source==LifeAction::Tire||source==LifeAction::Hydrant)){enemy.stagger=source==LifeAction::Hydrant?.6f:2.5f;}
        // 守護者の足の装甲はタイヤの突進でしか削れない
        if(kind==CityEnemy::Guardian&&enemy.armor>0){
            if(source==LifeAction::Tire){enemy.armor-=damage;
                life_.message=enemy.armor>0?"足の装甲 残り "+std::to_string(int(std::ceil(enemy.armor)))+" / 溜めた突進で砕け":std::string("装甲が砕けた！ 本体を叩き込め");life_.messageAge=1.6f;
                damage*=.5f;}
            else{damage*=.15f;if(life_.messageAge<=0){life_.message="装甲が硬い！ タイヤの溜め突進で足の装甲を砕こう";life_.messageAge=1.5f;}}}
        // 体力を減らして光らせる
        enemy.health-=damage;enemy.flash=.15f;
        // 倒したら落とし物を残し、エフェクトを出す
        if(enemy.health<=0){enemy.phase=SwarmPhase::Dead;++r.get<InkPlayer>(player_).swarmKills;
            life_.props.push_back(CityEnemyDrop(enemy));if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,3*std::sqrt(enemy.scale),2});
            // 倒した瞬間は少し止めて重く揺らす (大きい敵ほど強く)
            LifeImpact(ctx,ImpactKill,enemy.at,{0,1,0},std::clamp(.7f+.15f*enemy.scale,.8f,1.4f));}
        // 倒れていなければ怯ませる (大砲と守護者は怯まない)
        else if(kind!=CityEnemy::Cannon&&kind!=CityEnemy::Guardian){ChangeSwarmPhase(enemy,SwarmPhase::Recover);}
        (void)p;return;
    }
}
// スライムの体 (本体と子) の形を GPU 流体の目標として渡す (液体・武器・触手)
void ChronoSystem::SyncSlimeBodies(GameContext& ctx){
    // 研究所では顔をカメラに向ける
    ctx.renderer->SetSlimeFaceCamera(life_.lab||(devour_&&finished_)?1.f:0.f);
    // 体ごとの GPU 流体の目標の一覧
    using Target=Engine::Renderer::FluidMorphTarget;std::vector<Target> targets;targets.reserve(Engine::Renderer::kPlayerFluidParticles);
    // 研究所の開閉が変わったら流体を作り直す
    if(lifeWasLab_!=life_.lab){scene_->ResetFluidPreview();lifeWasLab_=life_.lab;lifeReseed_=true;lifePreviewEmitted_=false;lifeLastCounts_={};++life_.revision;}
    // 研究所では見本、それ以外では実際の体を描く
    auto renderedBody=[&](size_t i)->const LifeBody& {return life_.lab?(i==0?life_.preview:life_.previewChild):life_.bodies[i];};
    static_assert(LifeBodyCount<=Engine::Renderer::kFluidMorphBodyCount);
    const uint32_t allBodies=Engine::Renderer::kFluidMorphAllBodies;
    // 作り直すときは全部の体、そうでなければ数が変わった体だけ
    uint32_t mask=lifeReseed_?allBodies:0;for(size_t i=0;i<LifeBodyCount;++i){const auto& b=renderedBody(i);uint32_t count=life_.lab&&i>1?0:b.particles;if(lifeLastCounts_[i]!=count)mask=allBodies;else if(lifeLastAction_[i]!=int(b.action)+(i==0?DevourToolShape()*64:0))mask|=1u<<i;lifeLastCounts_[i]=count;lifeLastAction_[i]=int(b.action)+(i==0?DevourToolShape()*64:0);}
    // エディタでの確認を始めた / やめたときも、本体の粒子の割り当てを作り直す (液体と形の点を切り替える)
    {bool previewing=devour_&&devourPreviewTime_>=0&&!life_.lab;if(previewing!=devourWasPreview_){devourWasPreview_=previewing;mask|=1u;++life_.revision;}}
    const float frameDt=std::clamp(ctx.dt,0.f,.05f);
    // スライムの色
    auto color=LifeColor(life_.recipe.color);
    // 絵の具に飛び込んだら体はその色になる (塗った瞬間は明るく光る)
    if(devour_&&devourPaint_.w>0){const float glow=1+.8f*devourPaintFlash_;color={devourPaint_.x*glow,devourPaint_.y*glow,devourPaint_.z*glow,color.w};}
    // 各体: 形・粒子数・大きさ・行動の時間から点の目標を作る
    for(size_t i=0;i<(life_.lab?size_t(2):LifeBodyCount);++i){const auto& b=renderedBody(i);if(b.mass<=0)continue;
        uint32_t count=b.particles;
        // 形の番号 (1: ハンマー, 2: 剣, 3: 飛行機, 4: タイヤ, 5: 消火栓, 0: 液体)
        int shape=LifeActionInfo(b.action).fluidShape;
        // 部屋のギミックに乗っている間は、本体がその道具の形になる
        // (高い所から落ちている間はヘリコプターの形)
        const bool gimmick=devour_&&i==0&&!life_.lab&&DevourToolShape()>0;if(gimmick)shape=DevourToolShape();
        // 吸収モードの飛びかかり: 液体の体は楕円体の枠しか持てず、覆いかぶさる形を作れないので、
        // 飛びかかりの間だけ体の各点に行き先を渡して、エディタで作った形 (DevourAnimation) にする。
        // エディタで確認している間は、止まったままその時間の形を見せる
        bool preview=devour_&&i==0&&devourPreviewTime_>=0&&!life_.lab;
        bool biting=devour_&&i==0&&!life_.lab&&devourBiteAge_>=0&&b.action==LifeAction::Idle;
        bool wrapping=!gimmick&&devour_&&i==0&&!life_.lab&&!b.down&&(b.action==LifeAction::Engulf||preview||biting);
        bool liquid=!LifeWeaponForm(b.action)&&!wrapping&&!gimmick,weapon=LifeWeaponForm(b.action)&&!gimmick;
        const auto& cloud=LifeCloud(shape,count);float scale=LifeScale(b.mass),age=preview?devourPreviewTime_:biting?devourBiteAge_:b.actionAge;V radii=LifeRadii(b,life_.recipe);
        std::vector<V> pose;std::vector<float> wall;V bodyVelocity{};float unit=1,form=1,poseStep=0;
        if(wrapping){
            // 体の半径 (普段の形の大きさ) と、点ごとの前後の順位 (粒子の数ごとに 1 回だけ作る)
            LifeBody rest=b;rest.aspect=1;rest.air=0;V restRadii=LifeRadii(rest,life_.recipe);
            unit=std::cbrt(restRadii.x/2.55f*restRadii.y/1.8f*restRadii.z/2.55f)*2.27f;
            static std::map<uint32_t,std::pair<std::vector<V>,std::vector<float>>> ranks;auto& entry=ranks[count];
            if(entry.first.size()!=cloud.size()){entry.first.clear();for(const auto& p:cloud)entry.first.push_back({p.x,p.y,p.z});entry.second=DevourRanks(entry.first);}
            // 今の形と、点ごとの口の壁の割合。本当に飛びかかっているときは体そのものが進む。確認中は進んだ分を形に足す
            DevourPose(DevourAnim(),age,entry.first,entry.second,pose,devourPounce_.seed,&wall);
            if(preview){float travel=DevourLeapProgress(age)*devourPreviewDistance_;for(auto& p:pose)p.z+=travel;}
            else{bodyVelocity={b.velocity.x,0,b.velocity.z};
                // 飲み込んだ瞬間に上からつぶれて横へ広がる (地面を基準に縦を縮め、量が変わらないよう横へ広げる)
                float squash=DevourSquash(devourGulp_.age,devourGulp_.weight),wide=1/std::sqrt(1-squash);
                for(auto& v:pose){v.x*=wide;v.y*=1-squash;v.z*=wide;}
                if(biting)for(auto& v:pose)v=v*devourBiteScale_;}
            // 点の速度は前のフレームの形との差から求める (少し後の形をもう 1 度作ると、飛びかかりの間の処理が倍になる)。
            // 前の形が無い・時間が戻った・止まっている (ヒットストップ・確認中) ときは形の速度を 0 にする
            // (飛びかかりの始めは形へまだ寄せていないので、最初のフレームに速度が無くても目立たない)
            float step=age-devourPoseAge_;poseStep=devourPosePrevious_.size()==pose.size()&&step>1e-5f&&step<.1f?step:0.f;
            // 普段の液体から形へは、各粒子の今の位置からゆっくり寄せる (確認中は形そのものを見せる)
            form=preview?1.f:DevourFormBlend(age);}
        // 体全体の値 (特に地形へのレイ) を 6600 点それぞれで計算してはいけない。
        // 下で変わるのは点ごとの姿勢と速度だけ
        Target common{};common.blend=liquid?b.motion:wrapping?form:gimmick?LifeSmooth(DevourToolAge()/Chrono::GimmickFormTime):LifeSmooth(age/.25f);
        common.origin={b.at.x+(liquid?b.fluidOffset.x:0),b.at.y-1.25f+(liquid?.8f*b.air:0),b.at.z+(liquid?b.fluidOffset.z:0)};
        common.density=LifeDensity(b.mass,count,Engine::Renderer::kPlayerFluidParticles);
        // バネのコイルと掃除機のホースは細いので、粒子の広がりを大きくして線がつながるようにする
        if(b.action==LifeAction::Spring)common.density*=.7f;
        if(b.action==LifeAction::Vacuum)common.density*=.75f;
        // 大きい物の手前で開く口は、形を devourBiteScale_ 倍 (最大 1.35 倍) に広げる。粒子の数は同じなので、
        // 密度を体積の分 (倍率の 3 乗) だけ下げて粒子どうしの間を広げる。下げないと粒子が普段の間隔に固まろうとして
        // 広がった口の壁を埋めきれず、口の幕に穴が開く (大きい物ほど開きやすい)
        if(biting&&devourBiteScale_>1)common.density/=devourBiteScale_*devourBiteScale_*devourBiteScale_;
        auto heading=liquid?b.liquidForward:b.forward;
        common.forward={heading.x,float(i),heading.z};common.omega=liquid?life_.recipe.Cohesion():wrapping?DevourBodyStiffness:b.action==LifeAction::Engulf?36.f:b.weapon.active?80.f:32.f;
        common.material={life_.recipe.Damping(),life_.recipe.Viscosity(),life_.recipe.SurfaceTension(),life_.lab?1200.f:InkGround(b.at)};
        V drive=liquid?b.coreVelocity:b.velocity;common.motion={drive.x,drive.y,drive.z,b.air};
        common.shape={radii.x,b.down?radii.y*.25f:radii.y,radii.z,liquid?1.f:weapon?-float(b.action):gimmick?-float(shape):0.f};common.color=color;
        if(gimmick)common.omega=shape==Chrono::GimmickBallShape?70.f:shape==Chrono::GimmickHeliShape?80.f:shape==Chrono::GimmickSheetShape?30.f:48.f;
        if(liquid)common.reserved={b.flowShape.x,b.flowShape.z,b.coreLead.x-b.fluidOffset.x,b.coreLead.z-b.fluidOffset.z};
        // 斜面に沿わせる (吸収モード・液体の体と飛びかかりの形): 体の前後と左右で地面の高さを比べて傾きを求める。
        // 以前は体の中心の高さの水平な面を地面としていたので、坂では上の側が地面に埋まり、下の側が浮いて見えた。
        // 片側だけ高い所 (段差の縁) は傾きにしない。
        //   普段の液体の体: GPU は点の行き先を使わず、体を包む楕円体と床で形を作るので、傾きを blend にまとめて渡し、
        //     GPU 側 (FluidMorph.hlsli の MorphLocal / MorphGround) で楕円体と床を坂に沿って傾ける
        //   飛びかかりの形: 点の行き先で形を作るので、各点を傾きの分だけ上下にずらす
        V slope{};
        if(devour_&&!life_.lab&&(liquid||wrapping)&&!b.down){
            V f{heading.x,0,heading.z};f=Length(f)>.01f?Unit(f):V{0,0,1};const V right{f.z,0,-f.x};
            const float span=std::max(1.f,std::max(radii.x,radii.z)*.6f),center=InkGround(b.at);
            auto groundAt=[&](V d){V at=b.at+d;at.y=b.at.y+span;return InkGround(at);};
            auto grade=[&](V axis){const float ahead=(groundAt(axis*span)-center)/span,behind=(center-groundAt(axis*-span))/span;
                return std::abs(ahead-behind)>.35f?0.f:std::clamp((ahead+behind)*.5f,-1.f,1.f);};
            slope={grade(right),0,grade(f)};}
        auto& tilt=lifeSlope_[i];tilt=Lerp(tilt,slope,1-std::exp(-10*frameDt));
        const bool tilted=wrapping&&std::abs(tilt.x)+std::abs(tilt.z)>.01f;
        if(liquid)common.blend=Engine::Renderer::PackLiquidBlend(b.motion,devour_?tilt.x:0.f,devour_?tilt.z:0.f);
        // 飛びかかりの形: 下の側の点が地面で止まらないよう、流体の床を坂の下の端の高さまで下げる
        if(tilted)common.material.w-=(std::abs(tilt.x)+std::abs(tilt.z))*std::max(radii.x,radii.z)*1.2f;
        // 形の各点を、体の大きさと姿勢に合わせて目標の位置にする
        for(size_t pointIndex=0;pointIndex<cloud.size();++pointIndex){auto point=cloud[pointIndex];V local{point.x,point.y,point.z};
            if(liquid)local={local.x/2.55f*radii.x,local.y/1.8f*radii.y,local.z/2.55f*radii.z};
            V wrapVelocity{},gimmickVelocity{};
            if(wrapping){local=pose[pointIndex]*unit;
                // 点の速度も渡す。渡さないと粒子は秒速 30 までしか追えず、速く跳ぶ間に置いていかれて平たく流れてしまう
                V d=poseStep>0?(pose[pointIndex]-devourPosePrevious_[pointIndex])*(unit/poseStep):V{};V f=b.forward;
                // 形の変わる速さは、形へ寄せた割合の分だけ渡す (体そのものの進みはそのまま)
                wrapVelocity=bodyVelocity+(V{f.z,0,-f.x}*d.x+V{0,d.y,0}+f*d.z)*form;}
            if(weapon)local=LifeWeaponPose(b,local,b.weapon.age);
            // ギミックの形: 転がる・倒れる・傾く。点の速度は前のフレームの姿勢との差と体の速さ
            // (形の点は体の向きで回したローカルの座標なので、形の動きの速さも体の向きでワールドの向きに直す。
            //  直さないと +z 以外へ進むとき、回転翼などの速い点が間違った向きへ引かれて形が崩れる)
            if(gimmick){V before=Chrono::GimmickPosePoint(shape,local,DevourToolPose(true))*scale;local=Chrono::GimmickPosePoint(shape,local,DevourToolPose());
                const V d=frameDt>0?(local*scale-before)*(1/frameDt):V{},f=heading;gimmickVelocity=b.velocity+V{f.z,0,-f.x}*d.x+V{0,d.y,0}+V{f.x,0,f.z}*d.z;}
            if(b.down)local.y*=.25f;
            if(!liquid&&!wrapping)local=local*scale;
            Target target=common;target.local={local.x,std::max(.18f,local.y),local.z};
            // 斜面の傾きの分だけ上下にずらす (点の位置は体の右・上・前の向きの座標)
            if(tilted)target.local.y+=tilt.x*local.x+tilt.z*local.z;
            // 形ができる前から点の速度を渡す。渡さないと動いている間は粒子がばねで遅れ続け、形が完成しない
            if(weapon){V velocity=LifeWeaponVelocity(b,{point.x,point.y,point.z},frameDt);target.reserved={velocity.x,velocity.y,velocity.z,-1};}
            if(gimmick)target.reserved={gimmickVelocity.x,gimmickVelocity.y,gimmickVelocity.z,-1};
            // 開いた口の壁だけ形の点へ強く引き、尾と口を開けていない体は液体のように柔らかくする
            if(wrapping){target.reserved={wrapVelocity.x,wrapVelocity.y,wrapVelocity.z,-1};target.omega=DevourStiffness(age,wall[pointIndex]);}
            targets.push_back(target);
        }
        // 次のフレームの速度のために今の形を覚える (飛びかかっていないときは忘れる)
        if(i==0){if(wrapping){devourPosePrevious_=std::move(pose);devourPoseAge_=age;}else devourPosePrevious_.clear();}
    }
    // 取り込み中の触手を管として渡す
    std::vector<Engine::Renderer::FluidTendril> arms;
    // 取り込み中の体ごとに、つかんだ各対象への触手を節に分けて作る
    for(size_t owner=0;owner<(life_.lab?size_t(2):LifeBodyCount);++owner){const auto& b=renderedBody(owner);
        if(b.mass<=0||b.down||b.action!=LifeAction::Engulf)continue;
        for(size_t i=0;i<b.grabs.size();++i){const auto& grab=b.grabs[i];
            V previous=LifeTentaclePoint(b,life_.recipe,grab,i,0);
            float previousRadius=LifeTentacleRadius(b.mass,0);
            for(int segment=1;segment<=LifeTentacleSegments;++segment){float u=float(segment)/LifeTentacleSegments;
                V next=LifeTentaclePoint(b,life_.recipe,grab,i,u);float radius=LifeTentacleRadius(b.mass,u);
                if(Length(next-previous)>.001f)arms.push_back({EV(previous),previousRadius,EV(next),radius});
                previous=next;previousRadius=radius;
            }
        }
    }
    // 飲み込んだ大きい物の周りの皮
    if(devour_||slimePlayerDemo_)DevourMealSkin(arms);
    // 水鉄砲の水
    if(devour_)GimmickJets(arms);
    // 触手・変形の目標・色をレンダラーへ渡す
    ctx.renderer->SetFluidTendrils(std::move(arms));
    // 飛び散った体のしずくも描く範囲に入れる
    {std::vector<Engine::Vector4> splashes;for(const auto& s:devourSplashes_)splashes.push_back({s.at.x,s.at.y,s.at.z,s.radius});ctx.renderer->SetFluidVolumeExtras(std::move(splashes));}
    ctx.renderer->SetFluidMorphTargets(std::move(targets),life_.revision,mask,lifeReseed_,frameDt);lifeReseed_=false;
    ctx.renderer->SetSlimeColor(color);
}
// スライムの生活モードの更新: 入力・変形・移動・接触吸収・取り込み・踏みつけ・子の行動・武器の攻撃・街と敵
void ChronoSystem::UpdateSlimeLife(entt::registry& r,Player& p,GameContext& ctx){
    // 本体・入力の記録
    auto& ink=r.get<InkPlayer>(player_);auto& body=life_.bodies[0];auto* control=r.try_get<ControlFrame>(player_);
    float dt=std::clamp(ctx.dt,0.f,.05f);diagnostic_=control!=nullptr;
    body.previousForward=body.forward;
    if(slimePlayerDemo_)UpdateSlimePlayerDemo(r,p,ctx);
    // 変形の見せ場の間は、プレイヤー以外の時間を止めて見せ場だけを進める
    if(showcase_.active){UpdateMorphShowcase(r,p,ctx,dt);return;}
    // ヒットストップ: 敵・弾・体・流体を止め、カメラ (揺れ) だけを進める。押した入力は止まった後に受け付ける
    // チュートリアルの巻尺の説明の間は、ゲーム全体を止めて説明だけを進める
    if(devour_&&TickDevourTutorialPause(dt)){ctx.renderer->SetRogueWorldFrozen(true);InkCamera(r,p,ctx);return;}
    if(lifeHitStop_>0){lifeHitStop_-=dt;ctx.renderer->SetRogueWorldFrozen(true);InkCamera(r,p,ctx);return;}
    life_.messageAge=std::max(0.f,life_.messageAge-dt);
    // 世界を動かし、カーソルを隠す
    ctx.renderer->SetRogueWorldFrozen(false);Engine::WindowDX::SetCursorVisible(false);
    life_.age+=dt;life_.morphAge+=dt;p.stats.seconds+=dt;
    // 吸収モードの制限時間
    // エディタでの確認は、エディタが毎フレーム続けている間だけ
    if(devourPreviewHold_>0)--devourPreviewHold_;else devourPreviewTime_=-1;
    // ベッドの下のチュートリアルの間は時間を進めない
    if(devour_&&!finished_&&!DevourTutorialHolding()){float before=devourClock_.left;devourClock_.left=std::max(0.f,devourClock_.left-dt);TickDevourTimeSounds(before);}
    // ランクの知らせはチュートリアルの間も消えていく (時計と一緒に止めると、チュートリアルの後に古い知らせが出る)
    if(devour_&&!finished_)devourClock_.flash=std::max(0.f,devourClock_.flash-dt);
    // タイヤと消火栓をこのフレームで解放したか調べるために前の状態を記録する
    bool hadTire=body.tireUnlocked,hadHydrant=body.hydrantUnlocked,hadSignal=body.signalUnlocked,hadVacuum=body.vacuumUnlocked,hadSpring=body.springUnlocked;life_.signalNotice=std::max(0.f,life_.signalNotice-dt);SignalTick(life_.signal,dt);life_.tireNotice=std::max(0.f,life_.tireNotice-dt);life_.hydrantNotice=std::max(0.f,life_.hydrantNotice-dt);
    p.invincible=std::max(0.f,p.invincible-dt);p.damageAge+=dt;life_.stompCooldown=std::max(0.f,life_.stompCooldown-dt);life_.contactCooldown=std::max(0.f,life_.contactCooldown-dt);
    // カメラの向き
    // 目標の発表とステージの紹介の間は、操作を受け付けない (世界は動かしたまま、体はその場で待つ)
    const bool cutscene=devour_&&DevourTutorialCutscene();
    if(cutscene){}else if(control){yaw_=control->yaw;pitch_=control->pitch;}else if(ctx.input){float y,x;Controls::Look(dt,y,x);yaw_+=ctx.input->GetMouseDeltaX()*.0025f+y;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f+x,-.7f,.85f);}
    if(devour_)TickDevourTutorial(r,dt,!control||control->intro);
    // 右スティックの押し込みで、カメラを体の後ろへ向け直す
    if(!control&&UI::PadPressed(XINPUT_GAMEPAD_RIGHT_THUMB)){V facing=body.forward;facing.y=0;if(Length(facing)>.01f)yaw_=std::atan2(facing.x,facing.z);}
    // 入力: 取り込み・変形攻撃・スライムに戻る。変形は 1〜4 キー・十字キー・LB / RB で選ぶ
    bool absorb=!cutscene&&(control?control->absorb:Controls::Absorb());
    bool fire=!cutscene&&(control?control->attack:Controls::MorphAttack());
    bool returnToSlime=!cutscene&&(control?control->returnToSlime:Controls::ReturnToSlime());
    bool firePressed=fire&&!life_.previousFire,changedForm=false,returned=false;
    // 部屋のギミック: 輪の中で変形キーを押すと動く (押した入力はギミックが使う)
    if(devour_&&TickGimmickInput(r,p,ctx,firePressed,absorb&&!life_.previousAbsorb)){firePressed=false;life_.chargeLocked=true;}
    // メッセージを出す関数
    auto say=[&](const char* text){life_.message=text;life_.messageAge=2;};
    // スライムに戻る
    if(returnToSlime&&!life_.previousReturn&&LifeWeaponForm(body.action)){
        LifeReturnToSlime(body);life_.tackleTime=0;++life_.revision;returned=true;say("スライムに戻る / 再び取り込み可能");}
    // 武器の形で吸収を押したら、そのままスライムへ戻って吸収する (消火栓は同じボタンが水の防御なので除く)。
    // 戻りきるまでの間は押した入力を覚えておき、離していても戻った瞬間に吸収を始める
    if(absorb&&!life_.previousAbsorb&&LifeWeaponForm(body.action)&&body.action!=LifeAction::Hydrant){
        LifeReturnToSlime(body);body.restoreDuration=.2f;life_.tackleTime=0;++life_.revision;returned=true;lifeAbsorbBuffer_=.8f;}
    // 変形はハンマー (1 / 十字↑)・タイヤ (2 / →)・消火栓 (3 / ↓)・信号機 (4 / ←)
    const auto& forms=LifeForms();const int formCount=int(forms.size());
    int formIndex=LifeFormIndex();
    if(control&&control->morphForm>=1&&control->morphForm<=formCount)life_.form=forms[size_t(control->morphForm-1)];
    // 数字キーと十字キーは、押した瞬間にその形へ変形する
    int hotkey=0;
    if(!control&&ctx.input){if(ctx.input->Trigger(DIK_1))hotkey=1;else if(ctx.input->Trigger(DIK_2))hotkey=2;else if(ctx.input->Trigger(DIK_3))hotkey=3;else if(ctx.input->Trigger(DIK_4))hotkey=4;
        else if(formCount>4){const uint8_t more[]={DIK_5,DIK_6,DIK_7,DIK_8};for(int i=0;i<4;++i)if(ctx.input->Trigger(more[i]))hotkey=5+i;}}
    if(!control){if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_UP))hotkey=1;else if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT))hotkey=2;else if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_DOWN))hotkey=3;else if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT))hotkey=4;}
    // LB / RB: スティックから指を離さずに、解放済みの前後の形へ変形する (解放済みが無ければ隣を選ぶだけ)
    if(!control&&hotkey==0){int step=UI::PadPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)?1:UI::PadPressed(XINPUT_GAMEPAD_LEFT_SHOULDER)?3:0;
        if(step==3)step=formCount-1;if(step){int pick=(formIndex+step)%formCount;for(int i=0;i<formCount&&!LifeCanMorph(body,forms[size_t(pick)]);++i)pick=(pick+step)%formCount;hotkey=pick+1;}}
    if(control&&control->morphNow&&control->morphForm>=1&&control->morphForm<=formCount)hotkey=control->morphForm;
    bool morphNow=hotkey>0;if(morphNow)life_.form=forms[size_t(hotkey-1)];
    // 攻撃か数字キーを押したとき: 選んだ形が解放済みなら変形し、まだなら (攻撃なら) タックルする
    // 吸収モードの変形は、アイテムを拾って使える状態のときだけ
    bool itemLocked=devour_&&!devourFreeForms_&&!devourItemReady_;
    if(!itemLocked&&!GimmickRiding()&&((firePressed&&!returned)||morphNow)&&body.cooldown<=0&&(body.action==LifeAction::Idle||(LifeWeaponForm(body.action)&&body.action!=life_.form))){
        bool unlocked=LifeCanMorph(body,life_.form);
        if(!unlocked){
            if(firePressed&&body.action==LifeAction::Idle){life_.tackleTime=.24f;body.cooldown=.45f;}
            // 未解放の案内は行動のデータテーブルから引く (必要な量を %d に入れる)
            const auto& info=LifeActionInfo(life_.form);
            if(info.lockedHint){char hint[256];snprintf(hint,sizeof(hint),info.lockedHint,int(info.requiredMass));say(hint);}
        }else if(LifeEnterWeapon(body,life_.form,LifeFacesCamera(life_.form)?Forward(yaw_,0):body.forward)){changedForm=true;life_.chargeLocked=true;++life_.revision;if(devour_)PlayDevourSound(devourSounds_.morph,.55f);if(devour_&&!devourFreeForms_){devourItemReady_=false;devourMorphLeft_=CurrentDevourStage().itemTime;++devourLog_.morphs;DevourLogEvent("morph_start");}
            if(const char* hint=LifeEnterHint(body.action,devour_))say(hint);}}
    // 信号機: 押すたびに次の色を出す。待ち時間と点検の間は出せない
    else if(firePressed&&!returned&&body.action==LifeAction::Signal&&body.weapon.ready){SignalColor fired;
        if(SignalFire(life_.signal,fired))FireSignal(r,p,ctx,fired);
        else if(SignalResting(life_.signal)){life_.message="信号の点検中… あと "+std::to_string(int(std::ceil(life_.signal.wait)))+" 秒";life_.messageAge=1.2f;}}
    // 前フレームの入力を記録する
    life_.previousFire=fire;life_.previousReturn=returnToSlime;
    // 移動とジャンプ (カメラ基準)
    V input{};if(cutscene){}else if(control)input=Unit(control->move);else Controls::Move(input.x,input.z);
    V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    // 体の向きを更新する
    LifeUpdateFacing(body,wish,Forward(yaw_,0),dt);
    // タイヤ: 押している間はその場で回転を溜め (向きはカメラの前へ)、離すと溜めた分だけ突進する
    if(body.action==LifeAction::Tire){
        if(!fire)life_.chargeLocked=false;
        if(LifeTickTireCharge(body,fire&&!life_.chargeLocked&&!changedForm,dt)){body.attackSerial=++life_.nextAttack;
            auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[1],false,(.25f+.25f*body.weapon.dashPower)*audio->GetMasterSEVolume(),.7f+.5f*body.weapon.dashPower);
            LifeImpact(ctx,ImpactTireLaunch,body.at,{0,0,1},.5f+body.weapon.dashPower);}
        if(body.weapon.charging){V aim=Forward(yaw_,0);aim.y=0;if(Length(wish)>.05f)aim=wish;body.forward=LifeWeaponFacing(body.forward,Unit(aim),dt);}
        body.tireSpin=LifeTireSpinRate(body,dt);
        // 溜めている間は足元に土ぼこりの輪、突進中は通った跡に輪を残す (0.07 秒ごと)
        bool puff=std::fmod(life_.age,.07f)<dt&&swarmEffects_.size()<120;
        float scale=LifeScale(body.mass);V foot=body.at-V{0,1.1f,0};
        if(puff&&body.weapon.charging)swarmEffects_.push_back({foot-body.forward*(1.5f*scale),0,(1.2f+1.6f*body.weapon.charge)*scale,2});
        if(puff&&body.weapon.dash>0)swarmEffects_.push_back({foot,0,(1+1.5f*body.weapon.dashPower)*scale,2});
    }else body.tireSpin=0;
    // ジャンプ (空中で押すと踏みつけ)
    // 吸収モードにジャンプは無い (高い所へは坂で登る)
    bool jump=!devour_&&(control?control->jump:Controls::Jump()),jumped=jump&&!prevSpace_&&p.grounded;
    if(jumped){p.velocity.y=life_.recipe.Bounce();body.reboundAvailable=true;}else if(jump&&!prevSpace_&&!p.grounded&&life_.stompCooldown<=0){life_.stomping=true;p.velocity.y=-32;}
    prevSpace_=jump;
    // 吸収モードの飛びかかり: 溜め・跳ぶ・覆うの間は入力で動かさず、飛びかかりの速度に任せる
    if(devour_||slimePlayerDemo_){TickDevourPounce(r,p,ctx,dt);
        // 掃除機とバネは、変形した押しっぱなしを離してから使える
        if((body.action==LifeAction::Vacuum||body.action==LifeAction::Spring)&&!fire)life_.chargeLocked=false;
        bool held=fire&&!life_.chargeLocked&&!changedForm;
        TickDevourVacuum(r,ctx,dt,held);TickDevourSpring(p,dt,held,wish);
        // 吸収モードの変形は時間が来たらスライムへ戻る (跳んでいる間は着地を待つ)
        if(devour_&&!devourFreeForms_&&LifeWeaponForm(body.action)&&!devourHop_.active){devourMorphLeft_-=dt;
            if(devourMorphLeft_<=0){DevourLogEvent("morph_end");LifeReturnToSlime(body);body.restoreDuration=.2f;life_.tackleTime=0;++life_.revision;}}}
    bool pouncing=devour_&&body.action==LifeAction::Engulf&&body.actionAge<DevourCoverEnd;
    // 移動の速さ (配合と行動で変わる)
    // 吸収モードでは、大きくなるほど速く動く (体の大きさに対して同じくらいの速さに感じるように)
    float speed=life_.recipe.Speed()*LifeTravelScale(body)*(devour_?DevourSpeedScale(body.mass):1.f);
    if(devourSucking_)speed*=DevourVacuumSlow;
    if(devourHeavy_>0)speed*=DevourHeavySlow;else if(devourChew_>0)speed*=.65f;
    // 弾かれた直後は動けない
    if(devourStun_>0){devourStun_=std::max(0.f,devourStun_-dt);wish={};}
    if(body.action==LifeAction::Spring&&body.weapon.charging)speed*=.2f;
    // タックル中は前へ速く進む
    if(life_.tackleTime>0){life_.tackleTime=std::max(0.f,life_.tackleTime-dt);wish=body.forward;speed=25;}
    // タイヤは専用の速度、それ以外は配合の速度
    if(body.action==LifeAction::Tire&&body.weapon.ready)p.velocity=LifeTireChargeVelocity(body,p.velocity,wish,life_.recipe,p.grounded,dt,devour_?DevourSpeedScale(body.mass):1.f);
    else if(!pouncing)p.velocity=LifeMoveVelocity(p.velocity,wish,life_.recipe,p.grounded,dt,speed/life_.recipe.Speed());
    // 重力
    p.velocity.y-=life_.recipe.Gravity()*dt;
    // 高い所から落ちるとヘリコプターになって、回転翼でゆっくり降りる
    if(devour_)TickDevourHeli(p,dt,wish,pouncing||devourHop_.active||GimmickRiding()||LifeWeaponForm(body.action));
    // 次の位置を街の通れる範囲に収め、接地を判定する
    V old=body.at,next=old+p.velocity*dt;
    // バネで跳んでいる間は建物の上を越えるので、決まった弧をたどる
    // ギミックに乗っている間も、決まった動きをたどる
    const bool riding=devour_&&GimmickRiding();bool hopping=devourHop_.active||riding;
    if(riding){next=GimmickStep(r,p,ctx,dt,wish);if(GimmickRiding())p.velocity=dt>0?(next-old)*(1/dt):V{};}
    else if(hopping){next=DevourHopStep(dt);p.velocity=dt>0?(next-old)*(1/dt):V{};}
    else{V radii=LifeRadii(body,life_.recipe);V wanted=next;next=ConstrainLifeMovement(old,next,std::max(radii.x,radii.z)*.7f);
        // 吸収モード: 壁や飲めない物へ勢いよくぶつかると弾かれる (変形中と飛びかかり中は別の動き)
        if(devour_&&!pouncing&&!LifeWeaponForm(body.action)&&dt>0){V lost=wanted-next;lost.y=0;float hit=Length(lost)/dt,impact=DevourBumpImpact(hit,speed);
            // ベッドの下 (チュートリアルの場所) では、本の壁にぶつかっても弾かずに止めるだけにする
            const bool gentle=CurrentDevourStage().map=="room"&&Chrono::RoomUnderBed(old);
            if(impact>0&&devourBumpCool_<=0&&!gentle)DevourBump(r,p,ctx,old+Unit(lost)*4.f,impact);
            next=DevourPropBlock(r,p,ctx,old,next,speed);
            // ギミックの看板の柱は弾かずに横へ滑らせる (土台は InkGround の坂で乗り越える)
            next=GimmickSignSlide(next,DevourBodyRadius(body.mass));}
        if(next.x==old.x)p.velocity.x=0;if(next.z==old.z)p.velocity.z=0;}
    // 段差が高すぎれば水平に動かない
    float floor=InkGround(old),nextFloor=InkGround(next);
    // 吸収モードでは、体の大きさに合う段差は乗り越えられる。体の前の端で調べて、家具にめり込まないようにする
    float climb=devour_?LifeScale(body.mass):0;bool stepped=false;
    bool blocked=false;V pushDir{};
    if(devour_&&!hopping){V move=next-old;move.y=0;if(Length(move)>.001f){float edge=InkGround(next+Unit(move)*(LifeRadii(body,life_.recipe).x*.6f));if(edge>old.y+.1f+climb){next.x=old.x;next.z=old.z;nextFloor=floor;blocked=true;pushDir=Unit(move);}}}
    // 段差ジャンプ: 跳び越えられる高さの段差へ押し続けると、ぴょんと上へ跳ぶ
    if(blocked&&p.grounded&&!LifeWeaponForm(body.action)){devourStepPush_+=dt;
        float radius=DevourBodyRadius(body.mass),reach=LifeRadii(body,life_.recipe).x*.6f+radius*.5f;V landing=old+pushDir*reach;landing.y=old.y+DevourStepHeight(body.mass);float top=InkGround(landing);
        if(devourStepPush_>=DevourStepHold&&top-floor<=DevourStepHeight(body.mass)&&DevourWalkable(landing,radius*.7f)){devourStepPush_=0;landing.y=top+1.25f;
            devourHop_={true,false,old,landing,0,DevourStepTime,radius*.5f+1,true};body.springStretch=.35f;}}
    else if(!hopping)devourStepPush_=0;
    if(!hopping&&nextFloor>old.y+.1f+climb){next.x=old.x;next.z=old.z;nextFloor=floor;}
    else if(!hopping&&p.grounded&&nextFloor>floor+.15f+LifeDistance(old,next)*.65f)stepped=true;
    // 接地を判定し、地面へ合わせる
    bool grounded=p.grounded;p.grounded=!hopping&&SlimeGrounded(old.y,next.y,p.velocity.y,p.grounded,jumped,floor,nextFloor,LifeDistance(old,next));
    // 吸収モードは面より下へ沈まない (坂で浮いた直後に足元が上がると、面の中へ抜けて落ち続ける)
    if(stepped||(devour_&&!hopping&&next.y<=nextFloor+1.25f))p.grounded=true;
    if(p.grounded){next.y=nextFloor+1.25f;p.velocity.y=0;p.recoveryPoint=next;}
    // 体の位置と動きを更新する (タイヤは転がった分だけ回す)
    body.at=next;LifeTickMotion(body,life_.recipe,p.velocity,p.grounded,!grounded&&p.grounded,dt,body.forward);
    if(devourHop_.landed){if(devourHop_.step){devourHop_.landed=false;body.springStretch=.25f;}else DevourSpringLand(r,ctx);}
    if(body.action==LifeAction::Tire)body.tireAngle=std::remainder(body.tireAngle+Dot(body.at-old,body.forward)/(LifeTireRadius*LifeScale(body.mass))+body.tireSpin*dt,6.2831853f);
    // タイヤの突進: 通り道の敵に 1 回ずつ当て、バリケードや門も壊す。壁で止まったら突進を終える
    if(body.action==LifeAction::Tire&&body.weapon.dash>0){
        float power=body.weapon.dashPower,reach=LifeTireRadius*LifeScale(body.mass)*.9f;uint64_t hitKey=body.attackSerial*2;int hits=0;
        for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.lifeHitRevision!=hitKey&&LifeSegmentDistance(enemy.at,old,body.at)<reach+enemy.scale){
            enemy.lifeHitRevision=hitKey;LifeHit(r,p,ctx,enemy.id,LifeTireDashDamage(body,power),LifeAction::Tire,old);++hits;
            if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,2+2*power,2});}
        if(hits){LifeImpact(ctx,ImpactTireHit,body.at,{0,1,0},.7f+.6f*power);auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,.5f*audio->GetMasterSEVolume(),1.1f);}
        if(!slimePlayerDemo_)CityTireDashHits(r,ctx,body,hitKey);
        if(devour_&&power>=DevourTireSmashPower)DevourSmash(r,ctx,reach);
        if(p.velocity.x==0&&p.velocity.z==0){body.weapon.dash=0;LifeImpact(ctx,ImpactTireCrash,body.at,body.forward*-1.f+V{0,.5f,0});}
    }
    // 十分に大きければ、触れた物や敵をそのまま吸収する
    int touchCount=0;float touchGrowth=0;
    // 吸収モードでは、覆える物は触れるだけで体の中へ引き込む
    if(devour_)DevourTouch(r,old);
    else for(size_t i=0;i<life_.props.size();++i){auto& prop=life_.props[i];if(!prop.active)continue;
        if(LifeCollectTouch(life_,0x80000000u|uint32_t(i),prop.at,prop.required,0,1,prop.reward,old)){
            prop.active=false;++touchCount;touchGrowth+=prop.reward;
            if(prop.visual!=0xffffffffu){auto entity=entt::entity(prop.visual);if(r.valid(entity))r.get<MeshRendererComponent>(entity).enabled=false;}
        }}
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){auto drop=CityEnemyDrop(enemy);
        if(LifeCollectTouch(life_,enemy.id,enemy.at,drop.required,enemy.health,enemy.maxHealth,drop.reward,old)){
            enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;++touchCount;touchGrowth+=drop.reward;
        }}
    // 吸収したらメッセージ・音・エフェクトを出す
    if(touchCount){life_.message="接触吸収 "+std::to_string(touchCount)+"個 / 成長 +"+LifeNumber(touchGrowth);life_.messageAge=1.5f;
        auto* audio=Engine::Audio::GetInstance();audio->Play(lifeEngulfSound_,false,.35f*audio->GetMasterSEVolume(),1.2f);
        if(swarmEffects_.size()<128)swarmEffects_.push_back({body.at,0,2*LifeScale(body.mass),2});}
    // 吸収モード: 吸収ボタンで全身ごと飛びかかる (押し続けると、覆い終えるたびに次へ飛びかかる)
    if(devour_){if(!GimmickRiding()&&(absorb||lifeAbsorbBuffer_>0)&&StartDevourPounce(p,ctx))lifeAbsorbBuffer_=0;}
    // 取り込みを始める (対象が無ければ知らせる)
    else if(LifeWantsAbsorb(absorb||lifeAbsorbBuffer_>0,body)){
        if(StartLifeAbsorb(0))lifeAbsorbBuffer_=0;
        else if((absorb&&!life_.previousAbsorb)||(!absorb&&lifeAbsorbBuffer_>0)){lifeAbsorbBuffer_=0;say("NO ABSORBABLE TARGET / WEAKEN OR GROW FIRST");}}
    lifeAbsorbBuffer_=std::max(0.f,lifeAbsorbBuffer_-dt);
    life_.previousAbsorb=absorb;
    // 空中から着地したら踏みつけ (範囲内の敵にダメージ)
    if(!grounded&&p.grounded&&life_.stomping){life_.stomping=false;life_.stompCooldown=.7f;++life_.stomps;
        float radius=life_.recipe.StompRadius()*LifeScale(body.mass),damage=life_.recipe.StompDamage()*LifeScale(body.mass);int hits=0;
        for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&LifeDistance(enemy.at,body.at)<radius+enemy.scale){LifeHit(r,p,ctx,enemy.id,damage,LifeAction::Idle,body.at);++hits;}
        swarmEffects_.push_back({body.at,0,radius,2});life_.message="踏みつけ "+LifeNumber(damage)+"ダメージ / "+std::to_string(hits)+"体に命中";life_.messageAge=2;
        LifeImpact(ctx,hits?ImpactStomp:ImpactStompMiss,body.at,{0,1,0},.6f+.8f*life_.recipe.Ratio(1));}
    if(!grounded&&p.grounded&&body.reboundAvailable){body.reboundAvailable=false;p.velocity.y=life_.recipe.Rebound();if(p.velocity.y>0)p.grounded=false;}
    // プレイヤーの位置と大きさ、液体の見た目の値を反映する
    auto& transform=r.get<TransformComponent>(player_);transform.translate=Write(body.at);float size=LifeScale(body.mass);transform.scale={size,size,size};transform.rotate.y=std::atan2(body.forward.x,body.forward.z);
    ink.fluidDirection=body.liquidForward;ink.groundHeight=nextFloor;ink.groundSlope={};ink.airBlend=body.air;ink.fluidAspect=body.aspect;ink.fluidMotion=body.motion;ink.swimming=false;ink.phase=SlimePhase::Roaming;
    // 各体 (本体と子) の更新
    for(size_t index=0;index<life_.bodies.size();++index){auto& b=life_.bodies[index];if(b.mass<=0)continue;b.age+=dt;b.cooldown=std::max(0.f,b.cooldown-dt);b.actionAge+=dt;
        // 子: 本体から離れて自分で狩りをする
        if(index>0)TickLifeChild(r,p,ctx,index,dt);
        // 取り込み中: つかんだ物が手放されないか調べ、時間が来たら取り込みを確定する
        if(b.action==LifeAction::Engulf&&!devour_){int captured=0;float gained=0;bool remaining=false;
            // 各触手: 取り消し・確定済みならとばす
            for(auto& grab:b.grabs){
                if(grab.cancelled||grab.committed)continue;
                // つかんだ対象の今の位置 (小物か敵)
                V targetAt{};bool valid=false;bool prop=(grab.target&0x80000000u)!=0;uint32_t id=grab.target&0x7fffffffu;
                if(prop&&id<life_.props.size()){const auto& object=life_.props[id];targetAt=object.at;valid=object.active&&LifeCanAbsorb(b.mass,object.required);}
                else if(!prop)for(const auto& enemy:swarm_)if(enemy.id==id){targetAt=enemy.at;valid=SwarmAlive(enemy)&&LifeCanAbsorb(b.mass,CityEnemyRequired(enemy),enemy.health,enemy.maxHealth);break;}
                // 対象が無くなったか手放したら取り消す
                if(!valid||!LifeHoldGrab(b,targetAt,life_.recipe)){grab.cancelled=true;grab.cancelAge=b.actionAge;continue;}
                grab.at=targetAt;remaining=true;
                // 確定の時点に来たら取り込みを確定し、対象を消す
                if(b.actionAge>=life_.recipe.AbsorbDuration()*LifeTentacleCapturePhase&&LifeCommitGrab(life_,b,grab)){
                    ++captured;gained+=grab.reward;
                    if(prop){auto& object=life_.props[id];object.active=false;if(object.visual!=0xffffffffu){auto e=entt::entity(object.visual);if(r.valid(e))r.get<MeshRendererComponent>(e).enabled=false;}}
                    else for(auto& enemy:swarm_)if(enemy.id==id){enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;break;}
                }
            }
            // 取り込んだら音とメッセージ
            if(captured){auto* a=Engine::Audio::GetInstance();a->Play(lifeEngulfSound_,false,.6f*a->GetMasterSEVolume(),1+.1f*life_.recipe.Ratio(0));
                if(index==0){life_.message=std::to_string(captured)+"個まとめて取り込み / 成長 +"+LifeNumber(gained);life_.messageAge=1.7f;}}
            // 残りが無ければスライムに戻る
            if(!remaining&&!b.committed){LifeReturnToSlime(b);++life_.revision;}
        }
        // 大きさが足りなくなった武器はスライムに戻る
        if(LifeWeaponForm(b.action)&&!LifeCanMorph(b,b.action)){LifeReturnToSlime(b);++life_.revision;}
        // 武器の形: 攻撃の当たりを 2 回に分けて調べ、敵や門にダメージを与える
        if(LifeWeaponForm(b.action)){
            // 形ができたら武器の攻撃を進める (押した瞬間・押し続け)
            // 粒子がそろったら完成。そろわなくても 0.8 秒で使えるようにする (見た目はその後も形へ寄る)
            // 吸収モードは止まらずに使えるよう、もっと早く使えるようにする
            bool formed=b.actionAge>=(devour_?.15f:.3f)&&(b.actionAge>=(devour_?.35f:.8f)||ctx.renderer->FluidMorphReady(life_.revision,index,1.f*LifeScale(b.mass)));
            if(LifeTickWeapon(life_,b,dt,index==0&&firePressed&&!changedForm,index==0&&fire&&!returned,formed)){
                auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[0],false,.16f*audio->GetMasterSEVolume(),b.action==LifeAction::Hammer?1.8f:1.2f);
            }
            for(int pulse=0;pulse<2;++pulse){bool hit=false;uint64_t hitKey=b.attackSerial*2+uint64_t(pulse);
                for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.lifeHitRevision!=hitKey&&LifeWeaponTouches(b,enemy.at,1.25f*enemy.scale,pulse)){
                    enemy.lifeHitRevision=hitKey;LifeHit(r,p,ctx,enemy.id,LifeWeaponDamage(b),b.action,b.at);++life_.hammerHits;hit=true;}
                if(index==0&&!slimePlayerDemo_)CityWeaponHits(r,ctx,b,pulse,hitKey);
                if(index==0&&devour_&&b.action==LifeAction::Hammer)DevourHammerHits(r,ctx,b,pulse);
                if(hit){LifeImpact(ctx,b.action==LifeAction::Hammer?ImpactHammerHit:ImpactBladeHit,b.at+b.forward*(3*LifeScale(b.mass)),b.action==LifeAction::Hammer?V{0,1,0}:b.forward);
                    if(swarmEffects_.size()<128)swarmEffects_.push_back({b.at+b.forward*(3*LifeScale(b.mass)),0,3*LifeScale(b.mass),2});}
            }
        }
        // 取り込み → 回復 → 待機の順に進め、回復の終わりで成長を反映する
        // 吸収モードの飛びかかりは TickDevourPounce が終わらせる
        if(((b.action==LifeAction::Engulf&&!devour_)||b.action==LifeAction::Restore)&&b.actionAge>=LifeDuration(b,life_.recipe)){
            // 回復の終わりに成長を反映し、メッセージを出す
            if(b.action==LifeAction::Restore){b.action=LifeAction::Idle;float gained=LifeCompleteGrowth(life_,b);b.cooldown=.25f;
                if(gained>0){
                    life_.message="吸収して成長！ 本体 "+LifeNumber(body.mass)+" / 子分 "+std::to_string(life_.ChildCount())+"体";life_.messageAge=2;}
            }
            else b.action=LifeAction::Restore;
            b.actionAge=0;b.target=0xffffffffu;b.grabs.clear();++life_.revision;
        }
    }
    // 転がるタイヤやタックルが触れた敵にダメージ
    // 突進の当たりは上で調べるので、ここは普通に転がっているときとタックルだけ (盾や装甲は崩せない)
    bool rolling=body.action==LifeAction::Tire&&body.weapon.ready&&body.weapon.dash<=0;
    if(life_.contactCooldown<=0&&LifeDistance(old,body.at)>dt*(rolling?10.f:5.f)&&(body.action==LifeAction::Idle||rolling)){
        for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(rolling?LifeSegmentDistance(enemy.at,old,body.at):LifeDistance(enemy.at,body.at))<2.7f*LifeScale(body.mass)+enemy.scale){
            LifeHit(r,p,ctx,enemy.id,(rolling?20.f:12.f)*LifeScale(body.mass),LifeAction::Idle,body.at);life_.contactCooldown=.55f;break;}}
    // 狙いと発射は、体の移動と向きの後のこのフレームのカメラを使う
    InkCamera(r,p,ctx);if(devour_)DevourTourCamera(ctx);UpdateHydrantAim(ctx);
    UpdateHydrantWater(r,p,ctx,dt,fire,absorb);
    // 街のステージを更新する
    if(!slimePlayerDemo_)UpdateCityStage(r,p,ctx,dt);
    // タイヤや消火栓を初めて解放したら、その形を選んだ状態にして知らせる
    if(!devour_&&!hadTire&&body.tireUnlocked){life_.form=LifeAction::Tire;life_.tireNotice=6;StartMorphShowcase(r,ctx,LifeAction::Tire);}
    if(!hadHydrant&&body.hydrantUnlocked){life_.form=LifeAction::Hydrant;life_.hydrantNotice=6;StartMorphShowcase(r,ctx,LifeAction::Hydrant);}
    if(!hadSignal&&body.signalUnlocked){life_.form=LifeAction::Signal;life_.signalNotice=6;StartMorphShowcase(r,ctx,LifeAction::Signal);}
    // 吸収モードの変形アイテムは、拾ったら好きなときに 1 回使える
    if(devour_&&((!hadTire&&body.tireUnlocked)||(!hadVacuum&&body.vacuumUnlocked)||(!hadSpring&&body.springUnlocked))){devourItemReady_=true;devourLog_.itemTaken=devourLog_.elapsed;DevourLogEvent("item_taken");PlayDevourSound(devourSounds_.morph,.7f,1.25f);life_.message="変形アイテムを手に入れた！ 左クリック / RTで変形";life_.messageAge=3;}
    // 街の敵を更新する
    ink.swarmSpawned=int(swarm_.size());
    // 信号機の横断歩道と輪を進める
    UpdateSignalEffects(r,p,ctx,dt);
    // エフェクトの時間を進めて古いものを消す
    for(auto& effect:swarmEffects_)effect.age+=dt;swarmEffects_.erase(std::remove_if(swarmEffects_.begin(),swarmEffects_.end(),[](const SwarmEffect& e){return e.age>.7f;}),swarmEffects_.end());
    // 体力・新しい材料の解放・演出。全段階を終えたらクリア
    p.mass=body.mass;auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(body.maxHealth);health.SetHp(body.health);health.SetDead(body.health<=0);
    // 5 回取り込んだら泡の材料を解放する
    if(life_.absorbed>=5&&lifeMaterials_<4){lifeMaterials_=4;SaveSlimeLab();}
    // 旧モードの値を戻し、演出と体の形を反映する
    p.worldScale=p.aimScale=1;p.aiming=false;p.action=Action::Free;
    Presentation(r,p,ctx);SyncSlimeBodies(ctx);
    // 体力が尽きたら負け、最後の段階の敵を全滅させたら勝ち
    if(devour_){
        // 吸収モード: 目標に届いたら大きく知らせる。時間切れ (か全部食べ尽くす) で結果を出す
        const auto& stage=CurrentDevourStage();int rank=DevourRankAt(stage,body.mass);
        // 「ランク X」と次の目標 (何 cm) は HUD の 1 つの知らせにまとめて出す (UI::Scale::Hud)。ここでは別の札を出さない
        if(!finished_&&rank>devourRank_){devourRank_=rank;devourClock_.flash=UI::Scale::RankFlashSeconds;bool top=rank==int(stage.goals.size());
            LifeImpact(ctx,ImpactDevourTier,body.at,{0,1,0});PlayDevourSound(top?devourSounds_.rankTop:devourSounds_.rankUp,.8f);}
        bool cleared=std::none_of(life_.props.begin(),life_.props.end(),[](const LifeCollectible& prop){return prop.active;});
        // 計測用 (--load-profile-auto): 1 回目は 2.5 秒で結果画面を出し、結果を開くときの引っかかりも記録する
        static bool autoFinished=false;
        if(!finished_&&!autoFinished&&life_.age>2.5f&&wcsstr(GetCommandLineW(),L"--load-profile-auto")){autoFinished=true;Engine::LoadProfile::Record("drive","result: auto finish",0);FinishDevour(p);}
        if(!finished_&&(devourClock_.left<=0||cleared))FinishDevour(p);
    }
    else if(body.health<=0)Finish(p,false);
}
// 取り込み中の触手の先に、つかんだ物 (または光る球) を描く
void ChronoSystem::DrawLifeTentacles(GameContext& ctx,const LifeBody& body){
    if(body.action!=LifeAction::Engulf)return;
    // 取り込みの進み具合
    float phase=body.actionAge/life_.recipe.AbsorbDuration();
    for(size_t i=0;i<body.grabs.size();++i){const auto& grab=body.grabs[i];
        // 本物の対象は取り込むまで残る。その後、見た目は先端と一緒に戻り、
        // 予約した報酬は回復を待つ
        // 対象を確定したか、研究所の見本なら、触手の先に描く
        if(!grab.cancelled&&(grab.committed||life_.lab)){
            // 確定後は触手の先と一緒に戻り、小さくなる
            V at=phase>=LifeTentacleCapturePhase?LifeTentaclePoint(body,life_.recipe,grab,i,1):grab.at;
            float shrink=1-.7f*LifeSmooth((phase-LifeTentacleCapturePhase)/(1-LifeTentacleCapturePhase));
            float radius=(life_.lab?.27f:LifeOrbRadius(grab.reward))*shrink;
            bool prop=(grab.target&0x80000000u)!=0;uint32_t index=grab.target&0x7fffffffu;
            // 街の小物なら実際のモデルで描く
            if(!life_.lab&&prop&&index<life_.props.size()&&!life_.props[index].orb&&DrawCityPropInTentacle(ctx,life_.props[index].visual,at,shrink))continue;
            // それ以外は報酬の大きさの色の球
            auto tint=life_.lab?V{.08f,.72f,1}:LifeOrbColor(grab.reward);Engine::Transform object;object.translate=EV(at);object.scale={radius,radius,radius};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,object,{tint.x,tint.y,tint.z,1});
        }
    }
}
// スライムの生活モードの描画: 群れ・街・変形アイテムの強調・触手・取り込める物
void ChronoSystem::DrawSlimeLife(entt::registry& r,GameContext& ctx){
    // 群れと街を描く
    DrawSwarm(r,ctx);if(!slimePlayerDemo_)DrawCityStage(ctx);
    // 信号機の灯器・横断歩道・止まった敵とマーカー
    DrawSignal(ctx);
    // 変形アイテム (タイヤ・消火栓) を光らせ、足元の輪と上の矢印で示す
    for(const auto& prop:life_.props)if(prop.active&&prop.morphItem!=LifeMorphItem::None){
        auto entity=entt::entity(prop.visual);auto visual=cityVisuals_.find(prop.visual);
        if(r.valid(entity)&&r.all_of<TransformComponent,MeshRendererComponent>(entity)&&visual!=cityVisuals_.end()){
            const auto& v=visual->second;auto t=r.get<TransformComponent>(entity).GetTransform();
            // スキャンの中心 (取り込んだときの原点も含む) を基準に広げる
            V center=V{t.translate.x,t.translate.y,t.translate.z}+v.center;t.scale=EV(v.scale*1.035f);t.translate=EV(center-v.center*1.035f);
            ctx.renderer->DrawMeshInstanced(v.mesh,white_,t,{1,.63f,.08f,.8f},"MorphItemHighlight");
            // 吸収モード: 台座の上で回って弾む
            if(devour_){auto& spin=r.get<TransformComponent>(entity);spin.rotate.y=life_.age*1.6f;spin.translate.y=devourItemY_+.5f*std::sin(life_.age*3);}
        }
        // 吸収モード: 足元で広がる輪 (光の柱は台座と一緒に作ってある)
        if(devour_){float ground=InkGround(prop.at);
                        for(int k=0;k<3;++k){float u=std::fmod(life_.age*.7f+float(k)/3,1.f);Engine::Transform wave;wave.translate={prop.at.x,ground+.2f,prop.at.z};wave.scale={8+26*u,1,8+26*u};ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,wave,{1,.65f,.08f,.4f*(1-u)},"HordeEffect");}
            continue;}
        Engine::Transform ring;ring.translate={prop.at.x,InkGround(prop.at)+.18f,prop.at.z};ring.scale={3.2f,1,3.2f};
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,ring,{1,.65f,.08f,.25f},"HordeEffect");
        float bounce=.4f*std::sin(life_.age*3);
        V tip{prop.at.x,prop.at.y+4.8f+bounce,prop.at.z};
        ctx.renderer->DrawLine3D(EV(tip+V{-1,1,0}),EV(tip),{1,.72f,.15f,1});
        ctx.renderer->DrawLine3D(EV(tip+V{1,1,0}),EV(tip),{1,.72f,.15f,1});
    }
    // 部屋のギミックの作動範囲の膜
    if(devour_&&DevourShelfDive()<=0)DrawGimmickCurtains(ctx);
    // こぼれた液体 (近いものだけ)
    if(devour_)DrawDevourSpills(ctx);
    // スライムが通った所のぬれ跡と、大きい物を飲んで育ったときのしずく (結果の「おなかの中」では描かない)
    if(devour_&&DevourShelfDive()<=0){DrawDevourTrail(ctx);DrawDevourGrowth(ctx);}
    // 吸収モードの目印 (門と広場の大物): 上に大きな矢印を立てる
    // (結果の「おなかの中」では、体の外の目印を描かない)
    if(devour_&&DevourShelfDive()<=0)for(const auto& prop:life_.props)if(prop.active&&prop.beacon&&!prop.sealed){float size=std::max(3.f,prop.radius*.5f),bounce=size*.2f*std::sin(life_.age*3);
        V tip{prop.at.x,prop.at.y+prop.radius*2.4f+size+bounce,prop.at.z};V side{size,size,0},back{0,size,size};
        for(V wing:{side,V{-side.x,side.y,0},back,V{0,back.y,-back.z}})ctx.renderer->DrawLine3D(EV(tip+wing),EV(tip),{1,.72f,.15f,1});
        ctx.renderer->DrawLine3D(EV(tip+V{0,size*3,0}),EV(tip),{1,.72f,.15f,1});}
    // 触手を描く (吸収モードでは、体の中で溶けていく物を描く)
    if(devour_||slimePlayerDemo_)DrawDevourMeals(ctx);
    else for(const auto& body:life_.bodies)DrawLifeTentacles(ctx,body);
    // 子分を縁取る。壁の向こうでも深度を見ない薄い縁が残るので、どこにいても分かる
    if(!life_.lab)for(size_t i=1;i<life_.bodies.size();++i){const auto& c=life_.bodies[i];if(c.mass<=0)continue;
        V radii=LifeRadii(c,life_.recipe);float height=(c.down?.3f:.75f)*radii.y;auto tint=c.down?Engine::Vector4{.45f,.5f,.55f,1}:LifeChildTint(c);
        float wide=std::max(1.3f,std::max(radii.x,radii.z)*1.1f);height=std::max(.8f,height);
        Engine::Transform t;t.translate={c.at.x,c.at.y-1.25f+height,c.at.z};t.scale={wide,height*1.15f,wide};
        tint.w=c.down?.25f:.75f;ctx.renderer->DrawMeshInstanced(sphere_,white_,t,tint,"SlimeChildGlow");
        tint.w=c.down?.12f:.5f;ctx.renderer->DrawMeshInstanced(sphere_,white_,t,tint,"SlimeChildXRay");}
    // モデルの無い取り込める物: 光る球 (報酬の段階の輪付き) か、鎧の破片
    for(const auto& prop:life_.props)if(prop.active&&!(prop.sealed&&prop.ring==0)&&prop.visual==0xffffffffu){Engine::Transform t;
        if(prop.orb){float radius=prop.shard>0?prop.shard:LifeOrbRadius(prop.reward);auto tint=LifeOrbColor(prop.reward);Engine::Vector4 color{tint.x,tint.y,tint.z,1};
            // 砕けた欠片は元の物から弧を描いて飛んでくる
            V orbAt=prop.at;if(prop.shard>0){float u=std::clamp((life_.age-prop.shardStart)/.45f,0.f,1.f);orbAt=Lerp(prop.shardFrom,prop.at,u)+V{0,prop.shard*4*std::sin(3.1415927f*u),0};}
            t.translate=EV(orbAt);t.translate.y+=.12f*std::sin(life_.age*2+prop.at.x);t.scale={radius,radius,radius};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,color);t.scale={radius*.45f,radius*.45f,radius*.45f};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.9f,1,1,1});
            for(int ring=0;ring<=LifeOrbTier(prop.reward);++ring){float reach=radius*(1.25f+float(ring)*.38f);
                for(int segment=0;segment<24;++segment){float a=float(segment)*6.2831853f/24,b=float(segment+1)*6.2831853f/24;
                    ctx.renderer->DrawLine3D({prop.at.x+std::cos(a)*reach,prop.at.y-.8f,prop.at.z+std::sin(a)*reach},
                        {prop.at.x+std::cos(b)*reach,prop.at.y-.8f,prop.at.z+std::sin(b)*reach},color);}}
            continue;
        }
        t.translate=EV(prop.at);float size=prop.required>100?1.35f:.55f;t.scale={size*swarmMeshes_[0].inverseExtent.x,size*.7f*swarmMeshes_[0].inverseExtent.y,size*swarmMeshes_[0].inverseExtent.z};
        ctx.renderer->DrawMeshInstanced(swarmMeshes_[0].handle,white_,t,prop.required>100?Engine::Vector4{.5f,.6f,.45f,1}:Engine::Vector4{.65f,.78f,.4f,1},"Default");}
}
// 子分の印: 画面内なら頭上に小さな体力の棒、画面外なら端に向きの矢印を出す
void ChronoSystem::DrawLifeChildMarkers(GameContext& ctx){
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    auto viewProj=ctx.camera->View()*ctx.camera->Proj();
    for(size_t i=1;i<life_.bodies.size();++i){const auto& c=life_.bodies[i];if(c.mass<=0)continue;
        auto tint=c.down?Engine::Vector4{.45f,.5f,.55f,.8f}:LifeChildTint(c);V head=c.at+V{0,LifeRadii(c,life_.recipe).y*1.6f+.6f,0};
        DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(head.x,head.y,head.z,1),viewProj));
        float w=std::abs(clip.w)<.001f?.001f:clip.w,x=(clip.x/w*.5f+.5f)*1280,y=(.5f-clip.y/w*.5f)*720;
        if(clip.w>.1f&&x>24&&x<1256&&y>24&&y<696){
            if(c.down){ui.Dot(x,y,3,tint);continue;}
            ui.Fill({x-15,y-2,30,5},{.01f,.02f,.02f,.75f});ui.Fill({x-14,y-1,28*std::clamp(c.health/std::max(1.f,c.maxHealth),0.f,1.f),3},tint);
            continue;}
        // 画面外: 中心から子分への向きで、HUD の帯より内側の縁に矢印を置く (後ろ側は向きを反転する)
        float dx=x-640,dy=y-320;if(clip.w<=.1f){dx=-dx;dy=-dy;}
        float length=std::sqrt(dx*dx+dy*dy);if(length<.01f)continue;dx/=length;dy/=length;
        float reach=std::min(dx!=0?(596/std::abs(dx)):1e9f,dy!=0?(262/std::abs(dy)):1e9f),ex=640+dx*reach,ey=320+dy*reach;
        ui.Dot(ex-dx*10,ey-dy*10,11,{.01f,.02f,.02f,.6f});
        float side=6;ui.Line(ex-dx*16-dy*side,ey-dy*16+dx*side,ex-dx*4,ey-dy*4,3,tint);ui.Line(ex-dx*16+dy*side,ey-dy*16-dx*side,ex-dx*4,ey-dy*4,3,tint);}
}
// 今の段階の敵の印: 画面内なら頭上に赤い下向きの印 (建物の向こうでも見える)、画面外なら端に向きの矢印
void ChronoSystem::DrawLifeEnemyMarkers(GameContext& ctx){
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    auto viewProj=ctx.camera->View()*ctx.camera->Proj();const V from=life_.bodies[0].at;
    // 今の段階の生きている敵を近い順に並べる
    std::vector<std::pair<float,const SwarmEnemy*>> targets;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.step==life_.step)targets.push_back({LifeDistance(from,enemy.at),&enemy});
    std::sort(targets.begin(),targets.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    const Engine::Vector4 red{1,.24f,.18f,1},dark{.03f,.01f,.01f,.7f};float pulse=.5f+.5f*std::sin(life_.age*6);int arrows=0;
    for(const auto& [distance,enemy]:targets){V head=enemy->at+V{0,2.4f*enemy->scale+2,0};
        DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(head.x,head.y,head.z,1),viewProj));
        float w=std::abs(clip.w)<.001f?.001f:clip.w,x=(clip.x/w*.5f+.5f)*1280,y=(.5f-clip.y/w*.5f)*720;
        // 近いほど大きく濃い
        float closeness=std::clamp(1-(distance-40)/360,.35f,1.f);
        if(clip.w>.1f&&x>30&&x<1250&&y>30&&y<690){
            // 下向きの三角 (敵の頭を指す)
            float s=(6+5*closeness)*(1+.15f*pulse);auto color=UI::WithAlpha(red,.55f+.45f*closeness);
            ui.Dot(x,y-s*.5f,s*1.15f,UI::WithAlpha(dark,.6f*closeness));
            for(float k=0;k<1;k+=.25f){float half=s*(1-k);ui.Line(x-half,y-s+k*s*1.6f,x+half,y-s+k*s*1.6f,s*.45f,color);}
            continue;}
        // 画面外: 近い 12 体まで、中心から敵への向きで縁に矢印を置く (後ろ側は向きを反転する)
        if(arrows>=12)continue;++arrows;
        float dx=x-640,dy=y-320;if(clip.w<=.1f){dx=-dx;dy=-dy;}
        float length=std::sqrt(dx*dx+dy*dy);if(length<.01f)continue;dx/=length;dy/=length;
        float reach=std::min(dx!=0?(596/std::abs(dx)):1e9f,dy!=0?(262/std::abs(dy)):1e9f),ex=640+dx*reach,ey=320+dy*reach;
        float size=10+8*closeness;auto color=UI::WithAlpha(red,.5f+.5f*closeness*(.7f+.3f*pulse));
        ui.Dot(ex-dx*size*.7f,ey-dy*size*.7f,size*.95f,dark);
        // 塗った矢じり: 根元から先へ細くなる線を重ねる
        for(float k=0;k<1;k+=.2f){float half=size*.65f*(1-k);float bx=ex-dx*size*(1.2f-k),by=ey-dy*size*(1.2f-k);
            ui.Line(bx-dy*half,by+dx*half,bx+dy*half,by-dx*half,size*.25f,color);}}
}
// スライムの生活モードの UI: 結果・現在の段階・取り込みの大きさ・門の体力・体の状態・操作・変形・消火栓の照準・案内
void ChronoSystem::DrawSlimeLifeUI(entt::registry& r,GameContext& ctx){
    // UI の準備
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    auto& body=life_.bodies[0];(void)r;
    if(slimePlayerDemo_){DrawSlimePlayerDemoUI(ctx);return;}
    // 変形の見せ場の間は HUD を隠し、見せ場の画面だけを描く
    if(showcase_.active&&!life_.lab){DrawMorphShowcase(ctx);return;}
    // 終わったら結果を表示する
    if(finished_){DrawDevourUI(ctx);return;}
    {
    // 影付きの文字 (真下へずらした黒の上に重ねる)。align=1 なら x を右端、.5 なら中央にする
    const Engine::Vector4 hazard{.95f,.76f,.19f,1},slime{.61f,.89f,.23f,1},white{.96f,.96f,.94f,1},black{.08f,.08f,.08f,1},red{.84f,.2f,.16f,1};
    auto shadowed=[&](const std::string& text,float x,float y,float size,Engine::Vector4 color,const char* font,float align=0){
        x-=ui.Width(text,size,font)*align;ui.Text(text,x,y+size*.09f,size,{0,0,0,.85f*color.w},font);ui.Text(text,x,y,size,color,font);};
    auto spaced=[&](const std::string& text,float x,float y,float size,Engine::Vector4 color,float spacing,float align=0){
        x-=ui.SpacedWidth(text,size,spacing)*align;ui.Spaced(text,x+1,y+2,size,spacing,{0,0,0,.8f*color.w});return ui.Spaced(text,x,y,size,spacing,color);};
    // 吸収モード: 見出し・残りの敵・門・体力の代わりに、制限時間・大きさの段階・大きさを出す
    DrawDevourUI(ctx);
    // 左下: 体力の数値と、工事の縞の上を満たすスライムのゲージ
    // 右下: 変形のステッカー。選んでいる形は大きく、未解放は斜線
    if(devourFreeForms_){const auto& forms=LifeForms();
    const char* icons[]={"form_hammer","form_tire","form_vacuum","form_spring"};
    const char* names[]={"ハンマー","タイヤ","掃除機","バネ"};const float tilt[]={5,-6,4,-3};
    int picked=LifeFormIndex();
    const auto& signal=life_.signal;
    for(int i=0;i<4;++i){bool open=LifeCanMorph(body,forms[i]),sel=i==picked,active=body.action==forms[i];
        float size=sel?87.f:74.f,unit=size/74,cx=984+float(i)*80,cy=580.f-(sel?8.f:0.f),rotation=(sel?0:tilt[i])*.0174533f,alpha=open?1:.55f;
        UI::Rect outer{cx-size*.5f,cy-size*.5f,size,size},inner{outer.x+4*unit,outer.y+4*unit,size-8*unit,size-8*unit};
        ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",{outer.x+2,outer.y+6,outer.w,outer.h},rotation,{0,0,0,.45f*alpha});
        ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",outer,rotation,UI::WithAlpha(white,alpha));
        ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",inner,rotation,sel?slime:UI::WithAlpha(black,alpha));
        if(active&&body.weapon.ready)ui.Halo(cx,cy,size*.8f,UI::WithAlpha(slime,.35f+.15f*std::sin(life_.age*6)));
        float icon=40*unit;ui.ImageRot(std::string("Resources/Textures/UI/HUD/")+icons[i]+".png",{cx-icon*.5f,cy-icon*.5f,icon,icon},rotation,sel?black:open?hazard:Engine::Vector4{.53f,.53f,.53f,alpha});
        if(!open)ui.Line(inner.x+6,inner.y+inner.h-6,inner.x+inner.w-6,inner.y+6,5,{.84f,.2f,.16f,alpha});
        float bx=outer.x+5,by=outer.y+3;ui.Dot(bx,by,14,UI::WithAlpha(black,alpha));ui.Dot(bx,by,12,UI::WithAlpha(hazard,alpha));
        // パッドなら変形する十字キーの向き、キーボードなら数字キーを札に出す
        if(UI::UsingPad()){static const char* dpad[]={"xbox_dpad_up","xbox_dpad_right","xbox_dpad_down","xbox_dpad_left"};ui.PadGlyph(dpad[i],bx-12,by-12,24);}
        else ui.Center(std::to_string(i+1),bx,by-11,16,black,UI::JapaneseFont);}
    // パッドなら LB / RB で前後の形へ変形できることを、ステッカーの左上に出す
    if(UI::UsingPad()){ui.PadGlyph("xbox_lb",866,616,26);ui.PadGlyph("xbox_rb",894,616,26);}
    // ステッカーの上: 選んでいる形の名前と状態
    bool open=LifeCanMorph(body,forms[picked]),active=body.action==forms[picked];
    const char* nextName=signal.next==SignalColor::Red?"NEXT  RED":signal.next==SignalColor::Green?"NEXT  GREEN":"NEXT  YELLOW";
    std::string state=active&&!body.weapon.ready?"FORMING":forms[picked]==LifeAction::Signal&&open?(SignalResting(signal)?"REST":nextName):active?(body.weapon.dash>0?"DASH":body.weapon.charging?"CHARGE":"READY"):open?"":forms[picked]==LifeAction::Hammer?"LOCK  MASS "+std::to_string(int(LifeHammerMass)):"LOCK  ITEM";
    char label[32];snprintf(label,sizeof(label),"FORM %02d",picked+1);
    spaced(std::string(label)+(state.empty()?"":"  /  "+state),1246,465.f,13,hazard,2,1);
    shadowed(names[picked],1246,483.f,30,open?white:UI::WithAlpha(white,.6f),UI::JapaneseFont,1);}
    // 下中央: 今の形で使う操作だけを 1 枚の札に並べる (ゲーム全体で同じ「黒い台のキー + 名前」の見た目。UI::Scale::PromptBar)
    {namespace S=UI::Scale;bool unlocked=LifeCanMorph(body,life_.form);std::string attack=unlocked?"変形":"タックル";
    std::vector<S::Prompt> keys;const S::Prompt absorbHint{S::KeyAbsorb,"吸収"},back{S::KeyBack,"戻る"},jump{S::KeyJump,"ジャンプ"};
    if(body.action==LifeAction::Hammer)keys={{S::KeyMorph,"叩きつけ"},{S::KeyMorph,"連続","長押し"},absorbHint,jump,back};
    else if(body.action==LifeAction::Hydrant)keys={{S::KeyMorph,"放水"},{S::KeyAbsorb,"水の防御"},back};
    else if(body.action==LifeAction::Vacuum)keys={{S::KeyMorph,"吸い込む","長押し"},{S::KeyMove,"移動"},absorbHint,back};
    else if(body.action==LifeAction::Spring)keys={{S::KeyMorph,"溜める","長押し"},{S::KeyMorph,"跳ぶ","離す"},{S::KeyMove,"向き"},absorbHint,back};
    else if(body.action==LifeAction::Tire)keys={{S::KeyMorph,"溜める","長押し"},{S::KeyMorph,"突進","離す"},{S::KeyMove,"曲がる"},absorbHint,back};
    else if(body.action==LifeAction::Signal){const auto& signal=life_.signal;
        std::string next=SignalResting(signal)?"点検中":signal.next==SignalColor::Red?"赤：足止め":signal.next==SignalColor::Green?"青：横断歩道":"黄：マーカー";
        keys={{S::KeyMorph,next},absorbHint,back};}
    else if(GimmickRiding())GimmickRidePrompts(keys);
    else if(!devourFreeForms_){if(devourItemReady_)keys={{S::KeyMorph,"変形"}};}
    else keys={{S::KeyMorph,attack},{S::KeyForms,"形を選ぶ"}};
    // 巻尺の上に出す
    S::PromptBar(ui,keys,640,568.f,20.f);}
    // タイヤの溜め: 体の足元に、溜まった量のゲージを出す (溜めきると光る)
    if((body.action==LifeAction::Tire||body.action==LifeAction::Spring)&&body.weapon.charging){
        DirectX::XMFLOAT4 clip;V foot=body.at-V{0,1.25f,0};DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(foot.x,foot.y,foot.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>.1f){float gx=std::clamp((clip.x/clip.w*.5f+.5f)*1280,90.f,1190.f),gy=std::clamp((.5f-clip.y/clip.w*.5f)*720+18,120.f,560.f),charge=body.weapon.charge;bool full=charge>=1;
            ui.Fill({gx-62,gy-2,124,12},black);ui.Fill({gx-60,gy,120,8},UI::WithAlpha(white,.25f));ui.Fill({gx-60,gy,120*charge,8},full?hazard:slime);
            if(full)ui.Halo(gx,gy+4,70,UI::WithAlpha(hazard,.35f+.2f*std::sin(life_.age*20)));
            spaced(full?"MAX":"CHARGE",gx,gy+12,12,full?hazard:white,3,.5f);}}
    // 解放のお知らせかメッセージ (見出しの下)
    if(life_.hydrantNotice>0)shadowed(UI::UsingPad()?"消火栓変形 解放！ 十字キー↓で変形 → RTで放水 / LTで防御":"消火栓変形 解放！ 3で変形 → 左クリックで放水 / 右クリックで防御",640,190,18,hazard,UI::JapaneseFont,.5f);
    else if(life_.signalNotice>0)shadowed(UI::UsingPad()?"信号機変形 解放！ 十字キー←で変形 → RTで 赤 → 青 → 黄 の順に信号を出す":"信号機変形 解放！ 4で変形 → 左クリックで 赤 → 青 → 黄 の順に信号を出す",640,190,18,hazard,UI::JapaneseFont,.5f);
    else if(life_.tireNotice>0)shadowed(UI::UsingPad()?"タイヤ変形 解放！ 十字キー→で変形 → RT長押しで回転を溜め、離して突進":"タイヤ変形 解放！ 左長押しで回転を溜め、離して突進",640,190,18,hazard,UI::JapaneseFont,.5f);
    // ランクの知らせ (HUD の赤い札と次の目標) が出ている間は、その下へずらして重ねない
    else if(life_.messageAge>0)UI::Scale::Notice(ui,life_.message,640,devour_&&!finished_&&devourClock_.flash>0&&devourTutorial_.step<0?UI::Scale::RankNoticeY:130.f);
    DrawSignalUI(ctx);DrawLifeEnemyMarkers(ctx);DrawLifeChildMarkers(ctx);
    // 消火栓の照準: 届かない・遮られるときは金色にして着弾点に印を付ける
    if(hydrantAim_.active){
        bool obstructed=hydrantAim_.blocked||!hydrantAim_.reachable;
        Engine::Vector4 color=!body.weapon.ready?UI::Muted:obstructed?UI::Gold:hydrantAim_.enemy?Engine::Vector4{1,.36f,.24f,1}:Engine::Vector4{.65f,1,1,1};
        // 暗い縁で、空・ガラス・明るい水に対しても見やすくする
        for(auto bar:{UI::Rect{626,359,8,2},UI::Rect{646,359,8,2},UI::Rect{639,346,2,8},UI::Rect{639,366,2,8}}){
            ui.Fill({bar.x-1,bar.y-1,bar.w+2,bar.h+2},{.015f,.025f,.03f,.9f});ui.Fill(bar,color);
        }
        ui.Dot(640,360,2.8f,{.015f,.025f,.03f,.9f});ui.Dot(640,360,1.4f,color);
        if(obstructed){
            ui.Center(hydrantAim_.blocked?"射線が遮られています":"射程外",640,386,13,UI::Gold,UI::JapaneseFont);
            DirectX::XMFLOAT4 clip;const auto& point=hydrantAim_.impact;
            DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(point.x,point.y,point.z,1),ctx.camera->View()*ctx.camera->Proj()));
            if(clip.w>.1f){float x=(clip.x/clip.w*.5f+.5f)*1280,y=(.5f-clip.y/clip.w*.5f)*720;
                if(x>10&&x<1270&&y>110&&y<575){ui.Line(x-4,y-4,x+4,y+4,2,UI::Gold);ui.Line(x-4,y+4,x+4,y-4,2,UI::Gold);}}
        }
    }
    // 最初のチュートリアルの課題と巻尺の説明 (他の表示の上に重ねる)
    DrawDevourTutorial(ctx);
    }
}
}
