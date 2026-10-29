// SlimeIntro.cpp
// ステージ開始のカメラ演出 (ChronoSystem の一部)。
// ゴールの交差点から火事場・大通りへと道のりを逆にたどり、最後にスタートの路地の真上から
// いつものカメラへ降りてくる。スペース / Enter / A / START の長押しでスキップできる。
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
// 各カットの始まりの時刻 (交差点・火事場・大通り・路地) と全体の長さ
constexpr float IntroCuts[]={0,2.8f,5.4f,8.f},IntroEnd=11.2f;
// 路地の真上から降り始める時刻・カットの切り替えの暗転の長さ・スキップに要る長押しの時間・スキップ後の暗転
constexpr float IntroDescend=8.6f,IntroCutFade=.3f,IntroSkipHold=1.f,IntroSkipFade=.35f;
// 今のカットの番号
int IntroShot(float age){int shot=0;for(int i=1;i<4;++i)if(age>=IntroCuts[i])shot=i;return shot;}
// カットの中での進み (0〜1)
float IntroProgress(float age,int shot){float end=shot<3?IntroCuts[shot+1]:IntroEnd;return std::clamp((age-IntroCuts[shot])/(end-IntroCuts[shot]),0.f,1.f);}
}
// 演出の更新: 敵・弾・子分は進めず、カメラと本体の見た目だけを動かす
void ChronoSystem::UpdateStageIntro(entt::registry& r,Player& p,GameContext& ctx,float dt){
    auto& intro=intro_;auto* control=r.try_get<ControlFrame>(player_);
    // スクリプトの確認は、指定したときだけ演出を見せる
    if(control&&!control->intro){intro.active=false;return;}
    Engine::WindowDX::SetCursorVisible(false);p.cameraOpacity=1;
    intro.age+=dt;
    // スキップ: 押している間だけゲージが溜まり、離すとゆっくり戻る
    bool held=control?control->jump:Controls::Key(VK_SPACE)||Controls::Key(VK_RETURN)||Controls::Pad(XINPUT_GAMEPAD_A)||Controls::Pad(XINPUT_GAMEPAD_START);
    intro.hold=held?intro.hold+dt:std::max(0.f,intro.hold-dt*2);
    bool skipped=intro.hold>=IntroSkipHold;
    // 各カットの構図
    const auto& walk=CityWalk();const V crossing=CityCrossing(),start=CityStart();
    const CityRect fire=walk[9],avenue=walk[3];
    int shot=IntroShot(intro.age);float u=ShotEase(IntroProgress(intro.age,shot),0,1);
    CinematicPose pose;
    if(shot==0){
        // ゴールの交差点: 高い所から回り込みながら少し降りる
        float a=-2.2f+.5f*u;
        pose=AimShot(crossing+V{std::cos(a)*240,190-30*u,std::sin(a)*240},crossing+V{0,8,0},.95f);
    }else if(shot==1){
        // 火事場: 燃える区画を斜め上から横へ流す
        V c{(fire.x0+fire.x1)*.5f,0,(fire.z0+fire.z1)*.5f};
        pose=AimShot(c+V{-190+70*u,95-20*u,-150+40*u},c+V{30,6,60},.9f);
    }else if(shot==2){
        // 大通り: 通りの上をスタートの方へ進む
        float x=(avenue.x0+avenue.x1)*.5f,z=avenue.z1-60-220*u;
        pose=AimShot({x+40-30*u,75-25*u,z},{x,12,z-260},.95f);
    }else{
        // スタートの路地: 真上から見下ろし、いつものカメラへ降りてくる。路地の中だけを通るので建物に入らない
        // 位置と回転を別々に補間すると途中でスライムが画面の外へ出るので、注視点も路地の先から体へ移しながら向け続ける
        V topAt=start+V{0,250-20*ShotEase(intro.age,IntroCuts[3],IntroDescend),-15},topLook=start+V{0,0,90};
        InkCamera(r,p,ctx);
        V gameAt=Read(ctx.camera->Position());float gameFov=2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]));
        float descend=ShotEase(intro.age,IntroDescend,IntroEnd);
        pose=AimShot(Lerp(topAt,gameAt,descend),Lerp(topLook,GetLifeCameraSubject().center,descend),.9f+(gameFov-.9f)*descend);
    }
    float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y);
    ctx.camera->StopShake();ctx.camera->SetHandheld(0);
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,aspect,LifeCameraNear,2000);
    // 本体の形と見た目を反映する (敵・子分・弾は更新しないので止まったまま)
    const auto& body=life_.bodies[0];
    auto& transform=r.get<TransformComponent>(player_);float size=LifeScale(body.mass);transform.scale={size,size,size};transform.rotate.y=std::atan2(body.forward.x,body.forward.z);
    p.worldScale=p.aimScale=1;Presentation(r,p,ctx);SyncSlimeBodies(ctx);
    // 終わったら、いつものカメラに戻す。スキップしたときは暗転から戻す
    if(skipped||intro.age>=IntroEnd){
        intro.active=false;intro.fadeOut=skipped?IntroSkipFade:0.f;
        InkCamera(r,p,ctx);
        // 押しっぱなしの入力を消費し、始まった瞬間にジャンプや攻撃が出ないようにする
        prevSpace_=control?control->jump:Controls::Jump();
        life_.previousFire=control?control->attack:Controls::MorphAttack();life_.previousAbsorb=control?control->absorb:Controls::Absorb();
        life_.previousReturn=control?control->returnToSlime:Controls::ReturnToSlime();
    }
}
// 演出の画面: 上下の黒帯・カットの切り替えの暗転・場所の名前・スキップの案内
void ChronoSystem::DrawStageIntro(GameContext& ctx){
    const auto& intro=intro_;UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const float age=intro.age;
    // 上下の黒帯 (始めと終わりはなめらかに出入りする)
    float bars=ShotEase(age,0,.4f)*(1-ShotEase(age,IntroEnd-.6f,IntroEnd));
    ui.Fill({-400,-400,2080,400+64*bars},{.01f,.02f,.02f,1});
    ui.Fill({-400,720-64*bars,2080,800},{.01f,.02f,.02f,1});
    // 場所の名前: カットごとに左から滑り込ませる
    int shot=IntroShot(age);float end=shot<3?IntroCuts[shot+1]:IntroDescend+.9f,local=age-IntroCuts[shot];
    float slide=ShotEase(local,.2f,.6f),fade=slide*(1-ShotEase(age,end-.5f,end-.15f));
    if(fade>0){
        static const CityArea areas[]={CityArea::Crossing,CityArea::Fire,CityArea::Avenue,CityArea::Alley};
        static const char* tags[]={"GOAL","AREA 3","AREA 2","START"};
        static const char* lines[]={"街の最奥、ここを目指せ","燃え続ける街区","封鎖された大通り","小さなスライムの逆襲が始まる"};
        const Engine::Vector4 accent=shot==0?UI::Gold:UI::Lime;
        float x=96-(1-slide)*80;
        ui.Fill({x,564,6,74},UI::WithAlpha(accent,fade));
        ui.Label(tags[shot],x+22,560,15,UI::WithAlpha(accent,fade),5);
        const std::string name=CityAreaName(areas[shot]);
        ui.Text(name,x+24,582,44,{0,0,0,.8f*fade},UI::JapaneseFont);ui.Text(name,x+20,578,44,UI::WithAlpha(UI::Paper,fade),UI::JapaneseFont);
        ui.Text(lines[shot],x+22,630,17,UI::WithAlpha(UI::Paper,.75f*fade),UI::JapaneseFont);
    }
    // カットの切り替えと始まりの暗転
    float black=1-ShotEase(age,0,.5f);
    for(int i=1;i<4;++i)black=std::max(black,1-std::abs(age-IntroCuts[i])/IntroCutFade);
    if(black>0)ui.Fill({-400,-400,2080,1520},{0,0,0,std::clamp(black,0.f,1.f)});
    // スキップの案内: ボタンの周りに長押しのゲージを描く
    float ratio=std::clamp(intro.hold/IntroSkipHold,0.f,1.f),show=bars*(.65f+.35f*(ratio>0?1.f:0.f));
    const bool pad=UI::UsingPad();const float cx=1180,cy=688;
    ui.Ring(cx,cy,19,ratio,UI::WithAlpha(UI::Lime,show),4);
    ui.Image(pad?"Resources/Textures/UI/kenney_input-prompts_1.5/Xbox Series/Default/xbox_button_a.png":"Resources/Textures/UI/kenney_input-prompts_1.5/Keyboard & Mouse/Default/keyboard_space.png",{cx-15,cy-15,30,30},{1,1,1,show});
    const char* skip="長押しでスキップ";
    ui.Text(skip,cx-28-ui.Width(skip,16,UI::JapaneseFont),cy-10,16,UI::WithAlpha(UI::Paper,.8f*show),UI::JapaneseFont);
}
}
