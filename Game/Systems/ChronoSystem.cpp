// ChronoSystem.cpp
// 現在のゲームの中心のシステムの共通の処理: 初期化、当たり判定 (箱の掃引と移動)、ダメージと反応、演出、
// 終了と記録、毎フレームの更新・描画・UI の振り分け。本編の処理は別の .cpp にある (ChronoSystemInternal.h の一覧)。
#include "ChronoSystemInternal.h"

namespace Game {

// 名前付きのメッシュのエンティティを作る (当たり判定の箱とカメラの遮蔽も付ける)
entt::entity ChronoSystem::Mesh(entt::registry& r,const std::string& name,const std::string& path,V center,V size){
    // 名前に「Chrono 」を付けたエンティティを作り、モデルを読み込む
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name);
    auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelPath=path;
    mr.modelHandle=Engine::Renderer::GetInstance()->LoadObjMesh(path);mr.useCubemap=false;
    // 中心の位置に置く
    auto& t=r.emplace<TransformComponent>(e);t.translate=Write(center);
    auto* model=Engine::Renderer::GetInstance()->GetModel(mr.modelHandle);
    // モデルの頂点の範囲を求め、指定の大きさに合うよう拡大して中心をそろえる
    if(model && !model->GetData().vertices.empty()){
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:model->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        V scale{size.x/std::max(hi.x-lo.x,0.001f),size.y/std::max(hi.y-lo.y,0.001f),size.z/std::max(hi.z-lo.z,0.001f)};
        V mid=(lo+hi)*0.5f;t.scale=Write(scale);
        t.translate=Write(center-V{mid.x*scale.x,mid.y*scale.y,mid.z*scale.z});
    }
    // 草のブロックと自然物はカメラを遮る物として登録する
    if(path==grass || path.find("/Nature/")!=std::string::npos)
        r.emplace<CameraOccluder>(e,center,size,1.0f);
    return e;
}

// PLAY 開始時の初期化: モードを決め、前回のエンティティを消し、プレイヤー・音・ステージ・記録を用意する
void ChronoSystem::Reset(entt::registry& r){
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::Reset");
    // 旧来の確認用のテストも動かせるようにする。通常のプレイは常に新しいシューターを使う
    // コマンドラインの引数で、インクのモードとスライムの生活モードを決める
#ifdef NDEBUG
    // Release packages contain the current city game; diagnostics use the same assets.
    inkMode_=true;slimeLife_=true;
#else
    inkMode_=!wcsstr(GetCommandLineW(),L"--chrono-smoke")&&!wcsstr(GetCommandLineW(),L"--creature-smoke")&&!wcsstr(GetCommandLineW(),L"--snake-smoke");
    slimeLife_=inkMode_&&(!wcsstr(GetCommandLineW(),L"-smoke")||wcsstr(GetCommandLineW(),L"--slime-life-smoke"))&&!wcsstr(GetCommandLineW(),L"--legacy-domain");
#endif
    slimePlayerDemo_ = scene_->IsSlimePlayerDemo();
    if(slimePlayerDemo_) inkMode_ = slimeLife_ = true;
    // スライムの変形のターゲットを消す
    Engine::Renderer::GetInstance()->SetFluidMorphTargets({},0);
    if(inkMode_){creature_=true;arena_=false;inkShots_.clear();inkSurfaces_.clear();inkMeshes_.clear();inkDirty_=true;}
    // 前回作った「Chrono 」のエンティティを消す
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    // 名前でプレイヤーとボスを探す
    player_=boss_=entt::null;
    for(auto e:r.view<NameComponent>()){
        const auto& n=r.get<NameComponent>(e).name;if(n=="Player")player_=e;if(n=="Boss")boss_=e;
    }
    // 撃破の演出・捕獲・終了・カメラ・ムービーの状態を初期化する
    bossDefeat_=defeatBurst_=false;defeatAge_=bossFlash_=0;bossDebris_.clear();
    candidateCapture_=captureShown_=0;captureScanClock_=0;captureAge_=10;
    initialized_=r.valid(player_);finished_=won_=false;finishAge_=0;sparks_.clear();creatureSolids_.clear();
    prevL_=Down(VK_LBUTTON);prevM_=Down(VK_MBUTTON);prevSpace_=Controls::Jump();prevShift_=Down(VK_SHIFT);
    yaw_=0;pitch_=0.12f;zoom_=17;shoulder_=1.8f;chainCameraHold_=0;fov_=1.0472f;
    cameraReady_=false;zoomVelocity_=0;cameraBoom_=-1;composition_={};viewPitch_=pitch_;postStrength_=0;droplets_.clear();manualCameraHold_=0;
    openingPending_=true;cinematicActive_=false;cinematicBoss_=false;cinematicAge_=0;
    // プレイヤーがいなければここまで
    if(!initialized_)return;
    // 戦闘の BGM を流す
    Music::Play(Music::Battle);
    // ボスの生き物のコンポーネントを外し、元のモデルを表示する
    if(r.valid(boss_)){r.remove<CreatureBoss>(boss_);r.get<MeshRendererComponent>(boss_).enabled=true;}
    // プレイヤーの状態を作り直し、位置・体力・入力を初期化する
    r.emplace_or_replace<Player>(player_);
    auto& pt=r.get<TransformComponent>(player_);pt.translate={0,1.3f,-27};pt.scale={1,1,1};
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(100);hp.SetMaxHp(200);hp.SetDead(false);
    auto& pi=r.get<PlayerInputComponent>(player_);pi.selectedCan=CanType::None;pi.lockedEnemy=entt::null;pi.isRadialMenuOpen=false;
    if(auto* box=r.try_get<BoxColliderComponent>(player_))box->enabled=false;
    // 白テクスチャ・球のメッシュ・音を読み込む
    auto* renderer=Engine::Renderer::GetInstance();white_=renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    sphere_=renderer->LoadObjMesh(ball);
    if(hitSound_==0xffffffff)hitSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/suittikirikae.mp3");
    if(coreSound_==0xffffffff)coreSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/kettei.mp3");
    // ポストエフェクトを既定に戻してから、ステージを作る
    renderer->SetPostEffect("Default");renderer->SetPostProcessParams({});
    Build(r);CacheSolids(r);
    if(inkMode_)renderer->SetPostEffect("ChronoFocus");
    // 最高記録を読み込む
    bestTime_=0;bestChain_=0;newTime_=newChain_=false;
    try{std::ifstream file(RecordPath());if(file){nlohmann::json j;file>>j;bestTime_=j.value("time",0.0f);bestChain_=j.value("chain",0);}}catch(...){}
    renderer->SetAmbientColor({0.65f,0.68f,0.75f});
}

