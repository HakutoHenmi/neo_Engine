// DevourTutorial.cpp
// 吸収モードの部屋のステージの最初 (ベッドの下) のチュートリアル (ChronoSystem の一部)。
// 課題を 1 つずつ出し、クリアしたら次を出す。チュートリアルの間は制限時間を進めず、時計も出さない。
//   1. 動かし方と視点: 画面の上の中央に操作のキーを出し、動かしたら・視点を回したらそれぞれ印を付ける
//   2. 巻尺の見方: ゲーム全体を止め、巻尺の周りを暗くして説明する (クリック / A で次へ)
//   3. 段差の大きさまで育つ: 目標の大きさと今の大きさを出す
//   4. 段差の上り方: 収納ケースへ進み続けると跳び越えられる。ベッドの下を出たら時間が動き出す
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
// 説明を進める入力 (クリック・A・Enter・Space)
bool TutorialAdvanceHeld(){return Controls::Key(VK_LBUTTON)||Controls::Key(VK_RETURN)||Controls::Key(VK_SPACE)||Controls::Pad(XINPUT_GAMEPAD_A);}
}
// 部屋のステージをベッドの下から始めたときだけ、チュートリアルを始める
void ChronoSystem::StartDevourTutorial(){
    devourTutorial_={};const auto& stage=CurrentDevourStage();
    if(!devour_||stage.map!="room"||stage.gates.empty()||!Chrono::RoomUnderBed(stage.spawn+V{0,1,0}))return;
    devourTutorial_.step=0;devourTutorial_.started=false;
}
// 段差の大きさ (チュートリアルの目標)
float ChronoSystem::DevourTutorialGoal()const{const auto& stage=CurrentDevourStage();return stage.gates.empty()?0.f:stage.gates.front();}
// 課題を進める (止めていない間、毎フレーム)
void ChronoSystem::TickDevourTutorial(float dt){
    auto& t=devourTutorial_;if(t.step<0||finished_)return;const auto& body=life_.bodies[0];
    if(!t.started){t.started=true;t.last=body.at;t.yaw=yaw_;t.pitch=pitch_;}
    t.age+=dt;
    // ベッドの下を出たら (段差の課題の途中でも) チュートリアルを終えて時間を動かす
    if(t.step!=1&&t.step<4&&!Chrono::RoomUnderBed(body.at)){t.step=4;t.age=0;t.clear=-1;PlayDevourSound(devourSounds_.rankTop,.6f);return;}
    // クリアした課題は少し見せてから次へ
    if(t.clear>=0){t.clear+=dt;if(t.clear<kTutorialClear)return;t.clear=-1;t.age=0;++t.step;
        if(t.step==1){t.page=0;t.previousAdvance=TutorialAdvanceHeld();}
        if(t.step==5)t.step=-1;
        return;}
    switch(t.step){
    case 0:{V d=body.at-t.last;d.y=0;t.moved+=std::min(3.f,Length(d));t.looked+=std::abs(std::remainder(yaw_-t.yaw,6.2831853f))+std::abs(pitch_-t.pitch);
        if(t.moved>=kTutorialMove&&t.looked>=kTutorialLook){t.clear=0;PlayDevourSound(devourSounds_.rankUp,.45f,1.3f);}break;}
    case 2:if(DevourSizeCm(CurrentDevourStage(),body.mass)+1e-3f>=DevourTutorialGoal()){t.clear=0;PlayDevourSound(devourSounds_.rankUp,.55f,1.15f);}break;
    case 4:if(t.age>=kTutorialStart){t.step=-1;}t.clear=-1;break;}
    t.last=body.at;t.yaw=yaw_;t.pitch=pitch_;
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
    else if(t.step==4){const float pop=std::min(1.f,t.age*5),fade=std::clamp((kTutorialStart-t.age)/.4f,0.f,1.f);const std::string text="スタート！ 時間内にもっと大きくなろう";
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
