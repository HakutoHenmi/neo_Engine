// UIValidationScene.h
// UI と描画の自動テスト用シーンをまとめたファイル (--ui-smoke などのコマンドライン引数で起動する)。
// 影・グラフィック設定・ビームのカメラ・回避・ボス・結果画面・各メニューの表示を確かめ、
// 結果を tests/out/ui-smoke.txt と画像に書き出す。
#pragma once
// テストで表示するシーン
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/Scenes/GameOverScene.h"
// プレイヤー向けの UI (共通部品・グラフィック設定・BGM・クレジット)
#include "../Game/UI/GameUI.h"
#include "../Game/UI/GraphicsUI.h"
#include "../Game/UI/SceneMusic.h"
#include "../Game/UI/CreditsUI.h"
// 画面の保存 (DirectXTex)
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
// ゲームシーンとスライム本編の定義
#include "../Game/Scenes/GameScene.h"
#include "../Game/Chrono/ChronoComponents.h"
#include "../Game/Chrono/InkRules.h"

// ゲームを止め、同じカメラからそれぞれの表示を比べる
class ShadowValidationScene final : public Game::GameScene {
    // ウィンドウ・経過フレーム・成功したか
    Engine::WindowDX* window_=nullptr;int frame_=0;bool passed_=true;
    // 元のグラフィック設定 (最後に戻す)
    Engine::Renderer::GraphicsSettings original_{};
    // 今の画面を PNG に保存する
    void Capture(const wchar_t* path){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);
        passed_&=SUCCEEDED(hr);
    }
public:
    // 本編のシーンを読み込み、AO・RT の影・被写界深度・モーションブラーを切る
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();Game::GameScene::Initialize(dx,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
        auto* r=Engine::Renderer::GetInstance();original_=r->GetGraphicsSettings();
        auto s=r->GetGraphicsSettings();s.ambientOcclusion=0;s.rtShadows=false;s.dof=s.motionBlur=0;r->ApplyGraphicsSettings(s);
    }
    void Update()override{
        // 34 フレームまではゲームを進め、その後は止めて比べる
        if(frame_<34)Game::GameScene::Update();++frame_;
        auto* r=Engine::Renderer::GetInstance();auto s=r->GetGraphicsSettings();
        // 35: 基準 → 65: AO あり → 95: RT の影あり → 125: 両方あり の順に保存する
        if(frame_==35){Capture(L"tests/out/shadow-baseline.png");s.ambientOcclusion=1;r->ApplyGraphicsSettings(s);}
        if(frame_==65){Capture(L"tests/out/shadow-gtao.png");s.ambientOcclusion=0;s.rtShadows=true;r->ApplyGraphicsSettings(s);}
        if(frame_==95){Capture(L"tests/out/shadow-rt.png");s.ambientOcclusion=1;r->ApplyGraphicsSettings(s);}
        if(frame_==125)Capture(L"tests/out/shadow-combined.png");
        // DLSS と RT の影が動いていたかを確かめ、設定を戻して結果を書く
        if(frame_==126){
            passed_&=r->DlssActive()&&r->DlssEvaluatedFrames()>100&&r->RtShadowFrames()>50&&r->RtShadowPixels()>0&&r->RtLitPixels()>0;
            s=original_;std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" shadow comparison: DLSS frames="<<r->DlssEvaluatedFrames()<<" RT frames="<<r->RtShadowFrames()<<" shadow pixels="<<r->RtShadowPixels()<<" lit pixels="<<r->RtLitPixels()<<" four fixed-camera captures";
            PostQuitMessage(passed_?0:2);
        }
    }
    void Draw()override{
        // 揺れを止めて決まった位置からステージを見る (--shadow-oblique なら斜めから)
        auto& camera=GetCamera();camera.StopShake();camera.SetHandheld(0);camera.SetPosition(0,6,-16);camera.LookAt(0,1,20,0,1,0);
        if(wcsstr(GetCommandLineW(),L"--shadow-oblique")){camera.SetPosition(14,3,-14);camera.LookAt(0,1,0,0,1,0);}
        Game::GameScene::Draw();
    }
    void DrawEditor()override{}
};