// 当たり判定に使う箱の一覧を作る
void ChronoSystem::CacheSolids(entt::registry& r){
    solids_.clear();
    // 固い物の箱を ワールド座標の AABB として集める
    for(auto e:r.view<Solid,BoxColliderComponent,TransformComponent>()){
        const auto& b=r.get<BoxColliderComponent>(e);const auto& t=r.get<TransformComponent>(e);
        V c=Read(t.translate)+Read(b.center),h=Read(b.size)*0.5f;solids_.push_back({c-h,c+h});
    }
}

// 箱をずらしながら動かしたとき、最初に当たる箱を求める
bool ChronoSystem::Sweep(V p,V d,V half,float& fraction,V& normal)const{
    // 地形の箱と生き物の箱の中で一番早く当たるものを選ぶ
    bool found=false;fraction=1;for(const auto& box:solids_){float f;V n;
        if(Chrono::Sweep(p,d,box,half,f,n)&&f<fraction){fraction=f;normal=n;found=true;}}
    for(const auto& box:creatureSolids_){float f;V n;
        if(Chrono::Sweep(p,d,box,half,f,n)&&f<fraction){fraction=f;normal=n;found=true;}}
    return found;
}

// 2 点の間が空いているか
bool ChronoSystem::Clear(V a,V b,float radius)const{float f;V n;return !Sweep(a,b-a,{radius,radius,radius},f,n);}

// 壁に沿って動かす (接地も判定する)
ChronoSystem::V ChronoSystem::Move(V p,V d,V half,bool& grounded,V* velocity)const{
    grounded=false;
    // 最大 3 回、当たった壁に沿って残りの移動を滑らせる
    for(int i=0;i<3 && Length(d)>1e-5f;++i){float f;V n;
        if(!Sweep(p,d,half,f,n)){p=p+d;break;}
        p=p+d*std::max(0.0f,f-0.001f);if(n.y>0.5f)grounded=true;
        if(Length(n)<0.5f)break;
        d=d*(1-f);d=d-n*Dot(d,n);
        if(velocity)*velocity=*velocity-n*std::min(0.0f,Dot(*velocity,n));
    }
    // 少し下に何かあれば接地とする
    float f;V n;if(Sweep(p,{0,-0.06f,0},half,f,n)&&n.y>0.5f)grounded=true;
    return p;
}

// エンティティの中心
ChronoSystem::V ChronoSystem::Center(entt::registry& r,entt::entity e)const{
    return Read(r.get<TransformComponent>(e).translate)+r.get<Target>(e).offset;
}

