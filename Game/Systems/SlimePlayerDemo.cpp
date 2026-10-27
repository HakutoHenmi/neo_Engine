// Dedicated practice field; movement, deformation and water use the main game rules.
#include "ChronoSystemInternal.h"

namespace Game {
void ChronoSystem::BuildSlimePlayerDemo(entt::registry& r) {
    auto* renderer=Engine::Renderer::GetInstance();
    renderer->SetDirectionalShadowRange(110,160);
    std::vector<entt::entity> unused;
    for(auto e:r.view<CreaturePart>())unused.push_back(e);
    for(auto e:unused)r.destroy(e);
    cityProps_.clear();cityGateEntity_=entt::null;cityGateProp_=-1;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();
    inkSurfaces_.clear();
    InkSurface surface;surface.origin={-70,0,-50};surface.u={1,0,0};surface.v={0,0,1};surface.width=140;surface.depth=160;
    inkSurfaces_.push_back(surface);
    auto box=[&](const std::string& name,V at,V size,Engine::Vector4 color){
        auto e=Mesh(r,"Demo "+name,grass,at,size);auto& mesh=r.get<MeshRendererComponent>(e);
        mesh.textureHandle=white_;mesh.texturePath="Resources/Textures/white1x1.png";mesh.color={color.x,color.y,color.z,color.w};mesh.shaderName="Default";
        r.remove<CameraOccluder>(e);return e;
    };
    box("practice floor",{0,-.3f,30},{140,.6f,160},{.24f,.31f,.36f,1});
    for(int x=-60;x<=60;x+=10)box("grid X "+std::to_string(x),{float(x),.012f,30},{.045f,.02f,158},{.42f,.53f,.57f,1});
    for(int z=-40;z<=100;z+=10)box("grid Z "+std::to_string(z),{0,.013f,float(z)},{138,.02f,.045f},{.42f,.53f,.57f,1});
    box("runway left",{-6,.025f,45},{.2f,.02f,110},{.95f,.76f,.2f,1});
    box("runway right",{6,.025f,45},{.2f,.02f,110},{.95f,.76f,.2f,1});
    for(int z=0;z<=90;z+=10)box("distance marker "+std::to_string(z),{0,.028f,float(z)},{10,.02f,.18f},{.7f,.85f,.9f,1});
    box("west edge",{-70,.3f,30},{.3f,.6f,160},{.1f,.65f,.7f,1});
    box("east edge",{70,.3f,30},{.3f,.6f,160},{.1f,.65f,.7f,1});
    box("north edge",{0,.3f,110},{140,.6f,.3f},{.1f,.65f,.7f,1});
    box("south edge",{0,.3f,-50},{140,.6f,.3f},{.1f,.65f,.7f,1});
}
ChronoSystem::V ChronoSystem::ConstrainLifeMovement(V from,V to,float radius)const {
    if(!slimePlayerDemo_)return CityConstrain(from,to,radius,cityGates_);
    to.x=std::clamp(to.x,-69.f+radius,69.f-radius);
    to.z=std::clamp(to.z,-49.f+radius,109.f-radius);return to;
}
void ChronoSystem::UpdateSlimePlayerDemo(entt::registry& r,Player& player,GameContext& ctx) {
    if(!ctx.input)return;
    auto& body=life_.bodies[0];
    const auto* control=r.try_get<ControlFrame>(player_);
    if(ctx.input->Trigger(DIK_H)||(control&&control->demoHit)) {
        player.invincible=0;
        DamageSlimeLife(r,player,std::min(20.f,std::max(0.f,body.health-1)),ctx,body.at+V{0,0,4});
    }
    const int step=int(ctx.input->Trigger(DIK_EQUALS))-int(ctx.input->Trigger(DIK_MINUS));
    if(step) {
        body.mass=body.health=body.maxHealth=std::clamp(body.mass+float(step)*100.f,100.f,1600.f);
        if(LifeWeaponForm(body.action)&&!LifeCanMorph(body,body.action))LifeReturnToSlime(body);
        life_.absorbedMass=std::max(life_.absorbedMass,body.mass-LifeStartMass);++life_.revision;
    }
}
void ChronoSystem::DrawSlimePlayerDemoUI(GameContext& ctx) {
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const auto& b=life_.bodies[0];
    const char* selected=life_.form==LifeAction::Tire?"TIRE":life_.form==LifeAction::Hydrant?"HYDRANT":"SIGNAL";
    ui.Rounded({22,20,510,152},{.025f,.045f,.055f,.88f},16);
    ui.Label("SLIME PLAYER / PRACTICE FIELD",40,35,20,UI::Lime);
    ui.Text("移動・ジャンプ・変形を自由に試せます",40,66,20,UI::Paper,UI::JapaneseFont);
    ui.Label("MASS "+std::to_string(int(b.mass))+"   CHILDREN "+std::to_string(life_.ChildCount())+"   SELECT "+selected,40,104,15,UI::Gold);
    const char* status=!LifeWeaponForm(b.action)?"SLIME":!b.weapon.ready?"FORMING":b.weapon.active?"ATTACK":"READY";
    ui.Label(std::string("STATE: ")+status,40,132,15,UI::Paper);
    ui.Rounded({22,559,1236,139},{.025f,.045f,.055f,.88f},16);
    ui.Text("WASD 移動 / マウス 視点 / Space ジャンプ / 右クリック 吸収・水の防御",40,575,19,UI::Paper,UI::JapaneseFont);
    ui.Text("1 タイヤ / 2 消火栓 / 3 信号機 → 左クリックで変形・攻撃 / タイヤは長押しで溜め突進 / R 戻る",40,609,18,UI::Paper,UI::JapaneseFont);
    ui.Text("H 被弾分裂 / -・= 大きさ変更 / F5 試験場リセット / Esc ポーズ",40,647,18,UI::Lime,UI::JapaneseFont);
    if(life_.messageAge>0)ui.Center(life_.message,640,170,17,UI::Gold,UI::JapaneseFont);
    DrawLifeChildMarkers(ctx);
}
}