// グラフィック設定画面のテスト: テクスチャのミップマップと、ボタンの判定が画面の大きさに合うか
class GraphicsValidationScene final : public Game::GameScene {
    Engine::WindowDX* window_=nullptr;int frame_=0;bool passed_=true;
    Engine::Renderer::GraphicsSettings original_{};
    // 今の画面を PNG に保存する
    void Capture(const wchar_t* path){DirectX::ScratchImage image;auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);passed_&=SUCCEEDED(hr);}
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        // 本編のシーンを読み込む
        window_=dx;Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();Game::GameScene::Initialize(dx,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));auto* r=Engine::Renderer::GetInstance();original_=r->GetGraphicsSettings();
        // 地面のテクスチャにミップマップが作られているか
        for(auto path:{"Resources/Textures/PBR/Grass004_NormalDX.jpg","Resources/Textures/PBR/Grass004_Roughness.jpg"})passed_&=r->TextureMipLevels(r->LoadTexture2D(path,false))==11;
        passed_&=r->TextureMipLevels(r->LoadTexture2D("Resources/Textures/AmbientCG/grass004_color_1k.jpg"))==11;
    }
    void Update()override{
        Game::GameScene::Update();++frame_;auto* renderer=Engine::Renderer::GetInstance();using namespace Game::UI;
        // 35 フレーム目: 地面を保存してポーズする
        if(frame_==35){Capture(L"tests/out/graphics-ground.png");SetPaused(true);}
        // 37 フレーム目: いろいろな大きさの画面で、DLSS のボタンを押せるか
        if(frame_==37){
            // Canvas に渡る前に、EditorUI がパネルの座標を出力画像の座標に換算している
            for(auto size:{Engine::Vector2{640,360},Engine::Vector2{1050,590},Engine::Vector2{1600,900}}){Canvas ui(renderer,size.x,size.y);auto b=DlssQualityButton;ui.SetPointer((b.x+b.w*.5f)*1.5f,(b.y+b.h*.5f)*1.5f);passed_&=ui.Hover(b)&&!ui.Hover(DlssOffButton);}
            Canvas ui(renderer,1050,590);auto& io=ImGui::GetIO();bool down=io.MouseDown[0];float duration=io.MouseDownDuration[0];io.MouseDown[0]=true;io.MouseDownDuration[0]=0;
            for(auto b:{DlssQualityButton,DlssOffButton}){ui.SetPointer((b.x+b.w*.5f)*1.5f,(b.y+b.h*.5f)*1.5f);UpdateGraphics(ui,*renderer);passed_&=renderer->GetGraphicsSettings().dlssQuality==(b.x==DlssQualityButton.x&&renderer->SupportsDlss());}
            io.MouseDown[0]=down;io.MouseDownDuration[0]=duration;
        }
        // 65 フレーム目: 設定画面を保存し、66 フレーム目に設定を戻して結果を書く
        if(frame_==65)Capture(L"tests/out/graphics-settings.png");
        if(frame_==66){renderer->ApplyGraphicsSettings(original_);std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" graphics: full editor canvas, DLSS ON/OFF hit targets, color/data mip chains, GPU captures";PostQuitMessage(passed_?0:2);}
    }
    // 35 フレーム目以降はグラフィック設定画面を描く
    void Draw()override{if(frame_<35){Game::GameScene::Draw();return;}Game::UI::Canvas ui(Engine::Renderer::GetInstance(),1050,590);Game::UI::DrawGraphics(ui,*Engine::Renderer::GetInstance());}
    void DrawEditor()override{}
};