// プレイヤーへのダメージ (完璧な回避の判定を含む)
void ChronoSystem::Damage(entt::registry& r,Player& p,float amount,GameContext& ctx,V source,float perfectWindow){
    // スライムの生活モードなら専用の被弾へ
    if(slimeLife_){
        DamageSlimeLife(r,p,amount,ctx,source);return;
    }
    // インクのモード: 完璧な回避の時間内なら避けて、時間を遅くする
    if(inkMode_&&!finished_){auto& ink=r.get<InkPlayer>(player_);
        if(ink.TryPerfectDodge(perfectWindow)){PerfectRogueLoop(r,p,ctx);p.invincible=std::max(p.invincible,SlimePerfectInvincibility);p.worldScale=.15f;
            ctx.camera->StartImpactShake(.10f,.08f,{0,1,0},12);return;}}
    // 無敵中・終了後は受けない
    if(p.invincible>0||finished_)return;
    // インクのモードでは量に合わせてダメージを増やし、描いている線を短くする
    if(inkMode_)amount*=SlimeMaximumMass/100.f*r.get<InkPlayer>(player_).rogue.Mitigation();
    if(inkMode_)r.get<InkPlayer>(player_).domainPath.Damage();
    // 量を減らし、無敵時間を付け、チェインを切る
    p.chainDrive=1;p.lastChainHit=10;p.mass=std::max(0.0f,p.mass-amount);p.invincible=0.65f;++p.stats.hitsTaken;p.flow.Break();
    p.damageSource=source;p.damageAge=0;
    p.buffer=0;p.bufferedTarget=entt::null;
    // 鎖の途中なら回復の姿勢へ
    if(p.action!=Action::Collapsed){p.action=Action::Recovery;p.timer=0.12f;p.target=entt::null;}
    ctx.camera->StartImpactShake(.16f,.18f,Write(Unit(Read(r.get<TransformComponent>(player_).translate)-source)),10);
    r.get<HealthComponent>(player_).SetHp(p.mass);
    if(p.mass<=0)Finish(p,false);
}
// チェインが途切れた理由
ChainFailure ChronoSystem::Failure(entt::registry& r,Player& p,GameContext& ctx){
    // 照準の正面の対象から、届かない理由を探す
    V origin=Read(r.get<TransformComponent>(player_).translate),cam=Read(ctx.camera->Position());
    V forward=Forward(yaw_,pitch_);float best=-1;ChainFailure reason=ChainFailure::NoTarget;
    for(auto e:r.view<Target,TransformComponent>()){
        const auto& tg=r.get<Target>(e);if(!tg.active||tg.kind==Kind::Boss)continue;
        V c=Center(r,e);float alignment=Dot(Unit(c-cam),forward),hit;
        if(p.aiming&&!RaySphere(cam,forward,c,tg.radius,100,hit))continue;
        if(alignment<0.55f||alignment<=best)continue;best=alignment;
        reason=Length(c-origin)>Reach(p.mass)+tg.radius?ChainFailure::OutOfRange:
            !Reachable(r,p,e,origin)?ChainFailure::Obstructed:ChainFailure::None;
    }
    return reason;
}

// 命中や失敗の反応 (粒子と音)
void ChronoSystem::Feedback(Player& p,V at,bool heavy){
    // ヒットストップを入れ、水滴を飛ばす
    p.hitStop=std::max(p.hitStop,heavy?0.08f:0.025f);
    for(int i=0;i<(heavy?18:10)&&droplets_.size()<96;++i){
        float angle=i*2.39996f;float speed=heavy?8.0f:5.0f;
        droplets_.push_back({at,{std::cos(angle)*speed,2.0f+(i%4)*1.2f,std::sin(angle)*speed},0});
    }
    // コンボが続くほど高い音にする
    auto* audio=Engine::Audio::GetInstance();
    audio->Play(heavy?coreSound_:hitSound_,false,0.3f*audio->GetMasterSEVolume(),
        std::min(1.8f,1.0f+0.065f*p.flow.combo));
}

// 演出: 火花と水滴を進め、時間の遅さや被弾に応じてポスト効果をかける
void ChronoSystem::Presentation(entt::registry& r,Player& p,GameContext& ctx){
    // 火花はプレイヤーへ吸い込まれ、0.35 秒で消える
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(auto& spark:sparks_){spark.age+=ctx.dt;spark.to=player;}
    sparks_.erase(std::remove_if(sparks_.begin(),sparks_.end(),[](const Spark& s){return s.age>0.35f;}),sparks_.end());
    // 水滴は重力で落ち、0.45 秒で消える
    for(auto& d:droplets_){d.age+=ctx.dt;d.velocity.y-=16*ctx.dt;d.position=d.position+d.velocity*ctx.dt;}
    droplets_.erase(std::remove_if(droplets_.begin(),droplets_.end(),[](const Droplet& d){return d.age>.45f;}),droplets_.end());
    // 時間が遅いほど強いポスト効果をかける
    float intensity=finished_?0:std::clamp((1-p.worldScale)/.8f,0.f,1.f);
    postStrength_+=(intensity-postStrength_)*(1-std::exp(-8*ctx.dt));
    Engine::Renderer::PostProcessParams params{};
    params.time=ctx.renderer->GetPostProcessParams().time;params.san=postStrength_;
    // 引き寄せ中は歪みを強め、被弾直後は周辺を暗くする
    params.distortion=p.action==Action::Pulling?1.0f:0.65f;
    params.vignette=p.damageAge<.35f?1-p.damageAge/.35f:0;
    // インクのモードでは、プレイヤーとボスの深度を被写界深度に渡す
    if(inkMode_&&!finished_){
        using namespace DirectX;XMFLOAT3 playerView,bossView;V boss=Center(r,boss_);
        XMStoreFloat3(&playerView,XMVector3TransformCoord(XMVectorSet(player.x,player.y,player.z,1),ctx.camera->View()));
        XMStoreFloat3(&bossView,XMVector3TransformCoord(XMVectorSet(boss.x,boss.y,boss.z,1),ctx.camera->View()));
        params.dofFocus=std::max(8.f,playerView.z);params.dofRange=32;params.dofStrength=0;params.dofBossDepth=bossView.z;
    }
    ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("ChronoFocus");
}

