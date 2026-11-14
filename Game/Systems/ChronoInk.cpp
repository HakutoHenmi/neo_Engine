// ChronoInk.cpp
// 新しいゲームのシミュレーションの流れ全体。
#include "ChronoSystemInternal.h"
#include "../Chrono/RoomRules.h"

namespace Game {
// インクのステージを作る: プレイヤーの状態・古いエンティティの削除・当たり判定・塗れる面・地形と景観・シェーダー・音・群れ
void ChronoSystem::BuildInk(entt::registry& r){
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::BuildInk");
    // インクのプレイヤーの状態を付け、体の量を満タンにする
    r.emplace_or_replace<InkPlayer>(player_);auto& p=r.get<Player>(player_);p.mass=SlimeMaximumMass;p.automatic=false;
    // 開幕のムービーは UpdateInk より前に動くので、描画の時点で体の量を満タンにしておく
    auto& health=r.get<HealthComponent>(player_);health.SetHp(p.mass);health.SetMaxHp(SlimeMaximumMass);health.SetDead(false);
    // ローグライトの乱数の種を時刻から決める
    r.get<InkPlayer>(player_).rogue.random=uint32_t(GetTickCount64())|1u;
    // 世界を動かし、流体のデバッグ表示を消す
    Engine::Renderer::GetInstance()->SetRogueWorldFrozen(false);
    Engine::Renderer::GetInstance()->SetDrawFluidDebugArrows(false);
    Engine::Renderer::GetInstance()->SetFluidVolumeDebugMode(0);
    // 前回作った「Chrono 」で始まるエンティティを消す
    std::vector<entt::entity> old;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)old.push_back(e);
    for(auto e:old)r.destroy(e);
    // 見えない当たり判定の箱を作る
    auto solid=[&](const std::string& name,V center,V size){
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name+" collision");
        r.emplace<TransformComponent>(e).translate=Write(center);
        r.emplace<BoxColliderComponent>(e).size=Write(size);
        r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
    };
    // 草原の土台の当たり判定
    solid("Meadow foundation",{0,-4.02f,20},{660,8,720});
    // フィールドが広くなっても跡の細かさを保つため、塗れる面を 9 枚に分ける
    auto add=[&](V origin,V u,V v,float width,float depth){InkSurface surface;surface.origin=origin;surface.u=u;surface.v=v;surface.width=width;surface.depth=depth;inkSurfaces_.push_back(surface);};
    for(float z:{-340.f,-100.f,140.f})for(float x:{-330.f,-110.f,110.f})add({x,0,z},{1,0,0},{0,0,1},220,240);
    auto* render=Engine::Renderer::GetInstance();
    // 草原は元の近距離の影を使う。街は塔のために範囲を広げる
    // 影の範囲 (草原は近距離)
    render->SetDirectionalShadowRange(100,100);
    // 太陽: シーンの「Sun」エンティティが毎フレーム光を決める。草原は元の
    // 45/30 度の太陽、街は高くして通りの向きにそろえる
    // 「Sun」エンティティの角度と明るさを設定する
    for(auto e:r.view<NameComponent,TransformComponent,DirectionalLightComponent>())if(r.get<NameComponent>(e).name=="Sun"){
        auto& t=r.get<TransformComponent>(e);auto& light=r.get<DirectionalLightComponent>(e);
        t.rotate={DirectX::XMConvertToRadians(62.f),DirectX::XMConvertToRadians(8.f),0};light.intensity=1.1f;}
    // 子分の縁取り: 見えている所と、壁越し (深度を見ない) の 2 種類
    render->CreateShaderPipelineTransparent("SlimeChildGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SlimeChildHighlightPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("SlimeChildXRay",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SlimeChildHighlightPS.hlsl",true,false);
    // 育ったときの水しぶき (しずくと霧。DevourGrowth.cpp)
    render->CreateShaderPipelineTransparent("SplashWater",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SplashWaterPS.hlsl",false,true);
    render->CreateShaderPipelineTransparent("SplashMist",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SplashMistPS.hlsl",false,true);
    // スライムが通った所のぬれ跡
    render->CreateShaderPipelineTransparent("WetTrail",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/WetTrailPS.hlsl",false,true);
    // 霧を描く四角 (XZ 面。法線 +Y をカメラへ向けて描く)
    growQuad_=render->CreateDynamicMesh({{{-1,0,-1,1},{0,0},{0,1,0}},{{1,0,-1,1},{1,0},{0,1,0}},{{1,0,1,1},{1,1},{0,1,0}},{{-1,0,1,1},{0,1},{0,1,0}}},{0,1,2,0,2,3});
    if(slimePlayerDemo_) BuildSlimePlayerDemo(r);else BuildCityStage(r);
    // こぼれた液体の管のメッシュ (DevourSpill)
    slimeBeamMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-beam.obj");
    // 敵の目や核などの光るシェーダー
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    // 誘導の液体の跡を消す
    render->SetGuidedLiquidTrail({});
    // 群れを作り、スライムの生活モードの準備をする
    BuildSwarm(r);
    BuildSlimeLife(r);
}
// インクの地面の高さ (真下へのレイで一番高い面)。surface に面の番号を返す
float ChronoSystem::InkGround(V at,int* surface)const{
    // 吸収モードの部屋は家具の高さがある
    // ギミックの看板の土台は、床の上のなだらかな坂として足す
    if(devour_&&CurrentDevourStage().map=="room"){if(surface)*surface=0;return std::max(Chrono::RoomGround(at),GimmickSignGround(at));}
    // 街のステージは y=0 で平らで、草原の塗れる面より広い
    if(!inkSurfaces_.empty()){if(surface)*surface=0;(void)at;return 0;}
    // 真下へのレイを各面に飛ばし、一番高い当たりを地面にする
    float height=-1000;int found=-1;
    for(int i=0;i<static_cast<int>(inkSurfaces_.size());++i){float f;V hit;
        if(inkSurfaces_[i].Ray({at.x,200,at.z},{0,-400,0},f,hit)&&hit.y>height){height=hit.y;found=i;}}
    if(surface)*surface=found;return height;
}
// インクのモードのカメラ: 追従・肩越し・溜めや領域での引き・消火栓の照準・壁へのめり込み防止・ボスの部位の透明化
void ChronoSystem::InkCamera(entt::registry& r,Player& p,GameContext& ctx){
    // プレイヤーの位置へなめらかに追従する
    V pos=Read(r.get<TransformComponent>(player_).translate);
    if(!cameraReady_){cameraFollow_=pos;cameraReady_=true;}
    cameraFollow_=Follow(cameraFollow_,pos,ctx.dt,1.2f);
    // バネで跳ぶ間は高さも速く変わるので、体を見失わないように追いつく
    if(devourHop_.active)cameraFollow_=Lerp(cameraFollow_,pos,1-std::exp(-20*ctx.dt));
    // カメラの前方向と右方向
    V forward=Forward(yaw_,pitch_),right{std::cos(yaw_),0,-std::sin(yaw_)};
    auto& ink=r.get<InkPlayer>(player_);
    // 撃っているか・溜めているか・その強さ
    bool firing=ink.phase==SlimePhase::Firing;
    bool charging=ink.phase==SlimePhase::Charging;
    float power=ink.Power(charging?ink.charge:ink.beamCharge);
    // 肩越しへの移動は両端をなめらかにする。狙いの入力は別に動く
    // 一定の時間で目標の値へ近づける関数
    auto approach=[&](float value,float target,float seconds){float step=std::min(ctx.dt,.05f)/seconds;return value+std::clamp(target-value,-step,step);};
    // 肩越しの量と、重い溜めの量
    float shoulderTarget=firing?1.f:charging?std::clamp(.4f+power/.35f*.6f,0.f,1.f):0.f;
    float heavyTarget=(charging||firing)?std::clamp((power-.35f)/.4f,0.f,1.f):0.f;
    ink.beamView=approach(ink.beamView,shoulderTarget,(charging||firing)?.22f:.36f);
    ink.heavyView=approach(ink.heavyView,heavyTarget,(charging||firing)?.28f:.42f);
    // なめらかに変化させた値
    float shot=Ease(ink.beamView),heavy=Ease(ink.heavyView);
    // 輪や誘導弾があるときは空からの構図へ広げる
    float domainTarget=0.f;
    // 空からの連射中は、移動や狙いを止めずに視野を広げる
    ink.cameraDomainView=approach(ink.cameraDomainView,domainTarget,domainTarget>0?.65f:.9f);
    float domainView=Ease(ink.cameraDomainView);
    // スライムの生活モードでは、体の大きさに合わせてカメラを引く
    LifeCameraSubject subject{};
    {subject=GetLifeCameraSubject();
        // 液体と武器の形では映す範囲 (中心と半径) が大きく変わる。形が変わったら、今の構図から新しい構図へイージングでつなぐ
        // 部屋のギミック (だるま落とし) の間は、体とだるまの両方が入る広い構図
        const auto& body=life_.bodies[0];int kind=LifeWeaponForm(body.action)?int(body.action):0;if(devour_&&GimmickCameraSubject(subject))kind=1000;V offset=subject.center-pos;auto& blend=cameraBlend_;
        if(blend.kind<0)blend={kind,1,offset,subject.radius,offset,subject.radius};
        else if(kind!=blend.kind){blend.kind=kind;blend.age=0;blend.fromOffset=blend.offset;blend.fromRadius=blend.radius;}
        blend.age=std::min(1.f,blend.age+std::min(ctx.dt,.05f)/Data::SlimeSettings()->cameraBlendSeconds);float eased=ShotEase(blend.age,0,1);
        blend.offset=Lerp(blend.fromOffset,offset,eased);blend.radius=blend.fromRadius+(subject.radius-blend.fromRadius)*eased;
        subject.center=pos+blend.offset;subject.radius=blend.radius;}
    float growthPullback=LifeCameraDistance(subject.radius,1.13f,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y))-17.f;
    // 引く量の合計をなめらかに反映する
    float pullback=growthPullback+((charging||firing)?3*power:0.f)+domainView*14+(devour_?DevourCameraKick(devourGulp_.age,devourGulp_.weight)*subject.radius:0.f)
        // バネで跳ぶ間は引いて、町を見渡す
        +(devourHop_.active?.9f*subject.radius*std::sin(3.1415927f*devourHop_.age/devourHop_.time):0.f);
    ink.cameraPullback+=(pullback-ink.cameraPullback)*(1-std::exp(-10*ctx.dt));
    // 注視点 (スライムの生活モードは体の中心)
    V focus=cameraFollow_+(subject.center-pos);
    // 消火栓の形か
    bool aimingHydrant=life_.bodies[0].action==LifeAction::Hydrant;
    // 肩越しの構図へ入るときも戻るときも、同じ時間をかけてなめらかに移る
    cameraHydrant_=std::clamp(cameraHydrant_+(aimingHydrant?1.f:-1.f)*std::min(ctx.dt,.05f)/Data::SlimeSettings()->cameraBlendSeconds,0.f,1.f);
    bool hydrant=cameraHydrant_>0;float hydrantBlend=ShotEase(cameraHydrant_,0,1);
    // カメラを置きたい位置
    V wanted=focus-forward*(17.f+ink.cameraPullback+heavy*5)+V{0,shot*1.2f+heavy*3+domainView*6,0};(void)right;
    // 壁やインクの面でカメラが埋まらないよう手前に寄せる
    float f;V n;
    // スライムの生活モードでは、消火栓の間もいつもの構図を壁で守って計算し続け、肩越しの構図との間を補間する
    // (片方だけを計算すると、切り替えた瞬間に壁際の持ち上げの分だけカメラが跳ぶ)
    {
        // 注視点からの向きへ太い箱を掃引し、遮られずに届く割合を返す。注視点を含む箱は無視する
        auto reach=[&](V direction,float distance){float best=1;V delta=direction*distance;
            auto test=[&](Chrono::Bounds b){if(Chrono::Sweep(focus,delta,b,{.9f,.9f,.9f},f,n)&&f>0&&f<best)best=f;};
            for(const auto& box:solids_)test(box);
            if(!cityGates_.barricadeOpen){const auto& b=CityBarricade();test({V{b.x0,0,b.z0-4},V{b.x1,14,b.z1}});}
            for(size_t i=0;i<CityWalls().size();++i)if(!cityGates_.WallOpen(i)){const auto& w=CityWalls()[i].rect;test({V{w.x0,0,w.z0},V{w.x1,14,w.z1}});}
            if(!cityGates_.gateOpen)test({CityGateCenter()+V{-32,0,-1.5f},CityGateCenter()+V{32,58,1.5f}});
            return best;};
        // 見上げたときに置きたい位置が路面の下へ潜ると、掃引が建物の箱の下をくぐって
        // 後から高さを戻したときに建物の中へ入るので、先に路面の上へ戻しておく
        wanted.y=std::max(wanted.y,.7f);
        float distance=std::max(Length(wanted-focus),.01f);V back=(wanted-focus)*(1/distance);
        // 後ろが塞がれていたら、届くまで上へ傾けた向きを試す (路地の壁際では体の上から見下ろす)
        auto lifted=[&](float k){return Unit(back+V{0,k,0});};
        const float lifts[]={0,.35f,.8f,1.5f,3};float goal=0,best=-1;
        for(float k:lifts){float open=reach(lifted(k),distance);if(open>best+.02f){best=open;goal=k;}if(open>=.97f)break;}
        // 吸収モード (輪の町・部屋): 建物や家具や部屋の壁はカメラの手前へ寄せず、間に入った物を透かす (下の CameraOccluder)。
        // カメラの位置と向きは操作の通りに保つ (壁際で寄せたり持ち上げたりすると、視点の操作と画面の動きが合わなくなる)
        if(devour_){goal=0;best=1;}
        // 持ち上げは少し速く、戻すのはゆっくり (速すぎると変形で構図が広がった瞬間に跳ね上がる)。届く距離は縮めるときは即座に、伸ばすときはゆっくり
        cameraLift_+=(goal-cameraLift_)*(1-std::exp(-(goal>cameraLift_?4.f:2.5f)*ctx.dt));
        V direction=lifted(cameraLift_);float open=devour_?1.f:reach(direction,distance);
        cameraReach_=open<cameraReach_?open:cameraReach_+(open-cameraReach_)*(1-std::exp(-3*ctx.dt));
        wanted=focus+direction*std::max(.5f,cameraReach_*distance-.3f);
        // 消火栓のときは肩越しの照準の位置へ移る
        if(hydrant){
            // 高さは体に合わせ、ピッチとは別にする。上を見てもカメラが
            // 路面へ下がらないようにし、視線はカメラの右に空ける
            V flat{std::sin(yaw_),0,std::cos(yaw_)};
            V shoulderFocus=pos+V{0,-1.25f+4.3f*LifeScale(life_.bodies[0].mass),0},shoulderAt=pos+LifeHydrantCameraOffset(life_.bodies[0].mass,flat);
            if(!devour_)for(const auto& box:solids_)if(Chrono::Sweep(shoulderFocus,shoulderAt-shoulderFocus,box,{.3f,.3f,.3f},f,n))shoulderAt=shoulderFocus+(shoulderAt-shoulderFocus)*std::max(.05f,f-.025f);
            wanted=Lerp(wanted,shoulderAt,hydrantBlend);focus=Lerp(focus,shoulderFocus,hydrantBlend);
        }
    }
    for(const auto& surface:inkSurfaces_){V hit;if(surface.Ray(focus,wanted-focus,f,hit))wanted=focus+(wanted-focus)*std::max(.05f,f-.06f);}
    wanted.y=std::max(wanted.y,.7f);
    // 吸収モードの部屋: カメラは壁の外へ出てよい (間の壁は透かす)。部屋の外の床 (RoomStage.cpp) の範囲と天井の下には収める
    if(devour_&&CurrentDevourStage().map=="room"){float h=CurrentDevourStage().half+Chrono::RoomOutsideFloor-10;wanted.x=std::clamp(wanted.x,-h,h);wanted.z=std::clamp(wanted.z,-h,h);wanted.y=std::min(wanted.y,290.f);}
    // それでも建物の箱の中に残ったら、外へ出るまで注視点の方へ寄せる
    if(!devour_&&hydrantBlend<1){
        auto inside=[&](V at){for(const auto& b:solids_)if(at.x>b.min.x-.2f&&at.x<b.max.x+.2f&&at.y>b.min.y-.2f&&at.y<b.max.y+.2f&&at.z>b.min.z-.2f&&at.z<b.max.z+.2f)return true;return false;};
        for(int i=0;i<20&&inside(wanted);++i)wanted=Lerp(wanted,focus,.15f);
    }
    // 撃っているときはビームの方向を見る
    V beamForward=firing?Forward(ink.beamYaw,ink.beamPitch):forward;
    ink.cameraBeamDirection=Unit(Lerp(ink.cameraBeamDirection,beamForward,1-std::exp(-14*ctx.dt)));
    // 注視の向き (構図の量で補間する)
    V lookAt=focus+ink.cameraBeamDirection*(45-heavy*20)+V{0,domainView*20,0};
    V view=Unit(Lerp(forward,Unit(lookAt-wanted),std::max(shot,domainView*.8f)));
    // スライムの生活モードでは注視点を向く (消火栓のときは前方)
    view=Unit(Lerp(Unit(focus-wanted),forward,hydrantBlend));
    if(aimingHydrant)ctx.camera->StopShake();
    // 吸収モードの結果: 本物の体を測れるよう、横から全身を見る構図へゆっくり移る。体は画面の右、地面は巻尺の高さに置く
    float resultBlend=0;constexpr float resultFov=.62f;
    if(!(devour_&&finished_))ctx.renderer->SetFluidInterior(0,0);
    if(devour_&&finished_){
        resultCameraAge_+=std::min(ctx.dt,.05f);resultBlend=ShotEase(std::min(1.f,resultCameraAge_/1.25f),0,1);
        const float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),half=std::tan(resultFov*.5f);
        if(resultCameraAge_<kResultSettledAge)MeasureDevourBody(ctx);
        // 体を囲む箱の 8 隅が、記録の面の右で巻尺の上の枠に入る一番近い位置を探す。手前の隅ほど大きく低く映るので、奥行きも入れて調べる
        constexpr float pitch=.13f,frameLeft=-.2f,frameRight=.92f,frameBottom=-.72f,frameTop=.7f;
        const float ground=InkGround(resultOrigin_),tanX=half*aspect,tanY=half;
        const V look=Forward(yaw_,pitch),side{std::cos(yaw_),0,-std::sin(yaw_)},flat{std::sin(yaw_),0,std::cos(yaw_)},up{std::sin(yaw_)*std::sin(pitch),std::cos(pitch),std::cos(yaw_)*std::sin(pitch)};
        float slideX=0,slideY=0;
        auto fits=[&](float away){float xLow=-1e9f,xHigh=1e9f,yLow=-1e9f,yHigh=1e9f;
            for(int corner=0;corner<8;++corner){const V at=side*(corner&1?resultHi_:resultLo_)+flat*(corner&2?resultFar_:resultNear_)+V{0,(corner&4?resultTopY_:ground)-resultOrigin_.y,0};
                const float x=Dot(at,side),y=Dot(at,up),z=Dot(at,look)+away;if(z<.1f)return false;
                xLow=std::max(xLow,x-frameRight*z*tanX);xHigh=std::min(xHigh,x-frameLeft*z*tanX);yLow=std::max(yLow,y-frameTop*z*tanY);yHigh=std::min(yHigh,y-frameBottom*z*tanY);}
            if(xLow>xHigh||yLow>yHigh)return false;
            // 横は枠の中央、縦は一番低い隅を巻尺の上に置く
            slideX=(xLow+xHigh)*.5f;slideY=yHigh;return true;};
        float tooClose=0,distance=4;for(int i=0;i<24&&!fits(distance);++i){tooClose=distance;distance*=1.5f;}
        for(int i=0;i<18;++i){const float middle=(tooClose+distance)*.5f;if(fits(middle))distance=middle;else tooClose=middle;}
        fits(distance);
        ctx.camera->StopShake();ctx.camera->SetHandheld(0);
        wanted=Lerp(wanted,resultOrigin_+side*slideX+up*slideY-look*distance,resultBlend);view=Unit(Lerp(view,look,resultBlend));wanted.y=std::max(wanted.y,.7f);
        // 測り終えたら体の中へ入る (DevourShelf.cpp)
        DevourShelfCamera(ctx,wanted,view,ground);
        // カメラと体の間にある物は隠す (やり直しで元に戻す)
        V toBody=resultOrigin_-wanted;toBody.y=0;const float span=Length(toBody);
        if(span>.01f){toBody=toBody*(1/span);V across{toBody.z,0,-toBody.x};const float lane=(resultHi_-resultLo_)*.6f;
            for(const auto& prop:life_.props){if(!prop.active||prop.visual==0xffffffffu)continue;V d=prop.at-wanted;d.y=0;const float along=Dot(d,toBody);
                if(along<=0||along>=span||std::abs(Dot(d,across))>lane+prop.radius)continue;
                auto entity=entt::entity(prop.visual);if(r.valid(entity)&&r.all_of<MeshRendererComponent>(entity)&&r.get<MeshRendererComponent>(entity).enabled){r.get<MeshRendererComponent>(entity).enabled=false;resultHidden_.push_back(entity);}}}
    }
    // カメラへ位置と回転を反映する
    ctx.camera->SetPosition(Write(wanted));
    ctx.camera->SetRotation(-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0);
    // 画角 (泳いでいるとき・溜め・輪で広げる)
    float goal=(ink.swimming?1.22f:1.13f)+heavy*.06f+domainView*.12f;fov_+=(goal-fov_)*(1-std::exp(-6*ctx.dt));
    // 7 度の素早い揺れを加え、0.4 秒以内に通常のカメラへ完全に戻す
    ctx.camera->SetProjection(fov_+.12f*SlimePerfectPulse(ink.perfectAge)+(resultFov-fov_)*resultBlend,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),
        // 大きくなってカメラが引くほど、遠くまで描く (体が描く範囲の外へ出て消えないように)
        LifeCameraNear+ink.cameraPullback*.002f,2000+4*ink.cameraPullback);
    // 消火栓のときは揺れを止める
    ctx.camera->SetHandheld(aimingHydrant?0.f:.008f);
    // 吸収モードで大きくなったら、影を落とす範囲も広げる
    if(devour_)ctx.renderer->SetDirectionalShadowRange(460+1.5f*ink.cameraPullback,1800+3*ink.cameraPullback,.3f);
    ctx.camera->SetShakeStrength(aimingHydrant?0.f:shakeSetting_*.5f);
    // 近いほどプレイヤーを半透明にする
    p.cameraOpacity=std::clamp((Length(wanted-pos)-2.5f)/5.f,.2f,1.f);
    // 吸収モードの輪の町: カメラからプレイヤーの体までの間にある建物と、カメラが中に入った建物を透かす
    // (体の上の方まで見通せるよう、線は体の中心と頭の 2 本を調べる)
    if(devour_){V body=pos,head=focus;float bodyRadius=LifeRadii(life_.bodies[0],life_.recipe).x;
        auto boxOf=[](V center,V size){return Chrono::Bounds{center-size*.5f,center+size*.5f};};
        auto contains=[](const Chrono::Bounds& b,V p,float pad){return p.x>b.min.x-pad&&p.x<b.max.x+pad&&p.y>b.min.y-pad&&p.y<b.max.y+pad&&p.z>b.min.z-pad&&p.z<b.max.z+pad;};
        // カメラが箱の中にいるか、カメラから体 (中心と頭) までの線が箱を通るか
        auto blocks=[&](const Chrono::Bounds& box){float fraction;V normal;const V pad{1,1,1};
            return contains(box,wanted,1)||(Chrono::Sweep(wanted,body-wanted,box,pad*(.3f+bodyRadius*.3f),fraction,normal)&&fraction<1)||(Chrono::Sweep(wanted,head-wanted,box,pad,fraction,normal)&&fraction<1);};
        for(auto e:r.view<CameraOccluder>()){auto& o=r.get<CameraOccluder>(e);
            const auto box=boxOf(o.center,o.size);bool occludes=blocks(box);
            // 乗っている家具は消さない。真上にある物 (ベッドの板) は消す
            if(body.y>o.center.y+o.size.y*.5f-2)occludes=contains(box,wanted,1);
            // 体がその物の範囲の中にいるとき (階段やレールや布団を登っているとき) は薄くしない
            if(contains(box,body,0))occludes=false;
            // 脚や頭の板・枕: 間に入ったときと、カメラが中に入ったときに物ごと薄くする (体がその中にいるときは除く)。
            // 上に乗っている部分 (枕など) は、本体の箱と同じくカメラが中に入ったときだけ薄くする
            // (乗っている面とカメラへの線が触れるので、そのままだと乗った瞬間にベッドごと透けてしまう)
            for(const auto& part:o.parts){const auto partBox=boxOf(part.center,part.size);if(contains(partBox,body,0))continue;
                // (体の中心よりわずかに高いだけの部分 (掛け布団の上から見た枕) も、体を隠さないので同じに扱う)
                const bool onTop=body.y>partBox.max.y-2-bodyRadius*.5f;if(onTop?contains(partBox,wanted,1):blocks(partBox)){occludes=true;break;}}
            o.opacity+=((occludes?.18f:1.f)-o.opacity)*(1-std::exp(-12.f*ctx.dt));}}
}

}