// ビームのカメラのテスト: 溜めの 3 段階それぞれで、カメラの演出と戻りを確かめる
class BeamCameraValidationScene final : public Game::GameScene {
    // ウィンドウ・溜めの段階・開始時刻と、各確認の状態
    Engine::WindowDX* window_=nullptr;int tier_=1;float start_=0;bool launched_=false,captured_=false,passed_=true,chargeChecked_=false;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();
        Game::GameScene::Initialize(dx,params);GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override{
        // プレイヤーと入力
        using namespace Game;using namespace Game::Chrono;auto& r=GetComponents();auto player=FindObjectByName("Player");
        auto& ink=r.get<InkPlayer>(player);auto& p=r.get<Player>(player);auto& input=r.get<ControlFrame>(player);input={};input.pitch=-.15f;p.invincible=1;
        // ボスの攻撃を止める
        r.get<CreatureBoss>(FindObjectByName("Boss")).sweepTime=-100;
        // まだ撃っていなければ、段階に合わせた溜めで撃ち始める
        if(!launched_){p.mass=800;ink.capacity=760;ink.reserve=40;ink.phase=SlimePhase::Charging;ink.charge=tier_==1?150.f:tier_==2?400.f:700.f;ink.previousAttack=true;launched_=true;start_=p.stats.seconds;}
        // 0.65 秒だけ攻撃を押し続ける
        input.attack=p.stats.seconds-start_<.65f;
        Game::GameScene::Update();
        // 溜め中のカメラの演出を確かめる
        if(!chargeChecked_&&ink.phase==SlimePhase::Charging&&p.stats.seconds-start_>.5f){
            passed_&=ink.beamView>.7f&&(tier_==1?ink.heavyView==0:tier_==2?(ink.heavyView>.3f&&ink.heavyView<.7f):ink.heavyView>.99f);
            chargeChecked_=true;
        }
        // 発射中のカメラの演出を確かめ、画面を保存する
        if(!captured_&&ink.phase==SlimePhase::Firing&&ink.phaseAge>.3f){
            passed_&=ink.beamView>.99f&&(tier_!=3||ink.heavyView>.99f);
            DirectX::ScratchImage image;auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(L"tests/out/beam-camera-"+std::to_wstring(tier_)+L".png").c_str());
            passed_&=SUCCEEDED(hr);captured_=true;
        }
        // 2.8 秒経ったら元に戻っているか確かめ、次の段階へ (3 段階で終わる)
        if(p.stats.seconds-start_>2.8f){passed_&=chargeChecked_&&captured_&&ink.beamView==0&&ink.heavyView==0;
            if(++tier_>3){std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" three beam cameras, GPU captures, eased return";PostQuitMessage(passed_?0:2);}
            launched_=false;captured_=false;chargeChecked_=false;
        }
    }
};
// 回避のテスト: ジャスト回避・通常の回避・遅い回避・ボスの攻撃に対する回避を確かめる
class DodgeValidationScene final : public Game::GameScene {
    bool perfectFollowup_=false,normalFollowup_=false,normalProtected_=false,blur_=false;
    bool grazeShot_=false,bonusHpSafe_=false;
    // 段階・開始時刻と、各確認の結果
    int phase_=0;float start_=0;bool success_=false,paint_=false,slow_=false,gray_=false,late_=false,liquid_=false,holdEnds_=false,autoSlide_=false;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        // 本編のシーンを読み込む
        Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();
        Game::GameScene::Initialize(dx,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        // プレイヤー・ボス・入力
        auto& r=GetComponents();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& input=r.get<ControlFrame>(player);input={};
        // 段階 0: 右へ動く
        if(phase_==0)input.move={1,0,0};
        if(phase_==1)input.dodge=true; // 押し続けても液状化が伸びたり、もう一度始まったりしないこと
        // 最後の段階以外はボスの攻撃を止める
        if(phase_!=5)r.get<CreatureBoss>(boss).sweepTime=-100;
        // プレイヤーの正面から速い羽を撃つ
        auto shoot=[&](){auto e=CreateEmptyEntity();auto pos=r.get<TransformComponent>(player).translate;pos.z+=3;
            r.emplace<TransformComponent>(e).translate=pos;r.emplace<SlimeFeather>(e).velocity={0,0,-300};};
        // 段階 0 → 1: 回避する
        if(phase_==0&&p.stats.seconds>.2f){input.dodge=true;phase_=1;start_=p.stats.seconds;}
        // 回避中にかすめる羽を撃つ
        if(phase_==1&&p.stats.seconds-start_>.16f&&!grazeShot_){
            auto e=CreateEmptyEntity();auto pos=r.get<TransformComponent>(player).translate;pos.x+=3;pos.z+=3;
            r.emplace<TransformComponent>(e).translate=pos;r.emplace<SlimeFeather>(e).velocity={0,0,-300};grazeShot_=true;}
        // 回避の後の羽
        if(phase_==1&&p.stats.seconds-start_>.62f&&!perfectFollowup_){shoot();perfectFollowup_=true;}
        // 結果を確かめて段階 2 へ
        if(phase_==1&&p.stats.seconds-start_>.7f){
            holdEnds_=!ink.dodgeLiquid&&p.action!=Action::Dodge;
            success_=ink.perfectDodges==1&&p.stats.hitsTaken==0;
            paint_=ink.deployed>500&&p.mass+ink.deployed+ink.spent>1300;
            phase_=2;
        }
        // 段階 2 → 3: 通常の回避と、その後の羽
        if(phase_==2&&p.stats.seconds>1.2f){input.dodge=true;phase_=3;start_=p.stats.seconds;}
        if(phase_==3&&p.stats.seconds-start_>.28f&&!normalFollowup_){shoot();normalFollowup_=true;}
        if(phase_==3&&p.stats.seconds-start_>.55f){normalProtected_=p.stats.hitsTaken==0;shoot();phase_=4;start_=p.stats.seconds;}
        // 段階 4: 遅い回避の結果を確かめ、ボスの叩きつけを当てて回避させる
        if(phase_==4&&p.stats.seconds-start_>.8f){
            late_=p.stats.hitsTaken==1&&ink.perfectDodges==1;
            auto& c=r.get<CreatureBoss>(boss);auto pos=r.get<TransformComponent>(player).translate;
            c.stage=CreatureStage::Snake;c.attack=CreatureAttack::Slam;c.sweepPhase=2;c.sweepTime=.24f;c.sweepHit=false;c.sweepCenter={pos.x,pos.y,pos.z};
            p.cooldown=0;p.invincible=0;input.dodge=true;phase_=5;start_=p.stats.seconds;
        }
        // 更新の前の体力と、ボーナスの状態
        float hpBefore=p.mass;bool burstBefore=ink.perfectBurst;
        Game::GameScene::Update();
        // 更新の後の状態を確かめる
        if(!burstBefore&&ink.perfectBurst)bonusHpSafe_=p.mass>=hpBefore-.001f;
        if(phase_==0)autoSlide_|=ink.swimming&&Length(p.velocity)>15;
        liquid_|=ink.dodgeLiquid;
        blur_|=Engine::Renderer::GetInstance()->GetPostProcessParams().chromaShift>.5f;
        if(ink.perfectAge>=0&&ink.perfectAge<.15f){slow_|=p.worldScale<.2f;gray_|=Engine::Renderer::GetInstance()->GetPostProcessParams().san>.9f;}
        // すべて確かめたら結果を書いて終了する
        if((phase_==5&&p.stats.seconds-start_>.25f)||p.stats.seconds>4){
            bool snake=p.stats.hitsTaken==1&&ink.perfectDodges==2;
            bool pass=success_&&paint_&&slow_&&gray_&&late_&&snake&&liquid_&&holdEnds_&&autoSlide_&&normalProtected_&&perfectFollowup_&&blur_&&bonusHpSafe_;
            std::ofstream("tests/out/ui-smoke.txt")<<(pass?"PASS":"FAIL")<<" perfect="<<success_<<" recoverable paint / conserved mass="<<paint_<<" slow="<<slow_<<" grayscale="<<gray_<<" early dodge takes hit="<<late_<<" snake perfect="<<snake<<" liquid="<<liquid_<<" held button ends="<<holdEnds_<<" auto slide="<<autoSlide_;
            std::ofstream("tests/out/ui-smoke.txt",std::ios::app)<<" normal followup protected="<<normalProtected_<<" perfect followup tested="<<perfectFollowup_<<" blur pulse="<<blur_<<" bonus HP safe="<<bonusHpSafe_<<" delayed feather graze="<<grazeShot_;
            PostQuitMessage(pass?0:2);
        }
    }
};
// ボスのテスト: 追跡・羽の連射・予告の停止・ロックオン・変形・尾・急降下などを確かめる
class BossValidationScene final : public Game::GameScene {
    // ウィンドウ・経過フレーム
    Engine::WindowDX* window_=nullptr;
    unsigned frame_=0;
    // 各確認の結果
    bool moved_=false,volley_=false,held_=true,outside_=true,lock_=true,captured_=false;
    bool morphHeld_=true,morphCaptured_=false,returned_=false,tail_=false,dive_=false,radial_=false,diveCaptured_=false,snakeCaptured_=false,tailCaptured_=false;
    bool headImpact_=false,diveImpact_=false,headImpactCaptured_=false,diveImpactCaptured_=false;size_t peakFeathers_=0;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        // 本編のシーンを読み込む
        window_=dx;Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();
        Game::GameScene::Initialize(dx,params);
        GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        // プレイヤー・ボス・入力
        auto& r=GetComponents();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& input=r.get<ControlFrame>(player);input={};
        // 決まったフレームでロックオンを切り替える
        input.lockOn=frame_==5||frame_==6||frame_==30||frame_==60;
        // プレイヤーは無敵
        auto& p=r.get<Player>(player);p.invincible=1;
        // 更新の前のボスの状態
        auto& c=r.get<CreatureBoss>(boss);auto before=c.offset;
        bool waiting=c.stage==CreatureStage::Opening&&c.sweepPhase==1;
        bool morphing=c.stage==CreatureStage::Assemble||c.stage==CreatureStage::Descend;
        auto previousStage=c.stage;
        Game::GameScene::Update();
        // 更新の後の状態を確かめる
        auto& ink=r.get<InkPlayer>(player);
        // ロックオンの切り替えが効いているか
        if(frame_==6)lock_&=ink.lockedOn;
        if(frame_==31)lock_&=!ink.lockedOn;
        if(frame_==61)lock_&=ink.lockedOn;
        // 蛇の形で動いたか・予告中は止まっているか・変形中は止まっているか・蛇に戻ったか
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==0&&Length(c.offset-before)>.01f)moved_=true;
        if(waiting)held_&=Length(c.offset-before)<.01f;
        if(morphing&&c.stage==previousStage)morphHeld_&=Length(c.offset-before)<.01f;
        if(previousStage==CreatureStage::Descend&&c.stage==CreatureStage::Snake)returned_=true;
        // 尾の薙ぎ払い・急降下・全方位の羽
        tail_|=c.stage==CreatureStage::Snake&&c.attack==CreatureAttack::HeadTail&&c.sweepPhase==6;
        dive_|=c.stage==CreatureStage::Dive;
        radial_|=c.stage==CreatureStage::Dive&&c.sweepPhase==3&&r.view<SlimeFeather>().size()>=18;
        // 着地の瞬間にカメラが揺れたか
        if(c.impactAge<.1f){
            if(c.stage==CreatureStage::Snake)headImpact_|=GetCamera().IsShaking();
            if(c.stage==CreatureStage::Dive)diveImpact_|=GetCamera().IsShaking();
        }
        // 羽の最大数
        peakFeathers_=std::max(peakFeathers_,r.view<SlimeFeather>().size());
        // 各場面の画面を保存する
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==0&&c.timer>1.f&&!snakeCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-snake-idle.png");
            snakeCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==6&&c.sweepTime>.18f&&!tailCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-tail-sweep.png");
            tailCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Dive&&c.sweepPhase==2&&c.sweepTime>.25f&&!diveCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-dive.png");
            diveCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Snake&&c.impactAge>.08f&&c.impactAge<.25f&&!headImpactCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-head-impact.png");
            headImpactCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Dive&&c.impactAge>.08f&&c.impactAge<.25f&&!diveImpactCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-dive-impact.png");
            diveImpactCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Assemble&&c.timer>3.5f&&!morphCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-reform.png");
            morphCaptured_=SUCCEEDED(hr);
        }
        // 扇状の羽: 外周にいるか・羽を撃ったか・画面を保存する
        if(c.stage==CreatureStage::Opening){
            auto pos=r.get<TransformComponent>(boss).translate;
            outside_&=(std::abs(pos.x)>220||pos.z< -220||pos.z>260)&&std::abs(pos.x)<290&&pos.z> -280&&pos.z<320;
            volley_|=r.get<Boss>(boss).emitted>=180;
            if(!captured_&&r.get<Boss>(boss).emitted>=108){
                DirectX::ScratchImage image;
                auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
                if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-volley.png");
                captured_=SUCCEEDED(hr);
            }
        }
        ++frame_;
        // 羽を撃って蛇に戻ったか、65 秒経ったら結果を書いて終了する
        if((volley_&&returned_)||p.stats.seconds>65){
            bool passed=moved_&&volley_&&held_&&outside_&&lock_&&captured_&&morphHeld_&&morphCaptured_&&returned_&&tail_&&dive_&&radial_&&diveCaptured_&&snakeCaptured_&&tailCaptured_&&headImpact_&&diveImpact_&&headImpactCaptured_&&diveImpactCaptured_&&peakFeathers_>=80;
            std::ofstream("tests/out/ui-smoke.txt")<<(passed?"PASS":"FAIL")<<" boss pursuit="<<moved_<<" volley="<<volley_<<" warning hold="<<held_<<" outer flight="<<outside_<<" lock toggle="<<lock_<<" capture="<<captured_<<" morph root fixed="<<morphHeld_<<" reform capture="<<morphCaptured_<<" returned="<<returned_<<" tail="<<tail_<<" dive="<<dive_<<" radial="<<radial_<<" dive capture="<<diveCaptured_<<" snake capture="<<snakeCaptured_<<" tail capture="<<tailCaptured_<<" head shake="<<headImpact_<<" dive shake="<<diveImpact_<<" impact captures="<<(headImpactCaptured_&&diveImpactCaptured_)<<" peak feathers="<<peakFeathers_;
            PostQuitMessage(passed?0:2);
        }
    }
};

