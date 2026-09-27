    bool slimeTiers_[4]{},slimeMono_=false,slimeReturn_=false,slimeMass_=true;
    bool portfolioTrail_=false,portfolioRecall_=false,portfolioCharged_=false;
    int slimePreviousCores_=0;int slimeTarget_=3;float slimeLastTotal_=Game::Chrono::SlimeMaximumMass;int slimeHits_=0;
    entt::entity slimeTestProp_=entt::null,slimeTestShot_=entt::null;bool slimeFixture_=false,slimeDissolved_=false;
    void UpdateInkTest(){
        if(wcsstr(GetCommandLineW(),L"--slime-field")){UpdateSlimeFieldTest();return;}
        if(wcsstr(GetCommandLineW(),L"--slime-polish")){UpdateSlimePolishTest();return;}
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss"),core=FindObjectByName("Chrono Weakpoint");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& t=r.get<TransformComponent>(player);auto& c=r.get<CreatureBoss>(boss);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        bool battle=wcsstr(GetCommandLineW(),L"--slime-battle")!=nullptr;
        bool low=wcsstr(GetCommandLineW(),L"--slime-low")!=nullptr;
        if(frames_==0){t.translate={-60,1.25f,-45};p.velocity={};if(low){p.mass=8;r.get<HealthComponent>(player).hp=8;}} // Fixed fixture spawn; every subsequent action uses production input.
        Vec pos{t.translate.x,t.translate.y,t.translate.z};
        auto ct=r.get<TransformComponent>(core).translate;Vec d=Unit(Vec{ct.x,ct.y,ct.z}-(pos+Vec{0,.25f,0}));
        input.yaw=std::atan2(d.x,d.z);input.pitch=-std::asin(std::clamp(d.y,-1.f,1.f));
        float required=(slimeTarget_==3?90.f:slimeTarget_==2?43.f:18.f)*ink.capacity/95.f;
        if(ink.phase==SlimePhase::Roaming){
            if(ink.deployed>=required&&(!low||(c.sweepPhase==3&&c.sweepTime<.8f)))input.attack=!ink.previousAttack;
            else{
                // Traverse a safe rectangular route and build charge on dry floor.
                Vec goals[]={{60,0,-45},{60,0,-25},{-60,0,-25},{-60,0,-45}};
                Vec goal=goals[inkStage_%4];Vec delta=goal-pos;delta.y=0;
                if(Length(delta)<3)++inkStage_;
                Vec world=Unit(delta);input.aim=true;
                input.move={world.x*std::cos(input.yaw)-world.z*std::sin(input.yaw),0,world.x*std::sin(input.yaw)+world.z*std::cos(input.yaw)};
                if(c.sweepPhase==2)input.dodge=p.cooldown<=0;
            }
        }else if(ink.phase==SlimePhase::Charging)input.attack=ink.deployed>.05f;
        if(!battle&&!slimeFixture_&&ink.phase==SlimePhase::Firing&&ink.tier==3&&ink.phaseAge>.3f){
            Vec axis=Unit(ink.beamEnd-ink.beamStart);slimeFixture_=true;
            slimeTestProp_=r.create();auto& prop=r.emplace<SlimeDissolvable>(slimeTestProp_);prop.center=ink.beamStart+axis*6;prop.half={.6f,.6f,.6f};prop.integrity=8;
            r.emplace<MeshRendererComponent>(slimeTestProp_);
            slimeTestShot_=r.create();auto& shot=r.emplace<HitboxComponent>(slimeTestShot_);shot.isProjectile=true;shot.tag=TagType::Enemy;
            auto at=ink.beamStart+axis*3;r.emplace<TransformComponent>(slimeTestShot_).translate={at.x,at.y,at.z};
        }
        Game::GameScene::Update();++frames_;
        if(wcsstr(GetCommandLineW(),L"--portfolio")){
            // An oblique presentation camera reveals the trail and beam length.
            Vec at{t.translate.x,t.translate.y,t.translate.z};
            Vec forward{std::sin(input.yaw),0,std::cos(input.yaw)};
            Vec right{forward.z,0,-forward.x};
            Vec eye=at+right*25-forward*16+Vec{0,19,0};
            Vec focus=at+forward*10+Vec{0,3,0};
            Vec view=Unit(focus-eye);
            GetCamera().SetPosition(eye.x,eye.y,eye.z);
            GetCamera().SetRotation(-std::asin(view.y),std::atan2(view.x,view.z),0);
            if(!portfolioTrail_&&ink.phase==SlimePhase::Roaming&&ink.deployed>ink.capacity*.8f){Capture(L"tests/out/portfolio-trail.png");portfolioTrail_=true;}
            if(!portfolioRecall_&&ink.phase==SlimePhase::Charging&&ink.charge>ink.capacity*.35f){Capture(L"tests/out/portfolio-recall.png");portfolioRecall_=true;}
            if(!portfolioCharged_&&ink.phase==SlimePhase::Charging&&ink.charge>ink.capacity*.85f){Capture(L"tests/out/portfolio-charged.png");portfolioCharged_=true;}
        }
        if(slimeFixture_)slimeDissolved_=!r.valid(slimeTestProp_)&&!r.valid(slimeTestShot_);
        inkPaint_|=ink.deployed>1;inkSwim_|=ink.swimming&&Length(p.velocity)>15;
        float total=p.mass+ink.deployed+ink.spent;
        slimeMass_&=total<=(p.stats.counters>slimePreviousCores_?SlimeMaximumMass+.02f:slimeLastTotal_+(ink.regenerating?1.61f:.02f))&&total>=-.01f;slimeLastTotal_=total;slimePreviousCores_=p.stats.counters;
        if(ink.phase==SlimePhase::Firing){
            slimeTiers_[ink.tier]=true;
            if(ink.tier==3&&ink.monoAge>=.035f&&ink.monoAge<.075f){slimeMono_|=Engine::Renderer::GetInstance()->GetPostProcessParams().san>.95f;Capture(L"tests/out/slime-monochrome.png");}
            if(ink.phaseAge>.23f&&!inkJetCapture_){Capture((L"tests/out/slime-beam-"+std::to_wstring(ink.tier)+L".png").c_str());inkJetCapture_=true;}
        }
        if(ink.phase==SlimePhase::Returning)slimeReturn_=true;
        if(slimeTiers_[slimeTarget_]&&ink.phase==SlimePhase::Roaming){slimeTarget_=battle?3:slimeTarget_==3?2:1;inkJetCapture_=false;}
        if(frames_%120==0){trace_<<"slime phase="<<int(ink.phase)<<" tier="<<ink.tier<<" body="<<p.mass<<" trail="<<ink.deployed<<" charge="<<ink.charge<<" spent="<<ink.spent<<" hit="<<ink.coreHits<<" pos="<<t.translate.x<<','<<t.translate.y<<','<<t.translate.z<<'\n';trace_.flush();}
        bool complete=slimeTiers_[1]&&slimeTiers_[2]&&slimeTiers_[3]&&ink.phase==SlimePhase::Roaming;
        if(battle)complete=p.stats.counters==3;
        if(complete||p.mass<=0||elapsed>(battle?180.f:85.f)){bool pass=complete&&inkPaint_&&inkSwim_&&slimeMono_&&slimeReturn_&&slimeMass_&&(battle||slimeDissolved_);
            Capture(L"tests/out/ink-result.png");std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" trail="<<inkPaint_<<" slide="<<inkSwim_<<" tiers="<<slimeTiers_[1]<<slimeTiers_[2]<<slimeTiers_[3]<<" monochrome="<<slimeMono_<<" return="<<slimeReturn_<<" mass="<<slimeMass_<<" dissolve="<<slimeDissolved_<<" cores="<<p.stats.counters<<" hits="<<ink.coreHits<<" elapsed="<<elapsed;PostQuitMessage(pass?0:2);}
    }
    bool fieldValid_=true;int fieldEdges_=0;
    void UpdateSlimeFieldTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player");auto& p=r.get<Player>(player);
        auto& t=r.get<TransformComponent>(player);auto& ink=r.get<InkPlayer>(player);
        auto& input=r.get<ControlFrame>(player);input={};input.cameraInput=true;
        // Four isolated boundary fixtures, then an overview of the production scene.
        int side=frames_/150,frame=frames_%150;
        if(side<4){
            if(frame==0){const Vec starts[]={{125,1.25f,0},{-125,1.25f,0},{0,1.25f,155},{0,1.25f,-115}};
                t.translate={starts[side].x,starts[side].y,starts[side].z};p.velocity={};}
            const Vec directions[]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
            input.move=directions[side];input.aim=true;p.invincible=1;
        }
        Game::GameScene::Update();
        if(side<4&&frame==149){
            fieldValid_&=p.grounded&&std::abs(t.translate.y-1.25f)<.1f&&ink.deployed>0;
            fieldValid_&=side==0?t.translate.x>137:side==1?t.translate.x<-137:side==2?t.translate.z>167:t.translate.z<-127;
            fieldValid_&=std::abs(t.translate.x)<=138.01f&&t.translate.z>=-128.01f&&t.translate.z<=168.01f;
            ++fieldEdges_;
        }
        if(side>=4){GetCamera().SetPosition(220,210,-260);GetCamera().SetRotation(.52f,-.64f,0);}
        if(frames_==630){
            int bosses=int(r.view<CreatureBoss>().size()),scenery=0;
            for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name=="Chrono Distant mountain"){
                ++scenery;fieldValid_&=!r.any_of<BoxColliderComponent,GpuMeshColliderComponent,Solid>(e);}
            fieldValid_&=bosses==1&&r.view<Hopper>().size()==0&&scenery==28&&fieldEdges_==4;
            Capture(L"tests/out/verdant-basin.png");
            std::ofstream("tests/out/ink-smoke.txt")<<(fieldValid_?"PASS":"FAIL")<<" field_edges="<<fieldEdges_<<" bosses="<<bosses<<" mountains="<<scenery<<" trail="<<ink.deployed;
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
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" support_min="<<polishSupportMin_<<" support_delta="<<polishSupportDelta_<<" rest_height="<<polishRestHeight_<<" air_height="<<polishAirHeight_<<" land_height="<<polishLandingHeight_<<" footprint="<<ink.footprintRadius<<" jump="<<polishJump_<<" landing="<<polishLand_<<" ramp_up="<<polishUp_<<" ramp_down="<<stopped<<" grounded="<<polishGrounded_<<" max_y_step="<<polishMaxStep_<<" body="<<p.mass<<" trail="<<ink.deployed;
            PostQuitMessage(pass?0:2);
        }
    }
