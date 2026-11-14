// DevourTutorial.cpp
// 吸収モードの部屋のステージの最初 (ベッドの下) のチュートリアル (ChronoSystem の一部)。
// 課題を 1 つずつ出し、クリアしたら次を出す。チュートリアルの間は制限時間を進めず、時計も出さない。
//   1. 動かし方と視点: 画面の上の中央に操作のキーを出し、動かしたら・視点を回したらそれぞれ印を付ける
//   2. 巻尺の見方: ゲーム全体を止め、巻尺の周りを暗くして説明する (クリック / A で次へ)
//   3. 段差の大きさまで育つ: 目標の大きさと今の大きさを出す
//   4. 段差の上り方: 収納ケースへ進み続けると跳び越えられる
// ベッドの下を出たら、操作を止めて
//   目標の発表: 制限時間と、めざす大きさ (クリアとランクの大きさ) を伝える (クリック / A で次へ)
//   ステージの紹介: 子ども部屋・奥の和室・スタートの収納ケースをカメラで見せる (クリック / A でとばせる)
// その後、収納ケースの上から「スタート！」で時間が動き出す。
// 操作のキーはキーボードとパッドで出し分ける。規則 (ベッドの下の範囲) は RoomRules.h。
#include "ChronoSystemInternal.h"
#include "../Chrono/RoomRules.h"
#include "../UI/ScaleUI.h"