// 負けた状態から始め、実際のマウスとキーボードで結果画面を操作するためのシーン。
// その後の入力・カーソル・シーンの切り替えは、すべて本番のコードで動く
class UIResultValidationScene final : public Game::GameScene {
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        // 本編のシーンを読み込み、プレイヤーの質量を 0 にして負けさせる
        Engine::SceneParameters params;params.stagePath=Game::Data::MainStagePath();
        Game::GameScene::Initialize(dx,params);
        auto player=FindObjectByName("Player");
        GetComponents().emplace<Game::Chrono::ControlFrame>(player);
        GetComponents().get<Game::Chrono::Player>(player).mass=0;
    }
};

// 本番のメニューを GPU で画像に保存する (OS への入力は送らない)
class UIValidationScene final : public Engine::IScene {
    // ウィンドウ・表示中のシーン・経過フレーム・ページ・成功したか
    Engine::WindowDX* window_=nullptr;
    std::unique_ptr<Engine::IScene> scene_;
    int frame_=0, page_=0;
    bool passed_=true;
    // ページに合わせてシーンを用意し、BGM が合っているかを確かめる
    void Load() {
        // クレジットのページではシーンを使わない
        if(page_>=5){scene_.reset();return;}
        // 0: タイトル / 1: セレクト / 2: ゲームオーバー / それ以外: シーンなし
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else if(page_==2)scene_=std::make_unique<Game::GameOverScene>();
        else scene_.reset();
        // シーンがあれば初期化し、無ければ勝利か戦闘の BGM を流す
        if(scene_)scene_->Initialize(window_,{});
        else Game::Music::Play(page_==3?Game::Music::Victory:Game::Music::Battle);
        // 流れている BGM が期待どおりか
        auto* audio=Engine::Audio::GetInstance();
        const Game::Music::Track tracks[]={Game::Music::Title,Game::Music::Select,Game::Music::Defeat,Game::Music::Victory,Game::Music::Battle};
        passed_&=audio && audio->IsBGMPlaying() && audio->CurrentBGM()==Game::Music::Path(tracks[page_]);
        // ポーズのページは BGM を小さくする
        if(audio)audio->SetBGMDucked(page_==4);
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        // 出力先のフォルダー
        window_=dx;std::filesystem::create_directories("tests/out");
        auto* r=Engine::Renderer::GetInstance();
        // 英字と日本語のフォントを準備し、タイトルが画面に収まるか確かめる
        passed_=r->InitTextSystem(Game::UI::Font,64);
        passed_&=r->InitTextSystem(Game::UI::JapaneseFont,64);
        const float titleWidth=r->MeasureTextWidth(Game::UI::GameTitle(),60.f/64,Game::UI::JapaneseFont);
        passed_&=titleWidth>0 && titleWidth<760;
        Load();
    }
    void Update()override {
        // 30 フレームごとに画面を保存する
        if(++frame_!=30)return;
        DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        // ページごとの保存先
        const wchar_t* paths[]={L"tests/out/ui-title.png",L"tests/out/ui-select.png",L"tests/out/ui-defeat.png",L"tests/out/ui-clear.png",L"tests/out/ui-pause.png",
            L"tests/out/credits-music.png",L"tests/out/credits-art.png",L"tests/out/credits-fonts.png",L"tests/out/credits-software.png"};
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),paths[page_]);
        passed_&=SUCCEEDED(hr);
        // すべてのページが終わったら、BGM の停止と再生を確かめて結果を書く
        if(++page_==5+Game::UI::CreditsPageCount()){
            auto* audio=Engine::Audio::GetInstance();
            audio->StopAll();passed_&=!audio->IsBGMPlaying() && audio->CurrentBGM().empty();
            Game::Music::Play(Game::Music::Title);passed_&=audio->IsBGMPlaying();
            std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" font, title fit, nine GPU captures including four credits pages, five BGM tracks, transitions and stop/restart";
            PostQuitMessage(passed_?0:2);return;
        }
        // 次のページへ
        frame_=0;Load();
    }
    void Draw()override {
        // シーンがあればその描画
        if(scene_){scene_->Draw();return;}
        Game::UI::Canvas ui(Engine::Renderer::GetInstance());
        // クレジットのページ
        if(page_>=5){Game::UI::DrawCredits(ui,page_-5);return;}
        // 勝利の結果画面か、ポーズ画面
        if(page_==3)ui.Result(true,"TIME  02:34.56","CORES  3 / 3     |     HITS TAKEN  12");
        else ui.Pause();
    }
};