// ゲームを終える (勝ちなら最高記録を保存する)
void ChronoSystem::Finish(Player& p,bool win){
    if(finished_)return;
    // 勝ち負けの曲を流し、プレイヤーの状態を止める
    Music::Play(win ? Music::Victory : Music::Defeat);
    finished_=true;won_=win;finishAge_=0;p.target=entt::null;p.preview=entt::null;
    p.action=Action::Free;p.velocity={};p.flow.remaining=0;p.flow.scale=1;p.aimScale=1;p.worldScale=1;p.aiming=false;
    p.hitStop=0;p.damageAge=1;
    // 通常のプレイで勝ったときだけ記録を更新して保存する
    if(!win||diagnostic_||arena_||creature_)return;
    newTime_=bestTime_==0||p.stats.seconds<bestTime_;newChain_=p.flow.maxCombo>bestChain_;
    if(newTime_)bestTime_=p.stats.seconds;if(newChain_)bestChain_=p.flow.maxCombo;
    try{auto path=RecordPath();if(!path.empty()){
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);file<<nlohmann::json{{"time",bestTime_},{"chain",bestChain_}}.dump(2);
    }}catch(...){OutputDebugStringA("Chrono: record could not be saved.\n");}
}

// 毎フレームの更新: 終了後は結果画面の操作、インクのモードなら UpdateInk、旧モードなら各更新
void ChronoSystem::Update(entt::registry& r,GameContext& ctx){
    // 初期化前・プレイヤーがいないときは何もしない
    if(!initialized_||!r.valid(player_))return;
    // スライムの生活モードで確認用のやり直しの指示があれば初期化する
    if(slimePlayerDemo_ && ctx.input && ctx.input->Trigger(DIK_F5)){Reset(r);return;}
    if(slimeLife_){const auto* control=r.try_get<ControlFrame>(player_);if(control&&control->restart){Reset(r);return;}}
    if(finished_){
        auto& p=r.get<Player>(player_);
        // ボスを倒した後は、結果の後ろで散らばった残骸の周りをカメラが回り続ける
        if(inkMode_&&bossDefeat_)UpdateBossDefeat(r,p,ctx);
        else{
            if(inkMode_){
                auto& ink=r.get<InkPlayer>(player_);
                ink.monoAge=-1;ink.phase=SlimePhase::Roaming;ink.charge=ink.spent=0;
                InkCamera(r,p,ctx);
            }
            Presentation(r,p,ctx);
        }
        finishAge_+=ctx.dt;Engine::WindowDX::SetCursorVisible(true);
        // 結果画面: R / Enter でやり直し、TAB でタイトルへ
        if(finishAge_>0.2f&&ctx.input){
            UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
            if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
            if(UI::Pressed(DIK_R)||UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_X)||ui.Click(UI::Retry)){
                if(inkMode_||arena_||creature_){Reset(r);return;}
                Engine::SceneParameters params;params.stagePath=Data::MainStagePath();
                Engine::SceneManager::GetInstance()->RequestChange("Game",params);
            }
            if(UI::Pressed(DIK_TAB)||UI::PadPressed(XINPUT_GAMEPAD_Y)||ui.Click(UI::Select))Engine::SceneManager::GetInstance()->RequestChange("Title");
        }
        return;
    }
    // インクのモードの更新
    if(inkMode_){UpdateInk(r,r.get<Player>(player_),ctx);return;}
    const auto* control=r.try_get<ControlFrame>(player_);
    // 旧モード: F3 で生き物、F2 でアリーナに切り替える
    bool creatureKey=control?control->creature:Down(VK_F3);
    if(creatureKey&&!prevCreature_){snakeOnly_=false;creature_=!creature_;arena_=false;prevCreature_=true;Reset(r);return;}
    prevCreature_=creatureKey;
    bool arenaKey=control?control->arena:Down(VK_F2);
    if(arenaKey&&!prevArena_){snakeOnly_=false;creature_=false;arena_=!arena_;prevArena_=true;Reset(r);return;}
    prevArena_=arenaKey;
    // 旧モード: F6 で画面の揺れの強さを切り替え、プレイヤー・ワールド・演出を更新する
    auto& p=r.get<Player>(player_);
    diagnostic_=r.all_of<ControlFrame>(player_);
    bool shakeKey=!control&&Down(VK_F6);if(shakeKey&&!prevShake_)shakeSetting_=(shakeSetting_+1)%3;prevShake_=shakeKey;
    ctx.camera->SetShakeStrength(shakeSetting_*.5f);
    UpdatePlayer(r,p,ctx);if(!finished_&&p.hitStop<=0)UpdateWorld(r,p,ctx);
    Presentation(r,p,ctx);
    // 敵の時間の速さを共有の戦闘の流れへ渡す
    if(ctx.combatFlow)ctx.combatFlow->SetChronoEnemyScale(p.hitStop>0?0:p.worldScale);
}

