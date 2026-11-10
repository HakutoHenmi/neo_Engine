// CityStage.cpp
// スライムの街のステージ (Docs/SlimeLife.md の「街ステージ」)。
// 動かない建物は PBR の材質ごとに 1 つの動的メッシュとして作る。小物・
// バリケード・シャッターの門・街の敵は、Resources/Credits/CityStage.md にある
// CC0 の Poly Haven の素材を使う
#include "CityBuilder.h"

namespace Game {
using namespace CityParts;
// 街のステージを作る: 影の範囲・シェーダー・材質・道路と横断歩道・建物・小物・バリケード・門・敵のモデル
void ChronoSystem::BuildCityStage(entt::registry& r){
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::BuildCityStage");
    // 小物の LOD は予約だけして組み立てを進め、関数を抜けるときにまとめて並列で作る
    struct AssetBatch{AssetBatch(){Engine::Renderer::GetInstance()->BeginAssetBatch();}~AssetBatch(){Engine::Renderer::GetInstance()->EndAssetBatch();}} assetBatch;
    using namespace Chrono;
    // 影の範囲を広げる
    auto* render=Engine::Renderer::GetInstance();
    // 塔は約 600 の高さがある。カメラの前に広く深い影を取って、影が欠けないようにする
    render->SetDirectionalShadowRange(460,1800,.3f);
    // 景観・街の面・ガラス・鎧・光・強調のシェーダー
    render->CreateShaderPipeline("EnvironmentSurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/EnvironmentSurfacePS.hlsl");
    // 通りの谷間のために空の光を足した環境の陰影。動的メッシュは範囲を持たないので、
    // 「EnvironmentSurface」で行う景観の個別カリングに入れてはいけない
    render->CreateShaderPipeline("CitySurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CitySurfacePS.hlsl");
    render->CreateShaderPipeline("LowpolySurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/LowpolySurfacePS.hlsl");
    render->CreateShaderPipeline("LowpolyNoShadow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/LowpolySurfacePS.hlsl");
    render->CreateShaderPipeline("CityGlass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityGlassPS.hlsl");
    render->CreateShaderPipeline("CityArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityArmorPS.hlsl");
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("MorphItemHighlight",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MorphItemHighlightPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("SpillLiquid",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SpillLiquidPS.hlsl",false,true);
    render->CreateShaderPipelineTransparent("GimmickCurtain",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/GimmickCurtainPS.hlsl",false,true);
    // 街の面のエンティティの名前 (材質ごと)
    const char* surfaceIds[CityMaterialCount]={"asphalt_02","large_grey_tiles","concrete_floor_worn_001","concrete_floor_worn_001","concrete_floor_worn_001",
        "concrete_wall_008","exterior_wall_cladding_02","rectangular_facade_tiles_02","concrete_tile_facade","concrete_slab_wall","grey_tiles",
        "painted_metal_shutter","rusty_metal_shutter","concrete_floor_worn_001","concrete_floor_worn_001","white_bricks","medieval_wood","castle_wall_slates","asphalt_02",
        "brick_wall_02","plastered_wall_02","weathered_plank_siding","clay_roof_tiles","grey_roof_tiles","roof_slates_02","patterned_paving","square_brick_paving",
        "acg/Facade001","acg/Facade003","acg/Facade005","acg/Facade006","acg/Facade012","acg/Facade014","acg/Facade018A","acg/Facade019A","acg/Facade020A"};
    // 材質: 吸収モード (部屋・住宅街) は写真の材質を使わず、すべて単色のローポリで描く
    // (色は FlatCityColor。法線は平ら、粗さと AO は白)
    std::array<CityModel,CityMaterialCount> surfaces;
    {CityModel flat;flat.texture=white_;flat.extra={render->LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png",false),white_,white_};surfaces.fill(flat);}
    Engine::LoadProfile::Record("mark","city: surfaces requested",0);
    // 材質ごとの色の補正
    auto tint=[](int i)->DirectX::XMFLOAT4{
        // 塗装は明るく、黄色の塗装・ガラス・金属・道路は色を変える
        switch(i){case CityPaint:return {2.5f,2.5f,2.45f,1};case CityPaintYellow:return {2.5f,1.85f,.45f,1};case CityAlleyFloor:return {1.3f,1.28f,1.25f,1};case CityGlass:return {.2f,.26f,.33f,1};
            case CityMetal:return {.24f,.25f,.27f,1};case CityRoad:return {.85f,.85f,.85f,1};case CityScorch:return {.16f,.14f,.13f,1};default:return {1,1,1,1};}};
    // 敵や破片に使う材質を記録する
    cityWood_=surfaces[CityWood];cityStoneSkin_=surfaces[CityStoneSkin];cityIron_=surfaces[CityShutter];cityArmor_=surfaces[CityMetal];cityBlock_=surfaces[CityConcrete];
    // 光る看板の一覧
    cityGlows_.clear();
    // 部屋か輪の町の地面と道路を作る
    CityBuilder city;
    if(CurrentDevourStage().map=="room")BuildRoomGround(city);else BuildRingGround(city);
    Engine::LoadProfile::Record("mark","city: geometry built",0);
    // 材質ごとのメッシュをエンティティとして置く
    for(int i=0;i<CityMaterialCount;++i){auto& g=city.geometry[size_t(i)];if(g.indices.empty())continue;
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City surface ")+surfaceIds[i]);
        auto& t=r.emplace<TransformComponent>(e);t.translate={0,0,0};t.scale={1,1,1};t.rotate={0,0,0};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.useCubemap=false;
        mr.textureHandle=surfaces[size_t(i)].texture;mr.extraTextureHandles=surfaces[size_t(i)].extra;mr.shaderName=i==CityGlass?"CityGlass":"CitySurface";mr.color=tint(i);
        // 地面も単色にして、ローポリの小物と見た目をそろえる
        {auto flat=FlatCityColor(i);mr.textureHandle=white_;mr.extraTextureHandles={surfaces[size_t(i)].extra[0],white_,white_};mr.shaderName="LowpolySurface";mr.color={flat.x,flat.y,flat.z,1};}}
    // 小物と見た目の記録を消す
    Engine::LoadProfile::Record("mark","city: surface entities",0);
    cityProps_.clear();cityVisuals_.clear();
    // 輪の町 (RingCity.cpp) か部屋 (RoomStage.cpp) の建物・小物・アイテムを置く
    cityWalls_.clear();cityBarricade_.clear();cityGateEntity_=entt::null;
    RingCityTools tools{[&](int i)->const CityModel& {return surfaces[size_t(i)];},tint};
    if(CurrentDevourStage().map=="room")BuildRoomStage(r,tools);else BuildRingCity(r,tools);
    Engine::LoadProfile::Record("mark","city: props placed",0);
    // 板・ゴーレムの部品・矢・破片・光る看板に共通で使う単位立方体
    std::vector<Engine::VertexData> cube;std::vector<uint32_t> cubeIndex;
    // 単位立方体のメッシュ (面ごとに 0〜1 の UV)
    {CityBuilder unit;unit.tile.fill(1);unit.Box(0,{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true);cube=unit.geometry[0].vertices;cubeIndex=unit.geometry[0].indices;
        for(auto& v:cube){// 面ごとに 0〜1 の UV
            float ax=std::abs(v.normal.x),ay=std::abs(v.normal.y);
            v.texcoord=ay>.5f?DirectX::XMFLOAT2{v.position.x+.5f,v.position.z+.5f}:ax>.5f?DirectX::XMFLOAT2{v.position.z+.5f,.5f-v.position.y}:DirectX::XMFLOAT2{v.position.x+.5f,.5f-v.position.y};}}
    cityCube_=render->CreateDynamicMesh(cube,cubeIndex);
    // 門・弾・破片などの状態を初期化する
    cityGates_={};cityBarricadeHealth_=CityBarricadeHealth();cityGateHealth_=CityGateHealth();cityBarricadeShake_=cityGateShake_=0;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();cityGateProp_=-1;cityAnnounced_=-1;ResetCityHazards();
}
// 路地の壁の体力と火事場の炎を最初の状態に戻す
void ChronoSystem::ResetCityHazards(){
    using namespace Chrono;
    for(size_t i=0;i<cityWalls_.size()&&i<CityWalls().size();++i){cityWalls_[i].health=CityWalls()[i].health;cityWalls_[i].shake=0;cityWalls_[i].hit=0;}
    cityFires_.clear();for(const auto& spot:devour_?RingData().fires:CityFires())cityFires_.push_back({spot.at,spot.radius,CityFireHealth(),0});
    cityHammerAnnounced_=false;
}
// 路地の壁を壊す (部品を破片にして飛ばし、通れるようにする。すべて壊れたら大通りへ)
void ChronoSystem::BreakCityWall(size_t index,bool absorbed){
    using namespace Chrono;
    if(index>=CityWalls().size()||cityGates_.WallOpen(index))return;
    cityGates_.wallsOpen|=1u<<index;
    // 部品の一部をそのまま破片にして飛ばす (長い部品は短く切る)。飲み込んだときは破片を出さない
    if(index<cityWalls_.size()&&!absorbed){int i=0;for(const auto& piece:cityWalls_[index].pieces){if(i++%2)continue;
        float a=CityNoise(i*7+int(index)*13)*6.283f,u=CityNoise(i*13+int(index));V size=piece.size;size.x=std::min(size.x,6.f);
        cityDebris_.push_back({piece.at,V{std::cos(a)*6*(.4f+u),10+u*14,std::sin(a)*6*(.4f+u)},V{u*6,a,u*4},size,piece.rotate,0,piece.material});}}
    const auto& wall=CityWalls()[index];int left=0;for(size_t i=0;i<CityWalls().size();++i)if(!cityGates_.WallOpen(i))++left;
    const char* how=absorbed?"を丸のみ！":"を破壊！";
    life_.message=left?wall.name+how+" 残り "+std::to_string(left)+" か所":wall.name+how+" 路地を抜けて大通りへ";life_.messageAge=3;
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
// バリケードか門を壊す (破片を飛ばし、通れるようにしてメッセージを出す)
void ChronoSystem::BreakCityGate(entt::registry& r,bool barricade,bool absorbed){
    using namespace Chrono;
    // 破片を飛ばす
    auto burst=[&](V at,V size,int material,int count,float spread){
        for(int i=0;i<count;++i){float a=CityNoise(i*7+int(at.x))*6.283f,u=CityNoise(i*13+int(at.z));
            V velocity{std::cos(a)*spread*(.4f+u),14+u*18,std::sin(a)*spread*(.4f+u)+16};
            cityDebris_.push_back({at+V{(CityNoise(i*3)-.5f)*size.x,CityNoise(i*5)*size.y,0},velocity,V{u*6,a,u*4},
                V{1.2f+u*3,.5f+u*.6f,.5f+u*.4f},{},0,material});}
    };
    // バリケード: 開けて破片を飛ばす
    if(barricade){cityGates_.barricadeOpen=true;if(!absorbed)burst(CityBarricadeCenter()+V{0,4,0},{44,14,1},0,26,10);
        life_.message=absorbed?"バリケードを丸のみ！ 大火事現場へ":"バリケード突破！ 大火事現場へ";life_.messageAge=3;}
    // 門: 開けて当たり判定と表示を消す (取り込んだときは破片なし)
    else{cityGates_.gateOpen=true;
        if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=false;
        if(r.valid(cityGateEntity_))r.get<MeshRendererComponent>(cityGateEntity_).enabled=false;
        if(cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size())life_.props[size_t(cityGateProp_)].active=false;
        if(!absorbed)burst(CityGateCenter()+V{0,10,0},{60,50,1},1,30,14);
        life_.message=absorbed?"シャッターごと吸い込んだ！ 事故多発交差点へ":"シャッターを破壊！ 事故多発交差点へ";life_.messageAge=3;}
    // 壊れた音
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
// スライムの武器が路地の壁・バリケード・門に当たったか (路地の壁はハンマーだけで壊せる。門は火事場を制圧した後だけ)
void ChronoSystem::CityWeaponHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,int pulse,uint64_t hitKey){
    using namespace Chrono;
    // 路地の壁: 壁に沿って並べた点のどこかに武器が触れたら、ハンマーなら体力を減らす
    for(size_t i=0;i<CityWalls().size()&&i<cityWalls_.size();++i){if(cityGates_.WallOpen(i)||cityWalls_[i].hit==hitKey)continue;
        const auto& wall=CityWalls()[i];auto& state=cityWalls_[i];bool touched=false;
        for(float s:{-.35f,0.f,.35f})for(float y:{2.f,6.f})touched|=LifeWeaponTouches(b,wall.Center()+wall.Axis()*(s*wall.Span())+V{0,y,0},6,pulse);
        if(!touched)continue;state.hit=hitKey;
        if(b.action!=LifeAction::Hammer){life_.message=wall.name+"はハンマーで叩き壊せる";life_.messageAge=2;continue;}
        state.health-=LifeWeaponDamage(b);state.shake=.35f;
        LifeImpact(ctx,ImpactStructureHit,wall.Center()+V{0,4,0});swarmEffects_.push_back({wall.Center()+V{0,4,0},0,8,2});
        auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,.55f*audio->GetMasterSEVolume(),.8f);
        if(state.health<=0){BreakCityWall(i);LifeImpact(ctx,ImpactStructureBreak,wall.Center()+V{0,4,0});}
        else{life_.message=wall.name+" 残り "+std::to_string(int(std::ceil(state.health/wall.health*100)))+"%";life_.messageAge=1.2f;}}
    // 武器が中心から左右に並んだ点のどこかに触れたか
    auto touches=[&](V center,float y,float radius){for(float x:{-16.f,0.f,16.f})if(LifeWeaponTouches(b,center+V{x,y,0},radius,pulse))return true;return false;};
    // バリケード: ハンマーなら体力を減らし、尽きたら壊す。それ以外の武器は案内を出す
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touches(CityBarricadeCenter(),6,8)){cityBarricadeHit_=hitKey;
        if(b.action==LifeAction::Hammer){cityBarricadeHealth_-=LifeWeaponDamage(b);cityBarricadeShake_=.35f;
            LifeImpact(ctx,ImpactStructureHit,CityBarricadeCenter()+V{0,4,0});swarmEffects_.push_back({CityBarricadeCenter()-V{0,0,4},0,10,2});
            if(cityBarricadeHealth_<=0){BreakCityGate(r,true,false);LifeImpact(ctx,ImpactStructureBreak,CityBarricadeCenter()+V{0,4,0});}
            else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth()*100)))+"%";life_.messageAge=1.2f;}}
        else{life_.message="道路封鎖のバリケードはタイヤの溜め突進で壊せる";life_.messageAge=2;}
    }
    // 門: 騎士団を倒すまでは案内だけ。それ以外は体力を減らし、尽きたら壊す
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&(touches(CityGateCenter(),10,10)||touches(CityGateCenter()+V{0,0,-3},30,10))){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="兵士がシャッターを守っている / 先に火事場を制圧しよう";life_.messageAge=2;return;}
        cityGateHealth_-=LifeWeaponDamage(b)*(b.action==LifeAction::Hammer?1.4f:1.f);cityGateShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityGateCenter()+V{0,10,0});swarmEffects_.push_back({CityGateCenter()-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0){BreakCityGate(r,false,false);LifeImpact(ctx,ImpactStructureBreak,CityGateCenter()+V{0,10,0});}
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth()*100)))+"% / 本体"+std::to_string(int(CityGateAbsorbMass()))+"ならシャッターごと吸い込める";life_.messageAge=1.4f;}
    }
}
// タイヤの突進がバリケードや門に当たったか (溜めが強いほど大きく削る。門は火事場を制圧した後だけ)
void ChronoSystem::CityTireDashHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,uint64_t hitKey){
    using namespace Chrono;
    // 体の中心から範囲の矩形までの水平の距離
    float reach=LifeTireRadius*LifeScale(b.mass)+1.5f;
    auto touching=[&](const CityRect& box){float dx=std::max({box.x0-b.at.x,0.f,b.at.x-box.x1}),dz=std::max({box.z0-b.at.z,0.f,b.at.z-box.z1});return std::sqrt(dx*dx+dz*dz)<reach;};
    float damage=LifeTireDashGateDamage(b.weapon.dashPower);
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touching(CityBarricade())){cityBarricadeHit_=hitKey;
        cityBarricadeHealth_-=damage;cityBarricadeShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityBarricadeCenter()+V{0,4,0},{0,1,0},1.2f);swarmEffects_.push_back({CityBarricadeCenter()-V{0,0,4},0,10,2});
        if(cityBarricadeHealth_<=0){BreakCityGate(r,true,false);LifeImpact(ctx,ImpactStructureBreak,CityBarricadeCenter()+V{0,4,0});}
        else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth()*100)))+"% / もっと溜めて突進しよう";life_.messageAge=1.4f;}}
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&touching(CityIronGate())){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="兵士がシャッターを守っている / 先に火事場を制圧しよう";life_.messageAge=2;return;}
        cityGateHealth_-=damage*1.4f;cityGateShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityGateCenter()+V{0,10,0});swarmEffects_.push_back({CityGateCenter()-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0){BreakCityGate(r,false,false);LifeImpact(ctx,ImpactStructureBreak,CityGateCenter()+V{0,10,0});}
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth()*100)))+"%";life_.messageAge=1.4f;}}
}
// 街のステージの更新: エリアの表示・門の開放・ヒント・段階の進行・破片・敵の弾・踏みつけ
void ChronoSystem::UpdateCityStage(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    using namespace Chrono;auto& body=life_.bodies[0];
    // 揺れとヒントの時間を減らす
    cityBarricadeShake_=std::max(0.f,cityBarricadeShake_-dt);cityGateShake_=std::max(0.f,cityGateShake_-dt);cityHintClock_=std::max(0.f,cityHintClock_-dt);
    for(auto& wall:cityWalls_)wall.shake=std::max(0.f,wall.shake-dt);
    // 火事場の炎: 触れると燃える (被弾の無敵時間ごとにダメージ)。消火栓の放水で小さくなり、消える
    for(auto& fire:cityFires_){fire.hiss=std::max(0.f,fire.hiss-dt);if(fire.health<=0)continue;
        float size=fire.radius*(.45f+.55f*fire.health/CityFireHealth());
        if(body.mass>0&&!body.down&&LifeDistance(body.at,fire.at)<size*.8f+LifeRadii(body,life_.recipe).x*.5f){Damage(r,p,CityFireDamage(),ctx,fire.at);
            if(cityHintClock_<=0){cityHintClock_=3;life_.message=body.hydrantUnlocked?"熱い！ 消火栓に変形して放水で火を消そう":"熱い！ 火事場の入口の金色の消火栓を吸収しよう";life_.messageAge=2;}}}
    // 破片は重力で落ち、地面で弾む
    for(auto& d:cityDebris_){d.age+=dt;d.velocity.y-=34*dt;d.at=d.at+d.velocity*dt;d.rotate=d.rotate+d.spin*dt;
        if(d.at.y<d.size.y*.5f){d.at.y=d.size.y*.5f;d.velocity=d.velocity*.35f;d.velocity.y=std::abs(d.velocity.y)*.3f;d.spin=d.spin*.5f;}}
    cityDebris_.erase(std::remove_if(cityDebris_.begin(),cityDebris_.end(),[](const CityDebris& d){return d.age>4;}),cityDebris_.end());
    // スライムの本体か分身に当たったらダメージ
    auto hurt=[&](V at,float radius,float damage){
        // 弾は本体 (体力と分裂) に当たるか、分身をすり減らす
        float reach=LifeRadii(body,life_.recipe).x*.85f+radius;
        if(body.mass>0&&!body.down&&LifeDistance(body.at,at)<reach&&std::abs(at.y-body.at.y)<reach+2){Damage(r,p,damage,ctx,at);return true;}
        for(size_t i=1;i<life_.bodies.size();++i){auto& c=life_.bodies[i];if(c.mass<=0||c.down)continue;
            float cr=2.4f*LifeScale(c.mass)+radius;if(LifeDistance(c.at,at)<cr&&std::abs(at.y-c.at.y)<cr+2){
                c.health=std::max(0.f,c.health-damage*.6f);if(c.health<=0){c.down=true;LifeReturnToSlime(c);++life_.revision;}return true;}}
        return false;};
    // 敵の弾: 防御の水の幕に当たれば止まり、本体に当たればダメージ
    for(auto& shot:cityShots_){V previous=shot.at;shot.age+=dt;shot.velocity.y-=(shot.shell?30.f:9.f)*dt;shot.at=shot.at+shot.velocity*dt;
        bool blocked=false;for(const auto& sample:ctx.renderer->GetHydrantWaterSnapshot().particles){
            V at{sample.at.x,sample.at.y,sample.at.z},velocity{sample.velocity.x,sample.velocity.y,sample.velocity.z};
            if(sample.type>1.15f&&LifeHydrantLiveWater(sample.type,sample.contactAge,velocity)&&
                LifeWaterSegmentDistance(previous,shot.at,at-velocity*std::min(.05f,dt*2),at)<(shot.shell?1.7f:1.1f)){
                blocked=true;shot.age=99;++life_.waterBlocks;if(swarmEffects_.size()<128)swarmEffects_.push_back({shot.at,0,2,2});break;}}
        if(blocked)continue;
        float ground=InkGround(shot.at);
        // 砲弾は地面で爆発して範囲ダメージ
        if(shot.shell){if(shot.at.y<=ground){shot.age=99;swarmEffects_.push_back({shot.at,0,11,1});LifeImpact(ctx,ImpactShellBlast,shot.at);
                hurt({shot.at.x,body.at.y,shot.at.z},9,CityShotDamage(true));}}
        // 矢は当たるか地面・壁で消える
        else if(hurt(shot.at,.6f,CityShotDamage(false))||shot.at.y<=ground||!CityWalkable({shot.at.x,0,shot.at.z},.1f,{true,true}))shot.age=99;
    }
    // 古い弾を消す
    cityShots_.erase(std::remove_if(cityShots_.begin(),cityShots_.end(),[](const CityShot& s){return s.age>4;}),cityShots_.end());
    // 守護者の踏みつけ: 0.85 秒後に範囲にダメージ
    for(auto& stomp:cityStomps_){float previous=stomp.age;stomp.age+=dt;
        if(previous<.85f&&stomp.age>=.85f){swarmEffects_.push_back({stomp.at,0,stomp.radius,1});LifeImpact(ctx,ImpactGuardianStomp,stomp.at);
            hurt({stomp.at.x,body.at.y,stomp.at.z},stomp.radius,22);}}
    cityStomps_.erase(std::remove_if(cityStomps_.begin(),cityStomps_.end(),[](const CityStomp& s){return s.age>1.2f;}),cityStomps_.end());
}
// 街のステージの描画: バリケード・破片・弾・光る看板・踏みつけの予告・敵 (ネズミ・大砲・石の守護者)
void ChronoSystem::DrawCityStage(GameContext& ctx){
    using namespace Chrono;auto* render=ctx.renderer;
    // 立方体を軸と大きさで描く
    auto part=[&](const CityModel& skin,V center,V right,V up,V forward,V size,Engine::Vector4 color){
        Engine::Matrix4x4 m{};V axes[3]={right*size.x,up*size.y,forward*size.z};
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=center.x;m.m[3][1]=center.y;m.m[3][2]=center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,skin.texture,m,color,"CitySurface",skin.extra);};
    // 回転の角度から 3 つの軸を求める関数
    auto euler=[&](V rotate,V& right,V& up,V& forward){
        // ヨー (y) の後、ヨーした座標系でピッチ (x) とロール (z) を回す
        float cy=std::cos(rotate.y),sy=std::sin(rotate.y),cp=std::cos(rotate.x),sp=std::sin(rotate.x),cr=std::cos(rotate.z),sr=std::sin(rotate.z);
        V r0{cy,0,-sy},u0{0,1,0},f0{sy,0,cy};
        V u1=u0*cp+f0*sp,f1=f0*cp-u0*sp;right=r0*cr+u1*sr;up=u1*cr-r0*sr;forward=f1;};
    // モデルを指定の幅で地面に置いて描く
    auto model=[&](const CityModel& m,V ground,float width,float yaw,Engine::Vector4 color){
        float scale=width/std::max({m.hi.x-m.lo.x,m.hi.z-m.lo.z,.001f});V mid=(m.lo+m.hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        Engine::Transform t;t.scale={scale,scale,scale};t.rotate={0,yaw,0};
        t.translate=EV(V{ground.x-(mid.x*cs+mid.z*sn)*scale,ground.y-m.lo.y*scale,ground.z-(-mid.x*sn+mid.z*cs)*scale});
        render->DrawMeshInstanced(m.mesh,m.texture,t,color,"CitySurface",m.extra);};
    // 床の輪のエフェクトを描く
    auto floorRing=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.2f,at.z});t.scale={size,1,size};color.w=float(kind)*2+std::clamp(progress,0.f,.999f);
        render->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    // バリケード: 揺れと、壊れ具合で暗くなる色
    if(!cityGates_.barricadeOpen){float shake=cityBarricadeShake_>0?std::sin(cityBarricadeShake_*90)*cityBarricadeShake_:0.f;
        float wear=1-.45f*(1-cityBarricadeHealth_/CityBarricadeHealth());
        for(const auto& plank:cityBarricade_){V right,up,forward;euler(plank.rotate,right,up,forward);
            part(cityWood_,plank.at+V{shake,0,0},right,up,forward,plank.size,{wear,wear*.95f,wear*.9f,1});}}
    // 材質の番号 (0 木・1 鉄・2 ブロック・3 金属) から立方体の材質を選ぶ
    auto skinOf=[&](int material)->const CityModel& {return material==0?cityWood_:material==1?cityIron_:material==2?cityBlock_:cityArmor_;};
    // 路地の壁: 揺れと、壊れ具合で暗くなる色
    for(size_t i=0;i<cityWalls_.size()&&i<CityWalls().size();++i){if(cityGates_.WallOpen(i))continue;const auto& wall=cityWalls_[i];
        V shake=CityWalls()[i].Axis()*(wall.shake>0?std::sin(wall.shake*90)*wall.shake:0.f);float wear=1-.45f*(1-wall.health/CityWalls()[i].health);
        for(const auto& piece:wall.pieces){V right,up,forward;euler(piece.rotate,right,up,forward);
            Engine::Vector4 color=piece.material==3?Engine::Vector4{.6f*wear,.62f*wear,.65f*wear,1}:piece.material==2?Engine::Vector4{.95f*wear,.93f*wear,.9f*wear,1}:Engine::Vector4{wear,wear*.95f,wear*.9f,1};
            part(skinOf(piece.material),piece.at+shake,right,up,forward,piece.size,color);}}
    // 破片: 時間が経つと小さくなる
    for(const auto& d:cityDebris_){V right,up,forward;euler(d.rotate,right,up,forward);float fade=std::clamp(4-d.age,0.f,1.f);
        part(skinOf(d.material),d.at,right,up,forward,d.size*fade,{1,1,1,1});}
    // 火事場の炎: 揺らぐ炎の舌を輪に並べ、中心は明るい芯。火が弱まるほど小さくなる
    {float t=life_.age;
    // 光る立方体を軸と大きさで描く
    auto glowBox=[&](V center,V right,V up,V forward,V size,Engine::Vector4 color){Engine::Matrix4x4 m{};V axes[3]={right*size.x,up*size.y,forward*size.z};
        for(int k=0;k<3;++k){m.m[k][0]=axes[k].x;m.m[k][1]=axes[k].y;m.m[k][2]=axes[k].z;m.m[k][3]=0;}
        m.m[3][0]=center.x;m.m[3][1]=center.y;m.m[3][2]=center.z;m.m[3][3]=1;render->DrawMeshInstanced(cityCube_,white_,m,color,"DomainGlow");};
    for(size_t i=0;i<cityFires_.size();++i){const auto& fire=cityFires_[i];if(fire.health<=0)continue;
        float power=.35f+.65f*fire.health/CityFireHealth(),size=fire.radius*power;V ground{fire.at.x,InkGround(fire.at),fire.at.z};
        // 外側は低く赤い炎、内側ほど高く黄色い炎。舌は細く、高さと傾きが揺らぐ
        for(int k=0;k<14;++k){float ring=k<8?.6f:.25f,a=float(k)*(k<8?.785f:1.047f)+float(i),wave=std::sin(t*(6+float(k%4))+float(k)*1.7f+float(i));
            float height=size*(k<8?.7f:1.25f)*(.75f+.3f*wave)+3,lean=.12f*std::sin(t*3.1f+float(k));
            V out{std::cos(a),0,std::sin(a)},side{-out.z,0,out.x},up=Unit(V{0,1,0}+side*lean);
            V at=ground+out*(size*ring)+up*(height*.5f);float width=size*(k<8?.2f:.26f);
            glowBox(at,side,up,out,{width,height,width},k<8?Engine::Vector4{1.7f,.32f+.08f*wave,.05f,1}:Engine::Vector4{1.8f,.75f+.15f*wave,.15f,1});}
        float flicker=1+.15f*std::sin(t*17+float(i)*3);
        glowBox(ground+V{0,size*.35f,0},{1,0,0},{0,1,0},{0,0,1},{size*.5f,size*.7f*flicker,size*.5f},{1.9f,1.05f,.3f,1});
        floorRing(fire.at,size*1.2f,0,.5f+.4f*std::sin(t*3+float(i)),{1,.35f,.05f,1});}}
    // 弾: 砲弾は球と着弾点の輪、矢は細長い箱
    for(const auto& shot:cityShots_){
        if(shot.shell){Engine::Transform t;t.translate=EV(shot.at);t.scale={1.3f,1.3f,1.3f};render->DrawMeshInstanced(sphere_,white_,t,{.06f,.06f,.07f,1});
            floorRing(shot.target,9,0,std::clamp(shot.age/1.7f,0.f,1.f),{1,.3f,.05f,1});}
        else{V forward=Unit(shot.velocity),right=Unit(SwarmCross({0,1,0},forward)),up=SwarmCross(forward,right);
            part(cityWood_,shot.at,right,up,forward,{.25f,.25f,3.2f},{.75f,.6f,.45f,1});}
    }
    // 光る看板・燃える窓 (揺らぐ)・非常灯 (点滅する)
    for(const auto& glow:cityGlows_){V forward=SwarmCross(glow.right,glow.up);Engine::Matrix4x4 m{};V axes[3]={glow.right*glow.size.x,glow.up*glow.size.y,forward*glow.size.z};
        Engine::Vector4 color=glow.color;float t=life_.age+glow.phase;
        if(glow.kind==1){float f=.7f+.25f*std::sin(t*9)+.12f*std::sin(t*23+glow.phase*2);color={color.x*f,color.y*f,color.z*f,color.w};axes[1]=axes[1]*(.85f+.2f*std::sin(t*5));}
        else if(glow.kind==2&&std::fmod(t,1.f)>.5f)continue;
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=glow.center.x;m.m[3][1]=glow.center.y;m.m[3][2]=glow.center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,white_,m,color,"DomainGlow");}
    // 踏みつけの予告の輪
    for(const auto& stomp:cityStomps_)if(stomp.age<.85f)floorRing(stomp.at,stomp.radius,0,stomp.age/.85f,{1,.25f,.04f,1});
    // 各敵: 光・状態異常の色・向き・地面の位置・出現の高さ
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto kind=CityKind(enemy);
        // 今の段階の敵は足元に赤い輪を出して目立たせる
        if(enemy.step==life_.step&&enemy.phase!=SwarmPhase::Spawn)floorRing(enemy.at,std::max(4.f,enemy.scale*3.f),0,.55f+.3f*std::sin(life_.age*5+float(enemy.id)),{1,.12f,.08f,1});
        float glow=1+enemy.flash*5;Engine::Vector4 tint{glow,glow,glow,1};if(enemy.corrosion>0)tint={.55f*glow,1.1f*glow,.55f*glow,1};
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z);V ground{enemy.at.x,InkGround(enemy.at),enemy.at.z};
        float rise=enemy.phase==SwarmPhase::Spawn?std::clamp(enemy.clock/.6f,.05f,1.f):1.f;
        // ネズミ: ちょこちょこ揺れる
        if(kind==CityEnemy::Rat){float scurry=std::sin(enemy.age*18+float(enemy.id))*.08f;
            model(cityRat_,ground+V{0,std::abs(scurry),0},CityRatWidth*enemy.scale/.75f*rise,yaw+CityRatYaw,tint);}
        // 大砲
        else if(kind==CityEnemy::Cannon)model(cityCannon_,ground,enemy.scale*CityCannonWidth,yaw+CityCannonYaw,tint);
        // 石の守護者: 脚・腕・胴・頭・目を立方体で組み、歩きと攻撃の構えで動かす
        else if(kind==CityEnemy::Guardian){
            float s=enemy.scale*.42f*rise,walk=enemy.phase==SwarmPhase::Chase?std::sin(enemy.age*2.4f):0.f;
            bool windup=enemy.phase==SwarmPhase::Windup;V f{std::sin(yaw),0,std::cos(yaw)},rt{std::cos(yaw),0,-std::sin(yaw)},up{0,1,0};
            for(float sideSign:{-1.f,1.f}){float swing=walk*.42f*sideSign;V legUp=up*std::cos(swing)+f*std::sin(swing),legF=f*std::cos(swing)-up*std::sin(swing);
                V hip=ground+V{0,s*2.2f,0}+rt*(sideSign*s*.62f);V leg=hip-legUp*(s*1.1f);
                part(cityStoneSkin_,leg,rt,legUp,legF,{s*.82f,s*2.2f,s*.9f},tint);
                if(enemy.armor>0)part(cityIron_,leg+legF*(s*.5f)-legUp*(s*.3f),rt,legUp,legF,{s*.95f,s*1.3f,s*.18f},{.55f*glow,.55f*glow,.6f*glow,1});
                float armSwing=windup?-2.3f:-walk*.5f*sideSign;V armUp=up*std::cos(armSwing)+f*std::sin(armSwing),armF=f*std::cos(armSwing)-up*std::sin(armSwing);
                V shoulder=ground+V{0,s*4.4f,0}+rt*(sideSign*s*1.75f);
                part(cityStoneSkin_,shoulder-armUp*(s*1.2f),rt,armUp,armF,{s*.75f,s*2.5f,s*.75f},tint);
                part(cityStoneSkin_,shoulder-armUp*(s*2.6f),rt,armUp,armF,{s*1.05f,s*.9f,s*1.05f},tint);}
            part(cityStoneSkin_,ground+V{0,s*2.45f,0},rt,up,f,{s*2.1f,s*.7f,s*1.2f},tint);
            part(cityStoneSkin_,ground+V{0,s*3.65f,0}+f*(s*.1f),rt,up,f,{s*2.9f,s*1.9f,s*1.55f},tint);
            part(cityStoneSkin_,ground+V{0,s*5.05f,0}+f*(s*.25f),rt,up,f,{s,s*.95f,s},tint);
            for(float sideSign:{-1.f,1.f}){Engine::Transform eye;eye.translate=EV(ground+V{0,s*5.15f,0}+f*(s*.78f)+rt*(sideSign*s*.25f));eye.scale={s*.13f,s*.13f,s*.13f};
                render->DrawMeshInstanced(sphere_,white_,eye,windup?Engine::Vector4{1,.2f,.05f,1}:Engine::Vector4{1,.6f,.15f,1},"DomainGlow");}
        }
    }
}
// 取り込みが決まった小物は、スキャンのモデルのまま触手に沿って戻ってくる
bool ChronoSystem::DrawCityPropInTentacle(GameContext& ctx,uint32_t visual,Chrono::Vec at,float shrink){
    // モデルの見た目の記録を探す (無ければ false)
    auto found=cityVisuals_.find(visual);if(found==cityVisuals_.end())return false;const auto& v=found->second;
    // 縮めながら触手の先の位置に描く
    Engine::Transform t;t.scale=EV(v.scale*shrink);t.rotate=EV(v.rotate);t.translate=EV(at-v.center*shrink);
    ctx.renderer->DrawMeshInstanced(v.mesh,v.texture,t,v.color,v.shader,v.extra);return true;
}
}
