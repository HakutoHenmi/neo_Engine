
    int rogueMenuFrames_=0,rogueStage_=0,rogueStartRank_=0,rogueChosen_=-1,rogueCombatFrames_=0;
    bool rogueRerolled_=false;
    float rogueFrozenTime_=0,rogueFrozenBattle_=0,rogueFrozenIdle_=0;bool rogueFreezeValid_=true,rogueResume_=false,rogueKillXP_=false;
    void UpdateRogueliteTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& registry=GetRegistry();auto player=FindObjectByName("Player");auto& ink=registry.get<InkPlayer>(player);auto& p=registry.get<Player>(player);
        auto& state=ink.rogue;auto& input=registry.get<ControlFrame>(player);input={};input.pitch=.1f;p.invincible=1;
        // Start with an explicit earned level: this fixture must not depend on incidental spawn XP.
        if(frames_++==0){state.random=73192;state.Gain(state.Required());}
        if(state.menu){
            if(rogueMenuFrames_++==0){rogueFrozenTime_=p.stats.seconds;rogueFrozenBattle_=ink.battleAge;rogueFrozenIdle_=state.idle;}
            rogueFreezeValid_&=p.stats.seconds==rogueFrozenTime_&&ink.battleAge==rogueFrozenBattle_;
            if(rogueMenuFrames_==20)Capture(L"tests/out/rogue-selection.png");
            if(rogueStage_==0){
                if(state.CanChoose()&&!rogueRerolled_){input.upgradeReroll=true;rogueRerolled_=true;}
                else if(state.CanChoose()&&rogueRerolled_){rogueChosen_=state.offers[0];rogueStartRank_=state.ranks[size_t(rogueChosen_)];input.upgradeChoice=0;}}
            else if(rogueStage_==1&&state.CanChoose()){p.mass=650;input.upgradeSkip=true;}
        }
        if(rogueStage_==2){state.pending=0;state.xp=0;}
        if(rogueStage_==2)++rogueCombatFrames_;
        if(rogueStage_==2&&rogueCombatFrames_%40==0){
            // Exercise the production volley with all five build families equipped.
            for(auto& rank:state.ranks)rank=1;
            DomainLoop loop;Vec pos{registry.get<TransformComponent>(player).translate.x,.16f,registry.get<TransformComponent>(player).translate.z};
            for(int i=0;i<=64;++i){float angle=float(i)*6.283185f/64;loop.points.push_back(pos+Vec{std::cos(angle)*35,0,std::sin(angle)*35});}loop.Measure();
            ink.domainPath.candidate=loop;input.attack=true;input.upgradeChoice=-1;
        }
        if(rogueCombatFrames_==150)SetPaused(true);
        bool menuBefore=state.menu;Game::GameScene::Update();
        if(rogueStage_==2&&(state.xp>0||state.pending>0))rogueKillXP_=true;
        if(menuBefore&&!state.menu){
            if(rogueStage_==0){rogueFreezeValid_&=state.ranks[size_t(rogueChosen_)]==rogueStartRank_+1&&state.rerolls==2&&state.idle>rogueFrozenIdle_;
                rogueStage_=1;state.Gain(state.Required());rogueMenuFrames_=0;}
            else{rogueFreezeValid_&=p.mass==710;rogueStage_=2;}}
        if(rogueStage_==2&&p.stats.seconds>rogueFrozenTime_+.1f)rogueResume_=true;
        if(rogueCombatFrames_==190)Capture(L"tests/out/rogue-builds.png");
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        if(rogueCombatFrames_>=220||elapsed>60){
            bool pass=rogueFreezeValid_&&rogueResume_&&rogueKillXP_&&ink.domainVolleys>=3&&state.orbit>0&&state.ranks[5]>0&&state.resonance>0;
            auto* renderer=Engine::Renderer::GetInstance();bool dlssRequested=wcsstr(GetCommandLineW(),L"--dlss-smoke")!=nullptr;
            bool rtRequested=wcsstr(GetCommandLineW(),L"--rt-smoke")!=nullptr;
            if(rtRequested)pass&=renderer->RtShadowFrames()>30&&renderer->RtShadowPixels()>0&&renderer->RtLitPixels()>0;
            if(dlssRequested)pass&=renderer->DlssEvaluatedFrames()>30;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" rogue earned_xp="<<rogueKillXP_<<" freeze="<<rogueFreezeValid_<<" resume="<<rogueResume_<<" level="<<state.level<<" rerolls="<<state.rerolls<<" volleys="<<ink.domainVolleys<<" kills="<<ink.swarmKills<<" orbit="<<state.orbit<<" resonance="<<state.resonance;
            if(dlssRequested)std::ofstream("tests/out/ink-smoke.txt",std::ios::app)<<" dlss_frames="<<renderer->DlssEvaluatedFrames()<<" status="<<renderer->DlssStatus();
            if(rtRequested)std::ofstream("tests/out/ink-smoke.txt",std::ios::app)<<" rt_frames="<<renderer->RtShadowFrames()<<" shadow_pixels="<<renderer->RtShadowPixels()<<" lit_pixels="<<renderer->RtLitPixels();
            PostQuitMessage(pass?0:2);
        }
    }