// 3D の描画 (インクのモードなら DrawInk)
void ChronoSystem::Draw(entt::registry& r,GameContext& ctx){
    if(!initialized_||!ctx.isPlaying||!r.valid(player_)||!ctx.renderer)return;
    if(inkMode_){DrawInk(r,ctx);return;}
    // 球を描く関数
    auto sphere=[&](V at,float size,Engine::Vector4 color){
        Engine::Transform transform;transform.translate=EV(at);transform.scale={size,size,size};
        ctx.renderer->DrawMesh(sphere_,white_,transform,color,"Default",0.3f,false);
    };
    // 腕はプレイヤーの SPH の体積の一部 (GameScene::SetGPUFluidTether)
    // 火花と水滴を球で描く
    for(const auto& s:sparks_){float a=std::clamp(s.age/0.35f,0.0f,1.0f);
        V at=Lerp(s.from,s.to,a*a);sphere(at,0.2f*(1-a)+0.08f,{0.2f,1.4f,1.6f,1});}
    for(const auto& d:droplets_)sphere(d.position,(d.dust?.8f:.18f)*(1-d.age/.45f)+.035f,d.dust?Engine::Vector4{.65f,.48f,.26f,1}:Engine::Vector4{.18f,1.1f,.6f,1});
    // 生き物のボス: 衝撃の輪と、攻撃の予告の線 (叩きつけは円、突進は矢印、翼は箱、薙ぎ払いは矩形)
    if(creature_&&r.valid(boss_)){
        const auto& c=r.get<CreatureBoss>(boss_);
        DrawBossImpact(ctx,c);
        if((c.stage==CreatureStage::Snake||c.stage==CreatureStage::Opening)&&(c.sweepPhase==1||c.sweepPhase==2)){
            auto color=c.sweepPhase==1?Engine::Vector4{1,.65f,.05f,1}:Engine::Vector4{2,.1f,.02f,1};
            V origin=c.sweepCenter+V{0,.12f,0};
            if(c.attack==CreatureAttack::Slam){
                for(int i=0;i<48;++i){float a=i*6.283185f/48,aa=(i+1)*6.283185f/48;
                    ctx.renderer->DrawLine3D(EV(origin+V{std::cos(a)*8,0,std::sin(a)*8}),EV(origin+V{std::cos(aa)*8,0,std::sin(aa)*8}),color);}
                for(float x:{-8.f,8.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,0}),EV(origin+V{x,32,0}),color);
            }else if(c.attack==CreatureAttack::Charge){
                for(float x:{-4.f,4.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,-20}),EV(origin+V{x,0,26}),color);
                ctx.renderer->DrawLine3D(EV(origin+V{0,0,20}),EV(origin+V{0,0,-16}),color);
                for(float x:{-3.f,3.f})ctx.renderer->DrawLine3D(EV(origin+V{0,0,-16}),EV(origin+V{x,0,-11}),color);
            }else if(c.attack==CreatureAttack::Wing){
                for(float y:{-6.f,6.f})for(float z:{-7.f,7.f})ctx.renderer->DrawLine3D(EV(origin+V{-16,y,z}),EV(origin+V{18,y,z}),color);
                for(float x:{-16.f,18.f})for(float z:{-7.f,7.f})ctx.renderer->DrawLine3D(EV(origin+V{x,-6,z}),EV(origin+V{x,6,z}),color);
            }else{
            for(float z:{-5.f,5.f})ctx.renderer->DrawLine3D(EV(origin+V{-15,0,z}),EV(origin+V{15,0,z}),color);
            for(float x:{-15.f,15.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,-5}),EV(origin+V{x,0,5}),color);
            float edge=c.sweepPhase==1?-12.f:-12.f+24.f*AttackEase(c.sweepTime/ActiveTime(c.attack));
            ctx.renderer->DrawLine3D(EV(origin+V{edge,0,-5}),EV(origin+V{edge,0,5}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{-10,0,0}),EV(origin+V{10,0,0}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{10,0,0}),EV(origin+V{7,0,-2}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{10,0,0}),EV(origin+V{7,0,2}),color);
            }
        }
    }
    // アリーナ: 突進の道か薙ぎ払いの扇を描く
    if(arena_&&r.valid(boss_)){
        const auto& b=r.get<Boss>(boss_);V c=Center(r,boss_)+V{0,-4.9f,0};
        if(b.phase==1||b.phase==2){
            Engine::Vector4 color=b.phase==1?Engine::Vector4{1,.65f,.08f,1}:Engine::Vector4{1,.1f,.1f,1};
            V side{b.attackDirection.z,0,-b.attackDirection.x};
            if(b.wave%2==0){for(float sign:{-1.f,1.f})ctx.renderer->DrawLine3D(EV(c+side*(sign*4.4f)),
                EV(c+side*(sign*4.4f)+b.attackDirection*19),color);
                ctx.renderer->DrawLine3D(EV(c+b.attackDirection*19-side*4.4f),EV(c+b.attackDirection*19+side*4.4f),color);
            }else for(int i=0;i<32;++i){float a=-1.47f+i*2.94f/32,aa=-1.47f+(i+1)*2.94f/32;
                ctx.renderer->DrawLine3D(EV(c+(b.attackDirection*std::cos(a)+side*std::sin(a))*7),
                    EV(c+(b.attackDirection*std::cos(aa)+side*std::sin(aa))*7),color);}
        }
    }
    // 飛んでいる羽の軌跡の線
    for(auto e:r.view<Feather,Target,TransformComponent>()){
        const auto& feather=r.get<Feather>(e);if(r.get<Target>(e).active||feather.age<feather.delay)continue;
        V at=Read(r.get<TransformComponent>(e).translate);
        ctx.renderer->DrawLine3D(EV(at),EV(at-Unit(feather.destination-feather.start)*5.f),{2,.2f,.02f,1});
    }
    // 弾はシアンと赤のシルエットのまま。選んだ対象だけに
    // HUD の枠を付け、戦場に重なる道の線は描かない
    // 弱点が出ているときは、核の周りに輪を描く
    if(r.valid(weakpoint_)&&r.get<Target>(weakpoint_).active){
        V c=Center(r,weakpoint_);float radius=2.5f+.2f*std::sin(r.get<Player>(player_).stats.seconds*8);
        for(int i=0;i<32;++i){float a=i*6.283185f/32,b=(i+1)*6.283185f/32;
            ctx.renderer->DrawLine3D(EV(c+V{std::cos(a)*radius,std::sin(a)*radius,0}),
                EV(c+V{std::cos(b)*radius,std::sin(b)*radius,0}),{1,.75f,.15f,1});}
    }
}

