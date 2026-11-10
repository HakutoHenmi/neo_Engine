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
    // 結果の間に隠した物を戻す
    for(auto e:resultHidden_)if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=true;
    resultHidden_.clear();
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::Reset");
    // デモのシーンか
    slimePlayerDemo_ = scene_->IsSlimePlayerDemo();
    // 吸収モード: 通常のプレイ。従来の進行を確かめる自動テストでは使わない
    devour_=!slimePlayerDemo_;
    if(devour_)LoadDevourStage();
    // スライムの変形のターゲットを消す
    Engine::Renderer::GetInstance()->SetFluidMorphTargets({},0);
    inkSurfaces_.clear();
    // 前回作った「Chrono 」のエンティティを消す
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    // 名前でプレイヤーを探す
    player_=entt::null;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name=="Player"){player_=e;break;}
    // 終了・カメラの状態を初期化する
    initialized_=r.valid(player_);finished_=won_=false;finishAge_=0;
    prevSpace_=Controls::Jump();
    yaw_=0;pitch_=0.12f;fov_=1.0472f;
    cameraReady_=false;postStrength_=0;
    // プレイヤーがいなければここまで
    if(!initialized_)return;
    // ステージの BGM を流す (街は専用の曲)
    Music::Play(devour_&&CurrentDevourStage().map=="ring"?Music::City:Music::Battle);
    // プレイヤーの状態を作り直し、位置・体力・入力を初期化する
    r.emplace_or_replace<Player>(player_);
    auto& pt=r.get<TransformComponent>(player_);pt.translate={0,1.3f,-27};pt.scale={1,1,1};
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(100);hp.SetMaxHp(200);hp.SetDead(false);
    if(auto* box=r.try_get<BoxColliderComponent>(player_))box->enabled=false;
    // 白テクスチャ・球のメッシュ・音を読み込む
    auto* renderer=Engine::Renderer::GetInstance();white_=renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    sphere_=renderer->LoadObjMesh(ball);
    if(hitSound_==0xffffffff)hitSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/Devour/hit.wav");
    if(coreSound_==0xffffffff)coreSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/Devour/confirm.wav");
    // ポストエフェクトを既定に戻してから、ステージを作る
    renderer->SetPostEffect("Default");renderer->SetPostProcessParams({});
    BuildInk(r);CacheSolids(r);
    renderer->SetPostEffect("ChronoFocus");
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
    // 地形の箱の中で一番早く当たるものを選ぶ
    bool found=false;fraction=1;for(const auto& box:solids_){float f;V n;
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


// プレイヤーへのダメージ (完璧な回避の判定を含む)
void ChronoSystem::Damage(entt::registry& r,Player& p,float amount,GameContext& ctx,V source,float perfectWindow){
    // スライムの生活モードの被弾
    DamageSlimeLife(r,p,amount,ctx,source);(void)perfectWindow;
}


// 演出: 時間の遅さや被弾に応じてポスト効果をかける
void ChronoSystem::Presentation(entt::registry& r,Player& p,GameContext& ctx){
    V player=Read(r.get<TransformComponent>(player_).translate);
    // 時間が遅いほど強いポスト効果をかける
    float intensity=finished_?0:std::clamp((1-p.worldScale)/.8f,0.f,1.f);
    postStrength_+=(intensity-postStrength_)*(1-std::exp(-8*ctx.dt));
    Engine::Renderer::PostProcessParams params{};
    params.time=ctx.renderer->GetPostProcessParams().time;params.san=postStrength_;
    // 引き寄せ中は歪みを強め、被弾直後は周辺を暗くする
    params.distortion=p.action==Action::Pulling?1.0f:0.65f;
    params.vignette=p.damageAge<.35f?1-p.damageAge/.35f:0;
    // プレイヤーの深度を被写界深度に渡す
    if(!finished_){
        using namespace DirectX;XMFLOAT3 playerView;
        XMStoreFloat3(&playerView,XMVector3TransformCoord(XMVectorSet(player.x,player.y,player.z,1),ctx.camera->View()));
        params.dofFocus=std::max(8.f,playerView.z);params.dofRange=32;params.dofStrength=0;
    }
    ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("ChronoFocus");
}

// ゲームを終える
void ChronoSystem::Finish(Player& p,bool win){
    if(finished_)return;
    // 勝ち負けの曲を流し、プレイヤーの状態を止める
    Music::Play(win ? Music::Victory : Music::Defeat);
    finished_=true;won_=win;finishAge_=0;resultCameraAge_=0;resultMeasured_=false;resultScreenSet_=false;p.target=entt::null;p.preview=entt::null;
    p.action=Action::Free;p.velocity={};p.flow.remaining=0;p.flow.scale=1;p.aimScale=1;p.worldScale=1;p.aiming=false;
    p.hitStop=0;p.damageAge=1;

}

// 毎フレームの更新: 終了後は結果画面の操作、それ以外はスライムの生活モードの更新
void ChronoSystem::Update(entt::registry& r,GameContext& ctx){
    // 初期化前・プレイヤーがいないときは何もしない
    if(!initialized_||!r.valid(player_))return;
    // スライムの生活モードで確認用のやり直しの指示があれば初期化する
    if(slimePlayerDemo_ && ctx.input && ctx.input->Trigger(DIK_F5)){Reset(r);return;}
    {const auto* control=r.try_get<ControlFrame>(player_);if(control&&control->restart){Reset(r);return;}}
    if(finished_){
        auto& p=r.get<Player>(player_);
        {
            auto& ink=r.get<InkPlayer>(player_);
            ink.monoAge=-1;ink.phase=SlimePhase::Roaming;ink.charge=ink.spent=0;
            InkCamera(r,p,ctx);
            Presentation(r,p,ctx);
            // 吸収モードの結果では本物の体を映し続ける
            if(devour_)SyncSlimeBodies(ctx);
        }
        finishAge_+=ctx.dt;Engine::WindowDX::SetCursorVisible(true);
        // 結果画面: R / Enter でやり直し、TAB でタイトルへ
        if(finishAge_>0.2f&&ctx.input){
            UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
            if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
            // 吸収モードの結果: 演出の途中は操作で最後まで飛ばす。クリアなら N / Enter / A で次のステージへ
            if(devour_){
                using namespace UI::Scale;
                const bool next=won_&&devourStage_+1<int(Chrono::DevourStages().size());
                const bool confirm=UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_A);
                if(finishAge_<2.2f){if(confirm||ctx.input->IsMouseTrigger(0))finishAge_=2.2f;return;}
                if(next&&(UI::Pressed(DIK_N)||UI::Pressed(DIK_RETURN)||ui.Click(ResultNext))){++devourStage_;SaveSlimeLab();Reset(r);return;}
                if(UI::Pressed(DIK_R)||(!next&&UI::Pressed(DIK_RETURN))||UI::PadPressed(XINPUT_GAMEPAD_X)||ui.Click(next?ResultRetry:ResultRetryOnly)){Reset(r);return;}
                if(UI::Pressed(DIK_TAB)||UI::PadPressed(XINPUT_GAMEPAD_Y)||ui.Click(next?ResultTitle:ResultTitleOnly))Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");
                return;
            }
            if(UI::Pressed(DIK_R)||UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_X)||ui.Click(UI::Retry)){Reset(r);return;}
            if(UI::Pressed(DIK_TAB)||UI::PadPressed(XINPUT_GAMEPAD_Y)||ui.Click(UI::Select))Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");
        }
        return;
    }
    // スライムの生活モードの更新
    UpdateSlimeLife(r,r.get<Player>(player_),ctx);
}

// 3D の描画
void ChronoSystem::Draw(entt::registry& r,GameContext& ctx){
    if(!initialized_||!ctx.isPlaying||!r.valid(player_)||!ctx.renderer)return;
    DrawSlimeLife(r,ctx);
}

// UI の描画
void ChronoSystem::DrawUI(entt::registry& r,GameContext& ctx){
    if(!initialized_||!r.valid(player_)||!ctx.renderer)return;
    DrawSlimeLifeUI(r,ctx);
}

} // namespace Game
