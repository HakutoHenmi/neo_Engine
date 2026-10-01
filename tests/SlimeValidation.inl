    bool slimeTiers_[4]{},slimeMono_=false,slimeReturn_=false,slimeMass_=true;
    bool portfolioTrail_=false,portfolioRecall_=false,portfolioCharged_=false;
    int slimePreviousCores_=0;int slimeTarget_=3;float slimeLastTotal_=Game::Chrono::SlimeMaximumMass;int slimeHits_=0;
    entt::entity slimeTestProp_=entt::null,slimeTestShot_=entt::null;bool slimeFixture_=false,slimeDissolved_=false;
    int domainTestRound_=0,domainTestNode_=1,domainTestCounts_[3]{};
    int chainNode_=1,chainMaxLoops_=0;bool chainFired_=false,chainCaptured_=false;float chainWait_=0;
    bool domainTestWaiting_=false,domainTestCapture_=false;float domainTestWait_=0;
    #include "RogueliteValidation.inl"
    void UpdateInkTest(){
        if(wcsstr(GetCommandLineW(),L"--rogue-smoke")){UpdateRogueliteTest();return;}
        if(wcsstr(GetCommandLineW(),L"--horde-smoke")){UpdateHordeTest();return;}
        if(wcsstr(GetCommandLineW(),L"--scenery-perf")){UpdateSceneryPerformanceTest();return;}
        if(wcsstr(GetCommandLineW(),L"--domain-skills")){UpdateDomainSkillsTest();return;}
        if(wcsstr(GetCommandLineW(),L"--domain-chain")){UpdateDomainChainTest();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-field")){UpdateSlimeFieldTest();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-polish")){UpdateSlimePolishTest();return;}
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;input.pitch=.55f;
        const float radii[]={8,16,28};float radius=radii[std::min(2,domainTestRound_)];
        // Invulnerability isolates the new offensive loop while the real boss continues its AI.
        p.invincible=1;
        if(frames_++==0){t.translate={-180+radius,1.25f,-160};p.velocity={};ink.domainPath.Clear();}
        if(!domainTestWaiting_&&domainTestRound_<3){
            Vec pos{t.translate.x,0,t.translate.z};float angle=domainTestNode_*6.283185f/48;
            Vec goal{-180+radius*std::cos(angle),0,-160+radius*std::sin(angle)};
            if(DomainDistance(pos,goal)<1){++domainTestNode_;angle=domainTestNode_*6.283185f/48;
                goal={-180+radius*std::cos(angle),0,-160+radius*std::sin(angle)};}
            input.move=Unit(goal-pos);
            if(ink.domainPath.candidate.Ready()){
                if(!domainTestCapture_){Capture(L"tests/out/domain-ready.png");domainTestCapture_=true;}
                input.move={};input.attack=true;
            }
        }
        int before=ink.domainVolleys;Game::GameScene::Update();
        if(ink.domainVolleys>before){domainTestCounts_[domainTestRound_]=ink.domainLastCount;domainTestWaiting_=true;domainTestWait_=0;}
        if(domainTestWaiting_){
            domainTestWait_+=1.f/60;
            if(domainTestWait_>.8f&&domainTestWait_<.85f)Capture((L"tests/out/domain-volley-"+std::to_wstring(domainTestRound_)+L".png").c_str());
            if(domainTestWait_>1&&ink.domainQueued==0&&ink.domainFlying==0){
                ++domainTestRound_;domainTestWaiting_=false;domainTestNode_=1;
                if(domainTestRound_<3){t.translate={-180+radii[domainTestRound_],1.25f,-160};p.velocity={};ink.domainPath.Clear();}
            }
        }
        if(frames_%120==0){trace_<<"domain round="<<domainTestRound_<<" length="<<ink.domainPath.length<<" ready="<<ink.domainPath.candidate.Ready()<<" queued="<<ink.domainQueued<<" flying="<<ink.domainFlying<<" cores="<<p.stats.counters<<" hits="<<ink.coreHits<<'\n';trace_.flush();}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(domainTestRound_>=3||elapsed>100){
            bool pass=domainTestRound_==3&&domainTestCounts_[0]==48&&domainTestCounts_[1]==domainTestCounts_[0]&&domainTestCounts_[2]==domainTestCounts_[0]&&ink.coreHits>0&&p.mass==SlimeMaximumMass;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" domains="<<ink.domainVolleys<<" missiles="<<domainTestCounts_[0]<<','<<domainTestCounts_[1]<<','<<domainTestCounts_[2]<<" core_hits="<<ink.coreHits<<" body="<<p.mass<<" queued="<<ink.domainQueued<<" flying="<<ink.domainFlying<<" elapsed="<<elapsed;
            Capture(L"tests/out/domain-result.png");PostQuitMessage(pass?0:2);
        }
    }
    int hordeNode_=1,hordeMaxAlive_=0,hordeSpawnAtBoss_=0,hordeMaxAttacks_=0;bool hordeEmerging_=false,hordeBoss_=false,hordeHidden_=true,hordeVolleyCapture_=false;float hordeBossAge_=0;
    void UpdateHordeTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;input.pitch=.05f;p.invincible=1;
        if(frames_++==0){t.translate={12,1.25f,-12};p.velocity={};ink.domainPath.Clear();}
        if(frames_<1800){
            for(auto e:r.view<CreaturePart,MeshRendererComponent>())hordeHidden_&=!r.get<MeshRendererComponent>(e).enabled;
            hordeHidden_&=ink.swarmKills==0&&ink.battlePhase==BattlePhase::Horde;
        }else{
            Vec pos{t.translate.x,0,t.translate.z};float a=hordeNode_*6.283185f/96;
            Vec goal{12*std::cos(a),0,-12+12*std::sin(a)};
            if(DomainDistance(pos,goal)<.8f){++hordeNode_;a=hordeNode_*6.283185f/96;goal={12*std::cos(a),0,-12+12*std::sin(a)};}
            input.move=Unit(goal-pos);input.attack=ink.domainPath.candidate.Ready();
        }
        Game::GameScene::Update();hordeMaxAlive_=std::max(hordeMaxAlive_,ink.swarmAlive);hordeMaxAttacks_=std::max(hordeMaxAttacks_,ink.swarmActiveAttacks);
        if(frames_==120)Capture(L"tests/out/horde-crowd.png");
        if(!hordeVolleyCapture_&&ink.domainVolleys==1&&ink.domainFlying>25){Capture(L"tests/out/horde-volley.png");hordeVolleyCapture_=true;}
        if(ink.battlePhase!=BattlePhase::Horde&&ink.swarmKills<SwarmBossKills)hordeHidden_=false;
        if(ink.battlePhase==BattlePhase::Emerging){
            if(!hordeEmerging_){hordeEmerging_=true;Capture(L"tests/out/horde-eruption.png");}
            if(ink.battleAge>2.7f&&ink.battleAge<2.74f)Capture(L"tests/out/horde-emerging.png");
        }
        if(ink.battlePhase==BattlePhase::Boss){
            if(!hordeBoss_){hordeBoss_=r.get<CreatureBoss>(*r.view<CreatureBoss>().begin()).stage==CreatureStage::Snake;hordeSpawnAtBoss_=ink.swarmSpawned;}
            hordeBossAge_+=1.f/60;
        }
        if(frames_%120==0){trace_<<"horde kills="<<ink.swarmKills<<" alive="<<ink.swarmAlive<<" phase="<<int(ink.battlePhase)<<" volleys="<<ink.domainVolleys<<" strikes="<<ink.swarmStrikes<<" detonations="<<ink.swarmDetonations<<" core_hits="<<ink.coreHits<<'\n';trace_.flush();}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if((hordeBossAge_>8&&ink.coreHits>0)||elapsed>210){
            bool pass=hordeHidden_&&hordeEmerging_&&hordeBoss_&&ink.swarmKills>=SwarmBossKills&&ink.swarmSpawned>hordeSpawnAtBoss_+12&&hordeMaxAlive_<=int(SwarmCapacity)&&hordeMaxAttacks_<=3&&ink.swarmStrikes>0&&ink.swarmDetonations>0&&ink.coreHits>0;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" horde kills="<<ink.swarmKills<<" max_alive="<<hordeMaxAlive_<<" max_attacks="<<hordeMaxAttacks_<<" hidden="<<hordeHidden_<<" emerging="<<hordeEmerging_<<" snake="<<hordeBoss_<<" reinforcements="<<ink.swarmSpawned-hordeSpawnAtBoss_<<" volleys="<<ink.domainVolleys<<" strikes="<<ink.swarmStrikes<<" detonations="<<ink.swarmDetonations<<" core_hits="<<ink.coreHits<<" elapsed="<<elapsed;
            Capture(L"tests/out/horde-boss.png");PostQuitMessage(pass?0:2);
        }
    }
    bool fieldValid_=true;int fieldEdges_=0;
    double sceneryGpu_[3]{},sceneryWall_[3]{};uint64_t sceneryOriginal_[3]{},scenerySelected_[3]{},sceneryOriginalVertices_[3]{},scenerySelectedVertices_[3]{};int scenerySamples_[3]{};
    std::chrono::steady_clock::time_point sceneryPrevious_{};
    void UpdateSceneryPerformanceTest(){
        using namespace Game;using namespace Game::Chrono;
        auto now=std::chrono::steady_clock::now();double seconds=sceneryPrevious_.time_since_epoch().count()?std::chrono::duration<double>(now-sceneryPrevious_).count():0;sceneryPrevious_=now;
        auto* renderer=Engine::Renderer::GetInstance();auto& r=GetRegistry();auto player=FindObjectByName("Player");
        auto& p=r.get<Player>(player);auto& t=r.get<TransformComponent>(player);auto& input=r.get<ControlFrame>(player);
        unsigned phase=std::min(2u,frames_/240),frame=frames_%240;
        if(frames_==0){t.translate={-60,1.25f,-45};p.velocity={};}
        if(frame>=80&&frame<200){const auto& stats=renderer->GetSceneryLodStats();const auto& gpu=renderer->GetFluidProfileStats();
            sceneryOriginal_[phase]+=stats.originalIndices;scenerySelected_[phase]+=stats.selectedIndices;
            sceneryOriginalVertices_[phase]+=stats.originalVertices;scenerySelectedVertices_[phase]+=stats.selectedVertices;
            sceneryGpu_[phase]+=gpu.lastMs[Engine::Renderer::SceneRender];sceneryWall_[phase]+=seconds*1000;++scenerySamples_[phase];}
        renderer->SetDistanceLodEnabled(phase>0);p.invincible=1;p.hitStop=1;input={};input.cameraInput=true;input.pitch=.18f;
        Game::GameScene::Update();auto params=renderer->GetPostProcessParams();params.dofStrength=phase==2?.75f:0;renderer->SetPostProcessParams(params);
        if(frame==210)Capture((L"tests/out/scenery-"+std::to_wstring(phase)+L".png").c_str());
        if(++frames_>=720){
            bool pass=scenerySamples_[0]>0&&sceneryOriginal_[0]>0&&sceneryOriginal_[0]==scenerySelected_[0]&&scenerySelected_[1]<sceneryOriginal_[1]*.8&&scenerySelected_[2]<sceneryOriginal_[2]*.8;
            std::ofstream report("tests/out/ink-smoke.txt");report<<(pass?"PASS":"FAIL")<<" scenery LOD + depth of field\n";
            for(int i=0;i<3;++i)report<<"phase="<<i<<" original_indices="<<sceneryOriginal_[i]/uint64_t(std::max(1,scenerySamples_[i]))<<" selected_indices="<<scenerySelected_[i]/uint64_t(std::max(1,scenerySamples_[i]))<<" original_vertices="<<sceneryOriginalVertices_[i]/uint64_t(std::max(1,scenerySamples_[i]))<<" selected_vertices="<<scenerySelectedVertices_[i]/uint64_t(std::max(1,scenerySamples_[i]))<<" gpu_scene_ms="<<sceneryGpu_[i]/std::max(1,scenerySamples_[i])<<" wall_frame_ms="<<sceneryWall_[i]/std::max(1,scenerySamples_[i])<<'\n';
            PostQuitMessage(pass?0:2);
        }
    }
    void UpdateDomainChainTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;input.pitch=.7f;p.invincible=1;
        const Vec origin{-180,0,-160};
        if(frames_++==0){t.translate={origin.x,1.25f,origin.z};p.velocity={};ink.domainPath.Clear();}
        if(!chainFired_){
            Vec pos{t.translate.x,0,t.translate.z};
            auto goalAt=[&](int node){int petal=std::min(2,(node-1)/96);float angle=petal*6.283185f/3;
                Vec radial{std::cos(angle),0,std::sin(angle)},tangent{-radial.z,0,radial.x};
                float a=3.14159265f+float(node-petal*96)*6.283185f/96;
                return origin+radial*(16*(1+std::cos(a)))+tangent*(8*std::sin(a));};
            Vec goal=goalAt(chainNode_);
            if(DomainDistance(pos,goal)<.8f&&chainNode_<288){++chainNode_;goal=goalAt(chainNode_);}
            input.move=Unit(goal-pos);
            if(ink.domainPath.candidate.Ready())chainMaxLoops_=std::max(chainMaxLoops_,ink.domainPath.candidate.Enclosures());
            if(chainMaxLoops_>=3){input.move={};
                if(!chainCaptured_){Capture(L"tests/out/chain-ready.png");chainCaptured_=true;}
                else input.attack=true;
            }
        }
        int before=ink.domainVolleys;Game::GameScene::Update();
        if(ink.domainVolleys>before)chainFired_=true;
        if(chainFired_){chainWait_+=1.f/60;if(chainWait_>.8f&&chainWait_<.82f)Capture(L"tests/out/chain-volley.png");}
        if(frames_%120==0){trace_<<"chain node="<<chainNode_<<" loops="<<chainMaxLoops_<<" fired="<<chainFired_<<" hits="<<ink.coreHits<<'\n';trace_.flush();}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if((chainFired_&&chainWait_>1&&ink.domainQueued==0&&ink.domainFlying==0)||elapsed>80){
            bool pass=chainFired_&&ink.domainLastEnclosures==3&&ink.domainLastShape==DomainShape::Loop&&ink.domainLastCount==80&&ink.coreHits>0;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" one-stroke loops="<<ink.domainLastEnclosures<<" missiles="<<ink.domainLastCount<<" power="<<DomainBulletPower(ink.domainLastEnclosures)<<" normal="<<(ink.domainLastShape==DomainShape::Loop)<<" hits="<<ink.coreHits<<" elapsed="<<elapsed;
            PostQuitMessage(pass?0:2);
        }
    }
    int skillRound_=0,skillNode_=1,skillEscape_=0;bool skillWaiting_=false,skillRemote_=true;float skillWait_=0;
    void UpdateDomainSkillsTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");auto& p=r.get<Player>(player);
        auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;input.pitch=.4f;p.invincible=1;
        Vec origin{-180,0,-160},pos{t.translate.x,0,t.translate.z};
        const DomainShape shapes[]={DomainShape::Triangle,DomainShape::Square,DomainShape::Infinity};
        if(frames_++==0){t.translate={origin.x,1.25f,origin.z};p.velocity={};ink.domainPath.Clear();}
        if(!skillWaiting_&&skillRound_<3){
            if(ink.domainPath.candidate.Ready()&&ink.domainPath.candidate.shape==shapes[skillRound_]){
                // Escape outside the lobes rather than draw a new dividing chord
                // through the figure eight (which correctly creates a third region).
                if(skillEscape_++<45)input.move=skillRound_==2?Vec{0,0,1}:Vec{-1,0,0};
                else{skillRemote_&=DomainDistance(pos,ink.domainPath.candidate.points.front())>12;
                    input.attack=true;Capture((L"tests/out/skill-ready-"+std::to_wstring(skillRound_)+L".png").c_str());}
            }else{
                Vec goal;
                if(skillRound_<2){
                    const Vec triangle[]={{0,0,0},{30,0,0},{15,0,26},{0,0,0}};
                    const Vec square[]={{0,0,0},{30,0,0},{30,0,30},{0,0,30},{0,0,0}};
                    int nodes=skillRound_==0?3:4;const Vec* route=skillRound_==0?triangle:square;
                    goal=origin+route[skillNode_%nodes];
                    if(DomainDistance(pos,goal)<1.2f){++skillNode_;goal=origin+route[skillNode_%nodes];}
                }else{
                    float a=skillNode_*6.283185f/96;goal=origin+Vec{28*std::sin(a),0,18*std::sin(a)*std::cos(a)};
                    if(DomainDistance(pos,goal)<1.2f){++skillNode_;a=skillNode_*6.283185f/96;goal=origin+Vec{28*std::sin(a),0,18*std::sin(a)*std::cos(a)};}
                }
                input.move=Unit(goal-pos);
            }
        }
        int before=ink.domainVolleys;Game::GameScene::Update();
        if(ink.domainVolleys>before){skillWaiting_=true;skillWait_=0;}
        if(skillWaiting_){skillWait_+=1.f/60;
            if(skillWait_>.8f&&skillWait_<.84f)Capture((L"tests/out/skill-volley-"+std::to_wstring(skillRound_)+L".png").c_str());
            if(skillWait_>1&&ink.domainQueued==0&&ink.domainFlying==0){++skillRound_;skillWaiting_=false;skillNode_=1;skillEscape_=0;
                if(skillRound_<3){t.translate={origin.x,1.25f,origin.z};p.velocity={};ink.domainPath.Clear();}}
        }
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(frames_%120==0){trace_<<"skill round="<<skillRound_<<" node="<<skillNode_<<" shape="<<int(ink.domainPath.candidate.shape)<<" ready="<<ink.domainPath.candidate.Ready()<<" escape="<<skillEscape_<<" flying="<<ink.domainFlying<<'\n';trace_.flush();}
        bool used=ink.domainSkillUses[1]>0&&ink.domainSkillUses[2]>0&&ink.domainSkillUses[3]>0;
        if(skillRound_>=3||(used&&p.stats.counters>=3)||elapsed>100){
            bool pass=used&&skillRemote_&&ink.coreHits>0;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" triangle="<<ink.domainSkillUses[1]<<" square="<<ink.domainSkillUses[2]<<" infinity="<<ink.domainSkillUses[3]<<" remote="<<skillRemote_<<" hits="<<ink.coreHits<<" elapsed="<<elapsed;
            PostQuitMessage(pass?0:2);
        }
    }
    void UpdateSlimeFieldTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");auto& p=r.get<Player>(player);
        auto& t=r.get<TransformComponent>(player);auto& ink=r.get<InkPlayer>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;
        // Four isolated boundary fixtures on the expanded floor, then an overview.
        int side=frames_/150,frame=frames_%150;
        if(side<4){
            if(frame==0){const Vec starts[]={{315,1.25f,0},{-315,1.25f,0},{0,1.25f,365},{0,1.25f,-325}};
                t.translate={starts[side].x,starts[side].y,starts[side].z};p.velocity={};}
            const Vec directions[]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
            input.move=directions[side];input.aim=true;p.invincible=1;
        }
        Game::GameScene::Update();
        if(side<4&&frame==149){
            fieldValid_&=p.grounded&&std::abs(t.translate.y-1.25f)<.1f&&ink.deployed>0;
            fieldValid_&=side==0?t.translate.x>327:side==1?t.translate.x<-327:side==2?t.translate.z>377:t.translate.z<-337;
            fieldValid_&=std::abs(t.translate.x)<=328.01f&&t.translate.z>=-338.01f&&t.translate.z<=378.01f;
            ++fieldEdges_;
        }
        if(side>=4){GetCamera().SetPosition(325,310,-390);GetCamera().SetRotation(.52f,-.64f,0);}
        if(frames_==630){
            int bosses=int(r.view<CreatureBoss>().size()),rockFaces=0,boulders=0,grass=0,grassPatches=0,pebbles=0,scannedStones=0,oldScenery=0;
            for(auto e:r.view<NameComponent>()){
                const auto& name=r.get<NameComponent>(e).name;
                if(name=="Chrono Scanned rock face")++rockFaces;
                if(name=="Chrono Border boulder")++boulders;
                if(name=="Chrono Meadow grass")++grass;
                if(name=="Chrono Meadow grass patch")++grassPatches;
                if(name=="Chrono Field pebble")++pebbles;
                if(name=="Chrono Scanned field stone")++scannedStones;
                if(name=="Chrono Distant mountain"||name=="Chrono Woodland tree"||name=="Chrono Basin rocks"||name=="Chrono Citadel tower"||name=="Chrono Creature floor"||name=="Chrono Sandstone cliff"||name=="Chrono Scanned cliff")++oldScenery;
                if(name=="Chrono Scanned rock face"||name=="Chrono Border boulder"||name=="Chrono Meadow grass"||name=="Chrono Meadow grass patch"||name=="Chrono Field pebble"||name=="Chrono Scanned field stone")fieldValid_&=!r.any_of<BoxColliderComponent,GpuMeshColliderComponent,Solid>(e);
            }
            fieldValid_&=bosses==1&&r.view<Hopper>().size()==0&&rockFaces==12&&boulders==12&&grass==16&&grassPatches==272&&pebbles==203&&scannedStones==8&&oldScenery==0&&fieldEdges_==4;
            Capture(L"tests/out/meadow-field.png");
            std::ofstream("tests/out/ink-smoke.txt")<<(fieldValid_?"PASS":"FAIL")<<" field_edges="<<fieldEdges_<<" bosses="<<bosses<<" rock_faces="<<rockFaces<<" boulders="<<boulders<<" grass="<<grass<<" grass_patches="<<grassPatches<<" pebbles="<<pebbles<<" scanned_stones="<<scannedStones<<" old_scenery="<<oldScenery<<" trail="<<ink.deployed;
            PostQuitMessage(fieldValid_?0:2);
        }
        ++frames_;
    }
    float polishSupportMin_=100,polishSupportDelta_=0,polishPreviousSupport_=-1;float polishRestHeight_=0,polishAirHeight_=0,polishLandingHeight_=100;bool polishJump_=false,polishLand_=false,polishDown_=false,polishUp_=false,polishGrounded_=true;float polishLastY_=1.25f,polishMaxStep_=0;
    void UpdateSlimePolishTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;input.pitch=.12f;
        if(frames_==0){t.translate={32,1.25f,-22};p.velocity={};}
        if(t.translate.z>60&&!polishDown_){polishDown_=true;polishUp_=true;Capture(L"tests/out/slime-dome-top.png");}
        bool stopped=polishDown_&&t.translate.z<-28;
        input.jump=stopped&&inkFinishFrames_==80;
        input.aim=!stopped;input.move=stopped?Vec{}:Vec{0,0,polishDown_?-1.f:1.f};
        if(wcsstr(GetCommandLineW(),L"--portfolio")&&frames_<60)input.aim=false;
        Game::GameScene::Update();++frames_;
        if(wcsstr(GetCommandLineW(),L"--portfolio")&&frames_==50)Capture(L"tests/out/portfolio-move.png");
        if(frames_>8&&!stopped){polishGrounded_&=p.grounded;polishMaxStep_=std::max(polishMaxStep_,std::abs(t.translate.y-polishLastY_));}
        polishLastY_=t.translate.y;
        if(t.translate.z>-10&&t.translate.z<30&&p.grounded){
            const auto& snapshot=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();
            float mean=0;for(auto q:snapshot.offsets){float h=q.y+1.43f-ink.groundSlope.x*q.x-ink.groundSlope.z*q.z;mean+=h;polishSupportMin_=std::min(polishSupportMin_,h);}
            if(!snapshot.offsets.empty()){mean/=float(snapshot.offsets.size());if(polishPreviousSupport_>=0)polishSupportDelta_=std::max(polishSupportDelta_,std::abs(mean-polishPreviousSupport_));polishPreviousSupport_=mean;}
        }else polishPreviousSupport_=-1;
        if(stopped){++inkFinishFrames_;
            float lo=100,hi=-100;for(auto q:Engine::Renderer::GetInstance()->GetFluidBodySnapshot().offsets){lo=std::min(lo,q.y);hi=std::max(hi,q.y);}float height=hi-lo;
            if(inkFinishFrames_==60){polishRestHeight_=height;Capture(L"tests/out/slime-dome.png");}
            if(inkFinishFrames_>80&&!p.grounded){if(!polishJump_&&ink.airBlend>.9f&&std::abs(p.velocity.y)<2.f){polishJump_=true;polishAirHeight_=height;Capture(L"tests/out/slime-jump.png");}}
            if(polishJump_&&p.grounded&&height<polishRestHeight_*.85f&&!polishLand_){polishLand_=true;polishLandingHeight_=height;Capture(L"tests/out/slime-land.png");}}
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(inkFinishFrames_>210||elapsed>30||p.mass<=0){
            bool pass=polishSupportMin_>.12f&&polishSupportDelta_<.18f&&polishJump_&&polishLand_&&ink.footprintSerial>0&&ink.footprintRadius>2&&polishUp_&&stopped&&polishGrounded_&&polishMaxStep_<.6f&&ink.deployed>100&&p.mass>SlimeMaximumMass*.5f&&!Engine::Renderer::GetInstance()->GetDrawFluidDebugArrows();
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" support_min="<<polishSupportMin_<<" support_delta="<<polishSupportDelta_<<" rest_height="<<polishRestHeight_<<" air_height="<<polishAirHeight_<<" land_height="<<polishLandingHeight_<<" footprint="<<ink.footprintRadius<<" jump="<<polishJump_<<" landing="<<polishLand_<<" traverse_out="<<polishUp_<<" traverse_back="<<stopped<<" grounded="<<polishGrounded_<<" max_y_step="<<polishMaxStep_<<" body="<<p.mass<<" trail="<<ink.deployed;
            PostQuitMessage(pass?0:2);
        }
    }
