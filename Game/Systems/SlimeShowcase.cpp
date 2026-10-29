// SlimeShowcase.cpp
// 変形アイテム (タイヤ・消火栓) を初めて取ったときの見せ場 (ChronoSystem の一部)。
// プレイヤー以外の時間を止め、カメラが正面から寄って周りを暗くし、変形した姿を見せてから元のカメラと光へ戻す。
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
// 寄り始めてから寄り終わるまで・暗くなるまで・変形を始めるまで・完成後に見せる時間・形ができなくても戻る時間・戻る時間
constexpr float ShowcaseIn=.55f,ShowcaseDark=.35f,ShowcaseMorph=.35f,ShowcaseHold=1.4f,ShowcaseGiveUp=4.5f,ShowcaseOut=.7f;
// 見せ場の太陽の明るさの割合
constexpr float ShowcaseSunScale=.35f;
}
// 見せ場を始める (スクリプトの確認は showcase を指定したときだけ)
void ChronoSystem::StartMorphShowcase(entt::registry& r,GameContext& ctx,LifeAction form){
    auto* control=r.try_get<ControlFrame>(player_);
    if(showcase_.active||slimePlayerDemo_||(control&&!control->showcase))return;
    auto subject=GetLifeCameraSubject();const auto& body=life_.bodies[0];
    V facing=body.forward;facing.y=0;if(Length(facing)<.01f)facing={0,0,1};
    showcase_={};showcase_.active=true;showcase_.form=form;showcase_.center=subject.center;showcase_.radius=subject.radius;showcase_.facing=Unit(facing);
    // 戻すための被写界深度を記録し、揺れを止めて BGM を下げる
    auto params=ctx.renderer->GetPostProcessParams();showcase_.dofFocus=params.dofFocus;showcase_.dofRange=params.dofRange;showcase_.dofStrength=params.dofStrength;
    ctx.camera->StopShake();Engine::Audio::GetInstance()->SetBGMDucked(true);
}
// 見せ場の更新: 敵・弾・子分・効果は進めず、本体の変形とカメラ・光だけを動かす
void ChronoSystem::UpdateMorphShowcase(entt::registry& r,Player& p,GameContext& ctx,float dt){
    auto& show=showcase_;auto& body=life_.bodies[0];auto* control=r.try_get<ControlFrame>(player_);
    Engine::WindowDX::SetCursorVisible(false);
    // 太陽: 始めに今の明るさを記録し、暗くした分だけ下げる
    DirectionalLightComponent* sun=nullptr;
    for(auto e:r.view<NameComponent,DirectionalLightComponent>())if(r.get<NameComponent>(e).name=="Sun"){sun=&r.get<DirectionalLightComponent>(e);break;}
    if(show.age==0&&sun)show.sun=sun->intensity;
    show.age+=dt;
    // 少し寄ってから変形を始める。取り込みの途中なら、その成長を先に反映する
    if(!show.morphed&&show.age>=ShowcaseMorph){show.morphed=true;
        if(body.action==LifeAction::Engulf||body.action==LifeAction::Restore){LifeCompleteGrowth(life_,body);body.grabs.clear();}
        if(body.action!=show.form&&LifeEnterWeapon(body,show.form,show.facing)){++life_.revision;
            auto* audio=Engine::Audio::GetInstance();audio->Play(lifeEngulfSound_,false,.5f*audio->GetMasterSEVolume(),.8f);}}
    // 変形を進める (攻撃は始めない)
    if(LifeWeaponForm(body.action)){body.actionAge+=dt;
        bool formed=body.actionAge>=.3f&&ctx.renderer->FluidMorphReady(life_.revision,0,1.f*LifeScale(body.mass));
        LifeTickWeapon(life_,body,dt,false,false,formed);}
    // 形が完成したら音を鳴らし、その姿を少し見せてから戻る
    bool ready=LifeWeaponForm(body.action)&&body.weapon.ready;
    if(ready&&show.readyAt<0){show.readyAt=show.age;auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[0],false,.3f*audio->GetMasterSEVolume(),1.5f);}
    float hold=show.readyAt>=0?std::max(show.readyAt+ShowcaseHold,ShowcaseIn+1.2f):show.morphed&&!LifeWeaponForm(body.action)?ShowcaseIn+1.2f:ShowcaseGiveUp;
    if(show.readyAt<0&&show.age>=ShowcaseGiveUp)hold=show.age;
    float into=ShotEase(show.age,0,ShowcaseIn),out=ShotEase(show.age,hold,hold+ShowcaseOut);
    // 寄る構図: 体の中心と大きさをなめらかに追い、正面の少し横・少し上から見る (見せている間はゆっくり回り込む)
    auto subject=GetLifeCameraSubject();float follow=1-std::exp(-8*dt);
    show.center=Lerp(show.center,subject.center,follow);show.radius+=(subject.radius-show.radius)*follow;
    float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),fov=.78f;
    float turn=.38f-.3f*ShotEase(show.age,ShowcaseIn,std::max(hold,ShowcaseIn+.1f));
    V side{show.facing.z,0,-show.facing.x},direction=Unit(show.facing+side*turn+V{0,.22f,0});
    V at=show.center+direction*(LifeCameraDistance(show.radius,fov,aspect)*.62f);
    // 建物の中へ入らないよう、街の通れる範囲に収める
    V flat=ConstrainLifeMovement({show.center.x,at.y,show.center.z},at,.6f);at.x=flat.x;at.z=flat.z;at.y=std::max(at.y,InkGround(at)+.7f);
    // 注視点を少し下げ、体を画面の上寄りに置いて下に名前の場所を空ける
    CinematicPose close=AimShot(at,show.center-V{0,show.radius*.22f,0},fov);
    // ゲームのカメラも毎フレーム更新し、そこから寄って、そこへ戻る
    InkCamera(r,p,ctx);
    CinematicPose game{Read(ctx.camera->Position()),Read(ctx.camera->Rotation()),2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]))};
    CinematicPose pose=BlendShot(BlendShot(game,close,into),game,out);
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,aspect,LifeCameraNear,2000);
    // 周りを暗くする: 太陽を下げ、背景をぼかす
    float dark=ShotEase(show.age,0,ShowcaseDark)*(1-out);
    if(sun)sun->intensity=show.sun*(1+(ShowcaseSunScale-1)*dark);
    auto params=ctx.renderer->GetPostProcessParams();
    params.dofFocus=show.dofFocus+(Length(show.center-pose.at)-show.dofFocus)*dark;params.dofRange=show.dofRange+(show.radius*2.5f-show.dofRange)*dark;params.dofStrength=show.dofStrength+(.75f-show.dofStrength)*dark;
    // 体の形と見た目を反映する (敵・子分・弾は更新しないので止まったまま)
    auto& transform=r.get<TransformComponent>(player_);float size=LifeScale(body.mass);transform.scale={size,size,size};transform.rotate.y=std::atan2(body.forward.x,body.forward.z);
    SyncSlimeBodies(ctx);
    // 押しっぱなしの入力を消費し、戻った瞬間に攻撃や吸収が出ないようにする
    life_.previousFire=control?control->attack:Controls::MorphAttack();life_.previousAbsorb=control?control->absorb:Controls::Absorb();
    life_.previousReturn=control?control->returnToSlime:Controls::ReturnToSlime();
    // 終わったら、光・被写界深度・音を元に戻す
    if(show.age>=hold+ShowcaseOut){show.active=false;if(sun)sun->intensity=show.sun;
        params.dofFocus=show.dofFocus;params.dofRange=show.dofRange;params.dofStrength=show.dofStrength;
        Engine::Audio::GetInstance()->SetBGMDucked(false);}
    ctx.renderer->SetPostProcessParams(params);
}
// 見せ場の画面: プレイヤーの周り以外を暗くし、完成した形の名前を出す
void ChronoSystem::DrawMorphShowcase(GameContext& ctx){
    const auto& show=showcase_;UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    float hold=show.readyAt>=0?std::max(show.readyAt+ShowcaseHold,ShowcaseIn+1.2f):ShowcaseGiveUp;
    float dark=ShotEase(show.age,0,ShowcaseDark)*(1-ShotEase(show.age,hold,hold+ShowcaseOut));
    // 体の中心を画面へ写し、そこだけ丸く明るく残す
    DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(show.center.x,show.center.y,show.center.z,1),ctx.camera->View()*ctx.camera->Proj()));
    float x=640,y=360;if(clip.w>.1f){x=(clip.x/clip.w*.5f+.5f)*1280;y=(.5f-clip.y/clip.w*.5f)*720;}
    float reach=330,alpha=.82f*dark;Engine::Vector4 shade{.0f,.01f,.02f,alpha};
    ui.Image("Resources/Textures/UI/HUD/spotlight.png",{x-reach,y-reach,reach*2,reach*2},shade);
    ui.Fill({-400,-400,2080,y-reach+400},shade);ui.Fill({-400,y+reach,2080,720-y-reach+400},shade);
    ui.Fill({-400,y-reach,x-reach+400,reach*2},shade);ui.Fill({x+reach,y-reach,1280-x-reach+400,reach*2},shade);
    // 形が完成したら、名前を横から滑り込ませる
    if(show.readyAt<0)return;
    float slide=ShotEase(show.age,show.readyAt,show.readyAt+.35f),fade=slide*(1-ShotEase(show.age,hold,hold+ShowcaseOut*.6f));
    bool hydrant=show.form==LifeAction::Hydrant,signal=show.form==LifeAction::Signal;const Engine::Vector4 hazard{.95f,.76f,.19f,fade},white{.96f,.96f,.94f,fade};
    const char* name=hydrant?"消火栓":signal?"信号機":"タイヤ";const char* line=hydrant?"放水で攻撃・水の幕で防御":signal?"赤で止め、青で渡らせ、黄で弾けさせる":"溜めて突進・体当たり";
    float cx=640+(1-slide)*-120;
    ui.Fill({cx-260,598,520,4},UI::WithAlpha(hazard,.9f));
    ui.LabelCenter("NEW FORM",cx,574,16,hazard,6);
    float w=ui.Width(name,64,UI::JapaneseFont);
    ui.Text(name,cx-w*.5f+3,610,56,{0,0,0,.85f*fade},UI::JapaneseFont);ui.Text(name,cx-w*.5f,604,56,white,UI::JapaneseFont);
    ui.Center(line,cx,676,18,white,UI::JapaneseFont);
}
}