namespace Game {
namespace {
// 1 つ目の課題: 動かす距離と、視点を回す量 (ラジアン)
constexpr float kTutorialMove=25,kTutorialLook=1.5f;
// 課題をクリアしてから次を出すまでの時間・巻尺の説明の 1 枚を読ませる最短の時間・最後の「スタート」を出す時間
constexpr float kTutorialClear=1.1f,kTutorialPageMin=.35f,kTutorialStart=1.8f;
constexpr int kTutorialPages=3;
// チュートリアルの後の段の番号: 目標の発表・ステージの紹介・カウントダウン (3・2・1)・スタートの知らせ
constexpr int kStepBriefing=4,kStepTour=5,kStepCountdown=6,kStepStart=7;
// カウントダウンの数字の数 (1 つ 1 秒)
constexpr int kCountdownFrom=3;
// 目標の発表: 読ませる最短の時間と、押さなくても進む時間
constexpr float kBriefingMin=.8f,kBriefingAuto=8;
// ステージの紹介のカット (始まりの時刻)・全体の長さ・カットの切り替えの暗転・とばせるまでの時間
constexpr float kTourCuts[]={0,2.9f,5.6f},kTourEnd=8.4f,kTourFade=.35f,kTourSkipMin=.5f;
constexpr const char* kTourNames[]={"子ども部屋","奥の和室","ここからスタート"};
// 説明を進める入力 (クリック・A・Enter・Space)
bool TutorialAdvanceHeld(){return Controls::Key(VK_LBUTTON)||Controls::Key(VK_RETURN)||Controls::Key(VK_SPACE)||Controls::Pad(XINPUT_GAMEPAD_A);}
// 今のカットの番号と、カットの中での進み (0〜1)
int TourShot(float age){int shot=0;for(int i=1;i<3;++i)if(age>=kTourCuts[i])shot=i;return shot;}
float TourProgress(float age,int shot){const float end=shot<2?kTourCuts[shot+1]:kTourEnd;return std::clamp((age-kTourCuts[shot])/(end-kTourCuts[shot]),0.f,1.f);}
// カットの切り替えと終わりの暗転の濃さ
float TourBlack(float age){float black=0;for(float cut:{kTourCuts[1],kTourCuts[2],kTourEnd})black=std::max(black,1-std::abs(age-cut)/kTourFade);return std::clamp(black,0.f,1.f);}
// 制限時間の文: 180 秒 → 「3分」、210 秒 → 「3分30秒」
std::string TimeText(float seconds){const int total=int(std::lround(seconds)),minutes=total/60,rest=total%60;
    if(minutes<=0)return std::to_string(rest)+"秒";return std::to_string(minutes)+"分"+(rest?std::to_string(rest)+"秒":std::string());}
// 収納ケースの上の、スタートの位置 (ケースの真ん中の上)
Vec StartCaseTop(){return {(Chrono::RoomExitX0-60)*.5f,Chrono::RoomCaseHeight,-186};}
}
// 部屋のステージをベッドの下から始めたときだけ、チュートリアルを始める
void ChronoSystem::StartDevourTutorial(){
    devourTutorial_={};const auto& stage=CurrentDevourStage();
    if(!devour_||stage.map!="room"||stage.gates.empty()||!Chrono::RoomUnderBed(stage.spawn+V{0,1,0}))return;
    devourTutorial_.step=0;devourTutorial_.started=false;
}
// 段差の大きさ (チュートリアルの目標)
float ChronoSystem::DevourTutorialGoal()const{const auto& stage=CurrentDevourStage();return stage.gates.empty()?0.f:stage.gates.front();}
// 体を収納ケースの上に置き、部屋の中を向かせる (カメラはその後ろ)
void ChronoSystem::PlaceOnStartCase(entt::registry& r){
    auto& body=life_.bodies[0];const V top=StartCaseTop();
    body.at={top.x,top.y+1.25f,top.z};body.forward=body.liquidForward={0,0,1};yaw_=0;pitch_=.3f;
    if(auto* p=r.try_get<Player>(player_))p->velocity={};
    if(auto* t=r.try_get<TransformComponent>(player_))t->translate=Write(body.at);
    // カメラの追従と持ち上げは新しい位置からやり直す
    cameraReady_=false;cameraLift_=0;
}
// 課題を進める (止めていない間、毎フレーム)。cinematics が false なら目標の発表と紹介を飛ばす
void ChronoSystem::TickDevourTutorial(entt::registry& r,float dt,bool cinematics){
    auto& t=devourTutorial_;if(t.step<0||finished_)return;const auto& body=life_.bodies[0];
    if(!t.started){t.started=true;t.last=body.at;t.yaw=yaw_;t.pitch=pitch_;}
    t.age+=dt;
    // 進める入力 (押した瞬間だけ)。スクリプトの確認 (ControlFrame がある) では実際のマウスとキーを読まない
    // (読むと、確認の実行中に PC を操作したクリックで段が進んでしまう)
    const bool scripted=r.try_get<ControlFrame>(player_)!=nullptr;
    const bool held=!scripted&&TutorialAdvanceHeld(),pressed=held&&!t.previousAdvance;t.previousAdvance=held;
    // 押していた入力で、操作が戻った瞬間に変形や吸収が始まらないようにする
    auto consumeInput=[&]{life_.previousFire=true;life_.previousAbsorb=true;life_.chargeLocked=true;};
    // ベッドの下を出たら (段差の課題の途中でも) 課題を終え、目標の発表へ
    if(t.step!=1&&t.step<kStepBriefing&&!Chrono::RoomUnderBed(body.at)){t.clear=-1;PlayDevourSound(devourSounds_.rankTop,.6f);
        if(cinematics){t.step=kStepBriefing;t.age=0;t.previousAdvance=held;}else{t.step=kStepStart;t.age=0;}
        return;}
    // クリアした課題は少し見せてから次へ
    if(t.clear>=0){t.clear+=dt;if(t.clear<kTutorialClear)return;t.clear=-1;t.age=0;++t.step;
        if(t.step==1){t.page=0;t.previousAdvance=TutorialAdvanceHeld();}
        return;}
    switch(t.step){
    case 0:{V d=body.at-t.last;d.y=0;t.moved+=std::min(3.f,Length(d));t.looked+=std::abs(std::remainder(yaw_-t.yaw,6.2831853f))+std::abs(pitch_-t.pitch);
        if(t.moved>=kTutorialMove&&t.looked>=kTutorialLook){t.clear=0;PlayDevourSound(devourSounds_.rankUp,.45f,1.3f);}break;}
    case 2:if(DevourSizeCm(CurrentDevourStage(),body.mass)+1e-3f>=DevourTutorialGoal()){t.clear=0;PlayDevourSound(devourSounds_.rankUp,.55f,1.15f);}break;
    // 目標の発表を読んだら、体を収納ケースの上へ移してステージの紹介へ (カメラは紹介の構図なので、移すところは見えない)
    case kStepBriefing:if((pressed&&t.age>=kBriefingMin)||t.age>=kBriefingAuto){t.step=kStepTour;t.age=0;PlaceOnStartCase(r);PlayDevourSound(devourSounds_.note,.35f,1.1f);}break;
    // 紹介が終わるか、とばしたら、暗転から収納ケースの上のいつものカメラへ戻してカウントダウン
    case kStepTour:if(t.age>=kTourEnd||(pressed&&t.age>=kTourSkipMin)){t.step=kStepCountdown;t.age=0;}break;
    // 3・2・1 (数字が変わるたびに音を鳴らす)。終わったら「スタート！」で時間と操作が動き出す
    case kStepCountdown:{const float before=t.age-dt-kTourFade,now=t.age-kTourFade;
        if(now>=0&&(before<0||std::floor(before)!=std::floor(now))&&now<float(kCountdownFrom))PlayDevourSound(devourSounds_.note,.5f,1.f+.08f*std::floor(now));
        if(now>=float(kCountdownFrom)){t.step=kStepStart;t.age=0;consumeInput();PlayDevourSound(devourSounds_.rankUp,.6f,1.25f);}break;}
    case kStepStart:if(t.age>=kTutorialStart)t.step=-1;t.clear=-1;break;}
    t.last=body.at;t.yaw=yaw_;t.pitch=pitch_;
}
// ステージの紹介のカメラ: いつものカメラ (InkCamera) を計算した後に、紹介の構図で上書きする
void ChronoSystem::DevourTourCamera(GameContext& ctx){
    const auto& t=devourTutorial_;if(t.step!=kStepTour||finished_)return;
    const int shot=TourShot(t.age);const float u=ShotEase(TourProgress(t.age,shot),0,1);const V start=StartCaseTop();
    Chrono::CinematicPose pose;
    if(shot==0){
        // 子ども部屋: 本棚の側の高い所から、ベッドと机の方を見渡しながら横へ流す
        pose=Chrono::AimShot(V{250-70*u,215-25*u,10-30*u},V{-40,0,-150},1.05f);
    }else if(shot==1){
        // 奥の和室: 和室の奥の角から、畳と入口の方を見渡す
        pose=Chrono::AimShot(V{-255+55*u,170-20*u,280},V{60,0,140},1.f);
    }else{
        // スタートの収納ケース: 部屋の側の低い所から、ケースの上の体へ寄る (ベッドの板の下を通して見る)
        pose=Chrono::AimShot(V{start.x+14-14*u,20-5*u,start.z+62-24*u},start+V{0,2,0},.8f);
    }
    ctx.camera->StopShake();ctx.camera->SetHandheld(0);
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),LifeCameraNear,2000);
}
// 巻尺の説明の間はゲーム全体を止める。止めているフレームは true
bool ChronoSystem::TickDevourTutorialPause(float dt){
    auto& t=devourTutorial_;if(t.step!=1||t.clear>=0)return false;
    t.age+=dt;const bool held=TutorialAdvanceHeld(),pressed=held&&!t.previousAdvance;t.previousAdvance=held;
    if(pressed&&t.age>=kTutorialPageMin){t.age=0;if(++t.page>=kTutorialPages){t.step=2;t.page=0;
        // 進めたクリックで変形や吸収が始まらないようにする
        life_.previousFire=true;life_.previousAbsorb=true;life_.chargeLocked=true;return false;}
        PlayDevourSound(devourSounds_.note,.3f,1.2f);}
    return true;
}
// 上の中央の課題の札と、巻尺の説明 (周りを暗くして巻尺を照らす)
void ChronoSystem::DrawDevourTutorial(GameContext& ctx){
    const auto& t=devourTutorial_;if(t.step<0||finished_)return;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);namespace S=UI::Scale;
    const bool pad=UI::UsingPad();const auto& stage=CurrentDevourStage();const float cm=DevourSizeCm(stage,life_.bodies[0].mass),goal=DevourTutorialGoal();
    const Engine::Vector4 black=S::Black,cream=S::Cream,yellow=S::Yellow,red=S::Red,white=S::White;
    // 印 (✓): 書体に頼らず線で描く
    auto check=[&](float x,float y,float s,Engine::Vector4 c){ui.Line(x,y+s*.5f,x+s*.38f,y+s*.88f,s*.2f,c);ui.Line(x+s*.38f,y+s*.88f,x+s,y+s*.1f,s*.2f,c);};
    // 操作のキーの並び: キーボードはキーの絵を並べ、パッドはスティックやボタンの絵
    // キーは他の操作の案内と同じ部品 (黒い台の上のキーの絵) で出す
    auto keys=[&](const S::InputKey& key,float x,float y,float size){return S::KeyChip(ui,key,x,y,size,pad);};
    // 札: 見出し (やってみよう n/3) と本文、その下に操作の行
    auto card=[&](const std::string& head,const std::string& text,float rows,float clear){
        const float w=std::max(470.f,ui.Width(text,28,S::Jp)+56),h=94+rows*50,x=640-w*.5f,y=S::HudMargin+(clear>0?-6*std::sin(std::min(1.f,clear*4)*3.1415927f):0.f);
        S::Plate(ui,{x,y,w,h},clear>0?yellow:cream);ui.Fill({x,y,w,34},black);ui.Text(head,x+16,y+3,22,yellow,S::Jp);
        ui.Text(text,x+20,y+44,28,black,S::Jp);
        if(clear>0){const float s=40;check(x+w-s-18,y+44,s,red);}
        return UI::Rect{x,y,w,h};};
    if(t.step==0){const auto box=card("やってみよう 1/3","スライムを動かして、まわりを見てみよう",2,t.clear>=0?1.f:0.f);
        auto row=[&](float y,const S::InputKey& art,const std::string& what,bool done){float x=keys(art,box.x+24,y,40)+12;ui.Text(what,x,y+5,26,black,S::Jp);
            if(done)check(x+ui.Width(what,26,S::Jp)+14,y+6,28,red);};
        row(box.y+88,S::KeyMove,"移動",t.moved>=kTutorialMove);row(box.y+138,S::KeyLook,"視点",t.looked>=kTutorialLook);}
    else if(t.step==2){
        // 育つ間はずっと出ているので、上の中央に細い札 1 本だけにする (視界を広く残す)
        const float w=430,h=50,x=640-w*.5f,y=S::HudMargin,clear=t.clear>=0?1.f:0.f;const std::string head="3/3",text="めざせ "+DevourSizeText(goal),now=DevourSizeText(std::min(cm,goal));
        S::Plate(ui,{x,y,w,h},clear>0?yellow:cream,3,4);ui.Fill({x,y,48,h},black);ui.Text(head,x+9,y+11,20,yellow,S::Jp);
        ui.Text(text,x+62,y+5,24,black,S::Jp);
        if(clear>0)check(x+w-40,y+8,28,red);else ui.Text(now,x+w-14-ui.Width(now,22,S::Jp),y+6,22,black,S::Jp);
        const float start=stage.start,u=std::clamp((cm-start)/std::max(.01f,goal-start),0.f,1.f),bx=x+62,bw=w-76;
        ui.Fill({bx,y+h-11,bw,6},{0,0,0,.18f});ui.Fill({bx,y+h-11,bw*u,6},clear>0?red:Engine::Vector4{.45f,.72f,.1f,1});}
    else if(t.step==3){const auto box=card("段差を上ろう","収納ケースに向かって進み続けると、跳び越えられる",1,t.clear>=0?1.f:0.f);
        float x=keys(S::KeyMove,box.x+24,box.y+88,40)+12;ui.Text("押し続ける",x,box.y+95,26,black,S::Jp);}
    else if(t.step==kStepBriefing){
        // 目標の発表: 画面を少し暗くし、制限時間とめざす大きさ、ランクの大きさを中央の札で伝える
        const float pop=ShotEase(t.age,0,.25f);ui.Fill({-400,-400,2080,1520},{0,0,0,.45f*pop});
        const std::string head="ステージ  "+stage.name,lead="ベッドの下から出られた！ ここからが本番",
            main=TimeText(stage.time)+"で "+DevourSizeText(stage.goals.front())+" をめざそう";
        std::string ranks="もっと大きくなるとランクが上がる   ";
        for(int i=1;i<=int(stage.goals.size());++i)ranks+=DevourRankName(stage,i)+" "+DevourSizeText(stage.goals[size_t(i-1)])+(i<int(stage.goals.size())?"   ":"");
        const float w=std::max({ui.Width(main,40,S::Jp),ui.Width(ranks,22,S::Jp),ui.Width(lead,24,S::Jp)})+80,h=232,x=640-w*.5f,y=210-18*(1-pop);
        ui.MultiplyAlpha(pop);
        S::Plate(ui,{x,y,w,h},cream);ui.Fill({x,y,w,38},black);ui.Text(head,x+18,y+4,24,yellow,S::Jp);
        ui.Text(lead,x+40,y+54,24,black,S::Jp);
        // 制限時間とめざす大きさは大きく赤く
        ui.Text(main,x+40,y+92,40,red,S::Jp);
        ui.Fill({x+28,y+152,w-56,2},{0,0,0,.15f});ui.Text(ranks,x+40,y+162,22,black,S::Jp);
        const std::string next="で次へ";const float bob=2*std::sin(life_.age*6),nx=x+w-26-ui.Width(next,22,S::Jp);
        if(t.age>=kBriefingMin){S::KeyChip(ui,S::KeyNext,nx-46,y+h-38+bob,34,pad);ui.Text(next,nx,y+h-34,22,black,S::Jp);}}
    else if(t.step==kStepTour){
        // ステージの紹介: 上下の黒帯・場所の名前・とばす操作。カットの切り替えは暗転でつなぐ
        const float bars=ShotEase(t.age,0,.4f),shot=float(TourShot(t.age)),local=t.age-kTourCuts[size_t(shot)];
        ui.Fill({-400,-400,2080,400+70*bars},{.01f,.02f,.02f,1});ui.Fill({-400,720-70*bars,2080,800},{.01f,.02f,.02f,1});
        const std::string name=kTourNames[size_t(shot)];const float appear=ShotEase(local,.25f,.6f);
        {UI::Canvas label(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);label.MultiplyAlpha(appear);
            S::Plate(label,{60,560,label.Width(name,34,S::Jp)+48,56},yellow);label.Text(name,84,565,34,black,S::Jp);}
        // 目標はずっと右上に出しておく
        const std::string goalText=TimeText(stage.time)+"で "+DevourSizeText(stage.goals.front());ui.Text(goalText,1240-ui.Width(goalText,24,S::Jp),20,24,yellow,S::Jp);
        if(t.age>=kTourSkipMin){const std::string skip="でとばす";const float sx=1240-ui.Width(skip,20,S::Jp);S::KeyChip(ui,S::KeyNext,sx-42,668,30,pad);ui.Text(skip,sx,672,20,white,S::Jp);}
        const float dark=TourBlack(t.age);if(dark>0)ui.Fill({-400,-400,2080,1520},{0,0,0,dark});}
    else if(t.step==kStepCountdown){
        // 紹介の終わりの暗転から戻し、画面の中央に 3・2・1 を大きく出す (数字ごとにぽんと出て、次の数字の前に縮んで消える)
        const float dark=1-std::clamp(t.age/kTourFade,0.f,1.f);if(dark>0)ui.Fill({-400,-400,2080,1520},{0,0,0,dark});
        const float age=t.age-kTourFade;if(age<0)return;
        const int number=kCountdownFrom-int(std::floor(age));if(number<1)return;const float local=age-std::floor(age);
        const float pop=1+.35f*(1-ShotEase(local,0,.18f)),fade=1-ShotEase(local,.75f,1);const float size=230*pop;
        // 数字は HUD の大きさの数字と同じ書体 (大きな文字用の解像度)
        const std::string text=std::to_string(number);const float w=ui.Width(text,size,S::NumBig);
        ui.MultiplyAlpha(fade);
        // 黒い縁取り (少しずらして重ねる) の上に黄色の数字
        for(int k=0;k<8;++k){const float a=float(k)*.785f;ui.Text(text,640-w*.5f+std::cos(a)*8,250-size*.5f+std::sin(a)*8,size,black,S::NumBig);}
        ui.Text(text,640-w*.5f,250-size*.5f,size,yellow,S::NumBig);}
    else if(t.step==kStepStart){
        // カウントダウンの後、収納ケースの上から「スタート！」
        const float age=t.age;
        const float pop=std::min(1.f,age*5),fade=std::clamp((kTutorialStart-age)/.4f,0.f,1.f);const std::string text="スタート！ "+TimeText(stage.time)+"で "+DevourSizeText(stage.goals.front())+" をめざそう";
        const float size=40*(.8f+.2f*pop),w=ui.Width(text,size,S::Jp)+60;ui.MultiplyAlpha(fade);S::Plate(ui,{640-w*.5f,150,w,size*1.6f},red);ui.Text(text,640-w*.5f+30,150+size*.15f,size,white,S::Jp);}
    else if(t.step==1&&t.clear<0){
        // ゲームを止めて、巻尺の周りを暗くする。説明する所を赤い枠で囲む
        const UI::Rect hole{0,594,1280,126};const Engine::Vector4 dim{0,0,0,.68f};
        ui.Fill({-400,-400,2080,400+hole.y},dim);
        const float glow=.6f+.4f*std::sin(life_.age*5);ui.Fill({0,hole.y-4,1280,4},{yellow.x,yellow.y,yellow.z,glow});
        UI::Rect focus=hole;std::string text,sub;
        if(t.page==0){focus={4,600,S::HudReadX+2,118};text="左下の数字が、いまのスライムの大きさ";sub="巻尺の赤い線のところも、いまの大きさを指している";}
        else if(t.page==1){focus={S::HudReadX,S::HudTapeTop-4,1026,S::HudTapeHeight+8};text="飲み込んで大きくなると、巻尺が左へ流れていく";sub="右にある目盛りほど大きい";}
        else{const float x=S::HudTapeX(goal,devourHudCm_>0?devourHudCm_:cm);focus={std::clamp(x-20,S::HudReadX,1000.f),S::HudTapeTop-46,200,S::HudTapeHeight+50};
            text="旗は目標の大きさ。旗が赤い線まで来たら届いた合図";sub="「段差 "+DevourSizeText(goal)+"」まで育つと、ベッドの下から出られる";}
        for(int k=0;k<4;++k){const float g=float(k)*2;const auto c=Engine::Vector4{red.x,red.y,red.z,k?.35f:1.f};
            ui.Fill({focus.x-g,focus.y-g,focus.w+2*g,4},c);ui.Fill({focus.x-g,focus.y+focus.h+g-4,focus.w+2*g,4},c);ui.Fill({focus.x-g,focus.y-g,4,focus.h+2*g},c);ui.Fill({focus.x+focus.w+g-4,focus.y-g,4,focus.h+2*g},c);}
        // 説明の札 (画面の中央の下寄り) と、次へ進む操作
        const float w=std::max(ui.Width(text,30,S::Jp),ui.Width(sub,22,S::Jp))+64,x=640-w*.5f,y=388;
        S::Plate(ui,{x,y,w,172},cream);ui.Fill({x,y,w,34},black);ui.Text("巻尺の見方  "+std::to_string(t.page+1)+"/"+std::to_string(kTutorialPages)+"   (やってみよう 2/3)",x+16,y+3,22,yellow,S::Jp);
        ui.Text(text,x+32,y+48,30,black,S::Jp);ui.Text(sub,x+32,y+90,22,black,S::Jp);
        const std::string next=t.page+1<kTutorialPages?"で次へ":"でとじる";const float bob=2*std::sin(life_.age*6),nx=x+w-24-ui.Width(next,22,S::Jp);
        // 次へ進む操作は本文の下の行に置く (本文と重ならないように)
        ui.Fill({x+20,y+124,w-40,2},{0,0,0,.15f});
        S::KeyChip(ui,S::KeyNext,nx-46,y+130+bob,36,pad);ui.Text(next,nx,y+134,22,black,S::Jp);}
}
}
