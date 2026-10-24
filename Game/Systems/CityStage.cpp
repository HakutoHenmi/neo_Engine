// CityStage.cpp
// スライムの街のステージ (Docs/SlimeLife.md の「街ステージ」)。
// 動かない建物は PBR の材質ごとに 1 つの動的メッシュとして作る。小物・
// バリケード・シャッターの門・街の敵は、Resources/Credits/CityStage.md にある
// CC0 の Poly Haven の素材を使う
#include "ChronoSystemInternal.h"

namespace Game {
// このファイルの中だけで使う型と関数
namespace {
// 街の材質の種類 (道路・歩道・塗装・コンクリート・ガラス・金属など)
enum CityMaterialId {CityRoad,CitySidewalk,CityAlleyFloor,CityPaint,CityPaintYellow,CityConcrete,CityCladding,CityFacadeTiles,CityDarkTiles,
    CitySlab,CityGreyTiles,CityShutter,CityRustShutter,CityGlass,CityMetal,CityWhiteBrick,CityWood,CityStoneSkin,CityMaterialCount};
// 材質ごとの頂点と添字
struct CityGeometry {std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;};
// 街の形を材質ごとのメッシュとして組み立てる
struct CityBuilder {
    // 材質ごとの形と、テクスチャを繰り返す大きさ
    std::array<CityGeometry,CityMaterialCount> geometry;
    std::array<float,CityMaterialCount> tile{30,10,16,6,6,20,8,12,14,18,8,8,8,8,6,10,8,22};
    // UV は面の平面に沿ってワールドの単位で付けるので、繰り返しの大きさが物理的にそろう
    // 多角形を三角形に分けて追加する (法線から UV の向きを決める)
    void Polygon(int material,std::initializer_list<Chrono::Vec> points,Chrono::Vec normal){
        // 材質の形と、追加する頂点の最初の番号
        using Chrono::Vec;auto& g=geometry[size_t(material)];uint32_t base=uint32_t(g.vertices.size());
        Vec n=Chrono::Unit(normal);Vec u=std::abs(n.y)>.99f?Vec{1,0,0}:Chrono::Unit(Chrono::SwarmCross({0,1,0},n));Vec v=Chrono::SwarmCross(n,u);
        float size=tile[size_t(material)];
        // 各頂点の位置・UV (面の上のワールド座標を繰り返しの大きさで割る)・法線
        for(Vec p:points){Engine::VertexData vertex{};vertex.position={p.x,p.y,p.z,1};vertex.texcoord={Chrono::Dot(p,u)/size,-Chrono::Dot(p,v)/size};
            vertex.normal={n.x,n.y,n.z};g.vertices.push_back(vertex);}
        const auto* p=points.begin();
        // 地形で確かめた通り、表の面は cross(p1-p0,p2-p0) が法線の向きになる
        bool forward=Chrono::Dot(Chrono::SwarmCross(p[1]-p[0],p[2]-p[0]),n)>0;
        // 扇形に三角形を作る (表の向きに合わせて頂点の順を決める)
        for(uint32_t i=1;i+1<uint32_t(points.size());++i){
            if(forward)g.indices.insert(g.indices.end(),{base,base+i,base+i+1});else g.indices.insert(g.indices.end(),{base,base+i+1,base+i});}
    }
    // 箱を作る (bottom なら底面も作る)
    void Box(int material,Chrono::Vec lo,Chrono::Vec hi,bool bottom=false){
        using Chrono::Vec;
        // 6 面を順に追加する (底は指定したときだけ)
        Polygon(material,{{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z}},{1,0,0});
        Polygon(material,{{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z}},{-1,0,0});
        Polygon(material,{{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z}},{0,0,1});
        Polygon(material,{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z}},{0,0,-1});
        Polygon(material,{{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}},{0,1,0});
        if(bottom)Polygon(material,{{lo.x,lo.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z}},{0,-1,0});
    }
    // 壁に付いた箱: along は壁に沿う向き、out は壁から出る向き。ずれは壁の平面からの距離
    void FacadeBox(int material,Chrono::Vec center,Chrono::Vec along,Chrono::Vec out,float halfAlong,float halfY,float out0,float out1){
        // パイプラインは両面を描くので、壁から始まる箱は裏の面が壁の平面に重なって
        // Z ファイティングを起こす。そこで壁に付く箱は少し壁に埋める
        // 外へ出る箱が壁から始まるなら、壁へ少し埋める
        if(out0<=.01f&&out1>out0)out0=-.4f;
        // 壁に沿う向きと外へ出る向きで箱の 2 つの角を求める
        using Chrono::Vec;Vec a=center+along*halfAlong+out*out0,b=center-along*halfAlong+out*out1;
        Box(material,{std::min(a.x,b.x),center.y-halfY,std::min(a.z,b.z)},{std::max(a.x,b.x),center.y+halfY,std::max(a.z,b.z)});
    }
    // 道路に塗った平らな帯 (a から b まで、指定の幅)
    void Strip(int material,Chrono::Vec a,Chrono::Vec b,float width,float y=-.08f){
        // 道の向きに垂直な幅の分だけ広げた四角形
        using Chrono::Vec;Vec d=Chrono::Unit(b-a),n{-d.z,0,d.x};n=n*(width*.5f);a.y=b.y=y;
        Polygon(material,{a-n,b-n,b+n,a+n},{0,1,0});
    }
};
// 建物の種類 (路地・通り・高層ビル) と建物の情報
enum CityStyle {CityStyleAlley,CityStyleStreet,CityStyleTower};
struct CityBuilding {Chrono::CityRect foot;Chrono::Vec out;float height;int style,skin,trim;bool allSides;};
// スキャンしたネズミと大砲: 敵の大きさあたりの長さと、向きに合わせるヨー
constexpr float CityRatWidth=5.6f,CityRatYaw=0,CityCannonWidth=3.4f,CityCannonYaw=0;
// 1 階と 2 階以上の高さ
constexpr float CityGroundFloor=24,CityFloor=19;
// 整数から 0〜1 の疑似乱数を作る
float CityNoise(int seed){float n=std::sin(float(seed)*12.9898f+78.233f)*43758.5453f;return n-std::floor(n);}
}
// 街のステージを作る: 影の範囲・シェーダー・材質・道路と横断歩道・建物・小物・バリケード・門・敵のモデル
void ChronoSystem::BuildCityStage(entt::registry& r){
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
    render->CreateShaderPipeline("CityGlass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityGlassPS.hlsl");
    render->CreateShaderPipeline("CityArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityArmorPS.hlsl");
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("MorphItemHighlight",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MorphItemHighlightPS.hlsl",true,true);
    // Poly Haven の材質 (色・法線・粗さ・AO) を読み込む
    auto material=[&](const std::string& id){CityModel m;
        // 色と、法線・粗さ・AO のマップ
        m.texture=render->LoadTexture2D("Resources/Textures/PolyHaven/"+id+"_diff_2k.jpg");
        for(const char* map:{"nor_dx","Rough","AO"}){std::string path="Resources/Textures/PolyHaven/"+id+"_"+map+"_2k.jpg";
            // AO の無いスキャンもある。白にすると光を遮らない
            m.extra.push_back(std::filesystem::exists(path)?render->LoadTexture2D(path,false):white_);}
        return m;};
    // 材質ごとの Poly Haven の素材の名前
    const char* surfaceIds[CityMaterialCount]={"asphalt_02","large_grey_tiles","concrete_floor_worn_001","concrete_floor_worn_001","concrete_floor_worn_001",
        "concrete_wall_008","exterior_wall_cladding_02","rectangular_facade_tiles_02","concrete_tile_facade","concrete_slab_wall","grey_tiles",
        "painted_metal_shutter","rusty_metal_shutter","concrete_floor_worn_001","concrete_floor_worn_001","white_bricks","medieval_wood","castle_wall_slates"};
    // 材質を読み込む
    std::array<CityModel,CityMaterialCount> surfaces;for(int i=0;i<CityMaterialCount;++i)surfaces[size_t(i)]=material(surfaceIds[i]);
    // 材質ごとの色の補正
    auto tint=[](int i)->DirectX::XMFLOAT4{
        // 塗装は明るく、黄色の塗装・ガラス・金属・道路は色を変える
        switch(i){case CityPaint:return {2.5f,2.5f,2.45f,1};case CityPaintYellow:return {2.5f,1.85f,.45f,1};case CityAlleyFloor:return {1.3f,1.28f,1.25f,1};case CityGlass:return {.2f,.26f,.33f,1};
            case CityMetal:return {.24f,.25f,.27f,1};case CityRoad:return {.85f,.85f,.85f,1};default:return {1,1,1,1};}};
    // 敵や破片に使う材質を記録する
    cityWood_=surfaces[CityWood];cityStoneSkin_=surfaces[CityStoneSkin];cityIron_=surfaces[CityShutter];cityArmor_=surfaces[CityMetal];
    CityBuilder city;
    // 道路: 全体をアスファルトにし、歩道・路地の床・塗装を重ねる
    city.Polygon(CityRoad,{{-1100,-.2f,-900},{1200,-.2f,-900},{1200,-.2f,1400},{-1100,-.2f,1400}},{0,1,0});
    // 床の四角形を作る関数
    auto floorQuad=[&](int m,CityRect w,float y){city.Polygon(m,{{w.x0,y,w.z0},{w.x1,y,w.z0},{w.x1,y,w.z1},{w.x0,y,w.z1}},{0,1,0});};
    // 路地の床
    for(int i=0;i<3;++i)floorQuad(CityAlleyFloor,CityWalk()[size_t(i)],0);
    // 歩道 (縁石を付ける辺を選べる)
    auto pavement=[&](CityRect w,bool curbX0,bool curbX1,bool curbZ0,bool curbZ1){floorQuad(CitySidewalk,w,.15f);
        // 縁石: 道路側の端に沿った低いコンクリートの帯 (見た目だけで、床は平らのまま)
        if(curbX0)city.Box(CityConcrete,{w.x0-.6f,-.05f,w.z0},{w.x0+.6f,.45f,w.z1});if(curbX1)city.Box(CityConcrete,{w.x1-.6f,-.05f,w.z0},{w.x1+.6f,.45f,w.z1});
        if(curbZ0)city.Box(CityConcrete,{w.x0,-.05f,w.z0-.6f},{w.x1,.45f,w.z0+.6f});if(curbZ1)city.Box(CityConcrete,{w.x0,-.05f,w.z1-.6f},{w.x1,.45f,w.z1+.6f});};
    // 大通り: 幅 30 の歩道の間の 4 車線
    pavement({-12,-472,18,-300},false,true,false,false);pavement({-12,-230,18,-72},false,true,false,false);
    pavement({138,-472,168,-300},true,false,false,false);pavement({138,-230,168,-72},true,false,false,false);
    // 車線の白線と中央の黄色い線
    for(float z=-462;z<-82;z+=40){if(z>-312&&z<-218)continue;city.Strip(CityPaint,{48,0,z},{48,0,z+20},1);city.Strip(CityPaint,{108,0,z},{108,0,z+20},1);}
    city.Strip(CityPaintYellow,{77,0,-466},{77,0,-90},.8f);city.Strip(CityPaintYellow,{79,0,-466},{79,0,-90},.8f);
    // 横断歩道の縞
    auto zebra=[&](CityRect band,bool alongZ){// 縞は道路を横切る。alongZ は道路が z 方向に通っていること
        if(alongZ){for(float x=band.x0+3;x<band.x1-2;x+=9)city.Strip(CityPaint,{x,0,band.z0},{x,0,band.z1},4.5f);}
        else for(float z=band.z0+3;z<band.z1-2;z+=9)city.Strip(CityPaint,{band.x0,0,z},{band.x1,0,z},4.5f);};
    // 大通りの横断歩道
    zebra({18,-110,138,-86},true);zebra({18,-470,138,-450},true);
    // 横の通り: 両側に歩道
    // 東西の横の通りの歩道
    for(float x0:{-160.f,164.f}){float x1=x0+152;pavement({x0,-300,x1,-286},false,false,false,true);pavement({x0,-244,x1,-230},false,false,true,false);}
    // スクランブル交差点: 幅 160 の道路 2 本が中心で交わり、四隅は歩道
    // 交差点の中心と道路の幅の半分
    const CityRect plaza=CityWalk()[7];const float cx=CityCrossing().x,cz=CityCrossing().z,h=CityRoadHalf();
    // 交差点の四隅の歩道
    pavement({plaza.x0,plaza.z0,cx-h,cz-h},false,true,false,true);pavement({cx+h,plaza.z0,plaza.x1,cz-h},true,false,false,true);
    pavement({plaza.x0,cz+h,cx-h,plaza.z1},false,true,true,false);pavement({cx+h,cz+h,plaza.x1,plaza.z1},true,false,true,false);
    // 交差点の 4 方向の横断歩道
    zebra({cx-h,cz-h-46,cx+h,cz-h-8},true);zebra({cx-h,cz+h+8,cx+h,cz+h+46},true);
    zebra({cx-h-46,cz-h,cx-h-8,cz+h},false);zebra({cx+h+8,cz-h,cx+h+46,cz+h},false);
    // スクランブル交差点の特徴の斜めの横断歩道 (縞はそれぞれの対角線に垂直)
    // 交差点の 2 本の対角線に沿った縞
    for(int d=0;d<2;++d){V a{cx-h+12,0,d?cz+h-12:cz-h+12},b{cx+h-12,0,d?cz-h+12:cz+h-12};V dir=Unit(b-a),side{-dir.z,0,dir.x};
        for(float t=0;t<Length(b-a);t+=9){V m=a+dir*t;city.Strip(CityPaint,m-side*16,m+side*16,4.5f);}}
    // 停止線
    for(float s:{-1.f,1.f}){city.Strip(CityPaint,{cx-h,0,cz+s*(h+52)},{cx+h,0,cz+s*(h+52)},2);city.Strip(CityPaint,{cx+s*(h+52),0,cz-h},{cx+s*(h+52),0,cz+h},2);}
    // 交差点から延びる道路: 車線の線を遠くまで続ける
    for(float t=plaza.z1+20;t<1350;t+=40){city.Strip(CityPaint,{cx-40,0,t},{cx-40,0,t+20},1);city.Strip(CityPaint,{cx+40,0,t},{cx+40,0,t+20},1);}
    for(float t=plaza.x1+20;t<1150;t+=40){city.Strip(CityPaint,{t,0,cz-40},{t+20,0,cz-40},1);city.Strip(CityPaint,{t,0,cz+40},{t+20,0,cz+40},1);}
    for(float t=plaza.x0-20;t>-1050;t-=40){city.Strip(CityPaint,{t,0,cz-40},{t-20,0,cz-40},1);city.Strip(CityPaint,{t,0,cz+40},{t-20,0,cz+40},1);}
    // 床の範囲の外側に沿って建物を並べる
    // 建物の一覧と乱数の種
    std::vector<CityBuilding> buildings;int seed=0;
    // 外壁の材質の候補
    const int skins[]={CityConcrete,CityCladding,CityFacadeTiles,CityGreyTiles,CityWhiteBrick,CitySlab,CityDarkTiles};
    // 建物を 1 列並べる (幅と階数は乱数で決める)
    auto row=[&](float fixed,float from,float to,V out,float depth,int minFloors,int maxFloors,int style){
        float at=from;
        // 建物の幅 (高層は広い)
        while(at<to-4){float width=std::min(to-at,(style==CityStyleTower?70.f:34.f)+CityNoise(++seed)*(style==CityStyleTower?60.f:36.f));if(to-(at+width)<20)width=to-at;
            CityRect foot{};
            // 外向きの向きで建物の敷地を決める
            if(std::abs(out.x)>.5f){float x0=out.x>0?fixed-depth:fixed,x1=out.x>0?fixed:fixed+depth;foot={x0,at,x1,at+width};}
            else{float z0=out.z>0?fixed-depth:fixed,z1=out.z>0?fixed:fixed+depth;foot={at,z0,at+width,z1};}
            // 階数と高さ
            int floors=minFloors+int(CityNoise(++seed)*float(maxFloors-minFloors+1));
            float height=CityGroundFloor+float(floors)*CityFloor;
            // 外壁と縁の材質 (種類で選ぶ)
            int skin=style==CityStyleAlley?(CityNoise(++seed)<.4f?CityWhiteBrick:CityNoise(++seed)<.5f?CityConcrete:CitySlab):skins[int(CityNoise(++seed)*6.99f)];
            int trim=style==CityStyleTower?(CityNoise(++seed)<.5f?CityDarkTiles:CitySlab):CityConcrete;
            buildings.push_back({foot,out,height,style,skin,trim,style==CityStyleTower});at+=width;}
    };
    // 裏路地: 高く近い壁
    // 裏路地の両側の壁
    row(-22,-700,-540,{1,0,0},50,6,10,CityStyleAlley);row(22,-700,-584,{-1,0,0},50,6,10,CityStyleAlley);
    row(-700,-72,72,{0,0,1},50,7,10,CityStyleAlley);row(-540,-72,6,{0,0,-1},50,6,9,CityStyleAlley);
    row(56,-540,-480,{1,0,0},50,6,8,CityStyleAlley);row(-584,72,150,{0,0,1},50,6,9,CityStyleAlley);row(100,-584,-480,{-1,0,0},50,6,10,CityStyleAlley);
    // 大通りの手前の路地
    row(-472,-62,54,{0,0,1},8,6,8,CityStyleStreet);row(-472,102,218,{0,0,1},8,6,8,CityStyleStreet);
    // 大通りと横の通り
    row(-12,-472,-300,{1,0,0},50,8,14,CityStyleStreet);row(-12,-230,-72,{1,0,0},50,8,14,CityStyleStreet);
    row(168,-472,-300,{-1,0,0},50,8,14,CityStyleStreet);row(168,-230,-72,{-1,0,0},50,8,14,CityStyleStreet);
    // 横の通りの建物
    row(-230,-160,-62,{0,0,-1},50,6,10,CityStyleStreet);row(-300,-160,-62,{0,0,1},50,6,10,CityStyleStreet);row(-160,-300,-230,{1,0,0},50,6,10,CityStyleStreet);
    row(-230,218,316,{0,0,-1},50,6,10,CityStyleStreet);row(-300,218,316,{0,0,1},50,6,10,CityStyleStreet);row(316,-300,-230,{-1,0,0},50,6,10,CityStyleStreet);
    // 双子の塔が大通りと交差点の間のシャッターの門を挟む
    // シャッターの門の両側の双子の塔
    buildings.push_back({{-62,-72,48,-40},{0,0,-1},CityGroundFloor+17*CityFloor,CityStyleTower,CityGlass,CityDarkTiles,true});
    buildings.push_back({{108,-72,218,-40},{0,0,-1},CityGroundFloor+19*CityFloor,CityStyleTower,CityGlass,CitySlab,true});
    // スクランブル交差点: 四隅に高層ビル。延びる道路の間は中層の建物
    // 交差点の四隅の高層ビルと、延びる道路沿いの建物
    row(-40,-352,-62,{0,0,1},60,12,26,CityStyleTower);row(-40,218,508,{0,0,1},60,12,26,CityStyleTower);
    row(plaza.x0,-40,cz-h,{1,0,0},60,14,30,CityStyleTower);row(plaza.x0,cz+h,plaza.z1,{1,0,0},60,14,30,CityStyleTower);
    row(plaza.x1,-40,cz-h,{-1,0,0},60,14,30,CityStyleTower);row(plaza.x1,cz+h,plaza.z1,{-1,0,0},60,14,30,CityStyleTower);
    row(plaza.z1,-352,cx-h,{0,0,-1},60,16,32,CityStyleTower);row(plaza.z1,cx+h,508,{0,0,-1},60,16,32,CityStyleTower);
    row(cx-h,plaza.z1+60,1300,{1,0,0},50,8,16,CityStyleStreet);row(cx+h,plaza.z1+60,1300,{-1,0,0},50,8,16,CityStyleStreet);
    row(cz-h,-1050,plaza.x0-60,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,-1050,plaza.x0-60,{0,0,-1},50,8,16,CityStyleStreet);
    row(cz-h,plaza.x1+60,1150,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,plaza.x1+60,1150,{0,0,-1},50,8,16,CityStyleStreet);
    // 光る看板の一覧と、ネオンの色
    cityGlows_.clear();
    const Engine::Vector4 neon[]={{1,.25f,.55f,1},{.2f,.85f,1,1},{1,.75f,.2f,1},{.55f,1,.35f,1},{.75f,.4f,1,1}};
    // 各建物: 本体の箱・屋上・正面 (高層ビルはガラスの外壁、それ以外は店と窓)・カメラ用の当たり判定
    for(const auto& b:buildings){
        const auto& f=b.foot;
        // 建物の本体の箱
        city.Box(b.skin==CityGlass?b.trim:b.skin,{f.x0,0,f.z0},{f.x1,b.height,f.z1});
        // 屋上: 手すりの輪と植木の箱
        float p=1.2f;
        // 屋上の 4 辺の手すり
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x1,b.height+2.5f,f.z0+p});city.Box(b.trim,{f.x0,b.height,f.z1-p},{f.x1,b.height+2.5f,f.z1});
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x0+p,b.height+2.5f,f.z1});city.Box(b.trim,{f.x1-p,b.height,f.z0},{f.x1,b.height+2.5f,f.z1});
        // 高層ビルの屋上の塔とアンテナ
        if(b.style==CityStyleTower){V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float hx=(f.x1-f.x0)*.5f,hz=(f.z1-f.z0)*.5f;
            float crown=12+CityNoise(++seed)*22;
            city.Box(b.trim,c+V{-hx+6,0,-hz+6},c+V{hx-6,crown,hz-6});
            city.Box(CityMetal,c+V{-hx*.45f,crown,-hz*.45f},c+V{hx*.45f,crown+9,hz*.45f});
            if(CityNoise(++seed)<.6f)city.Box(CityMetal,c+V{-.8f,crown+9,-.8f},c+V{.8f,crown+9+30+CityNoise(++seed)*40,.8f});}
        // 屋上の設備
        {V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float sx=(f.x1-f.x0)*.18f,sz=(f.z1-f.z0)*.18f;
            city.Box(CityConcrete,c+V{-sx,0,-sz},c+V{sx*.3f,9,sz*.4f});city.Box(CityMetal,c+V{sx*.5f,0,-sz},c+V{sx*1.4f,6,-sz*.1f});}
        // 4 つの面のうち、正面 (全面を飾る高層は全部) だけを飾る
        V faces[4]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        for(V out:faces){
            bool main=std::abs(out.x-b.out.x)<.1f&&std::abs(out.z-b.out.z)<.1f;
            if(!main&&!b.allSides)continue;
            // 面に沿う向き・面の幅・面の始点・中央
            V along=std::abs(out.x)>.5f?V{0,0,1}:V{1,0,0};
            float width=std::abs(out.x)>.5f?f.z1-f.z0:f.x1-f.x0;
            V face=std::abs(out.x)>.5f?V{out.x>0?f.x1:f.x0,0,f.z0}:V{f.x0,0,out.z>0?f.z1:f.z0};
            V mid=face+along*(width*.5f);
            if(b.style==CityStyleTower){
                // 低層部: 柱の間の全面ガラスの店、その上はガラスの帯と腰壁のカーテンウォール
                float podium=CityGroundFloor+CityFloor;
                city.FacadeBox(CityGlass,mid+V{0,podium*.5f,0},along,out,width*.5f,podium*.5f,0,.45f);
                for(float s=0;s<=width+.1f;s+=16)city.FacadeBox(b.trim,face+along*std::min(s,width-1.f)+V{0,podium*.5f,0},along,out,1,podium*.5f,0,1.2f);
                city.FacadeBox(b.trim,mid+V{0,podium+1.5f,0},along,out,width*.5f+.5f,1.5f,0,2.5f);
                for(float y=podium+3;y<b.height-2;y+=CityFloor){city.FacadeBox(CityGlass,mid+V{0,y+8.5f,0},along,out,width*.5f,7.5f,0,.45f);
                    city.FacadeBox(b.trim,mid+V{0,y+17.5f,0},along,out,width*.5f,1.5f,0,1.1f);}
                for(float s=8;s<width-2;s+=12)city.FacadeBox(CityMetal,face+along*s+V{0,(podium+b.height)*.5f,0},along,out,.35f,(b.height-podium)*.5f,0,1.6f);
                // 低層部の上の大きな画面は交差点の方を向く
                if(width>60&&CityNoise(++seed)<.55f){float w=std::min(width*.5f,46.f);V c=mid+V{0,podium+30,0}+out*1.2f;
                    city.FacadeBox(CityMetal,c,along,out,w*.5f+1,13,0,1);
                    cityGlows_.push_back({c+out*1.5f,along,{0,1,0},{w,24,.4f},neon[seed%5]});}
                continue;
            }
            // 柱の間の数
            int bays=std::max(1,int(width/14));
            // 1 階: 通りはガラス・シャッター・看板の帯の店、路地は錆びたシャッターと扉
            // 1 階の各区画
            for(int bay=0;bay<bays;++bay){V c=face+along*(width*(float(bay)+.5f)/float(bays));float half=width/float(bays)*.5f-1.2f;
                // 路地: 扉か錆びたシャッター
                if(b.style==CityStyleAlley){bool door=(bay+seed)%3==0;
                    if(door){city.FacadeBox(CityMetal,c+V{0,9,0},along,out,3.4f,9,0,.3f);city.FacadeBox(CityConcrete,c+V{0,18.6f,0},along,out,4.4f,.6f,0,.8f);}
                    else{city.FacadeBox(CityRustShutter,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);city.FacadeBox(CityMetal,c+V{0,20.2f,0},along,out,half+.4f,1.2f,0,1.4f);}}
                // 通り: シャッターかガラスの店と、柱
                else{bool shut=CityNoise(seed*7+bay)<.25f;
                    city.FacadeBox(shut?CityShutter:CityGlass,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);
                    for(float s:{-1.f,1.f})city.FacadeBox(CityMetal,c+along*(s*(half+.6f))+V{0,10,0},along,out,.6f,10,0,.8f);}}
            // 通りの建物の看板の帯
            if(b.style==CityStyleStreet){city.FacadeBox(CityDarkTiles,mid+V{0,22,0},along,out,width*.5f,2,0,1.6f);
                // 通りの一部の正面には縦のネオン看板を付ける
                if(CityNoise(++seed)<.45f){V c=face+along*(width*(.15f+.7f*CityNoise(++seed)))+V{0,48,0}+out*4;
                    city.FacadeBox(CityMetal,c,along,out,.6f,1,0,4);
                    cityGlows_.push_back({c+out*2.2f,out,{0,1,0},{3,32,2.2f},neon[seed%5]});}}
            // 上の階: 枠と窓台のある窓。通りの一部の建物にはバルコニー
            // 上の各階の窓
            for(float y=CityGroundFloor;y+CityFloor<=b.height+.1f;y+=CityFloor){float wy=y+10;
                for(int bay=0;bay<bays;++bay){V c=face+along*(width*(float(bay)+.5f)/float(bays))+V{0,wy,0};
                    float half=b.style==CityStyleAlley?2.6f:std::min(4.6f,width/float(bays)*.5f-2.f);
                    city.FacadeBox(CityGlass,c,along,out,half,5,0,.3f);
                    city.FacadeBox(CityMetal,c-V{0,5.4f,0},along,out,half+.6f,.4f,0,1.1f);
                    city.FacadeBox(CityMetal,c+V{0,5.3f,0},along,out,half+.4f,.3f,0,.8f);
                    for(float s:{-1.f,1.f})city.FacadeBox(CityMetal,c+along*(s*(half+.2f)),along,out,.25f,5,0,.7f);
                    if(b.style==CityStyleAlley&&CityNoise(int(wy)*13+bay*7+seed)<.18f)city.FacadeBox(CityMetal,c+along*(half+5)-V{0,3,0},along,out,2.4f,1.8f,0,2.4f);
                    if(b.style==CityStyleStreet&&CityNoise(int(wy)*17+bay*5+seed)<.22f){
                        city.FacadeBox(CityConcrete,c-V{0,6.2f,0},along,out,half+2.5f,.5f,0,4.5f);city.FacadeBox(CityMetal,c-V{0,4.4f,0}+out*4.3f,along,out,half+2.5f,1.3f,0,.2f);}}}
            // 路地の建物の縦の配管
            if(b.style==CityStyleAlley)for(int k=0;k<2;++k){V c=face+along*(width*(.2f+.6f*float(k)))+V{0,b.height*.5f,0};
                city.FacadeBox(CityMetal,c,along,out,.5f,b.height*.5f,.3f,1.3f);}
        }
        // 追従カメラが通りの中に留まるよう、カメラ用の当たり判定を置く
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City building collision");
        r.emplace<TransformComponent>(e).translate=Write(V{(f.x0+f.x1)*.5f,b.height*.5f,(f.z0+f.z1)*.5f});
        r.emplace<BoxColliderComponent>(e).size=Write(V{f.x1-f.x0,b.height,f.z1-f.z0});
        r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
    }
    // シャッターの門の上のまぐさで双子の塔をつなぐ
    city.Box(CityDarkTiles,{46,58,-64},{110,96,-48});
    // 材質ごとのメッシュをエンティティとして置く
    for(int i=0;i<CityMaterialCount;++i){auto& g=city.geometry[size_t(i)];if(g.indices.empty())continue;
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City surface ")+surfaceIds[i]);
        auto& t=r.emplace<TransformComponent>(e);t.translate={0,0,0};t.scale={1,1,1};t.rotate={0,0,0};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.useCubemap=false;
        mr.textureHandle=surfaces[size_t(i)].texture;mr.extraTextureHandles=surfaces[size_t(i)].extra;mr.shaderName=i==CityGlass?"CityGlass":"CitySurface";mr.color=tint(i);}
    // 小物はスキャンの比率を保つ。目標の幅で等倍の拡大率を決め、一番低い頂点を路面に置く
    // 読み込んだ小物のモデルのキャッシュ
    std::map<std::string,CityModel> models;
    // モデルを読み込み、大きさの範囲を求める (同じモデルは使い回す)
    auto model=[&](const std::string& id)->CityModel& {auto found=models.find(id);if(found!=models.end())return found->second;
        CityModel m=material(id);m.mesh=render->LoadObjMesh("Resources/Models/PolyHaven/"+id+"_2k.fbx");
        if(auto* source=render->GetModel(m.mesh)){m.lo={1e9f,1e9f,1e9f};m.hi={-1e9f,-1e9f,-1e9f};
            for(const auto& v:source->GetData().vertices){m.lo={std::min(m.lo.x,v.position.x),std::min(m.lo.y,v.position.y),std::min(m.lo.z,v.position.z)};
                m.hi={std::max(m.hi.x,v.position.x),std::max(m.hi.y,v.position.y),std::max(m.hi.z,v.position.z)};}
            render->PrepareDistanceLods(m.mesh);}
        return models.emplace(id,m).first->second;};
    // 小物と見た目の記録を消す
    cityProps_.clear();cityVisuals_.clear();
    // スキャンした小物を置く (required が 0 より大きければ取り込める物になる)
    auto place=[&](const std::string& id,V at,float width,float yaw,float required=0,float reward=0){
        // 幅から拡大率を決め、一番低い頂点を地面に合わせて置く
        const auto& m=model(id);V lo=m.lo,hi=m.hi;
        float scale=width/std::max({hi.x-lo.x,hi.z-lo.z,.001f});V mid=(lo+hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        // 小物のエンティティを作る
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop "+id);
        auto& t=r.emplace<TransformComponent>(e);t.scale=Write(V{scale,scale,scale});t.rotate=Write(V{0,yaw,0});
        t.translate=Write(V{at.x-(mid.x*cs+mid.z*sn)*scale,at.y-lo.y*scale,at.z-(-mid.x*sn+mid.z*cs)*scale});
        // メッシュ・材質・シェーダー
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=m.mesh;mr.textureHandle=m.texture;mr.extraTextureHandles=m.extra;mr.shaderName="CitySurface";mr.useCubemap=false;
        // 見た目の中心を記録する (触手で運ぶときに使う)
        V center=V{at.x,at.y+(mid.y-lo.y)*scale,at.z};
        cityVisuals_[uint32_t(e)]={m.mesh,m.texture,m.extra,{scale,scale,scale},{0,yaw,0},center-Read(t.translate),(hi.y-lo.y)*scale,"CitySurface"};
        // 触手は背の高い小物を足元近くでつかむので、取り込みの高さの判定が路面と合う
        if(required>0)cityProps_.push_back({e,V{at.x,at.y+std::min(1.2f,(mid.y-lo.y)*scale),at.z},required,reward,id=="old_tyre"?LifeMorphItem::Tire:LifeMorphItem::None});
        return e;};
    // 作った小物 (街灯・シャッターの門) はそれぞれ 1 つのメッシュを共有し、スキャンと同じように取り込める
    // 作ったメッシュの小物を置く (取り込める物にもできる)
    auto placeMesh=[&](const char* name,uint32_t mesh,int skin,V at,float yaw,float height,float required,float reward,LifeMorphItem item=LifeMorphItem::None){
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City prop ")+name);
        auto& t=r.emplace<TransformComponent>(e);t.scale={1,1,1};t.rotate=Write(V{0,yaw,0});t.translate=Write(at);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=mesh;mr.textureHandle=surfaces[size_t(skin)].texture;mr.extraTextureHandles=surfaces[size_t(skin)].extra;
        mr.shaderName="CitySurface";mr.useCubemap=false;mr.color=tint(skin);
        cityVisuals_[uint32_t(e)]={mesh,surfaces[size_t(skin)].texture,surfaces[size_t(skin)].extra,{1,1,1},{0,yaw,0},{0,height*.5f,0},height,"CitySurface"};
        if(required>0)cityProps_.push_back({e,at+V{0,1.2f,0},required,reward,item});
        return e;};
    // ローカルの形から金属のメッシュを作る
    auto localMesh=[&](auto&& build){CityBuilder local;build(local);auto& g=local.geometry[CityMetal];return render->CreateDynamicMesh(g.vertices,g.indices);};
    // 現代的な街灯: 台座・柱・張り出した腕・照明 (ローカルの +x が腕の向き)
    uint32_t lampMesh=localMesh([](CityBuilder& b){b.Box(CityMetal,{-1,0,-1},{1,2.4f,1});b.Box(CityMetal,{-.45f,0,-.45f},{.45f,42,.45f});
        b.Box(CityMetal,{0,40.4f,-.3f},{12,41.2f,.3f});b.Box(CityMetal,{9,39.2f,-1.4f},{14.5f,40.6f,1.4f});});
    // 街灯を置く
    auto lamp=[&](V at,float yaw){placeMesh("street light",lampMesh,CityMetal,at,yaw,42,420,30);};
    // エリア 1: スライムの大きさに合った裏路地の散らかった物 (最初から取り込める)
    // 見える所の、最初の取り込み範囲に必ず触媒を置く
    // タイヤ (変形アイテム)
    place("old_tyre",{6,0,-672},3.8f,.35f,15,12);
    // 小さな散らかった物の種類・幅・報酬
    const char* smallIds[]={"trashbag","cardboard_box_01","plastic_crate_01","plastic_crate_02","metal_jerrycan","old_tyre","WetFloorSign_01","wooden_crate_01"};
    const float smallWidth[]={3.4f,3.2f,3,3.4f,2.6f,3.8f,2.4f,4.6f},smallReward[]={12,12,10,10,10,12,8,14};
    // 散らかった物を順に置く
    int clutter=0;
    // 小さな散らかった物を置く
    auto scatter=[&](V at){int k=clutter%8;place(smallIds[k],at,smallWidth[k]*(.9f+.25f*CityNoise(clutter*3+1)),CityNoise(clutter*3+2)*6.283f,15,smallReward[k]);++clutter;};
    // 路地の散らかった物の位置
    for(V at:{V{-16,0,-690},V{-12,0,-684},V{15,0,-672},V{17,0,-664},V{-17,0,-652},V{-14,0,-646},V{16,0,-636},V{-17,0,-622},V{18,0,-608},
              V{-15,0,-600},V{-17,0,-572},V{4,0,-556},V{22,0,-578},V{40,0,-580},V{94,0,-572},V{94,0,-548},V{61,0,-536},V{94,0,-526},
              V{62,0,-512},V{95,0,-500},V{0,0,-661},V{-6,0,-592},V{60,0,-492},V{96,0,-490}})scatter(at);
    // 中くらいの物の種類・幅・取り込みに必要な量
    const char* mediumIds[]={"industrial_pastic_container","plastic_container","propane_tank","wooden_crate_02","hand_truck"};
    const float mediumWidth[]={6,5.6f,3.6f,6.8f,4};const float mediumRequired[]={120,100,110,140,130};
    int medium=0;
    // 中くらいの物を置く
    for(V at:{V{-15,0,-676},V{16,0,-628},V{-16,0,-612},V{14,0,-590},V{90,0,-566},V{62,0,-550},V{94,0,-512},V{62,0,-500},V{-14,0,-560},V{30,0,-552}}){
        int k=medium%5;place(mediumIds[k],at,mediumWidth[k],CityNoise(medium*5+3)*6.283f,mediumRequired[k],30);++medium;}
    // 配電盤とマンホール
    for(V at:{V{-19,0,-640},V{19,0,-598},V{97,0,-534}})place("power_box_01",at,3.4f,at.x<0?1.5708f:-1.5708f,160,26);
    place("water_manhole_cover",{2,.02f,-644},4.4f,0);place("water_manhole_cover",{78,.02f,-520},4.4f,.7f);
    // 工事のバリケード: 柱の間の合板の囲いと、後ろに積んだ木箱。ハンマーで壊す
    // バリケードの柱・板・斜めの補強・木箱
    cityBarricade_.clear();const V bc=CityBarricadeCenter();
    for(float x:{-21.f,0.f,21.f})cityBarricade_.push_back({bc+V{x,7,0},{1.6f,14,1.6f},{0,0,0}});
    for(int i=0;i<6;++i){float y=1.6f+float(i)*2.25f;cityBarricade_.push_back({bc+V{(CityNoise(i)-.5f)*2.f,y,.8f+float(i%2)*.4f},{46,1.7f,.55f},{0,0,(CityNoise(i+9)-.5f)*.06f}});}
    for(float s:{-1.f,1.f})cityBarricade_.push_back({bc+V{s*10,7,1.6f},{25,1.4f,.5f},{0,0,s*.48f}});
    for(int i=0;i<5;++i)cityBarricade_.push_back({bc+V{-16+float(i)*8,2.4f,-3.5f},{4.8f,4.8f,4.8f},{0,CityNoise(i+31)*.6f,0}});
    // エリア 2: 大通り。街灯・止めた車・公衆電話・配電盤
    // 消火栓のメッシュ (円柱を組み合わせる)
    uint32_t hydrantMesh=localMesh([](CityBuilder& b){
        // 円柱 (先細りも可) を作る
        auto cylinder=[&](V origin,V axis,V right,V up,float radius,float endRadius,float length){
            constexpr int sides=16;for(int i=0;i<sides;++i){float a=float(i)*6.2831853f/sides,c=float(i+1)*6.2831853f/sides;
                V ra=right*std::cos(a)+up*std::sin(a),rc=right*std::cos(c)+up*std::sin(c),end=origin+axis*length;
                b.Polygon(CityMetal,{origin+ra*radius,origin+rc*radius,end+rc*endRadius,end+ra*endRadius},Unit(ra+rc));
                b.Polygon(CityMetal,{origin,origin+rc*radius,origin+ra*radius},axis*-1.f);
                b.Polygon(CityMetal,{end,end+ra*endRadius,end+rc*endRadius},axis);}};
        cylinder({0,0,0},{0,1,0},{1,0,0},{0,0,1},1.75f,1.75f,.5f);
        cylinder({0,.5f,0},{0,1,0},{1,0,0},{0,0,1},1.15f,1.15f,3.5f);
        cylinder({0,4,0},{0,1,0},{1,0,0},{0,0,1},1.2f,.2f,1.1f);
        cylinder({-2.2f,2.9f,0},{1,0,0},{0,1,0},{0,0,1},.45f,.45f,4.4f);
        cylinder({0,3.1f,1},{0,0,1},{1,0,0},{0,1,0},.5f,.5f,1.2f);
    });
    // 消火栓を置く (赤くする)
    for(V at:{V{86,0,-460},V{8,0,-270},V{144,0,-120}}){auto e=placeMesh("fire hydrant",hydrantMesh,CityMetal,at,0,5.1f,15,24,LifeMorphItem::Hydrant);
        r.get<MeshRendererComponent>(e).color={1.8f,.22f,.08f,1};}
    // 街灯を両側に置く
    for(int i=0;i<5;++i){float z=-440+float(i)*72;if(z>-312&&z<-218)z+=96;lamp({14,0,z},0);lamp({142,0,z+36},3.14159f);}
    // 止めた車
    for(V at:{V{30,0,-430},V{30,0,-360},V{126,0,-400},V{126,0,-330},V{30,0,-190},V{126,0,-150},V{126,0,-200},V{30,0,-130}})
        place("covered_car",at,26,0,600,45);
    // 公衆電話・配電盤
    for(V at:{V{0,0,-420},V{156,0,-180},V{2,0,-140}})place("korean_public_payphone_01",at,4.4f,at.x<50?1.5708f:-1.5708f,300,24);
    for(V at:{V{-2,0,-350},V{158,0,-430},V{158,0,-110},V{-2,0,-100}})place(CityNoise(int(at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,at.x<50?1.5708f:-1.5708f,260,20);
    // 横の通りの車とコンテナ
    for(V at:{V{-120,0,-265},V{-60,0,-265},V{220,0,-265},V{290,0,-265}})place("covered_car",at,26,1.5708f,600,45);
    for(V at:{V{-148,0,-292},V{-20,0,-236},V{176,0,-238},V{304,0,-292}})place("industrial_pastic_container",at,6,CityNoise(int(at.x))*6.283f,120,22);
    // 交差点の手前のバリア
    for(float x:{58.f,78.f,98.f})place(x==78?"concrete_road_barrier_02":"concrete_road_barrier",{x,0,-96},10,0,350,25);
    // マンホール
    for(V at:{V{6,0,-460},V{150,0,-460}})place("water_manhole_cover",at+V{0,.02f,0},4.4f,0);
    // 鋼鉄のシャッターの門: 騎士団を倒した後に取り込むか壊す
    // シャッターの門のメッシュ (板と枠)
    uint32_t gateMesh=[&]{CityBuilder local;local.Box(CityShutter,{-31,0,-1.2f},{31,58,1.2f});local.Box(CityShutter,{-32,52,-3.5f},{32,58,3.5f});
        auto& g=local.geometry[CityShutter];return render->CreateDynamicMesh(g.vertices,g.indices);}();
    // 門を置き、当たり判定を付ける
    cityGateEntity_=placeMesh("shutter gate",gateMesh,CityShutter,CityGateCenter(),0,58,0,0);
    {auto& collider=r.emplace<BoxColliderComponent>(cityGateEntity_);collider.size={64,58,2.4f};collider.center={0,29,0};r.emplace<TagComponent>(cityGateEntity_).tag=TagType::Wall;}
    // エリア 3: スクランブル交差点。四隅の街灯・放置された車・バリアの列・像
    // 交差点の四隅と辺の街灯
    for(float sx:{-1.f,1.f})for(float sz:{-1.f,1.f}){V corner{cx+sx*(h+10),0,cz+sz*(h+10)};
        lamp(corner,sx>0?(sz>0?-2.356f:2.356f):(sz>0?-.785f:.785f));
        lamp(corner+V{sx*90,0,0},sz>0?-1.5708f:1.5708f);lamp(corner+V{0,0,sz*90},sx>0?3.14159f:0);}
    // 放置された車 (交差点の中だけ)
    for(int i=0;i<14;++i){float a=float(i)*.4488f,radius=40+CityNoise(i*7)*120;V at{cx+std::cos(a)*radius,0,cz+std::sin(a)*radius};
        if(std::abs(at.x-cx)>h&&std::abs(at.z-cz)>h)continue;place("covered_car",at,26,a+CityNoise(i*11)*1.2f,600,45);}
    // 大砲の周りのバリア
    for(V cannon:{V{-172,0,90},V{328,0,90},V{-172,0,590},V{328,0,590}})
        for(int k=0;k<3;++k){float a=float(k)*1.0472f+(cannon.z>cz?3.6652f:.5236f)+(cannon.x>cx?.5236f:0);
            place(k==1?"concrete_road_barrier_02":"concrete_road_barrier",cannon+V{std::cos(a)*20,0,std::sin(a)*20},10,a+1.5708f,350,25);}
    // 遊べる交差点の端で、延びる道路を封鎖する
    // 延びる道路の封鎖
    for(float t=-60;t<=60;t+=20){place("concrete_road_barrier",{cx+t,0,plaza.z1-6},10,0);place("concrete_road_barrier",{plaza.x0+6,0,cz+t},10,1.5708f);place("concrete_road_barrier",{plaza.x1-6,0,cz+t},10,1.5708f);}
    // 公衆電話・配電盤
    for(V at:{V{-280,0,-20},V{440,0,-20},V{-280,0,700},V{440,0,700}})place("korean_public_payphone_01",at,4.4f,0,300,24);
    for(V at:{V{-40,0,200},V{196,0,200},V{-40,0,480},V{196,0,480},V{-260,0,240},V{420,0,440}})place(CityNoise(int(at.x+at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,0,260,20);
    // 台座と像
    {V pedestal{-200,0,-10};CityBuilder local;local.Box(CityMetal,{-8,0,-8},{8,6,8});auto& g=local.geometry[CityMetal];
        placeMesh("statue pedestal",render->CreateDynamicMesh(g.vertices,g.indices),CityConcrete,pedestal,0,6,0,0);
        place("gothic_statue",pedestal+V{0,6,0},12,.6f,2200,260);}
    // 敵のモデル (ネズミと大砲)
    cityRat_=model("street_rat");cityCannon_=model("cannon_01");
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
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();cityGateProp_=-1;cityAnnounced_=-1;
}
// 段階の敵を出す
void ChronoSystem::SpawnCityStep(int step){
    using namespace Chrono;
    // 段階の敵を作り、地面の高さに置く
    for(const auto& spawn:CityStepSpawns(step)){
        auto enemy=CitySpawnEnemy(spawn,uint32_t(swarm_.size()+1),step);enemy.at.y=InkGround(enemy.at)+enemy.scale;swarm_.push_back(enemy);}
}
// 現在の段階で生きている敵の数
int ChronoSystem::CityStepAlive()const{
    // 今の段階で生きている敵を数える
    int alive=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.step==life_.step)++alive;return alive;
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
    if(barricade){cityGates_.barricadeOpen=true;burst(CityBarricadeCenter()+V{0,4,0},{44,14,1},0,26,10);
        life_.message="バリケード破壊！ 市街地へ";life_.messageAge=3;}
    // 門: 開けて当たり判定と表示を消す (取り込んだときは破片なし)
    else{cityGates_.gateOpen=true;
        if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=false;
        if(r.valid(cityGateEntity_))r.get<MeshRendererComponent>(cityGateEntity_).enabled=false;
        if(cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size())life_.props[size_t(cityGateProp_)].active=false;
        if(!absorbed)burst(CityGateCenter()+V{0,10,0},{60,50,1},1,30,14);
        life_.message=absorbed?"シャッターごと吸い込んだ！ 大交差点へ":"シャッターを破壊！ 大交差点へ";life_.messageAge=3;}
    // 壊れた音
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
// スライムの武器がバリケードや門に当たったか (ハンマーで壊せる。門は騎士団を倒した後だけ)
void ChronoSystem::CityWeaponHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,int pulse,uint64_t hitKey){
    using namespace Chrono;
    // 武器が中心から左右に並んだ点のどこかに触れたか
    auto touches=[&](V center,float y,float radius){for(float x:{-16.f,0.f,16.f})if(LifeWeaponTouches(b,center+V{x,y,0},radius,pulse))return true;return false;};
    // バリケード: ハンマーなら体力を減らし、尽きたら壊す。それ以外の武器は案内を出す
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touches(CityBarricadeCenter(),6,8)){cityBarricadeHit_=hitKey;
        if(b.action==LifeAction::Hammer){cityBarricadeHealth_-=LifeWeaponDamage(b);cityBarricadeShake_=.35f;
            ctx.camera->StartImpactShake(.12f,.14f,{0,1,0},16);swarmEffects_.push_back({CityBarricadeCenter()-V{0,0,4},0,10,2});
            if(cityBarricadeHealth_<=0)BreakCityGate(r,true,false);
            else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth()*100)))+"%";life_.messageAge=1.2f;}}
        else{life_.message="工事バリケードはハンマーで叩き壊せる";life_.messageAge=2;}
    }
    // 門: 騎士団を倒すまでは案内だけ。それ以外は体力を減らし、尽きたら壊す
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&(touches(CityGateCenter(),10,10)||touches(CityGateCenter()+V{0,0,-3},30,10))){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="騎士団がシャッターを守っている / 先に騎士団を倒そう";life_.messageAge=2;return;}
        cityGateHealth_-=LifeWeaponDamage(b)*(b.action==LifeAction::Hammer?1.4f:1.f);cityGateShake_=.35f;
        ctx.camera->StartImpactShake(.12f,.16f,{0,1,0},14);swarmEffects_.push_back({CityGateCenter()-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0)BreakCityGate(r,false,false);
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth()*100)))+"% / 本体"+std::to_string(int(CityGateAbsorbMass()))+"ならシャッターごと吸い込める";life_.messageAge=1.4f;}
    }
}
// 街のステージの更新: エリアの表示・門の開放・ヒント・段階の進行・破片・敵の弾・踏みつけ
void ChronoSystem::UpdateCityStage(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    using namespace Chrono;auto& body=life_.bodies[0];
    // 揺れとヒントの時間を減らす
    cityBarricadeShake_=std::max(0.f,cityBarricadeShake_-dt);cityGateShake_=std::max(0.f,cityGateShake_-dt);cityHintClock_=std::max(0.f,cityHintClock_-dt);
    // 新しいエリアに入ったら名前を表示する
    auto area=CityAreaAt(body.at);
    if(int(area)!=cityAnnounced_){cityAnnounced_=int(area);
        life_.message="AREA "+std::to_string(int(area)+1)+"  "+CityAreaName(area);life_.messageAge=3;}
    // 今の段階で生きている敵の数
    int alive=CityStepAlive();
    // 騎士団が倒れると、鉄の門が取り込める (または壊せる) 対象になる
    if(!cityGateUnlocked_&&(life_.step>4||(life_.step==4&&alive==0))){cityGateUnlocked_=true;
        if(cityGateProp_>=0)life_.props[size_t(cityGateProp_)].active=true;
        life_.message="シャッターの守りが解けた！ 本体"+std::to_string(int(CityGateAbsorbMass()))+"でシャッターごと吸い込む / 武器で破壊";life_.messageAge=4;}
    // 門が取り込まれたら壊す
    if(cityGateUnlocked_&&!cityGates_.gateOpen&&cityGateProp_>=0&&!life_.props[size_t(cityGateProp_)].active)BreakCityGate(r,false,true);
    // 閉じた門に近づいたら、大きさに応じたヒントを出す
    if(cityHintClock_<=0&&life_.messageAge<=0){
        if(!cityGates_.barricadeOpen&&LifeDistance(body.at,CityBarricadeCenter())<34){cityHintClock_=4;
            life_.message=body.mass<LifeHammerMass?"本体"+std::to_string(int(LifeHammerMass))+"まで育ててハンマーに変形し、バリケードを叩き壊そう":std::string("1でハンマーを選び、左クリック／RTで叩き壊そう");life_.messageAge=3;}
        else if(!cityGates_.gateOpen&&LifeDistance(body.at,CityGateCenter())<50){cityHintClock_=4;
            life_.message=!cityGateUnlocked_?std::string("騎士団を倒すとシャッターの守りが解ける"):body.mass<CityGateAbsorbMass()?"本体"+std::to_string(int(CityGateAbsorbMass()))+"でシャッターごと吸い込める / 今は武器で破壊":std::string("右クリック／Xでシャッターごと吸い込もう");life_.messageAge=3;}
    }
    // 門が開いていて敵がいなければ、少し待って次の段階へ進む
    bool gate=life_.step==1?cityGates_.barricadeOpen:life_.step==4?cityGates_.gateOpen:true;
    if(alive==0&&gate&&life_.step+1<CityStepCount){
        life_.stepDelay=std::max(0.f,life_.stepDelay-dt);
        if(life_.stepDelay<=0){++life_.step;life_.stepDelay=2;SpawnCityStep(life_.step);
            life_.message="段階 "+std::to_string(life_.step+1)+" / "+std::to_string(CityStepCount)+"  "+CityStepInfo(life_.step).title;life_.messageAge=3.5f;}
    }
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
        if(shot.shell){if(shot.at.y<=ground){shot.age=99;swarmEffects_.push_back({shot.at,0,11,1});ctx.camera->StartImpactShake(.1f,.08f,{0,1,0},14);
                hurt({shot.at.x,body.at.y,shot.at.z},9,CityShotDamage(true));}}
        // 矢は当たるか地面・壁で消える
        else if(hurt(shot.at,.6f,CityShotDamage(false))||shot.at.y<=ground||!CityWalkable({shot.at.x,0,shot.at.z},.1f,{true,true}))shot.age=99;
    }
    // 古い弾を消す
    cityShots_.erase(std::remove_if(cityShots_.begin(),cityShots_.end(),[](const CityShot& s){return s.age>4;}),cityShots_.end());
    // 守護者の踏みつけ: 0.85 秒後に範囲にダメージ
    for(auto& stomp:cityStomps_){float previous=stomp.age;stomp.age+=dt;
        if(previous<.85f&&stomp.age>=.85f){swarmEffects_.push_back({stomp.at,0,stomp.radius,1});ctx.camera->StartImpactShake(.3f,.22f,{0,1,0},9);
            hurt({stomp.at.x,body.at.y,stomp.at.z},stomp.radius,22);}}
    cityStomps_.erase(std::remove_if(cityStomps_.begin(),cityStomps_.end(),[](const CityStomp& s){return s.age>1.2f;}),cityStomps_.end());
}
// 街の敵の更新: 種類ごとの行動 (大砲・巡回・弓兵・突進と守護者) と、本体や分身への攻撃
void ChronoSystem::UpdateCityEnemies(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    // 攻撃中の数と、同時に攻撃できる数
    using namespace Chrono;auto& ink=r.get<InkPlayer>(player_);auto& body=life_.bodies[0];
    int active=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash))++active;
    const int maxActive=life_.step>=5?4:2;
    // 生きている敵の数を数え直す
    ink.swarmAlive=0;
    // 一番近いスライムの体を狙う
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        // 敵の種類
        ++ink.swarmAlive;auto kind=CityKind(enemy);
        int victim=0;float nearest=LifeDistance(body.at,enemy.at);
        for(size_t i=1;i<life_.bodies.size();++i)if(life_.bodies[i].mass>0&&!life_.bodies[i].down){float d=LifeDistance(life_.bodies[i].at,enemy.at);if(d<nearest){nearest=d;victim=int(i);}}
        // 狙う体と、前の位置と状態
        auto& targetBody=life_.bodies[size_t(victim)];V before=enemy.at;auto phase=enemy.phase;
        // よろけの時間を減らす
        enemy.stagger=std::max(0.f,enemy.stagger-dt);
        // 近づいたら気付く
        if(!enemy.alerted&&nearest<CityAlertRange(kind)){enemy.alerted=true;
            // 侵入者を見つけたら、同じ巡回の近くの仲間にも知らせる
            for(auto& other:swarm_)if(SwarmAlive(other)&&!other.alerted&&LifeDistance(other.at,enemy.at)<45)other.alerted=true;}
        // 触手につかまれているか
        bool held=LifeTargetReserved(life_,enemy.id);
        // 狙う体への向きと距離
        V toward=targetBody.at-enemy.at;toward.y=0;float distance=Length(toward);V direction=distance>.01f?toward*(1/distance):enemy.heading;
        // 時間を進める関数 (出現が終わったら追跡へ)
        auto idleTick=[&](){enemy.age+=dt;enemy.clock+=dt;enemy.flash=std::max(0.f,enemy.flash-dt);
            if(enemy.phase==SwarmPhase::Spawn&&enemy.clock>=.6f)ChangeSwarmPhase(enemy,SwarmPhase::Chase);};
        // 触手につかまれている間は動かない
        if(held){}
        // 大砲: 向きを合わせて放物線の弾を撃つ
        else if(kind==CityEnemy::Cannon){idleTick();
            enemy.heading=Unit(Lerp(enemy.heading,direction,1-std::exp(-2.5f*dt)));
            if(enemy.alerted&&enemy.phase!=SwarmPhase::Spawn){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<170){enemy.shotClock=4+SwarmVariation(enemy.id,enemy.attacks++)*1.5f;
                    V muzzle=enemy.at+enemy.heading*(enemy.scale*1.6f)+V{0,enemy.scale*.6f,0};V target=targetBody.at;target.y=InkGround(target);
                    cityShots_.push_back({muzzle,CityShellVelocity(muzzle,target,1.7f),0,true,target});enemy.flash=.25f;
                    ctx.camera->StartImpactShake(.06f,.04f,{0,1,0},18);}}}
        else if(!enemy.alerted){idleTick();
            // 家と巡回の地点の間を歩く速さで巡回する
            V goal=enemy.patrol-enemy.at;goal.y=0;float speed=2.6f*std::sqrt(enemy.scale);
            if(Length(goal)<1.5f)std::swap(enemy.home,enemy.patrol);
            else{V step=Unit(goal);enemy.heading=Unit(Lerp(enemy.heading,step,1-std::exp(-5*dt)));enemy.at=enemy.at+step*std::min(Length(goal),speed*dt);}}
        // 弓兵: 距離を保って横に動き、先を読んで矢を撃つ
        else if(kind==CityEnemy::Archer){idleTick();
            if(enemy.phase!=SwarmPhase::Spawn){float standoff=CityStandoff(kind);
                V side{direction.z,0,-direction.x};if(enemy.id%2)side=side*-1.f;
                V move=direction*((distance-standoff)*.6f)+side*(4.f*std::sin(enemy.age*.7f+float(enemy.id)));
                float speed=6.f*std::sqrt(enemy.scale)*(enemy.stagger>0?.3f:1.f);
                if(Length(move)>.5f)enemy.at=enemy.at+Unit(move)*std::min(Length(move),speed*dt);
                enemy.heading=Unit(Lerp(enemy.heading,direction,1-std::exp(-8*dt)));
                enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<115&&enemy.stagger<=0){enemy.shotClock=2.2f+SwarmVariation(enemy.id,enemy.attacks++)*1.2f;
                    V from=enemy.at+V{0,enemy.scale*.9f,0}+enemy.heading*enemy.scale;float flight=distance/62;
                    V aim=targetBody.at+targetBody.velocity*std::min(flight,1.2f)+V{0,.6f,0};
                    V velocity=Unit(aim-from)*62;velocity.y+=4.5f*flight;
                    cityShots_.push_back({from,velocity,0,false,aim});}}}
        // それ以外: 群れと同じ追跡と突進。守護者は踏みつけも行う
        else{
            float pace=kind==CityEnemy::Guardian?.45f:1.f;
            TickSwarm(enemy,targetBody.at,dt*pace,InkGround(enemy.at),active<maxActive&&enemy.stagger<=0);
            if(kind==CityEnemy::Guardian&&enemy.phase==SwarmPhase::Chase){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<75){enemy.shotClock=6.5f;cityStomps_.push_back({V{enemy.at.x,0,enemy.at.z},0,36});}}
        }
        // 攻撃の予告に入った数を数える
        if(phase!=SwarmPhase::Windup&&enemy.phase==SwarmPhase::Windup)++active;
        // 通れる範囲に収め、突進が当たったらダメージ
        float margin=kind==CityEnemy::Guardian?18.f:std::max(1.f,enemy.scale*1.1f);
        enemy.at=CityConstrain(before,enemy.at,margin,cityGates_);
        enemy.at.y=InkGround(enemy.at)+enemy.scale;enemy.corrosion=enemy.health<=enemy.maxHealth*.3f?1.f:0.f;
        // 突進が当たったらダメージ (本体なら体力、子なら子の体力)
        if(phase==SwarmPhase::Dash&&!enemy.struck){float hit,hitRadius=2.2f*LifeScale(targetBody.mass)+1.2f*enemy.scale;V delta=enemy.at-before;
            if(LifeDistance(enemy.at,targetBody.at)<hitRadius||(Length(delta)>.001f&&RaySphere(before,Unit(delta),targetBody.at,hitRadius,Length(delta),hit))){
                enemy.struck=true;float damage=CityDamage(kind);if(victim==0)Damage(r,p,damage,ctx,enemy.at);
                else {targetBody.health=std::max(0.f,targetBody.health-damage*.7f);if(targetBody.health<=0){targetBody.down=true;LifeReturnToSlime(targetBody);++life_.revision;}}
            }}
        // カメラとの間の透明度
        enemy.cameraOpacity=SwarmCameraOpacity(Read(ctx.camera->Position()),body.at,enemy.at,false);
    }
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
    // 破片: 時間が経つと小さくなる
    for(const auto& d:cityDebris_){V right,up,forward;euler(d.rotate,right,up,forward);float fade=std::clamp(4-d.age,0.f,1.f);
        part(d.material==0?cityWood_:cityIron_,d.at,right,up,forward,d.size*fade,{1,1,1,1});}
    // 弾: 砲弾は球と着弾点の輪、矢は細長い箱
    for(const auto& shot:cityShots_){
        if(shot.shell){Engine::Transform t;t.translate=EV(shot.at);t.scale={1.3f,1.3f,1.3f};render->DrawMeshInstanced(sphere_,white_,t,{.06f,.06f,.07f,1});
            floorRing(shot.target,9,0,std::clamp(shot.age/1.7f,0.f,1.f),{1,.3f,.05f,1});}
        else{V forward=Unit(shot.velocity),right=Unit(SwarmCross({0,1,0},forward)),up=SwarmCross(forward,right);
            part(cityWood_,shot.at,right,up,forward,{.25f,.25f,3.2f},{.75f,.6f,.45f,1});}
    }
    // 光る看板
    for(const auto& glow:cityGlows_){V forward=SwarmCross(glow.right,glow.up);Engine::Matrix4x4 m{};V axes[3]={glow.right*glow.size.x,glow.up*glow.size.y,forward*glow.size.z};
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=glow.center.x;m.m[3][1]=glow.center.y;m.m[3][2]=glow.center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,white_,m,glow.color,"DomainGlow");}
    // 踏みつけの予告の輪
    for(const auto& stomp:cityStomps_)if(stomp.age<.85f)floorRing(stomp.at,stomp.radius,0,stomp.age/.85f,{1,.25f,.04f,1});
    // 各敵: 光・状態異常の色・向き・地面の位置・出現の高さ
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto kind=CityKind(enemy);
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
    ctx.renderer->DrawMeshInstanced(v.mesh,v.texture,t,{1,1,1,1},v.shader,v.extra);return true;
}
}
