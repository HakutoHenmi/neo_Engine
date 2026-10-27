// ChronoInk.cpp
// 新しいゲームのシミュレーションの流れ全体。
#include "ChronoSystemInternal.h"

namespace Game {
// インクのステージを作る: プレイヤーの状態・古いエンティティの削除・当たり判定・塗れる面・地形と景観・シェーダー・音・群れ
void ChronoSystem::BuildInk(entt::registry& r){
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
    for(auto e:r.view<NameComponent>())if((r.get<NameComponent>(e).name.rfind("Chrono ",0)==0&&e!=weakpoint_&&!r.any_of<CreaturePart,SerpentRelay>(e))||r.all_of<Anchor>(e)||r.all_of<Feather>(e))old.push_back(e);
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
    // 景観用のシェーダーとテクスチャ (PBR の法線・粗さ・AO を含む)
    auto* render=Engine::Renderer::GetInstance();
    render->CreateShaderPipeline("EnvironmentSurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/EnvironmentSurfacePS.hlsl");
    render->CreateShaderPipeline("MeadowGrass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MeadowGrassPS.hlsl");
    render->CreateShaderPipeline("MeadowGround",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MeadowGroundPS.hlsl");
    // 地面・岩・石・草の色のテクスチャ
    const auto groundTex=render->LoadTexture2D("Resources/Textures/PolyHaven/leafy_grass_diff_4k.jpg");
    const auto rockFaceTex=render->LoadTexture2D("Resources/Textures/PolyHaven/boulder_01_diff_2k.jpg");
    const auto boulderTex=render->LoadTexture2D("Resources/Textures/PolyHaven/namaqualand_boulder_02_diff_2k.jpg");
    const auto stoneTex=render->LoadTexture2D("Resources/Textures/PolyHaven/rock_09_diff_2k.jpg");
    const auto grassTex=render->LoadTexture2D("Resources/Textures/PolyHaven/grass_medium_02_rgba_2k.png");
    // テクスチャごとの PBR の追加マップ (法線・粗さ・AO)
    std::map<uint32_t,std::vector<uint32_t>> pbr;
    // PBR のフォルダから 3 つのマップを読み込む
    auto maps=[&](uint32_t albedo,const std::string& prefix,const std::array<std::string,3>& suffix){
        for(const auto& s:suffix)pbr[albedo].push_back(render->LoadTexture2D("Resources/Textures/PBR/"+prefix+s+".jpg",false));
    };
    // Poly Haven の新しいマップの名前で読み込む
    auto freshMaps=[&](uint32_t albedo,const char* id){for(const char* map:{"nor_dx","Rough","AO"})pbr[albedo].push_back(render->LoadTexture2D(std::string("Resources/Textures/PolyHaven/")+id+"_"+map+"_2k.jpg",false));};
    freshMaps(groundTex,"leafy_grass");freshMaps(rockFaceTex,"boulder_01");freshMaps(boulderTex,"namaqualand_boulder_02");freshMaps(grassTex,"grass_medium_02");
    maps(stoneTex,"rock_09_",{"nor_dx","Rough","AO"});
    // 景観のメッシュを置く
    auto scenery=[&](const std::string& name,const std::string& path,auto texture,V at,V size,const char* shader="EnvironmentSurface"){
        auto e=Mesh(r,name,path,at,size);auto& mr=r.get<MeshRendererComponent>(e);
        mr.textureHandle=texture;mr.shaderName=shader;
        if(pbr.count(texture))mr.extraTextureHandles=pbr[texture];
        r.remove<CameraOccluder>(e);return e;
    };
    // 草原は元の近距離の影を使う。街は塔のために範囲を広げる
    // 影の範囲 (草原は近距離)
    render->SetDirectionalShadowRange(100,100);
    // 太陽: シーンの「Sun」エンティティが毎フレーム光を決める。草原は元の
    // 45/30 度の太陽、街は高くして通りの向きにそろえる
    // 「Sun」エンティティの角度と明るさを設定する
    for(auto e:r.view<NameComponent,TransformComponent,DirectionalLightComponent>())if(r.get<NameComponent>(e).name=="Sun"){
        auto& t=r.get<TransformComponent>(e);auto& light=r.get<DirectionalLightComponent>(e);
        if(slimeLife_){t.rotate={DirectX::XMConvertToRadians(62.f),DirectX::XMConvertToRadians(8.f),0};light.intensity=1.1f;}
        else{t.rotate={DirectX::XMConvertToRadians(45.f),DirectX::XMConvertToRadians(30.f),0};light.intensity=1;}}
    // スライムの生活モードなら街のステージを作り、草原の景観はとばす
    // 子分の縁取り: 見えている所と、壁越し (深度を見ない) の 2 種類
    if(slimeLife_){render->CreateShaderPipelineTransparent("SlimeChildGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SlimeChildHighlightPS.hlsl",true,true);
        render->CreateShaderPipelineTransparent("SlimeChildXRay",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/SlimeChildHighlightPS.hlsl",true,false);}
    if(slimeLife_){if(slimePlayerDemo_) BuildSlimePlayerDemo(r);else BuildCityStage(r);goto sceneryDone;}
    // 草原の地面: 高さの関数から格子状の地形メッシュを作る
    {auto floor=scenery("Meadow ground","Resources/Models/Environment/meadow-floor.obj",groundTex,{0,0,0},{1,1,1},"MeadowGround");
    std::vector<Engine::VertexData> terrainVertices;std::vector<uint32_t> terrainIndices;
    // 格子の頂点の高さと法線を地形の関数から求める
    constexpr int grid=110;terrainVertices.reserve((grid+1)*(grid+1));
    for(int iz=0;iz<=grid;++iz)for(int ix=0;ix<=grid;++ix){float x=-550.f+ix*10.f,z=-560.f+iz*(1160.f/grid);
        float sx=(MeadowHeight(x+.5f,z)-MeadowHeight(x-.5f,z)),sz=(MeadowHeight(x,z+.5f)-MeadowHeight(x,z-.5f));V normal=Unit(V{-sx,1,-sz});
        Engine::VertexData vertex{};vertex.position={x,MeadowHeight(x,z),z,1};vertex.texcoord={x/4.f,z/4.f};vertex.normal=Write(normal);terrainVertices.push_back(vertex);
        if(ix<grid&&iz<grid){uint32_t a=iz*(grid+1)+ix,b=a+1,c=a+grid+1,d=c+1;terrainIndices.insert(terrainIndices.end(),{a,c,b,b,c,d});}
    }
    // 地形のメッシュを作って床に設定する
    r.get<MeshRendererComponent>(floor).modelHandle=render->CreateDynamicMesh(terrainVertices,terrainIndices);
    // 格子の頂点は既にワールドの単位なので、以前の平らなメッシュの正規化を
    // 使うと地形の高さが 1000 倍になってしまう
    auto& terrainTransform=r.get<TransformComponent>(floor);terrainTransform.scale={1,1,1};terrainTransform.translate={0,0,0};terrainTransform.rotate={0,0,0};
    // 床の色
    r.get<MeshRendererComponent>(floor).color={.76f,1.f,.78f,1};
    // スキャンしたモデルの比率を保つ。目標の幅から等倍の拡大率を決め、
    // 一番低い頂点を平らなアリーナの床に置く
    // スキャンしたモデルを、幅と向きを指定して地面に置く
    auto natural=[&](const std::string& name,const std::string& path,auto texture,V at,float width,float yaw,const char* shader="EnvironmentSurface"){
        auto e=scenery(name,path,texture,{0,0,0},{1,1,1},shader);
        auto* model=render->GetModel(r.get<MeshRendererComponent>(e).modelHandle);
        if(!model||model->GetData().vertices.empty())return e;
        // 草の LOD は、つながった葉と元の UV を丸ごと残す
        bool foliage=std::string(shader)=="MeadowGrass";
        render->PrepareDistanceLods(r.get<MeshRendererComponent>(e).modelHandle,foliage,foliage);
        // モデルの範囲を求めて拡大率と位置を決める
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:model->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        float scale=width/std::max({hi.x-lo.x,hi.z-lo.z,.001f});
        V mid=(lo+hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        auto& t=r.get<TransformComponent>(e);t.scale=Write({scale,scale,scale});t.rotate=Write({0,yaw,0});
        t.translate=Write({at.x-(mid.x*cs+mid.z*sn)*scale,MeadowHeight(at.x,at.z)+at.y-lo.y*scale,at.z-(-mid.x*sn+mid.z*cs)*scale});
        return e;
    };
    // 位置から決まる 0〜1 の疑似乱数
    auto noise=[](int seed){float n=std::sin(float(seed)*12.9898f)*43758.5453f;return n-std::floor(n);};
    const std::string rockFaceMesh="Resources/Models/PolyHaven/boulder_01_2k.fbx";
    const std::string boulderMesh="Resources/Models/PolyHaven/namaqualand_boulder_02_2k.fbx";
    const std::string stoneMesh="Resources/Models/PolyHaven/rock_09_1k.fbx";
    const std::string grassMesh="Resources/Models/PolyHaven/grass_medium_02_2k.fbx";
    // 外周の大岩・砂岩・野の石・小石・草むらを並べる
    for(int side=0;side<4;++side)for(int i=0;i<5;++i){
        float along=-300.f+i*150.f+(noise(side*31+i)*2-1)*22.f;
        V at=side<2?V{along,-.6f,side==0?-380.f:420.f}:V{side==2?-380.f:380.f,-.6f,along+20};
        natural("Weathered boulder",rockFaceMesh,rockFaceTex,at,20.f+noise(side*71+i)*16.f,noise(side*101+i)*6.28318f);
    }
    // 砂岩 (4 辺に 3 つずつ)
    for(int side=0;side<4;++side)for(int i=0;i<3;++i){
        float along=-290.f+i*290.f+(noise(side*37+i)*2-1)*17.f;
        V at=side<2?V{along,-2,side==0?-352.f:392.f}:V{side==2?-348.f:348.f,-2,along+20};
        natural("Sandstone boulder",boulderMesh,boulderTex,at,12.f+noise(side*83+i)*13.f,noise(side*113+i)*6.28318f);
    }
    // 野の石を 8 つ
    for(int i=0;i<8;++i){
        V at{-315.f+noise(i*3+1)*630.f,-.08f,-325.f+noise(i*3+2)*690.f};
        natural("Scanned field stone",stoneMesh,stoneTex,at,2.f+noise(i*3+3)*3.f,noise(i*5+4)*6.28318f);
    }
    // 小石を置く (詳細度を下げたメッシュを使う)
    auto pebble=[&](V at,float width,float yaw){
        auto e=natural("Scanned pebble",rockFaceMesh,rockFaceTex,at,width,yaw);
        auto& mr=r.get<MeshRendererComponent>(e);
        // 小さな石には固定の詳細度を落としたメッシュで十分で、
        // カメラが動いても描画とレイの形が変わらない
        mr.modelHandle=render->GetDistanceLodMeshes(mr.modelHandle)[2];
        // 比較用の起動引数が無ければ、さらに 2 段詳細度を下げる
        if(!wcsstr(GetCommandLineW(),L"--scenery-baseline"))for(int level=0;level<2;++level){
            render->PrepareDistanceLods(mr.modelHandle);mr.modelHandle=render->GetDistanceLodMeshes(mr.modelHandle)[2];}
    };
    // 全体に小石を 155 個、中央付近に 48 個
    for(int i=0;i<155;++i){
        V at{-315.f+noise(i*3+23)*630.f,-.08f,-325.f+noise(i*3+24)*690.f};
        pebble(at,.45f+noise(i*3+25)*1.5f,noise(i*5+26)*6.28318f);
    }
    for(int i=0;i<48;++i){
        V at{-78.f+noise(i*3+801)*156.f,-.08f,-105.f+noise(i*3+802)*180.f};
        pebble(at,.55f+noise(i*3+803)*1.25f,noise(i*5+804)*6.28318f);
    }
    // 草むらを格子状に置く (位置と大きさにばらつきを付ける)
    for(int z=0;z<21;++z)for(int x=0;x<22;++x){
        int index=z*22+x;V at{-320.f+x*30.f+(noise(index*3+1)*2-1)*9.f,-.015f,
            -335.f+z*34.f+(noise(index*3+2)*2-1)*9.f};
        natural("Scanned meadow tuft",grassMesh,grassTex,at,2.8f+noise(index*7+3)*2.f,noise(index*7+4)*6.28318f,"MeadowGrass");
    }
    // 中央付近の草むら (プレイヤーの開始位置は避ける)
    for(int i=0;i<110;++i){
        V at{-78.f+noise(i*3+251)*156.f,-.015f,-90.f+noise(i*3+252)*180.f};
        if(std::abs(at.x)<6&&std::abs(at.z)<12)continue;
        natural("Near meadow tuft",grassMesh,grassTex,at,1.4f+noise(i*3+253)*1.8f,noise(i*5+254)*6.28318f,"MeadowGrass");
    }
    // 手前の飾りで開幕の画面を縁取る (プレイヤーは隠さない)
    for(V at:{V{8,-.005f,-5},V{-9,-.005f,-2},V{14,-.005f,5},V{-16,-.005f,9}})
        natural("Foreground meadow tuft",grassMesh,grassTex,at,4.5f,noise(int(at.x*19+at.z*13))*6.28318f,"MeadowGrass");
    natural("Foreground weathered stone",rockFaceMesh,rockFaceTex,{-19,-.12f,18},4.8f,1.1f);
    natural("Foreground sandstone",boulderMesh,boulderTex,{25,-.1f,32},5.5f,2.3f);
    }
    // インクの面のメッシュ・ビームや領域のシェーダー・音を用意し、群れ (とスライムの生活) を作る
    sceneryDone:
    // インクの面のシェーダーとメッシュ (塗れる面ごと)
    render->CreateShaderPipeline("InkSurface",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/InkSurfacePS.hlsl");
    for(int i=0;i<static_cast<int>(inkSurfaces_.size());++i)inkMeshes_.push_back(render->LoadObjMesh("Resources/Models/Ink/surface"+std::to_string(i)+".obj"));
    // 跡・輪・誘導弾を消し、塗りのテクスチャを初期化する
    slimeTrails_.clear();homingDomains_.clear();domainMissiles_.clear();inkNeedsRebuild_=false;inkUploadClock_=0;inkDirty_=true;UploadInk();
    // ビームと輪のメッシュ
    slimeBeamMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-beam.obj");
    slimeRingMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-ring.obj");
    // ビーム・輪の光・輪の塗りのシェーダー
    render->CreateShaderPipelineTransparent("SlimeBeam",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/SlimeBeamPS.hlsl",false,true);
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("DomainFill",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainFillPS.hlsl",true,true);
    // 輪の塗りの三角形のメッシュ
    domainFillMesh_=render->CreateDynamicMesh({{{0,0,0,1},{0,0},{0,1,0}},{{1,0,0,1},{1,0},{0,1,0}},{{0,0,1,1},{0,1},{0,1,0}}},{0,1,2});
    // 候補の強調と液体を消す
    highlightedCandidate_.clear();candidateHighlight_.clear();
    trailLiquid_={};candidateLiquid_={};
    render->SetGuidedLiquidTrail({});
    // 音を一度だけ読み込む
    if(slimePressureSound_==0xffffffff){auto* audio=Engine::Audio::GetInstance();
        slimePressureSound_=audio->Load("Resources/Sound/slime-pressure.wav");slimeRecallSound_=audio->Load("Resources/Sound/slime-recall.wav");slimeAcidSound_=audio->Load("Resources/Sound/slime-acid.wav");}
    // 群れを作り、スライムの生活モードならその準備をする
    BuildSwarm(r);
    if(slimeLife_)BuildSlimeLife(r);
}
// 塗った面のマスクを 1 枚のテクスチャにまとめてアップロードする
void ChronoSystem::UploadInk(){
    // 4x? のタイルに塗れる面のマスクを並べて 1 枚のテクスチャにする
    constexpr int tile=InkSurface::Resolution,size=tile*4;std::vector<uint8_t> pixels(size*size*4,0);
    for(int k=0;k<static_cast<int>(inkSurfaces_.size());++k)for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){
        int index=(((k/4)*tile+y)*size+(k%4)*tile+x)*4;
        pixels[index]=inkSurfaces_[k].mask[y*tile+x];pixels[index+3]=255;}
    // テクスチャを更新する
    inkTexture_=Engine::Renderer::GetInstance()->UpdatePaintTexture("boss-arena",size,size,pixels.data());inkDirty_=false;
}
// インクの地面の高さ (真下へのレイで一番高い面)。surface に面の番号を返す
float ChronoSystem::InkGround(V at,int* surface)const{
    // 街のステージは y=0 で平らで、草原の塗れる面より広い
    if(slimeLife_&&!inkSurfaces_.empty()){if(surface)*surface=0;(void)at;return 0;}
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
    float domainTarget=(!homingDomains_.empty()||!domainMissiles_.empty())?1.f:0.f;
    // 空からの連射中は、移動や狙いを止めずに視野を広げる
    ink.cameraDomainView=approach(ink.cameraDomainView,domainTarget,domainTarget>0?.65f:.9f);
    float domainView=Ease(ink.cameraDomainView);
    // ボスの急降下や衝撃の直後はカメラを引く
    const auto& boss=r.get<CreatureBoss>(boss_);
    float arrival=boss.stage==CreatureStage::Dive&&boss.sweepPhase==2?6.f*AttackEase(boss.sweepTime/.65f):0.f;
    float aftershock=boss.impactAge<.45f?5.f*(1-boss.impactAge/.45f):0.f;
    // スライムの生活モードでは、体の大きさに合わせてカメラを引く
    LifeCameraSubject subject{};
    if(slimeLife_)subject=GetLifeCameraSubject();
    float growthPullback=slimeLife_?LifeCameraDistance(subject.radius,1.13f,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y))-17.f:0.f;
    // 引く量の合計をなめらかに反映する
    float pullback=growthPullback+((charging||firing)?3*power:0.f)+domainView*14+std::max(arrival,aftershock);
    ink.cameraPullback+=(pullback-ink.cameraPullback)*(1-std::exp(-10*ctx.dt));
    // 注視点 (スライムの生活モードは体の中心)
    V focus=cameraFollow_+V{0,2.f,0};
    if(slimeLife_)focus=cameraFollow_+(subject.center-pos);
    // 消火栓の形か
    bool hydrant=slimeLife_&&life_.bodies[0].action==LifeAction::Hydrant;
    // 肩越しのずれ (スライムの生活モードでは使わない)
    float shoulder=slimeLife_?0.f:1.5f+shot*4.5f+heavy*8;
    // カメラを置きたい位置
    V wanted=focus-forward*(17.f+ink.cameraPullback+heavy*5)+right*shoulder+V{0,shot*1.2f+heavy*3+domainView*6,0};
    // 消火栓のときは肩越しの照準の位置へ移る
    if(hydrant){
        // 高さは体に合わせ、ピッチとは別にする。上を見てもカメラが
        // 路面へ下がらないようにし、視線はカメラの右に空ける
        V flat{std::sin(yaw_),0,std::cos(yaw_)};
        float blend=LifeSmooth(life_.bodies[0].actionAge/.35f);
        V shoulderAt=pos+LifeHydrantCameraOffset(life_.bodies[0].mass,flat);
        wanted=Lerp(wanted,shoulderAt,blend);
        focus=Lerp(focus,pos+V{0,-1.25f+4.3f*LifeScale(life_.bodies[0].mass),0},blend);
    }
    // 壁やインクの面でカメラが埋まらないよう手前に寄せる
    float f;V n;
    for(const auto& box:solids_)if(Chrono::Sweep(focus,wanted-focus,box,{.3f,.3f,.3f},f,n))wanted=focus+(wanted-focus)*std::max(.05f,f-.025f);
    for(const auto& surface:inkSurfaces_){V hit;if(surface.Ray(focus,wanted-focus,f,hit))wanted=focus+(wanted-focus)*std::max(.05f,f-.06f);}
    wanted.y=std::max(wanted.y,.7f);
    // 撃っているときはビームの方向を見る
    V beamForward=firing?Forward(ink.beamYaw,ink.beamPitch):forward;
    ink.cameraBeamDirection=Unit(Lerp(ink.cameraBeamDirection,beamForward,1-std::exp(-14*ctx.dt)));
    // 注視の向き (構図の量で補間する)
    V lookAt=focus+ink.cameraBeamDirection*(45-heavy*20)+V{0,domainView*20,0};
    V view=Unit(Lerp(forward,Unit(lookAt-wanted),std::max(shot,domainView*.8f)));
    // スライムの生活モードでは注視点を向く (消火栓のときは前方)
    if(slimeLife_)view=hydrant?forward:Unit(focus-wanted);
    if(hydrant)ctx.camera->StopShake();
    // カメラへ位置と回転を反映する
    ctx.camera->SetPosition(Write(wanted));
    ctx.camera->SetRotation(-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0);
    // 画角 (泳いでいるとき・溜め・輪で広げる)
    float goal=(ink.swimming?1.22f:1.13f)+heavy*.06f+domainView*.12f;fov_+=(goal-fov_)*(1-std::exp(-6*ctx.dt));
    // 7 度の素早い揺れを加え、0.4 秒以内に通常のカメラへ完全に戻す
    ctx.camera->SetProjection(fov_+.12f*SlimePerfectPulse(ink.perfectAge),ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    // 消火栓のときは揺れを止める
    ctx.camera->SetHandheld(hydrant?0.f:.008f);
    ctx.camera->SetShakeStrength(hydrant?0.f:shakeSetting_*.5f);
    // 近いほどプレイヤーを半透明にする
    p.cameraOpacity=std::clamp((Length(wanted-pos)-2.5f)/5.f,.2f,1.f);
    // 衝撃の時にボスがプレイヤーを突き抜けることがある。カメラとプレイヤーの間の部位だけを
    // 薄くして、攻撃と床の合図が見えるようにする
    for(auto e:r.view<CreaturePart,CameraOccluder>()){
        auto& o=r.get<CameraOccluder>(e);float fraction;V normal;
        bool occludes=Chrono::Sweep(wanted,focus-wanted,
            {o.center-o.size*.48f,o.center+o.size*.48f},{.4f,.4f,.4f},fraction,normal);
        bool closeToCamera=Length(o.center-wanted)<Length(o.size)*.42f+2.f;
        o.opacity+=(((occludes||closeToCamera)?.03f:1.f)-o.opacity)*(1-std::exp(-20.f*ctx.dt));
    }
}
// ボス (鳥の形) の更新: 羽の弾・ダウン・集合・連射・扇状の羽・急降下・蛇への変形
void ChronoSystem::UpdateInkBoss(entt::registry& r,Player& p,GameContext& context){
    // ボスの攻撃の弱体化 (減速・沼) を時間に掛ける
    GameContext ctx=context;const auto& state=r.get<InkPlayer>(player_);ctx.dt*=state.rogueBossSlow>0?.5f:state.rogueBossPool?.65f:1.f;
    auto& c=r.get<CreatureBoss>(boss_);auto& b=r.get<Boss>(boss_);
    auto& ink=r.get<InkPlayer>(player_);const float dt=std::min(ctx.dt,.05f)*p.worldScale;
    V player=Read(r.get<TransformComponent>(player_).translate);
    // 羽の弾を動かし、プレイヤーや壁に当たったら消す
    std::vector<entt::entity> expired;
    // ボスの羽: 進め、面や壁に当たったら消える。プレイヤーに当たればダメージ
    for(auto e:r.view<SlimeFeather,TransformComponent>()){
        auto& feather=r.get<SlimeFeather>(e);auto& transform=r.get<TransformComponent>(e);
        V before=Read(transform.translate),delta=feather.velocity*dt;float hit;
        bool blocked=false;float travel=Length(delta);
        for(const auto& surface:inkSurfaces_){V at;float fraction;if(surface.Ray(before,delta,fraction,at)){blocked=true;travel=std::min(travel,Length(delta)*fraction);}}
        for(const auto& box:solids_){V normal;float fraction;if(Chrono::Sweep(before,delta,box,{.3f,.3f,.3f},fraction,normal)){blocked=true;travel=std::min(travel,Length(delta)*fraction);}}
        bool contact=RaySphere(before,Unit(delta),player,2.f,travel,hit);
        // 広い「かすり」の範囲は回避の判定だけに使い、ダメージの当たりは広げない
        if(ink.dodgeAge<=.24f&&!ink.perfectUsed)
            contact|=RaySphere(before,Unit(delta),ink.dodgeOrigin,4.f,travel,hit)||RaySphere(before,Unit(delta),player,4.f,travel,hit);
        feather.life-=dt;
        if(contact){p.damageReason="HIT - FEATHER VOLLEY";Damage(r,p,14,ctx,before,.24f);}
        if(contact||blocked||feather.life<=0)expired.push_back(e);
        else transform.translate=Write(before+delta);
    }
    for(auto e:expired)r.destroy(e);
    if(finished_)return;
    // ダウン中は弱点を出して止まる
    if(ink.downTimer>0){ink.downTimer=std::max(0.f,ink.downTimer-dt);c.recoil=.35f;
        r.get<Target>(weakpoint_).active=!b.countered;PoseCreature(r);return;}
    // ダウン中は弱点を出して動かない。蛇の形なら UpdateCreature へ任せる
    if(c.stage==CreatureStage::Snake){UpdateCreature(r,p,ctx);return;}
    // 時間・反動・衝撃の時間を進める
    c.age+=dt;c.timer+=dt;c.recoil=std::max(0.f,c.recoil-dt);c.impactAge+=ctx.dt;
    // 段階の切り替えと、目標へ向かう移動
    auto transition=[&](CreatureStage stage){c.stage=stage;c.timer=0;c.sweepPhase=0;c.sweepTime=0;};
    // 目標へ向かう移動 (加速しながら近づく)
    auto move=[&](V goal,float speed){V delta=goal-c.offset;
        c.moveVelocity=Lerp(c.moveVelocity,Unit(delta)*std::min(speed,Length(delta)*3.f),1-std::exp(-5.5f*dt));
        c.offset=c.offset+c.moveVelocity*dt;};
    V base=EvaluateCreature(c.age,c.form).core;
    // 集合: 蛇から鳥の形へ変形する
    if(c.stage==CreatureStage::Assemble){
        // 最初のフレームで飛行の角度を決める
        if(c.timer<=dt*1.5f){V at=Center(r,boss_)-V{0,0,20};c.flightAngle=std::atan2(at.x,at.z);c.volleys=0;}
        c.form=std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,1).core;c.heading=c.morphHeading;
            transition(CreatureStage::Volley);b.countered=false;}
    // 飛行: プレイヤーの周りを回り、扇状の羽か急降下へ移る
    }else if(c.stage==CreatureStage::Volley){
        c.form=1;
        // 速く減衰する弧で、ゆっくり追いかけ回さずに翼を動かし続ける
        float bearing=std::atan2(player.x,player.z-20);
        c.flightAngle+=dt*(.48f+std::clamp(std::remainder(bearing-c.flightAngle,6.283185f),-1.f,1.f)*.18f);
        // プレイヤーの方へ少し寄りながら周りを回る
        move(SlimeFlightPoint(c.flightAngle)-base,180);
        if(c.timer>=4.5f&&Length(SlimeFlightPoint(c.flightAngle)-Center(r,boss_))<45){
            c.moveVelocity={};
            // 一定時間後に連射の位置に来たら、扇状の羽か急降下を選ぶ
            if(c.volleys%2==0){transition(CreatureStage::Opening);c.sweepPhase=1;c.volleyClock=0;b.emitted=0;}
            else{c.diveStart=Center(r,boss_);c.sweepCenter={std::clamp(player.x,-300.f,300.f),0,std::clamp(player.z,-310.f,350.f)};
                c.sweepCenter.y=InkGround(c.sweepCenter);transition(CreatureStage::Dive);c.sweepPhase=1;c.sweepHit=false;}
        }
    // 扇状の羽: 9 本ずつの羽を左右交互に 20 回撃つ
    }else if(c.stage==CreatureStage::Opening){
        c.sweepTime+=dt;
        // 9 本の羽の扇を 20 回、レーンの間に隙間を空けてプレイヤーへ撃つ
        if(c.sweepPhase==1&&c.sweepTime>=1.15f){c.sweepPhase=2;c.sweepTime=0;c.volleyClock=.14f;}
        if(c.sweepPhase==2){
            // 羽を撃ち始める
            c.volleyClock+=dt;
            while(c.volleyClock>=.14f&&b.emitted<180){
                c.volleyClock-=.14f;int wave=b.emitted/9;float side=wave%2==0?-1.f:1.f;
                // 0.14 秒ごとに 9 本の扇を撃つ (180 本で終わり)
                V right{std::cos(c.heading),0,-std::sin(c.heading)};
                V start=Center(r,boss_)+right*(side*15)+V{0,5,0};
                V aim=player+p.velocity*.2f;V axis=Unit(aim-start);
                float yaw=std::atan2(axis.x,axis.z),pitch=-std::asin(axis.y);
                for(int lane=-4;lane<=4;++lane){
                V direction=Forward(yaw+lane*.055f+std::sin(wave*.55f)*.12f,pitch+(wave%3-1)*.012f);
                auto e=Mesh(r,"Bird volley feather","Resources/Models/Chrono/feather.obj",start,{1.2f,.55f,4.5f});
                r.emplace<SlimeFeather>(e).velocity=direction*62;
                r.get<TransformComponent>(e).rotate={-std::asin(direction.y),std::atan2(direction.x,direction.z),0};
                r.get<MeshRendererComponent>(e).color={1.6f,.65f,.15f,1};++b.emitted;
                }
            }
            if(b.emitted>=180){c.sweepPhase=3;c.sweepTime=0;++c.volleys;}
        }
        // 反撃されたか撃ち終えて 2 秒経ったら次へ
        if(b.countered||(c.sweepPhase==3&&c.sweepTime>=2.f)){
            transition(b.countered||c.volleys>=2?CreatureStage::Descend:CreatureStage::Volley);}
    // 急降下: 予告の円へ落ちて衝撃を与え、全方位へ羽を飛ばす
    }else if(c.stage==CreatureStage::Dive){
        c.form=1;c.sweepTime+=dt;
        if(c.sweepPhase==1&&c.sweepTime>=1.15f){c.sweepPhase=2;c.sweepTime=0;}
        if(c.sweepPhase==2){
            // 予告の後、目標の地点へ急降下する
            V impact=c.sweepCenter+V{0,23,0};
            V center=Lerp(c.diveStart,impact,AttackEase(c.sweepTime/.65f));
            c.offset=center-base;
            if(c.sweepTime>=.65f){
                // 着地したら、近くのプレイヤーにダメージと衝撃、全方位へ羽を飛ばす
                V delta=player-c.sweepCenter;delta.y=0;
                if(Length(delta)<11.f&&player.y<c.sweepCenter.y+7){p.damageReason="HIT - BIRD DIVE";Damage(r,p,26,ctx,c.sweepCenter,.16f);}
                BossImpact(r,ctx,c.sweepCenter,{0,1,0},1.2f);
                for(int i=0;i<18;++i){
                    float angle=(i+.25f*(i%2))*6.283185f/18.f;
                    V direction{std::cos(angle),0,std::sin(angle)};
                    V start=c.sweepCenter+direction*5.f+V{0,2.5f,0};
                    auto e=Mesh(r,"Dive radial feather","Resources/Models/Chrono/feather.obj",start,{1.1f,.45f,3.6f});
                    r.emplace<SlimeFeather>(e).velocity=direction*58.f;
                    r.get<TransformComponent>(e).rotate={0,std::atan2(direction.x,direction.z),0};
                    r.get<MeshRendererComponent>(e).color={1.7f,.45f,.12f,1};
                }
                c.sweepPhase=3;c.sweepTime=0;
            }
        // 隙が終わったか反撃されたら下降へ
        }else if(c.sweepPhase==3&&(c.sweepTime>=2.2f||b.countered)){
            ++c.volleys;transition(CreatureStage::Descend);
        }
    // 下降: 鳥から蛇の形へ戻る
    }else{
        c.form=1-std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,0).core;c.heading=c.morphHeading;
            transition(CreatureStage::Snake);c.form=0;}
    }
    // プレイヤーの方を向き、攻撃の後だけ弱点を出す
    V toward=player-Center(r,boss_);
    float heading=std::atan2(-toward.x,-toward.z);
    c.heading+=std::remainder(heading-c.heading,6.283185f)*(1-std::exp(-2*dt));
    r.get<Target>(weakpoint_).active=!b.countered&&
        ((c.stage==CreatureStage::Opening||c.stage==CreatureStage::Dive)&&c.sweepPhase==3);
    PoseCreature(r);
}
// インクのモードの描画: ビーム・塗った面・群れ・輪の届く範囲・ボスの影と攻撃の予告
void ChronoSystem::DrawInk(entt::registry& r,GameContext& ctx){
    if(slimeLife_){DrawSlimeLife(r,ctx);return;}
    DrawSlimeBeam(r,ctx);
    // 塗った面を描く
    Engine::Transform identity;
    for(auto mesh:inkMeshes_)ctx.renderer->DrawMesh(mesh,inkTexture_,identity,{1,1,1,1},"InkSurface",.4f,false);
    // 群れを描く
    DrawSwarm(r,ctx);
    // 地面の点線の輪: 候補の輪から出る弾がどこまで届くか
    if(const auto& candidate=r.get<InkPlayer>(player_).domainPath.candidate;candidate.Ready()&&!finished_){
        const auto reach=DomainReach(candidate);
        auto ring=[&](float angle){V at=reach.center+V{std::cos(angle)*reach.radius,0,std::sin(angle)*reach.radius};at.y=InkGround(at)+.3f;return at;};
        for(int i=0;i<72;i+=2)ctx.renderer->DrawLine3D(EV(ring(float(i)*6.283185f/72)),EV(ring(float(i+1)*6.283185f/72)),{.35f,1,.85f,1});
    }
    // ボス戦だけ: 蛇の影・衝撃・土ぼこり・攻撃の予告
    if(r.get<InkPlayer>(player_).battlePhase!=BattlePhase::Boss)return;
    const auto& c=r.get<CreatureBoss>(boss_);
    if(c.stage==CreatureStage::Snake){
        // 蛇の背骨の下に平たい影を描く
        for(auto e:r.view<CreaturePart,TransformComponent>()){
            const auto& part=r.get<CreaturePart>(e);
            // 背骨 3 つごとに平たい楕円を 1 つ置くと、生き物に形状を足さずに
            // 連続した接地の影に見える
            if(part.role!=CreatureRole::Spine||part.segment<6||part.segment%3!=0)continue;
            const auto& t=r.get<TransformComponent>(e);
            Engine::Transform shadow;shadow.translate={t.translate.x,InkGround(Read(t.translate))+.075f,t.translate.z};
            float width=11.f-5.f*float(part.segment)/25.f;
            shadow.scale={width*2.f,.09f,18.f};
            ctx.renderer->DrawMesh(sphere_,white_,shadow,{.035f,.09f,.025f,1},"Default",0,false);
        }
    }
    DrawBossImpact(ctx,c);
    // 土ぼこりの粒
    for(const auto& d:droplets_)if(d.dust){
        Engine::Transform t;t.translate=EV(d.position);
        float size=.8f*(1-d.age/.45f)+.035f;t.scale={size,size,size};
        ctx.renderer->DrawMesh(sphere_,white_,t,{.68f,.56f,.35f,1},"Default",.3f,false);
    }
    // 蛇の攻撃の予告 (地面の矩形)
    if(c.stage==CreatureStage::Snake&&(c.sweepPhase==1||c.sweepPhase==2||c.sweepPhase==5||c.sweepPhase==6)){auto color=c.sweepPhase==1||c.sweepPhase==5?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        float width=c.sweepPhase>=5?22.f:c.attack==CreatureAttack::Charge||c.attack==CreatureAttack::HeadTail?5.f:c.attack==CreatureAttack::Slam?8.f:16.f;
        float depth=c.sweepPhase>=5?7.f:c.attack==CreatureAttack::Charge||c.attack==CreatureAttack::HeadTail?24.f:c.attack==CreatureAttack::Slam?8.f:7.f;
        V center=c.sweepCenter;center.y=InkGround(center)+.12f;
        for(float x:{-width,width})ctx.renderer->DrawLine3D(EV(center+V{x,0,-depth}),EV(center+V{x,0,depth}),color);
        for(float z:{-depth,depth})ctx.renderer->DrawLine3D(EV(center+V{-width,0,z}),EV(center+V{width,0,z}),color);
    }
    // 急降下の予告 (地面の円)
    if(c.stage==CreatureStage::Dive&&(c.sweepPhase==1||c.sweepPhase==2)){
        V center=c.sweepCenter+V{0,.15f,0};
        Engine::Vector4 color=c.sweepPhase==1?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        for(int i=0;i<32;++i){float a=i*6.283185f/32.f,b=(i+1)*6.283185f/32.f;
            ctx.renderer->DrawLine3D(EV(center+V{std::cos(a)*11,0,std::sin(a)*11}),
                EV(center+V{std::cos(b)*11,0,std::sin(b)*11}),color);}
    }
}

}
