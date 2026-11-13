// UIValidationScene.h
// UI と描画の自動テスト用シーンをまとめたファイル (--ui-smoke などのコマンドライン引数で起動する)。
// 影・グラフィック設定・結果画面・各メニューの表示を確かめ、
// 結果を tests/out/ui-smoke.txt と画像に書き出す。
#pragma once
// テストで表示するシーン
#include "../Game/Scenes/TitleScene.h"
#include "../Game/UI/CollectionUI.h"
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
    // ずかんのページ (クレジットの後): 見本の数で開く
    Game::UI::CollectionState collection_;
    int CollectionPage()const{return 5+Game::UI::CreditsPageCount();}
    // ページに合わせてシーンを用意し、BGM が合っているかを確かめる
    void Load() {
        // クレジットのページではシーンを使わない
        if(page_>=5){scene_.reset();
            if(page_==CollectionPage()){collection_={};collection_.age=9;const auto& entries=Game::Collection::Entries();
                for(size_t i=0;i<entries.size();i+=2)collection_.counts[entries[i].id]=int(i%7)+1;collection_.selected=4;collection_.pickedAt=0;}
            return;}
        // 0: タイトル / 1: セレクト / 2: ゲームオーバー / それ以外: シーンなし
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else scene_.reset();
        // シーンがあれば初期化し、無ければ勝利か戦闘の BGM を流す
        if(scene_)scene_->Initialize(window_,{});
        else Game::Music::Play(page_==2?Game::Music::Defeat:page_==3?Game::Music::Victory:Game::Music::Battle);
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
            L"tests/out/credits-music.png",L"tests/out/credits-sound.png",L"tests/out/credits-art.png",L"tests/out/credits-fonts.png",L"tests/out/credits-software.png"};
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),page_==CollectionPage()?L"tests/out/ui-collection.png":paths[page_]);
        passed_&=SUCCEEDED(hr);
        // すべてのページが終わったら、BGM の停止と再生を確かめて結果を書く
        if(++page_==CollectionPage()+1){
            auto* audio=Engine::Audio::GetInstance();
            audio->StopAll();passed_&=!audio->IsBGMPlaying() && audio->CurrentBGM().empty();
            Game::Music::Play(Game::Music::Title);passed_&=audio->IsBGMPlaying();
            std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" font, title fit, ten GPU captures including five credits pages, five BGM tracks, transitions and stop/restart";
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
        if(page_==CollectionPage()){Game::UI::DrawCollection(ui,collection_);return;}
        if(page_>=5){Game::UI::DrawCredits(ui,page_-5);return;}
        // 勝利の結果画面か、ポーズ画面
        namespace S=Game::UI::Scale;
        if(page_==2){S::ResultView view;view.stage="子ども部屋";view.cm=11.8f;view.goalCm=15;view.items=38;view.percent=12;S::Result(ui,view,9);}
        else if(page_==3){S::ResultView view;view.stage="輪の町";view.cm=12631;view.goalCm=5000;view.timeLeft=45;view.rank="S";view.won=true;view.newBest=true;view.hasNext=true;view.items=722;view.percent=30;S::Result(ui,view,9);}
        else{ui.Clear({.28f,.2f,.2f,1});S::PauseView view;view.stage="子ども部屋";view.cm=11.8f;view.timeLeft=135;view.goals={{"C",15.f},{"B",20.f},{"A",25.f},{"S",30.f}};S::Pause(ui,view,0,9);}
    }
};
