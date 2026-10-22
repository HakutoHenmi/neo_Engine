// Included by ChronoSystem.cpp: the slime city stage (Docs/SlimeLife.md "街ステージ").
// Static architecture is generated as one dynamic mesh per PBR material. Props,
// the barricade, the shutter gate and city enemies use the CC0 Poly Haven assets in
// Resources/Credits/CityStage.md.
namespace Game {
namespace {
enum CityMaterialId {CityRoad,CitySidewalk,CityAlleyFloor,CityPaint,CityPaintYellow,CityConcrete,CityCladding,CityFacadeTiles,CityDarkTiles,
    CitySlab,CityGreyTiles,CityShutter,CityRustShutter,CityGlass,CityMetal,CityWhiteBrick,CityWood,CityStoneSkin,CityMaterialCount};
struct CityGeometry {std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;};
struct CityBuilder {
    std::array<CityGeometry,CityMaterialCount> geometry;
    std::array<float,CityMaterialCount> tile{30,10,16,6,6,20,8,12,14,18,8,8,8,8,6,10,8,22};
    // UVs follow the face plane in world units, so tiling stays physically consistent.
    void Polygon(int material,std::initializer_list<Chrono::Vec> points,Chrono::Vec normal){
        using Chrono::Vec;auto& g=geometry[size_t(material)];uint32_t base=uint32_t(g.vertices.size());
        Vec n=Chrono::Unit(normal);Vec u=std::abs(n.y)>.99f?Vec{1,0,0}:Chrono::Unit(Chrono::SwarmCross({0,1,0},n));Vec v=Chrono::SwarmCross(n,u);
        float size=tile[size_t(material)];
        for(Vec p:points){Engine::VertexData vertex{};vertex.position={p.x,p.y,p.z,1};vertex.texcoord={Chrono::Dot(p,u)/size,-Chrono::Dot(p,v)/size};
            vertex.normal={n.x,n.y,n.z};g.vertices.push_back(vertex);}
        const auto* p=points.begin();
        // Terrain proves front faces satisfy cross(p1-p0,p2-p0) along the normal.
        bool forward=Chrono::Dot(Chrono::SwarmCross(p[1]-p[0],p[2]-p[0]),n)>0;
        for(uint32_t i=1;i+1<uint32_t(points.size());++i){
            if(forward)g.indices.insert(g.indices.end(),{base,base+i,base+i+1});else g.indices.insert(g.indices.end(),{base,base+i+1,base+i});}
    }
    void Box(int material,Chrono::Vec lo,Chrono::Vec hi,bool bottom=false){
        using Chrono::Vec;
        Polygon(material,{{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z}},{1,0,0});
        Polygon(material,{{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z}},{-1,0,0});
        Polygon(material,{{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z}},{0,0,1});
        Polygon(material,{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z}},{0,0,-1});
        Polygon(material,{{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}},{0,1,0});
        if(bottom)Polygon(material,{{lo.x,lo.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z}},{0,-1,0});
    }
    // A box on a facade: `along` spans the wall, `out` leaves it; offsets are relative to the wall plane.
    void FacadeBox(int material,Chrono::Vec center,Chrono::Vec along,Chrono::Vec out,float halfAlong,float halfY,float out0,float out1){
        // Pipelines draw both faces: a box starting on the wall would put its back face on the wall
        // plane and z-fight with it, so wall-mounted boxes are sunk slightly into the wall.
        if(out0<=.01f&&out1>out0)out0=-.4f;
        using Chrono::Vec;Vec a=center+along*halfAlong+out*out0,b=center-along*halfAlong+out*out1;
        Box(material,{std::min(a.x,b.x),center.y-halfY,std::min(a.z,b.z)},{std::max(a.x,b.x),center.y+halfY,std::max(a.z,b.z)});
    }
    // A flat painted strip on the road, from a to b with the given width.
    void Strip(int material,Chrono::Vec a,Chrono::Vec b,float width,float y=-.08f){
        using Chrono::Vec;Vec d=Chrono::Unit(b-a),n{-d.z,0,d.x};n=n*(width*.5f);a.y=b.y=y;
        Polygon(material,{a-n,b-n,b+n,a+n},{0,1,0});
    }
};
enum CityStyle {CityStyleAlley,CityStyleStreet,CityStyleTower};
struct CityBuilding {Chrono::CityRect foot;Chrono::Vec out;float height;int style,skin,trim;bool allSides;};
// Scanned rat and cannon: length in world units per enemy scale, and yaw to face along the heading.
constexpr float CityRatWidth=5.6f,CityRatYaw=0,CityCannonWidth=3.4f,CityCannonYaw=0;
constexpr float CityGroundFloor=24,CityFloor=19;
float CityNoise(int seed){float n=std::sin(float(seed)*12.9898f+78.233f)*43758.5453f;return n-std::floor(n);}
}
void ChronoSystem::BuildCityStage(entt::registry& r){
    using namespace Chrono;
    auto* render=Engine::Renderer::GetInstance();
    // Towers reach ~600 units: a wide, deep shadow map ahead of the camera keeps their shadows whole.
    render->SetDirectionalShadowRange(460,1800,.3f);
    render->CreateShaderPipeline("EnvironmentSurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/EnvironmentSurfacePS.hlsl");
    // Environment shading with sky fill for street canyons. Dynamic meshes carry no bounds, so they
    // must not enter the renderer's per-instance scenery culling keyed on "EnvironmentSurface".
    render->CreateShaderPipeline("CitySurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CitySurfacePS.hlsl");
    render->CreateShaderPipeline("CityGlass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityGlassPS.hlsl");
    render->CreateShaderPipeline("CityArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityArmorPS.hlsl");
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("MorphItemHighlight",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MorphItemHighlightPS.hlsl",true,true);
    auto material=[&](const std::string& id){CityModel m;
        m.texture=render->LoadTexture2D("Resources/Textures/PolyHaven/"+id+"_diff_2k.jpg");
        for(const char* map:{"nor_dx","Rough","AO"}){std::string path="Resources/Textures/PolyHaven/"+id+"_"+map+"_2k.jpg";
            // A few scans ship without an AO map; white leaves the lighting unoccluded.
            m.extra.push_back(std::filesystem::exists(path)?render->LoadTexture2D(path,false):white_);}
        return m;};
    const char* surfaceIds[CityMaterialCount]={"asphalt_02","large_grey_tiles","concrete_floor_worn_001","concrete_floor_worn_001","concrete_floor_worn_001",
        "concrete_wall_008","exterior_wall_cladding_02","rectangular_facade_tiles_02","concrete_tile_facade","concrete_slab_wall","grey_tiles",
        "painted_metal_shutter","rusty_metal_shutter","concrete_floor_worn_001","concrete_floor_worn_001","white_bricks","medieval_wood","castle_wall_slates"};
    std::array<CityModel,CityMaterialCount> surfaces;for(int i=0;i<CityMaterialCount;++i)surfaces[size_t(i)]=material(surfaceIds[i]);
    auto tint=[](int i)->DirectX::XMFLOAT4{
        switch(i){case CityPaint:return {2.5f,2.5f,2.45f,1};case CityPaintYellow:return {2.5f,1.85f,.45f,1};case CityAlleyFloor:return {1.3f,1.28f,1.25f,1};case CityGlass:return {.2f,.26f,.33f,1};
            case CityMetal:return {.24f,.25f,.27f,1};case CityRoad:return {.85f,.85f,.85f,1};default:return {1,1,1,1};}};
    cityWood_=surfaces[CityWood];cityStoneSkin_=surfaces[CityStoneSkin];cityIron_=surfaces[CityShutter];cityArmor_=surfaces[CityMetal];
    CityBuilder city;
    // Streets: asphalt everywhere, then pavements, the alley floor and painted markings.
    city.Polygon(CityRoad,{{-1100,-.2f,-900},{1200,-.2f,-900},{1200,-.2f,1400},{-1100,-.2f,1400}},{0,1,0});
    auto floorQuad=[&](int m,CityRect w,float y){city.Polygon(m,{{w.x0,y,w.z0},{w.x1,y,w.z0},{w.x1,y,w.z1},{w.x0,y,w.z1}},{0,1,0});};
    for(int i=0;i<3;++i)floorQuad(CityAlleyFloor,CityWalk[size_t(i)],0);
    auto pavement=[&](CityRect w,bool curbX0,bool curbX1,bool curbZ0,bool curbZ1){floorQuad(CitySidewalk,w,.15f);
        // Kerbs: low concrete strips along the road-side edges (visual only; the floor stays flat).
        if(curbX0)city.Box(CityConcrete,{w.x0-.6f,-.05f,w.z0},{w.x0+.6f,.45f,w.z1});if(curbX1)city.Box(CityConcrete,{w.x1-.6f,-.05f,w.z0},{w.x1+.6f,.45f,w.z1});
        if(curbZ0)city.Box(CityConcrete,{w.x0,-.05f,w.z0-.6f},{w.x1,.45f,w.z0+.6f});if(curbZ1)city.Box(CityConcrete,{w.x0,-.05f,w.z1-.6f},{w.x1,.45f,w.z1+.6f});};
    // Avenue: four lanes between 30-unit pavements.
    pavement({-12,-472,18,-300},false,true,false,false);pavement({-12,-230,18,-72},false,true,false,false);
    pavement({138,-472,168,-300},true,false,false,false);pavement({138,-230,168,-72},true,false,false,false);
    for(float z=-462;z<-82;z+=40){if(z>-312&&z<-218)continue;city.Strip(CityPaint,{48,0,z},{48,0,z+20},1);city.Strip(CityPaint,{108,0,z},{108,0,z+20},1);}
    city.Strip(CityPaintYellow,{77,0,-466},{77,0,-90},.8f);city.Strip(CityPaintYellow,{79,0,-466},{79,0,-90},.8f);
    auto zebra=[&](CityRect band,bool alongZ){// Stripes cross the road; alongZ = the road runs along z.
        if(alongZ){for(float x=band.x0+3;x<band.x1-2;x+=9)city.Strip(CityPaint,{x,0,band.z0},{x,0,band.z1},4.5f);}
        else for(float z=band.z0+3;z<band.z1-2;z+=9)city.Strip(CityPaint,{band.x0,0,z},{band.x1,0,z},4.5f);};
    zebra({18,-110,138,-86},true);zebra({18,-470,138,-450},true);
    // Side streets: pavements on both sides.
    for(float x0:{-160.f,164.f}){float x1=x0+152;pavement({x0,-300,x1,-286},false,false,false,true);pavement({x0,-244,x1,-230},false,false,true,false);}
    // Scramble crossing: two 160-unit roads cross at its centre; the four corners are pavements.
    const CityRect plaza=CityWalk[7];const float cx=CityCrossing.x,cz=CityCrossing.z,h=CityRoadHalf;
    pavement({plaza.x0,plaza.z0,cx-h,cz-h},false,true,false,true);pavement({cx+h,plaza.z0,plaza.x1,cz-h},true,false,false,true);
    pavement({plaza.x0,cz+h,cx-h,plaza.z1},false,true,true,false);pavement({cx+h,cz+h,plaza.x1,plaza.z1},true,false,true,false);
    zebra({cx-h,cz-h-46,cx+h,cz-h-8},true);zebra({cx-h,cz+h+8,cx+h,cz+h+46},true);
    zebra({cx-h-46,cz-h,cx-h-8,cz+h},false);zebra({cx+h+8,cz-h,cx+h+46,cz+h},false);
    // Diagonal crossings, the signature of a scramble: stripes perpendicular to each diagonal.
    for(int d=0;d<2;++d){V a{cx-h+12,0,d?cz+h-12:cz-h+12},b{cx+h-12,0,d?cz-h+12:cz+h-12};V dir=Unit(b-a),side{-dir.z,0,dir.x};
        for(float t=0;t<Length(b-a);t+=9){V m=a+dir*t;city.Strip(CityPaint,m-side*16,m+side*16,4.5f);}}
    for(float s:{-1.f,1.f}){city.Strip(CityPaint,{cx-h,0,cz+s*(h+52)},{cx+h,0,cz+s*(h+52)},2);city.Strip(CityPaint,{cx+s*(h+52),0,cz-h},{cx+s*(h+52),0,cz+h},2);}
    // Road arms leave the crossing: lane lines continue into the distance.
    for(float t=plaza.z1+20;t<1350;t+=40){city.Strip(CityPaint,{cx-40,0,t},{cx-40,0,t+20},1);city.Strip(CityPaint,{cx+40,0,t},{cx+40,0,t+20},1);}
    for(float t=plaza.x1+20;t<1150;t+=40){city.Strip(CityPaint,{t,0,cz-40},{t+20,0,cz-40},1);city.Strip(CityPaint,{t,0,cz+40},{t+20,0,cz+40},1);}
    for(float t=plaza.x0-20;t>-1050;t-=40){city.Strip(CityPaint,{t,0,cz-40},{t-20,0,cz-40},1);city.Strip(CityPaint,{t,0,cz+40},{t-20,0,cz+40},1);}
    // Buildings line the outside of every floor region.
    std::vector<CityBuilding> buildings;int seed=0;
    const int skins[]={CityConcrete,CityCladding,CityFacadeTiles,CityGreyTiles,CityWhiteBrick,CitySlab,CityDarkTiles};
    auto row=[&](float fixed,float from,float to,V out,float depth,int minFloors,int maxFloors,int style){
        float at=from;
        while(at<to-4){float width=std::min(to-at,(style==CityStyleTower?70.f:34.f)+CityNoise(++seed)*(style==CityStyleTower?60.f:36.f));if(to-(at+width)<20)width=to-at;
            CityRect foot{};
            if(std::abs(out.x)>.5f){float x0=out.x>0?fixed-depth:fixed,x1=out.x>0?fixed:fixed+depth;foot={x0,at,x1,at+width};}
            else{float z0=out.z>0?fixed-depth:fixed,z1=out.z>0?fixed:fixed+depth;foot={at,z0,at+width,z1};}
            int floors=minFloors+int(CityNoise(++seed)*float(maxFloors-minFloors+1));
            float height=CityGroundFloor+float(floors)*CityFloor;
            int skin=style==CityStyleAlley?(CityNoise(++seed)<.4f?CityWhiteBrick:CityNoise(++seed)<.5f?CityConcrete:CitySlab):skins[int(CityNoise(++seed)*6.99f)];
            int trim=style==CityStyleTower?(CityNoise(++seed)<.5f?CityDarkTiles:CitySlab):CityConcrete;
            buildings.push_back({foot,out,height,style,skin,trim,style==CityStyleTower});at+=width;}
    };
    // Back alley: tall, close walls.
    row(-22,-700,-540,{1,0,0},50,6,10,CityStyleAlley);row(22,-700,-584,{-1,0,0},50,6,10,CityStyleAlley);
    row(-700,-72,72,{0,0,1},50,7,10,CityStyleAlley);row(-540,-72,6,{0,0,-1},50,6,9,CityStyleAlley);
    row(56,-540,-480,{1,0,0},50,6,8,CityStyleAlley);row(-584,72,150,{0,0,1},50,6,9,CityStyleAlley);row(100,-584,-480,{-1,0,0},50,6,10,CityStyleAlley);
    row(-472,-62,54,{0,0,1},8,6,8,CityStyleStreet);row(-472,102,218,{0,0,1},8,6,8,CityStyleStreet);
    // Avenue and side streets.
    row(-12,-472,-300,{1,0,0},50,8,14,CityStyleStreet);row(-12,-230,-72,{1,0,0},50,8,14,CityStyleStreet);
    row(168,-472,-300,{-1,0,0},50,8,14,CityStyleStreet);row(168,-230,-72,{-1,0,0},50,8,14,CityStyleStreet);
    row(-230,-160,-62,{0,0,-1},50,6,10,CityStyleStreet);row(-300,-160,-62,{0,0,1},50,6,10,CityStyleStreet);row(-160,-300,-230,{1,0,0},50,6,10,CityStyleStreet);
    row(-230,218,316,{0,0,-1},50,6,10,CityStyleStreet);row(-300,218,316,{0,0,1},50,6,10,CityStyleStreet);row(316,-300,-230,{-1,0,0},50,6,10,CityStyleStreet);
    // Twin towers frame the shutter gate between avenue and crossing.
    buildings.push_back({{-62,-72,48,-40},{0,0,-1},CityGroundFloor+17*CityFloor,CityStyleTower,CityGlass,CityDarkTiles,true});
    buildings.push_back({{108,-72,218,-40},{0,0,-1},CityGroundFloor+19*CityFloor,CityStyleTower,CityGlass,CitySlab,true});
    // Scramble crossing: towers on every corner; the road arms continue between mid-rise blocks.
    row(-40,-352,-62,{0,0,1},60,12,26,CityStyleTower);row(-40,218,508,{0,0,1},60,12,26,CityStyleTower);
    row(plaza.x0,-40,cz-h,{1,0,0},60,14,30,CityStyleTower);row(plaza.x0,cz+h,plaza.z1,{1,0,0},60,14,30,CityStyleTower);
    row(plaza.x1,-40,cz-h,{-1,0,0},60,14,30,CityStyleTower);row(plaza.x1,cz+h,plaza.z1,{-1,0,0},60,14,30,CityStyleTower);
    row(plaza.z1,-352,cx-h,{0,0,-1},60,16,32,CityStyleTower);row(plaza.z1,cx+h,508,{0,0,-1},60,16,32,CityStyleTower);
    row(cx-h,plaza.z1+60,1300,{1,0,0},50,8,16,CityStyleStreet);row(cx+h,plaza.z1+60,1300,{-1,0,0},50,8,16,CityStyleStreet);
    row(cz-h,-1050,plaza.x0-60,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,-1050,plaza.x0-60,{0,0,-1},50,8,16,CityStyleStreet);
    row(cz-h,plaza.x1+60,1150,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,plaza.x1+60,1150,{0,0,-1},50,8,16,CityStyleStreet);
    cityGlows_.clear();
    const Engine::Vector4 neon[]={{1,.25f,.55f,1},{.2f,.85f,1,1},{1,.75f,.2f,1},{.55f,1,.35f,1},{.75f,.4f,1,1}};
    for(const auto& b:buildings){
        const auto& f=b.foot;
        city.Box(b.skin==CityGlass?b.trim:b.skin,{f.x0,0,f.z0},{f.x1,b.height,f.z1});
        // Roof: parapet ring and plant boxes.
        float p=1.2f;
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x1,b.height+2.5f,f.z0+p});city.Box(b.trim,{f.x0,b.height,f.z1-p},{f.x1,b.height+2.5f,f.z1});
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x0+p,b.height+2.5f,f.z1});city.Box(b.trim,{f.x1-p,b.height,f.z0},{f.x1,b.height+2.5f,f.z1});
        if(b.style==CityStyleTower){V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float hx=(f.x1-f.x0)*.5f,hz=(f.z1-f.z0)*.5f;
            float crown=12+CityNoise(++seed)*22;
            city.Box(b.trim,c+V{-hx+6,0,-hz+6},c+V{hx-6,crown,hz-6});
            city.Box(CityMetal,c+V{-hx*.45f,crown,-hz*.45f},c+V{hx*.45f,crown+9,hz*.45f});
            if(CityNoise(++seed)<.6f)city.Box(CityMetal,c+V{-.8f,crown+9,-.8f},c+V{.8f,crown+9+30+CityNoise(++seed)*40,.8f});}
        {V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float sx=(f.x1-f.x0)*.18f,sz=(f.z1-f.z0)*.18f;
            city.Box(CityConcrete,c+V{-sx,0,-sz},c+V{sx*.3f,9,sz*.4f});city.Box(CityMetal,c+V{sx*.5f,0,-sz},c+V{sx*1.4f,6,-sz*.1f});}
        V faces[4]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        for(V out:faces){
            bool main=std::abs(out.x-b.out.x)<.1f&&std::abs(out.z-b.out.z)<.1f;
            if(!main&&!b.allSides)continue;
            V along=std::abs(out.x)>.5f?V{0,0,1}:V{1,0,0};
            float width=std::abs(out.x)>.5f?f.z1-f.z0:f.x1-f.x0;
            V face=std::abs(out.x)>.5f?V{out.x>0?f.x1:f.x0,0,f.z0}:V{f.x0,0,out.z>0?f.z1:f.z0};
            V mid=face+along*(width*.5f);
            if(b.style==CityStyleTower){
                // Podium: full-height shop glazing between columns, then a curtain wall of glass bands and spandrels.
                float podium=CityGroundFloor+CityFloor;
                city.FacadeBox(CityGlass,mid+V{0,podium*.5f,0},along,out,width*.5f,podium*.5f,0,.45f);
                for(float s=0;s<=width+.1f;s+=16)city.FacadeBox(b.trim,face+along*std::min(s,width-1.f)+V{0,podium*.5f,0},along,out,1,podium*.5f,0,1.2f);
                city.FacadeBox(b.trim,mid+V{0,podium+1.5f,0},along,out,width*.5f+.5f,1.5f,0,2.5f);
                for(float y=podium+3;y<b.height-2;y+=CityFloor){city.FacadeBox(CityGlass,mid+V{0,y+8.5f,0},along,out,width*.5f,7.5f,0,.45f);
                    city.FacadeBox(b.trim,mid+V{0,y+17.5f,0},along,out,width*.5f,1.5f,0,1.1f);}
                for(float s=8;s<width-2;s+=12)city.FacadeBox(CityMetal,face+along*s+V{0,(podium+b.height)*.5f,0},along,out,.35f,(b.height-podium)*.5f,0,1.6f);
                // Large screens on the podium crown face the crossing.
                if(width>60&&CityNoise(++seed)<.55f){float w=std::min(width*.5f,46.f);V c=mid+V{0,podium+30,0}+out*1.2f;
                    city.FacadeBox(CityMetal,c,along,out,w*.5f+1,13,0,1);
                    cityGlows_.push_back({c+out*1.5f,along,{0,1,0},{w,24,.4f},neon[seed%5]});}
                continue;
            }
            int bays=std::max(1,int(width/14));
            // Ground floor: shops with glazing, shutters and a sign band (street), or rusty shutters and doors (alley).
            for(int bay=0;bay<bays;++bay){V c=face+along*(width*(float(bay)+.5f)/float(bays));float half=width/float(bays)*.5f-1.2f;
                if(b.style==CityStyleAlley){bool door=(bay+seed)%3==0;
                    if(door){city.FacadeBox(CityMetal,c+V{0,9,0},along,out,3.4f,9,0,.3f);city.FacadeBox(CityConcrete,c+V{0,18.6f,0},along,out,4.4f,.6f,0,.8f);}
                    else{city.FacadeBox(CityRustShutter,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);city.FacadeBox(CityMetal,c+V{0,20.2f,0},along,out,half+.4f,1.2f,0,1.4f);}}
                else{bool shut=CityNoise(seed*7+bay)<.25f;
                    city.FacadeBox(shut?CityShutter:CityGlass,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);
                    for(float s:{-1.f,1.f})city.FacadeBox(CityMetal,c+along*(s*(half+.6f))+V{0,10,0},along,out,.6f,10,0,.8f);}}
            if(b.style==CityStyleStreet){city.FacadeBox(CityDarkTiles,mid+V{0,22,0},along,out,width*.5f,2,0,1.6f);
                // Vertical neon signs on some street facades.
                if(CityNoise(++seed)<.45f){V c=face+along*(width*(.15f+.7f*CityNoise(++seed)))+V{0,48,0}+out*4;
                    city.FacadeBox(CityMetal,c,along,out,.6f,1,0,4);
                    cityGlows_.push_back({c+out*2.2f,out,{0,1,0},{3,32,2.2f},neon[seed%5]});}}
            // Upper floors: punched windows with frames and sills; balconies on some street blocks.
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
            if(b.style==CityStyleAlley)for(int k=0;k<2;++k){V c=face+along*(width*(.2f+.6f*float(k)))+V{0,b.height*.5f,0};
                city.FacadeBox(CityMetal,c,along,out,.5f,b.height*.5f,.3f,1.3f);}
        }
        // Camera collision so the follow camera stays in the street.
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City building collision");
        r.emplace<TransformComponent>(e).translate=Write(V{(f.x0+f.x1)*.5f,b.height*.5f,(f.z0+f.z1)*.5f});
        r.emplace<BoxColliderComponent>(e).size=Write(V{f.x1-f.x0,b.height,f.z1-f.z0});
        r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
    }
    // Lintel above the shutter gate links the twin towers.
    city.Box(CityDarkTiles,{46,58,-64},{110,96,-48});
    for(int i=0;i<CityMaterialCount;++i){auto& g=city.geometry[size_t(i)];if(g.indices.empty())continue;
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City surface ")+surfaceIds[i]);
        auto& t=r.emplace<TransformComponent>(e);t.translate={0,0,0};t.scale={1,1,1};t.rotate={0,0,0};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.useCubemap=false;
        mr.textureHandle=surfaces[size_t(i)].texture;mr.extraTextureHandles=surfaces[size_t(i)].extra;mr.shaderName=i==CityGlass?"CityGlass":"CitySurface";mr.color=tint(i);}
    // Props keep each scan's proportions: a target width sets a uniform scale; the lowest vertex rests on the street.
    std::map<std::string,CityModel> models;
    auto model=[&](const std::string& id)->CityModel& {auto found=models.find(id);if(found!=models.end())return found->second;
        CityModel m=material(id);m.mesh=render->LoadObjMesh("Resources/Models/PolyHaven/"+id+"_2k.fbx");
        if(auto* source=render->GetModel(m.mesh)){m.lo={1e9f,1e9f,1e9f};m.hi={-1e9f,-1e9f,-1e9f};
            for(const auto& v:source->GetData().vertices){m.lo={std::min(m.lo.x,v.position.x),std::min(m.lo.y,v.position.y),std::min(m.lo.z,v.position.z)};
                m.hi={std::max(m.hi.x,v.position.x),std::max(m.hi.y,v.position.y),std::max(m.hi.z,v.position.z)};}
            render->PrepareDistanceLods(m.mesh);}
        return models.emplace(id,m).first->second;};
    cityProps_.clear();cityVisuals_.clear();
    auto place=[&](const std::string& id,V at,float width,float yaw,float required=0,float reward=0){
        const auto& m=model(id);V lo=m.lo,hi=m.hi;
        float scale=width/std::max({hi.x-lo.x,hi.z-lo.z,.001f});V mid=(lo+hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop "+id);
        auto& t=r.emplace<TransformComponent>(e);t.scale=Write(V{scale,scale,scale});t.rotate=Write(V{0,yaw,0});
        t.translate=Write(V{at.x-(mid.x*cs+mid.z*sn)*scale,at.y-lo.y*scale,at.z-(-mid.x*sn+mid.z*cs)*scale});
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=m.mesh;mr.textureHandle=m.texture;mr.extraTextureHandles=m.extra;mr.shaderName="CitySurface";mr.useCubemap=false;
        V center=V{at.x,at.y+(mid.y-lo.y)*scale,at.z};
        cityVisuals_[uint32_t(e)]={m.mesh,m.texture,m.extra,{scale,scale,scale},{0,yaw,0},center-Read(t.translate),(hi.y-lo.y)*scale,"CitySurface"};
        // Tentacles grab tall props near their foot so the capture height test matches the street.
        if(required>0)cityProps_.push_back({e,V{at.x,at.y+std::min(1.2f,(mid.y-lo.y)*scale),at.z},required,reward,id=="old_tyre"?LifeMorphItem::Tire:LifeMorphItem::None});
        return e;};
    // Generated props (street lights, the shutter gate) share one mesh each and absorb like scans.
    auto placeMesh=[&](const char* name,uint32_t mesh,int skin,V at,float yaw,float height,float required,float reward,LifeMorphItem item=LifeMorphItem::None){
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City prop ")+name);
        auto& t=r.emplace<TransformComponent>(e);t.scale={1,1,1};t.rotate=Write(V{0,yaw,0});t.translate=Write(at);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=mesh;mr.textureHandle=surfaces[size_t(skin)].texture;mr.extraTextureHandles=surfaces[size_t(skin)].extra;
        mr.shaderName="CitySurface";mr.useCubemap=false;mr.color=tint(skin);
        cityVisuals_[uint32_t(e)]={mesh,surfaces[size_t(skin)].texture,surfaces[size_t(skin)].extra,{1,1,1},{0,yaw,0},{0,height*.5f,0},height,"CitySurface"};
        if(required>0)cityProps_.push_back({e,at+V{0,1.2f,0},required,reward,item});
        return e;};
    auto localMesh=[&](auto&& build){CityBuilder local;build(local);auto& g=local.geometry[CityMetal];return render->CreateDynamicMesh(g.vertices,g.indices);};
    // A modern street light: base, pole, outreach arm and luminaire (local +x is the arm).
    uint32_t lampMesh=localMesh([](CityBuilder& b){b.Box(CityMetal,{-1,0,-1},{1,2.4f,1});b.Box(CityMetal,{-.45f,0,-.45f},{.45f,42,.45f});
        b.Box(CityMetal,{0,40.4f,-.3f},{12,41.2f,.3f});b.Box(CityMetal,{9,39.2f,-1.4f},{14.5f,40.6f,1.4f});});
    auto lamp=[&](V at,float yaw){placeMesh("street light",lampMesh,CityMetal,at,yaw,42,420,30);};
    // --- Area 1: back-alley clutter at slime scale (absorbable from the start).
    // A guaranteed catalyst in sight and within the starting absorption range.
    place("old_tyre",{6,0,-672},3.8f,.35f,15,12);
    const char* smallIds[]={"trashbag","cardboard_box_01","plastic_crate_01","plastic_crate_02","metal_jerrycan","old_tyre","WetFloorSign_01","wooden_crate_01"};
    const float smallWidth[]={3.4f,3.2f,3,3.4f,2.6f,3.8f,2.4f,4.6f},smallReward[]={12,12,10,10,10,12,8,14};
    int clutter=0;
    auto scatter=[&](V at){int k=clutter%8;place(smallIds[k],at,smallWidth[k]*(.9f+.25f*CityNoise(clutter*3+1)),CityNoise(clutter*3+2)*6.283f,15,smallReward[k]);++clutter;};
    for(V at:{V{-16,0,-690},V{-12,0,-684},V{15,0,-672},V{17,0,-664},V{-17,0,-652},V{-14,0,-646},V{16,0,-636},V{-17,0,-622},V{18,0,-608},
              V{-15,0,-600},V{-17,0,-572},V{4,0,-556},V{22,0,-578},V{40,0,-580},V{94,0,-572},V{94,0,-548},V{61,0,-536},V{94,0,-526},
              V{62,0,-512},V{95,0,-500},V{0,0,-661},V{-6,0,-592},V{60,0,-492},V{96,0,-490}})scatter(at);
    const char* mediumIds[]={"industrial_pastic_container","plastic_container","propane_tank","wooden_crate_02","hand_truck"};
    const float mediumWidth[]={6,5.6f,3.6f,6.8f,4};const float mediumRequired[]={120,100,110,140,130};
    int medium=0;
    for(V at:{V{-15,0,-676},V{16,0,-628},V{-16,0,-612},V{14,0,-590},V{90,0,-566},V{62,0,-550},V{94,0,-512},V{62,0,-500},V{-14,0,-560},V{30,0,-552}}){
        int k=medium%5;place(mediumIds[k],at,mediumWidth[k],CityNoise(medium*5+3)*6.283f,mediumRequired[k],30);++medium;}
    for(V at:{V{-19,0,-640},V{19,0,-598},V{97,0,-534}})place("power_box_01",at,3.4f,at.x<0?1.5708f:-1.5708f,160,26);
    place("water_manhole_cover",{2,.02f,-644},4.4f,0);place("water_manhole_cover",{78,.02f,-520},4.4f,.7f);
    // Construction barricade: plywood hoarding between posts with crates heaped behind, broken by the hammer.
    cityBarricade_.clear();const V bc=CityBarricadeCenter;
    for(float x:{-21.f,0.f,21.f})cityBarricade_.push_back({bc+V{x,7,0},{1.6f,14,1.6f},{0,0,0}});
    for(int i=0;i<6;++i){float y=1.6f+float(i)*2.25f;cityBarricade_.push_back({bc+V{(CityNoise(i)-.5f)*2.f,y,.8f+float(i%2)*.4f},{46,1.7f,.55f},{0,0,(CityNoise(i+9)-.5f)*.06f}});}
    for(float s:{-1.f,1.f})cityBarricade_.push_back({bc+V{s*10,7,1.6f},{25,1.4f,.5f},{0,0,s*.48f}});
    for(int i=0;i<5;++i)cityBarricade_.push_back({bc+V{-16+float(i)*8,2.4f,-3.5f},{4.8f,4.8f,4.8f},{0,CityNoise(i+31)*.6f,0}});
    // --- Area 2: avenue. Street lights, parked cars, payphones and utility cabinets.
    uint32_t hydrantMesh=localMesh([](CityBuilder& b){
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
    for(V at:{V{86,0,-460},V{8,0,-270},V{144,0,-120}}){auto e=placeMesh("fire hydrant",hydrantMesh,CityMetal,at,0,5.1f,15,24,LifeMorphItem::Hydrant);
        r.get<MeshRendererComponent>(e).color={1.8f,.22f,.08f,1};}
    for(int i=0;i<5;++i){float z=-440+float(i)*72;if(z>-312&&z<-218)z+=96;lamp({14,0,z},0);lamp({142,0,z+36},3.14159f);}
    for(V at:{V{30,0,-430},V{30,0,-360},V{126,0,-400},V{126,0,-330},V{30,0,-190},V{126,0,-150},V{126,0,-200},V{30,0,-130}})
        place("covered_car",at,26,0,600,45);
    for(V at:{V{0,0,-420},V{156,0,-180},V{2,0,-140}})place("korean_public_payphone_01",at,4.4f,at.x<50?1.5708f:-1.5708f,300,24);
    for(V at:{V{-2,0,-350},V{158,0,-430},V{158,0,-110},V{-2,0,-100}})place(CityNoise(int(at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,at.x<50?1.5708f:-1.5708f,260,20);
    for(V at:{V{-120,0,-265},V{-60,0,-265},V{220,0,-265},V{290,0,-265}})place("covered_car",at,26,1.5708f,600,45);
    for(V at:{V{-148,0,-292},V{-20,0,-236},V{176,0,-238},V{304,0,-292}})place("industrial_pastic_container",at,6,CityNoise(int(at.x))*6.283f,120,22);
    for(float x:{58.f,78.f,98.f})place(x==78?"concrete_road_barrier_02":"concrete_road_barrier",{x,0,-96},10,0,350,25);
    for(V at:{V{6,0,-460},V{150,0,-460}})place("water_manhole_cover",at+V{0,.02f,0},4.4f,0);
    // Steel shutter gate: absorbed or broken after the knights fall.
    uint32_t gateMesh=[&]{CityBuilder local;local.Box(CityShutter,{-31,0,-1.2f},{31,58,1.2f});local.Box(CityShutter,{-32,52,-3.5f},{32,58,3.5f});
        auto& g=local.geometry[CityShutter];return render->CreateDynamicMesh(g.vertices,g.indices);}();
    cityGateEntity_=placeMesh("shutter gate",gateMesh,CityShutter,CityGateCenter,0,58,0,0);
    {auto& collider=r.emplace<BoxColliderComponent>(cityGateEntity_);collider.size={64,58,2.4f};collider.center={0,29,0};r.emplace<TagComponent>(cityGateEntity_).tag=TagType::Wall;}
    // --- Area 3: scramble crossing. Lights at the corners, abandoned cars, barrier lines and a statue.
    for(float sx:{-1.f,1.f})for(float sz:{-1.f,1.f}){V corner{cx+sx*(h+10),0,cz+sz*(h+10)};
        lamp(corner,sx>0?(sz>0?-2.356f:2.356f):(sz>0?-.785f:.785f));
        lamp(corner+V{sx*90,0,0},sz>0?-1.5708f:1.5708f);lamp(corner+V{0,0,sz*90},sx>0?3.14159f:0);}
    for(int i=0;i<14;++i){float a=float(i)*.4488f,radius=40+CityNoise(i*7)*120;V at{cx+std::cos(a)*radius,0,cz+std::sin(a)*radius};
        if(std::abs(at.x-cx)>h&&std::abs(at.z-cz)>h)continue;place("covered_car",at,26,a+CityNoise(i*11)*1.2f,600,45);}
    for(V cannon:{V{-172,0,90},V{328,0,90},V{-172,0,590},V{328,0,590}})
        for(int k=0;k<3;++k){float a=float(k)*1.0472f+(cannon.z>cz?3.6652f:.5236f)+(cannon.x>cx?.5236f:0);
            place(k==1?"concrete_road_barrier_02":"concrete_road_barrier",cannon+V{std::cos(a)*20,0,std::sin(a)*20},10,a+1.5708f,350,25);}
    // Road arms are closed off where the playable crossing ends.
    for(float t=-60;t<=60;t+=20){place("concrete_road_barrier",{cx+t,0,plaza.z1-6},10,0);place("concrete_road_barrier",{plaza.x0+6,0,cz+t},10,1.5708f);place("concrete_road_barrier",{plaza.x1-6,0,cz+t},10,1.5708f);}
    for(V at:{V{-280,0,-20},V{440,0,-20},V{-280,0,700},V{440,0,700}})place("korean_public_payphone_01",at,4.4f,0,300,24);
    for(V at:{V{-40,0,200},V{196,0,200},V{-40,0,480},V{196,0,480},V{-260,0,240},V{420,0,440}})place(CityNoise(int(at.x+at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,0,260,20);
    {V pedestal{-200,0,-10};CityBuilder local;local.Box(CityMetal,{-8,0,-8},{8,6,8});auto& g=local.geometry[CityMetal];
        placeMesh("statue pedestal",render->CreateDynamicMesh(g.vertices,g.indices),CityConcrete,pedestal,0,6,0,0);
        place("gothic_statue",pedestal+V{0,6,0},12,.6f,2200,260);}
    cityRat_=model("street_rat");cityCannon_=model("cannon_01");
    // Shared unit cube for planks, golem blocks, arrows, debris and glowing signs.
    std::vector<Engine::VertexData> cube;std::vector<uint32_t> cubeIndex;
    {CityBuilder unit;unit.tile.fill(1);unit.Box(0,{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true);cube=unit.geometry[0].vertices;cubeIndex=unit.geometry[0].indices;
        for(auto& v:cube){// Per-face 0..1 UVs.
            float ax=std::abs(v.normal.x),ay=std::abs(v.normal.y);
            v.texcoord=ay>.5f?DirectX::XMFLOAT2{v.position.x+.5f,v.position.z+.5f}:ax>.5f?DirectX::XMFLOAT2{v.position.z+.5f,.5f-v.position.y}:DirectX::XMFLOAT2{v.position.x+.5f,.5f-v.position.y};}}
    cityCube_=render->CreateDynamicMesh(cube,cubeIndex);
    cityGates_={};cityBarricadeHealth_=CityBarricadeHealth;cityGateHealth_=CityGateHealth;cityBarricadeShake_=cityGateShake_=0;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();cityGateProp_=-1;cityAnnounced_=-1;
}
void ChronoSystem::SpawnCityStep(int step){
    using namespace Chrono;
    for(const auto& spawn:CityStepSpawns(step)){
        auto enemy=CitySpawnEnemy(spawn,uint32_t(swarm_.size()+1),step);enemy.at.y=InkGround(enemy.at)+enemy.scale;swarm_.push_back(enemy);}
}
int ChronoSystem::CityStepAlive()const{
    int alive=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.step==life_.step)++alive;return alive;
}
void ChronoSystem::BreakCityGate(entt::registry& r,bool barricade,bool absorbed){
    using namespace Chrono;
    auto burst=[&](V at,V size,int material,int count,float spread){
        for(int i=0;i<count;++i){float a=CityNoise(i*7+int(at.x))*6.283f,u=CityNoise(i*13+int(at.z));
            V velocity{std::cos(a)*spread*(.4f+u),14+u*18,std::sin(a)*spread*(.4f+u)+16};
            cityDebris_.push_back({at+V{(CityNoise(i*3)-.5f)*size.x,CityNoise(i*5)*size.y,0},velocity,V{u*6,a,u*4},
                V{1.2f+u*3,.5f+u*.6f,.5f+u*.4f},{},0,material});}
    };
    if(barricade){cityGates_.barricadeOpen=true;burst(CityBarricadeCenter+V{0,4,0},{44,14,1},0,26,10);
        life_.message="バリケード破壊！ 市街地へ";life_.messageAge=3;}
    else{cityGates_.gateOpen=true;
        if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=false;
        if(r.valid(cityGateEntity_))r.get<MeshRendererComponent>(cityGateEntity_).enabled=false;
        if(cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size())life_.props[size_t(cityGateProp_)].active=false;
        if(!absorbed)burst(CityGateCenter+V{0,10,0},{60,50,1},1,30,14);
        life_.message=absorbed?"シャッターごと吸い込んだ！ 大交差点へ":"シャッターを破壊！ 大交差点へ";life_.messageAge=3;}
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
void ChronoSystem::CityWeaponHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,int pulse,uint64_t hitKey){
    using namespace Chrono;
    auto touches=[&](V center,float y,float radius){for(float x:{-16.f,0.f,16.f})if(LifeWeaponTouches(b,center+V{x,y,0},radius,pulse))return true;return false;};
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touches(CityBarricadeCenter,6,8)){cityBarricadeHit_=hitKey;
        if(b.action==LifeAction::Hammer){cityBarricadeHealth_-=LifeWeaponDamage(b);cityBarricadeShake_=.35f;
            ctx.camera->StartImpactShake(.12f,.14f,{0,1,0},16);swarmEffects_.push_back({CityBarricadeCenter-V{0,0,4},0,10,2});
            if(cityBarricadeHealth_<=0)BreakCityGate(r,true,false);
            else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth*100)))+"%";life_.messageAge=1.2f;}}
        else{life_.message="工事バリケードはハンマーで叩き壊せる";life_.messageAge=2;}
    }
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&(touches(CityGateCenter,10,10)||touches(CityGateCenter+V{0,0,-3},30,10))){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="騎士団がシャッターを守っている / 先に騎士団を倒そう";life_.messageAge=2;return;}
        cityGateHealth_-=LifeWeaponDamage(b)*(b.action==LifeAction::Hammer?1.4f:1.f);cityGateShake_=.35f;
        ctx.camera->StartImpactShake(.12f,.16f,{0,1,0},14);swarmEffects_.push_back({CityGateCenter-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0)BreakCityGate(r,false,false);
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth*100)))+"% / 本体1400ならシャッターごと吸い込める";life_.messageAge=1.4f;}
    }
}
void ChronoSystem::UpdateCityStage(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    using namespace Chrono;auto& body=life_.bodies[0];
    cityBarricadeShake_=std::max(0.f,cityBarricadeShake_-dt);cityGateShake_=std::max(0.f,cityGateShake_-dt);cityHintClock_=std::max(0.f,cityHintClock_-dt);
    auto area=CityAreaAt(body.at);
    if(int(area)!=cityAnnounced_){cityAnnounced_=int(area);
        life_.message="AREA "+std::to_string(int(area)+1)+"  "+CityAreaName(area);life_.messageAge=3;}
    int alive=CityStepAlive();
    // The knights' fall unlocks the iron gate as an absorbable (or breakable) target.
    if(!cityGateUnlocked_&&(life_.step>4||(life_.step==4&&alive==0))){cityGateUnlocked_=true;
        if(cityGateProp_>=0)life_.props[size_t(cityGateProp_)].active=true;
        life_.message="シャッターの守りが解けた！ 本体"+std::to_string(int(CityGateAbsorbMass))+"でシャッターごと吸い込む / 武器で破壊";life_.messageAge=4;}
    if(cityGateUnlocked_&&!cityGates_.gateOpen&&cityGateProp_>=0&&!life_.props[size_t(cityGateProp_)].active)BreakCityGate(r,false,true);
    // Size-gated hints when the player reaches a closed gate.
    if(cityHintClock_<=0&&life_.messageAge<=0){
        if(!cityGates_.barricadeOpen&&LifeDistance(body.at,CityBarricadeCenter)<34){cityHintClock_=4;
            life_.message=body.mass<LifeHammerMass?"本体"+std::to_string(int(LifeHammerMass))+"まで育ててハンマーに変形し、バリケードを叩き壊そう":std::string("1でハンマーを選び、左クリック／RTで叩き壊そう");life_.messageAge=3;}
        else if(!cityGates_.gateOpen&&LifeDistance(body.at,CityGateCenter)<50){cityHintClock_=4;
            life_.message=!cityGateUnlocked_?std::string("騎士団を倒すとシャッターの守りが解ける"):body.mass<CityGateAbsorbMass?"本体"+std::to_string(int(CityGateAbsorbMass))+"でシャッターごと吸い込める / 今は武器で破壊":std::string("右クリック／Xでシャッターごと吸い込もう");life_.messageAge=3;}
    }
    bool gate=life_.step==1?cityGates_.barricadeOpen:life_.step==4?cityGates_.gateOpen:true;
    if(alive==0&&gate&&life_.step+1<CityStepCount){
        life_.stepDelay=std::max(0.f,life_.stepDelay-dt);
        if(life_.stepDelay<=0){++life_.step;life_.stepDelay=2;SpawnCityStep(life_.step);
            life_.message="段階 "+std::to_string(life_.step+1)+" / "+std::to_string(CityStepCount)+"  "+CityStepInfo(life_.step).title;life_.messageAge=3.5f;}
    }
    for(auto& d:cityDebris_){d.age+=dt;d.velocity.y-=34*dt;d.at=d.at+d.velocity*dt;d.rotate=d.rotate+d.spin*dt;
        if(d.at.y<d.size.y*.5f){d.at.y=d.size.y*.5f;d.velocity=d.velocity*.35f;d.velocity.y=std::abs(d.velocity.y)*.3f;d.spin=d.spin*.5f;}}
    cityDebris_.erase(std::remove_if(cityDebris_.begin(),cityDebris_.end(),[](const CityDebris& d){return d.age>4;}),cityDebris_.end());
    auto hurt=[&](V at,float radius,float damage){
        // Projectiles hit the main body (HP and split) or wear down a child fragment.
        float reach=LifeRadii(body,life_.recipe).x*.85f+radius;
        if(body.mass>0&&!body.down&&LifeDistance(body.at,at)<reach&&std::abs(at.y-body.at.y)<reach+2){Damage(r,p,damage,ctx,at);return true;}
        for(size_t i=1;i<life_.bodies.size();++i){auto& c=life_.bodies[i];if(c.mass<=0||c.down)continue;
            float cr=2.4f*LifeScale(c.mass)+radius;if(LifeDistance(c.at,at)<cr&&std::abs(at.y-c.at.y)<cr+2){
                c.health=std::max(0.f,c.health-damage*.6f);if(c.health<=0){c.down=true;LifeReturnToSlime(c);++life_.revision;}return true;}}
        return false;};
    for(auto& shot:cityShots_){V previous=shot.at;shot.age+=dt;shot.velocity.y-=(shot.shell?30.f:9.f)*dt;shot.at=shot.at+shot.velocity*dt;
        bool blocked=false;for(const auto& sample:ctx.renderer->GetHydrantWaterSnapshot().particles){
            V at{sample.at.x,sample.at.y,sample.at.z},velocity{sample.velocity.x,sample.velocity.y,sample.velocity.z};
            if(sample.type>1.15f&&LifeHydrantLiveWater(sample.type,sample.contactAge,velocity)&&
                LifeWaterSegmentDistance(previous,shot.at,at-velocity*std::min(.05f,dt*2),at)<(shot.shell?1.7f:1.1f)){
                blocked=true;shot.age=99;++life_.waterBlocks;if(swarmEffects_.size()<128)swarmEffects_.push_back({shot.at,0,2,2});break;}}
        if(blocked)continue;
        float ground=InkGround(shot.at);
        if(shot.shell){if(shot.at.y<=ground){shot.age=99;swarmEffects_.push_back({shot.at,0,11,1});ctx.camera->StartImpactShake(.1f,.08f,{0,1,0},14);
                hurt({shot.at.x,body.at.y,shot.at.z},9,CityShotDamage(true));}}
        else if(hurt(shot.at,.6f,CityShotDamage(false))||shot.at.y<=ground||!CityWalkable({shot.at.x,0,shot.at.z},.1f,{true,true}))shot.age=99;
    }
    cityShots_.erase(std::remove_if(cityShots_.begin(),cityShots_.end(),[](const CityShot& s){return s.age>4;}),cityShots_.end());
    for(auto& stomp:cityStomps_){float previous=stomp.age;stomp.age+=dt;
        if(previous<.85f&&stomp.age>=.85f){swarmEffects_.push_back({stomp.at,0,stomp.radius,1});ctx.camera->StartImpactShake(.3f,.22f,{0,1,0},9);
            hurt({stomp.at.x,body.at.y,stomp.at.z},stomp.radius,22);}}
    cityStomps_.erase(std::remove_if(cityStomps_.begin(),cityStomps_.end(),[](const CityStomp& s){return s.age>1.2f;}),cityStomps_.end());
}
void ChronoSystem::UpdateCityEnemies(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    using namespace Chrono;auto& ink=r.get<InkPlayer>(player_);auto& body=life_.bodies[0];
    int active=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash))++active;
    const int maxActive=life_.step>=5?4:2;
    ink.swarmAlive=0;
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        ++ink.swarmAlive;auto kind=CityKind(enemy);
        int victim=0;float nearest=LifeDistance(body.at,enemy.at);
        for(size_t i=1;i<life_.bodies.size();++i)if(life_.bodies[i].mass>0&&!life_.bodies[i].down){float d=LifeDistance(life_.bodies[i].at,enemy.at);if(d<nearest){nearest=d;victim=int(i);}}
        auto& targetBody=life_.bodies[size_t(victim)];V before=enemy.at;auto phase=enemy.phase;
        enemy.stagger=std::max(0.f,enemy.stagger-dt);
        if(!enemy.alerted&&nearest<CityAlertRange(kind)){enemy.alerted=true;
            // A spotted intruder alerts nearby members of the same patrol.
            for(auto& other:swarm_)if(SwarmAlive(other)&&!other.alerted&&LifeDistance(other.at,enemy.at)<45)other.alerted=true;}
        bool held=LifeTargetReserved(life_,enemy.id);
        V toward=targetBody.at-enemy.at;toward.y=0;float distance=Length(toward);V direction=distance>.01f?toward*(1/distance):enemy.heading;
        auto idleTick=[&](){enemy.age+=dt;enemy.clock+=dt;enemy.flash=std::max(0.f,enemy.flash-dt);
            if(enemy.phase==SwarmPhase::Spawn&&enemy.clock>=.6f)ChangeSwarmPhase(enemy,SwarmPhase::Chase);};
        if(held){}
        else if(kind==CityEnemy::Cannon){idleTick();
            enemy.heading=Unit(Lerp(enemy.heading,direction,1-std::exp(-2.5f*dt)));
            if(enemy.alerted&&enemy.phase!=SwarmPhase::Spawn){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<170){enemy.shotClock=4+SwarmVariation(enemy.id,enemy.attacks++)*1.5f;
                    V muzzle=enemy.at+enemy.heading*(enemy.scale*1.6f)+V{0,enemy.scale*.6f,0};V target=targetBody.at;target.y=InkGround(target);
                    cityShots_.push_back({muzzle,CityShellVelocity(muzzle,target,1.7f),0,true,target});enemy.flash=.25f;
                    ctx.camera->StartImpactShake(.06f,.04f,{0,1,0},18);}}}
        else if(!enemy.alerted){idleTick();
            // Patrol between the home and patrol points at walking pace.
            V goal=enemy.patrol-enemy.at;goal.y=0;float speed=2.6f*std::sqrt(enemy.scale);
            if(Length(goal)<1.5f)std::swap(enemy.home,enemy.patrol);
            else{V step=Unit(goal);enemy.heading=Unit(Lerp(enemy.heading,step,1-std::exp(-5*dt)));enemy.at=enemy.at+step*std::min(Length(goal),speed*dt);}}
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
        else{
            float pace=kind==CityEnemy::Guardian?.45f:1.f;
            TickSwarm(enemy,targetBody.at,dt*pace,InkGround(enemy.at),active<maxActive&&enemy.stagger<=0);
            if(kind==CityEnemy::Guardian&&enemy.phase==SwarmPhase::Chase){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<75){enemy.shotClock=6.5f;cityStomps_.push_back({V{enemy.at.x,0,enemy.at.z},0,36});}}
        }
        if(phase!=SwarmPhase::Windup&&enemy.phase==SwarmPhase::Windup)++active;
        float margin=kind==CityEnemy::Guardian?18.f:std::max(1.f,enemy.scale*1.1f);
        enemy.at=CityConstrain(before,enemy.at,margin,cityGates_);
        enemy.at.y=InkGround(enemy.at)+enemy.scale;enemy.corrosion=enemy.health<=enemy.maxHealth*.3f?1.f:0.f;
        if(phase==SwarmPhase::Dash&&!enemy.struck){float hit,hitRadius=2.2f*LifeScale(targetBody.mass)+1.2f*enemy.scale;V delta=enemy.at-before;
            if(LifeDistance(enemy.at,targetBody.at)<hitRadius||(Length(delta)>.001f&&RaySphere(before,Unit(delta),targetBody.at,hitRadius,Length(delta),hit))){
                enemy.struck=true;float damage=CityDamage(kind);if(victim==0)Damage(r,p,damage,ctx,enemy.at);
                else {targetBody.health=std::max(0.f,targetBody.health-damage*.7f);if(targetBody.health<=0){targetBody.down=true;LifeReturnToSlime(targetBody);++life_.revision;}}
            }}
        enemy.cameraOpacity=SwarmCameraOpacity(Read(ctx.camera->Position()),body.at,enemy.at,false);
    }
}
void ChronoSystem::DrawCityStage(GameContext& ctx){
    using namespace Chrono;auto* render=ctx.renderer;
    auto part=[&](const CityModel& skin,V center,V right,V up,V forward,V size,Engine::Vector4 color){
        Engine::Matrix4x4 m{};V axes[3]={right*size.x,up*size.y,forward*size.z};
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=center.x;m.m[3][1]=center.y;m.m[3][2]=center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,skin.texture,m,color,"CitySurface",skin.extra);};
    auto euler=[&](V rotate,V& right,V& up,V& forward){
        // Yaw (y), then pitch (x) and roll (z) in the yawed frame.
        float cy=std::cos(rotate.y),sy=std::sin(rotate.y),cp=std::cos(rotate.x),sp=std::sin(rotate.x),cr=std::cos(rotate.z),sr=std::sin(rotate.z);
        V r0{cy,0,-sy},u0{0,1,0},f0{sy,0,cy};
        V u1=u0*cp+f0*sp,f1=f0*cp-u0*sp;right=r0*cr+u1*sr;up=u1*cr-r0*sr;forward=f1;};
    auto model=[&](const CityModel& m,V ground,float width,float yaw,Engine::Vector4 color){
        float scale=width/std::max({m.hi.x-m.lo.x,m.hi.z-m.lo.z,.001f});V mid=(m.lo+m.hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        Engine::Transform t;t.scale={scale,scale,scale};t.rotate={0,yaw,0};
        t.translate=EV(V{ground.x-(mid.x*cs+mid.z*sn)*scale,ground.y-m.lo.y*scale,ground.z-(-mid.x*sn+mid.z*cs)*scale});
        render->DrawMeshInstanced(m.mesh,m.texture,t,color,"CitySurface",m.extra);};
    auto floorRing=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.2f,at.z});t.scale={size,1,size};color.w=float(kind)*2+std::clamp(progress,0.f,.999f);
        render->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    if(!cityGates_.barricadeOpen){float shake=cityBarricadeShake_>0?std::sin(cityBarricadeShake_*90)*cityBarricadeShake_:0.f;
        float wear=1-.45f*(1-cityBarricadeHealth_/CityBarricadeHealth);
        for(const auto& plank:cityBarricade_){V right,up,forward;euler(plank.rotate,right,up,forward);
            part(cityWood_,plank.at+V{shake,0,0},right,up,forward,plank.size,{wear,wear*.95f,wear*.9f,1});}}
    for(const auto& d:cityDebris_){V right,up,forward;euler(d.rotate,right,up,forward);float fade=std::clamp(4-d.age,0.f,1.f);
        part(d.material==0?cityWood_:cityIron_,d.at,right,up,forward,d.size*fade,{1,1,1,1});}
    for(const auto& shot:cityShots_){
        if(shot.shell){Engine::Transform t;t.translate=EV(shot.at);t.scale={1.3f,1.3f,1.3f};render->DrawMeshInstanced(sphere_,white_,t,{.06f,.06f,.07f,1});
            floorRing(shot.target,9,0,std::clamp(shot.age/1.7f,0.f,1.f),{1,.3f,.05f,1});}
        else{V forward=Unit(shot.velocity),right=Unit(SwarmCross({0,1,0},forward)),up=SwarmCross(forward,right);
            part(cityWood_,shot.at,right,up,forward,{.25f,.25f,3.2f},{.75f,.6f,.45f,1});}
    }
    for(const auto& glow:cityGlows_){V forward=SwarmCross(glow.right,glow.up);Engine::Matrix4x4 m{};V axes[3]={glow.right*glow.size.x,glow.up*glow.size.y,forward*glow.size.z};
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=glow.center.x;m.m[3][1]=glow.center.y;m.m[3][2]=glow.center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,white_,m,glow.color,"DomainGlow");}
    for(const auto& stomp:cityStomps_)if(stomp.age<.85f)floorRing(stomp.at,stomp.radius,0,stomp.age/.85f,{1,.25f,.04f,1});
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto kind=CityKind(enemy);
        float glow=1+enemy.flash*5;Engine::Vector4 tint{glow,glow,glow,1};if(enemy.corrosion>0)tint={.55f*glow,1.1f*glow,.55f*glow,1};
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z);V ground{enemy.at.x,InkGround(enemy.at),enemy.at.z};
        float rise=enemy.phase==SwarmPhase::Spawn?std::clamp(enemy.clock/.6f,.05f,1.f):1.f;
        if(kind==CityEnemy::Rat){float scurry=std::sin(enemy.age*18+float(enemy.id))*.08f;
            model(cityRat_,ground+V{0,std::abs(scurry),0},CityRatWidth*enemy.scale/.75f*rise,yaw+CityRatYaw,tint);}
        else if(kind==CityEnemy::Cannon)model(cityCannon_,ground,enemy.scale*CityCannonWidth,yaw+CityCannonYaw,tint);
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
// A committed prop travels back along its tentacle as the real scanned model.
bool ChronoSystem::DrawCityPropInTentacle(GameContext& ctx,uint32_t visual,Chrono::Vec at,float shrink){
    auto found=cityVisuals_.find(visual);if(found==cityVisuals_.end())return false;const auto& v=found->second;
    Engine::Transform t;t.scale=EV(v.scale*shrink);t.rotate=EV(v.rotate);t.translate=EV(at-v.center*shrink);
    ctx.renderer->DrawMeshInstanced(v.mesh,v.texture,t,{1,1,1,1},v.shader,v.extra);return true;
}
}