// UI の描画 (インクのモードなら DrawInkUI)
void ChronoSystem::DrawUI(entt::registry& r,GameContext& ctx){
    if(!initialized_||!r.valid(player_)||!ctx.renderer)return;
    if(inkMode_){DrawInkUI(r,ctx);return;}
    // 画面の大きさに合わせた倍率と、矩形と文字を描く関数
    const auto& p=r.get<Player>(player_);auto* render=ctx.renderer;
    float w=ctx.viewportSize.x>0?ctx.viewportSize.x:1280,h=ctx.viewportSize.y>0?ctx.viewportSize.y:720;
    float s=std::min(w/1280,h/720);
    auto rect=[&](float x,float y,float width,float height,Engine::Vector4 color){
        if(height>=40 && width<1280){ UI::Canvas ui(render,w,h);ui.Panel({x,y,width,height},UI::Ink);return; }
        Engine::Renderer::SpriteDesc d;d.x=x*s;d.y=y*s;d.w=width*s;d.h=height*s;d.color=color;d.layer=0;render->DrawSprite(white_,d);};
    auto text=[&](const std::string& label,float x,float y,float scale,Engine::Vector4 color=Engine::Vector4{0.9f,0.98f,1,1}){
        render->DrawString(label,x*s,y*s,scale*s,color,UI::Font);};
    const Engine::Vector4 bg=UI::Ink,cyan=UI::Lime,gold=UI::Gold;
    // 終了後の結果画面 (ランク・時間・チェイン・反撃・統計・最高記録・ボタン)
    if(finished_){
        rect(0,0,1280,720,{0.02f,0.03f,0.06f,0.86f});rect(250,82,780,530,bg);
        text(won_?"CHRONO CHAIN  /  CLEAR":"REFORM AND TRY AGAIN",290,106,0.65f);
        text(won_?p.stats.Rank(p.flow.maxCombo):"RETRY",840,160,1.7f,gold);
        text("CLEAR TIME     "+TimeText(p.stats.seconds)+(newTime_?"  NEW BEST":""),290,194,0.43f);
        text("MAX CHAIN      "+std::to_string(p.flow.maxCombo)+(newChain_?"  NEW BEST":""),290,239,0.43f);
        text("COUNTERS       "+std::to_string(p.stats.counters),290,284,0.43f);
        text("AIR KILLS  "+std::to_string(p.stats.maxAirKills)+"    ORBS BROKEN  "+std::to_string(p.stats.projectiles),290,347,0.32f);
        text("MAX MASS  "+std::to_string(static_cast<int>(p.stats.maxMass))+"    HITS  "+std::to_string(p.stats.hitsTaken)+"    COLLAPSES  "+std::to_string(p.stats.collapses),290,382,0.32f);
        text("MANUAL  "+std::to_string(p.stats.manual)+"    AUTO  "+std::to_string(p.stats.automatic)+"    SLOW  "+TimeText(p.flow.slowSeconds),290,417,0.32f);
        text("BEST  "+TimeText(bestTime_)+"    CHAIN  "+std::to_string(bestChain_),290,455,0.31f,cyan);
        rect(290,520,320,62,{0.07f,0.35f,0.48f,1});rect(650,520,320,62,{0.14f,0.17f,0.26f,1});
        UI::Canvas ui(render,w,h);ui.Button(UI::Retry,"RETRY",true);ui.Button(UI::Select,"STAGE SELECT");
        ui.Prompt("keyboard_r","RETRY","xbox_button_x","RETRY",310,475);ui.Prompt("keyboard_tab","STAGE SELECT","xbox_button_y","STAGE SELECT",710,475);
        return;
    }
    // 下の HUD: 量・負担・狙いのモード・時間の倍率・チェイン
    rect(422,610,436,76,bg);
    text("MASS "+std::to_string(static_cast<int>(p.mass)),438,621,.24f,cyan);
    rect(438,648,188,7,{.1f,.17f,.22f,1});rect(438,648,188*p.mass/200,7,cyan);
    text(p.instability>75?"STRAIN - LAND TO STABILIZE":"STRAIN",651,621,.22f,p.instability>75?gold:cyan);
    rect(651,648,190,7,{.16f,.15f,.13f,1});rect(651,648,190*p.instability/100,7,gold);
    text(p.aiming?"RMB FOCUS / MANUAL":p.automatic?(p.holdCancelled?"RELEASE LMB TO RESUME":"AUTO CHAIN / HOLD LMB"):"MANUAL / HOLD RMB",438,665,.20f);
    if(p.worldScale<.99f)text("TIME "+std::to_string(static_cast<int>(p.worldScale*100))+"%",737,665,.20f,cyan);
    if(p.flow.combo>0){text(std::to_string(p.flow.combo)+" CHAIN",582,542,.42f,gold);
        rect(580,579,125*p.flow.grace/2,3,gold);}
    // ボスの HUD: 指示・羽の壊した数・体力・反撃の数。画面外ならボスの方向を示す
    if(r.valid(boss_)){
        auto& boss=r.get<Boss>(boss_);auto& bt=r.get<Target>(boss_);
        if(boss.engaged){
            float hudX=creature_?-365.f:0.f;
            rect(380+hudX,8,520,55,bg);
            std::string instruction=boss.phase==0?"APPROACH - GET READY":boss.phase==3?(boss.countered?"COUNTER LANDED - REFORM":"WAVE ENDED - REGROUP"):
                boss.waveBreaks<3?"BREAK ORBS  "+std::to_string(std::min(3,boss.waveBreaks))+" / 3":"BOSS EXPOSED - GRAB THE BODY";
            if(arena_)instruction=boss.phase==1?(boss.wave%2==0?"CHARGE INCOMING - DODGE SIDEWAYS":"SWEEP INCOMING - LEAVE THE ARC"):
                boss.phase==2?"ATTACK ACTIVE":boss.phase==3?"OPENING - COUNTER OR LAND":"ARENA - WATCH THE BOSS";
            if(creature_){const auto& c=r.get<CreatureBoss>(boss_);
                instruction=c.stage==CreatureStage::Snake?(c.sweepPhase==1?"SWEEP INCOMING - BACKSTEP OR CHAIN UP":c.sweepPhase==2?"SWEEP ACTIVE - STAY ABOVE THE JAW":c.sweepPhase==3?(c.sweepHit?"RECOVER - NEXT SWEEP SOON":"CORE OPEN - COUNTER NOW"):"SERPENT - WATCH THE HEAD"):
                    c.stage==CreatureStage::Assemble?"MORPHING - REPOSITION / RELAYS CLOSED":
                    c.stage==CreatureStage::Volley?"FEATHER VOLLEY - GET READY":
                    c.stage==CreatureStage::Descend?"REFORMING - LAND TO RECOVER":
                    boss.waveBreaks<3?"CHAIN FEATHERS  "+std::to_string(boss.waveBreaks)+" / 3":"CORE OPEN - CHAIN UP AND STRIKE";
                if(c.sweepPhase==1)instruction=c.attack==CreatureAttack::Sweep?"SWEEP - BACKSTEP OR CHAIN UP":c.attack==CreatureAttack::Charge?"CHARGE - DODGE SIDEWAYS":c.attack==CreatureAttack::Slam?"HEAD SLAM - LEAVE THE COLUMN":"WING SWEEP - SWITCH ROUTE OR DROP";
                if(c.sweepPhase==2)instruction=c.attack==CreatureAttack::Sweep?"SWEEP ACTIVE":c.attack==CreatureAttack::Charge?"CHARGE ACTIVE":c.attack==CreatureAttack::Slam?"SLAM ACTIVE":"WING ACTIVE - DROP OR EVADE";
                if(c.wingCounter)instruction="WING RECOVERY - CORE OPEN";
            }
            text(instruction,408+hudX,15,.27f,gold);
            if(!arena_)for(int i=0;i<3;++i)rect(410.0f+hudX+static_cast<float>(i)*154,36,143,3,boss.waveBreaks>i?cyan:Engine::Vector4{.17f,.23f,.29f,1});
            rect(410+hudX,44,451,4,{0.1f,0.12f,0.2f,1});rect(410+hudX,44,451*std::max(0.0f,bt.hp)/360,4,{0.95f,0.23f,0.45f,1});
            text("COUNTERS "+std::to_string(p.stats.counters)+" / 3",410+hudX,51,.14f);
            V c=Center(r,boss_);if(creature_){auto& creature=r.get<CreatureBoss>(boss_);if(creature.sweepPhase==1||creature.sweepPhase==2)c=creature.attention;}using namespace DirectX;
            XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
            if(clip.w<=0||std::abs(clip.x)>clip.w*.85f||std::abs(clip.y)>clip.w*.75f){
                V d=c-Read(ctx.camera->Position());float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
                float len=std::max(.01f,std::sqrt(x*x+y*y));x/=len;y/=len;
                float sx=std::clamp(640+x*490,110.f,1090.f),sy=std::clamp(360+y*245,95.f,575.f);
                text(boss.phase==1?"! BOSS ATTACK !":"BOSS",sx-40,sy,.26f,gold);
                for(int i=0;i<7;++i){float q=i-3.f;rect(sx+x*(22-std::abs(q)*3)-y*q*3,sy+y*(22-std::abs(q)*3)+x*q*3,4,4,gold);}
            }
        }else text("CHAIN UP TO THE CITADEL",494,34,0.3f,gold);
    }
    // 操作の案内と各種の表示
    if(p.stats.seconds<12)text("WASD Move  SPACE Jump  SHIFT Dodge  LMB Grab  RMB Aim  MMB Auto",270,698,.20f);
    text("F2 ARENA / F3 BIRD / F4 SNAKE",creature_?980.f:24.f,22,.20f,cyan);
    text("F6 SHAKE "+std::to_string(shakeSetting_*50)+"%",creature_?980.f:24.f,42,.18f,cyan);
    if(creature_)text(p.lastChainHit<.32f?"TAP ON IMPACT - BUILD SPEED":"CYAN: CHAIN   RED: DANGER   GREEN: RELAY",410,568,.20f,cyan);
    if(creature_)text("CHAIN SPEED "+std::to_string(static_cast<int>(p.chainDrive*100))+"%",24,85,.22f,gold);
    text(p.action==Action::Collapsed?"REFORMING - DODGE LOCKED":p.cooldown>0?"DODGE RECHARGING":"SHIFT - CANCEL / DODGE",487,594,.18f,cyan);
    if(p.landingAge<.6f)text("LANDED - STABILIZING",515,495,.25f,cyan);
    if(p.instability>75){float pulse=.5f+.5f*std::sin(p.stats.seconds*10);
        rect(600,460,80,3,{1,.45f,.1f,pulse});}
    if(p.action==Action::Collapsed)text("UNSTABLE!  REFORMING",480,430,0.47f,{1,0.25f,0.25f,1});
    // 照準と、狙っている対象の枠 (負担が大きすぎるなら赤)
    if(p.aiming||p.automatic){
        auto color=r.valid(p.preview)?cyan:Engine::Vector4{0.65f,0.7f,0.75f,1};
        rect(631,359,7,2,color);rect(643,359,7,2,color);rect(639,351,2,7,color);rect(639,363,2,7,color);
        if(r.valid(p.preview)&&r.get<Target>(p.preview).active){
            V c=Center(r,p.preview);float cost=StrainCost(p.mass,Length(c-Read(r.get<TransformComponent>(player_).translate)));
            bool unsafe=p.instability+cost>=100;auto col=unsafe?Engine::Vector4{1,0.25f,0.15f,1}:cyan;
            using namespace DirectX;auto v=XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj());
            XMFLOAT4 clip;XMStoreFloat4(&clip,v);
            if(clip.w>0){float x=(clip.x/clip.w*0.5f+0.5f)*w/s,y=(-clip.y/clip.w*0.5f+0.5f)*h/s;
                rect(x-20,y-20,40,2,col);rect(x-20,y+18,40,2,col);rect(x-20,y-20,2,40,col);rect(x+18,y-20,2,40,col);}
            text(unsafe?"OVERLOAD RISK":p.action==Action::Pulling?"PULLING":r.get<Target>(p.preview).kind==Kind::Anchor?"SAFE RELAY":r.all_of<Feather>(p.preview)?(r.get<Feather>(p.preview).fast?"SHORTCUT - EXPIRES SOON":"STABLE FEATHER"):"GRAB",590,392,0.25f,col);
        }
    }
    // 鎖が失敗した理由を表示する
    if(p.failureTime>0){
        const char* reason="";
        switch(p.failure){
        case ChainFailure::NoTarget:reason="NO TARGET - AIM AT AN ORB";break;
        case ChainFailure::OutOfRange:reason="OUT OF REACH - MOVE CLOSER";break;
        case ChainFailure::Obstructed:reason="BLOCKED - CHANGE ANGLE";break;
        case ChainFailure::Recovering:reason=p.buffer>0?"QUEUED - RELEASE LMB TO CANCEL":"RECOVERING - SHIFT TO DODGE";break;
        case ChainFailure::Unstable:reason="UNSTABLE - WAIT TO REFORM";break;
        case ChainFailure::Missed:reason="MISSED - AIM AT THE TARGET";break;
        case ChainFailure::Fell:reason="FALL - RETURNED TO LAST LANDING";break;
        default:break;
        }
        text(reason,505,419,.22f,gold);
    }
    // 遮られている対象に印を付ける
    if(r.valid(p.blockedPreview)){
        V c=Center(r,p.blockedPreview);using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=(clip.x/clip.w*.5f+.5f)*w/s,y=(-clip.y/clip.w*.5f+.5f)*h/s;
            for(int i=0;i<7;++i){float q=i*5.f-15;rect(x+q,y+q,3,3,gold);rect(x+q,y-q,3,3,gold);}
            text("BLOCKED",x-28,y+23,.18f,gold);}
    }
    // 被弾の方向を赤い矢印で示す
    if(p.damageAge<.6f){
        V d=Unit(p.damageSource-Read(r.get<TransformComponent>(player_).translate));
        float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
        float len=std::sqrt(x*x+y*y);if(len>.01f){x/=len;y/=len;}
        auto red=Engine::Vector4{1,.18f,.12f,1-p.damageAge/.6f};
        for(int i=0;i<7;++i){float q=i-3.f;
            rect(640+x*(79-std::abs(q)*3)-y*q*3,360+y*(79-std::abs(q)*3)+x*q*3,4,4,red);}
        text(p.damageReason,534,469,.25f,red);
    }
}

} // namespace Game
