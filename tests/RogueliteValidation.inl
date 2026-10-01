
    int rogueMenuFrames_=0,rogueStage_=0,rogueStartRank_=0,rogueChosen_=-1;
    float rogueFrozenTime_=0,rogueFrozenBattle_=0,rogueFrozenIdle_=0;bool rogueFreezeValid_=true,rogueResume_=false,rogueKillXP_=false;
    void UpdateRogueliteTest(){
        using namespace Game;using namespace Game::Chrono;
        auto& registry=GetRegistry();auto player=FindObjectByName("Player");auto& ink=registry.get<InkPlayer>(player);auto& p=registry.get<Player>(player);
        auto& state=ink.rogue;auto& input=registry.get<ControlFrame>(player);input={};input.pitch=.1f;p.invincible=1;
        if(frames_++==0){state.random=73192;state.Gain(20);}
        if(state.menu){
            if(rogueMenuFrames_++==0){rogueFrozenTime_=p.stats.seconds;rogueFrozenBattle_=ink.battleAge;rogueFrozenIdle_=state.idle;}
            rogueFreezeValid_&=p.stats.seconds==rogueFrozenTime_&&ink.battleAge==rogueFrozenBattle_;
            if(rogueMenuFrames_==20)Capture(L"tests/out/rogue-selection.png");
            if(rogueStage_==0){
                if(rogueMenuFrames_==30)input.upgradeReroll=true;
                if(rogueMenuFrames_==60){rogueChosen_=state.offers[0];rogueStartRank_=state.ranks[size_t(rogueChosen_)];input.upgradeChoice=0;}}
            else if(rogueStage_==1&&rogueMenuFrames_==30){p.mass=650;input.upgradeSkip=true;}
        }
        if(rogueStage_==2){state.pending=0;state.xp=0;}
        if(rogueStage_==2&&frames_%40==0){
            // Exercise the production volley with all five build families equipped.
            for(auto& rank:state.ranks)rank=1;
            DomainLoop loop;Vec pos{registry.get<TransformComponent>(player).translate.x,.16f,registry.get<TransformComponent>(player).translate.z};
            for(int i=0;i<=64;++i){float angle=float(i)*6.283185f/64;loop.points.push_back(pos+Vec{std::cos(angle)*35,0,std::sin(angle)*35});}loop.Measure();
            ink.domainPath.candidate=loop;input.attack=true;input.upgradeChoice=-1;
        }
        if(frames_==350)SetPaused(true);
        bool menuBefore=state.menu;Game::GameScene::Update();
        if(rogueStage_==2&&(state.xp>0||state.pending>0))rogueKillXP_=true;
        if(menuBefore&&!state.menu){
            if(rogueStage_==0){rogueFreezeValid_&=state.ranks[size_t(rogueChosen_)]==rogueStartRank_+1&&state.rerolls==2&&state.idle>rogueFrozenIdle_;
                rogueStage_=1;state.Gain(state.Required());rogueMenuFrames_=0;}
            else{rogueFreezeValid_&=p.mass==710;rogueStage_=2;}}
        if(rogueStage_==2&&p.stats.seconds>rogueFrozenTime_+.1f)rogueResume_=true;
        if(frames_==390)Capture(L"tests/out/rogue-builds.png");
        if(frames_>=420){
            bool pass=rogueFreezeValid_&&rogueResume_&&rogueKillXP_&&ink.domainVolleys>=3&&state.orbit>0&&state.ranks[5]>0&&state.resonance>0;
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" rogue earned_xp="<<rogueKillXP_<<" freeze="<<rogueFreezeValid_<<" resume="<<rogueResume_<<" level="<<state.level<<" rerolls="<<state.rerolls<<" volleys="<<ink.domainVolleys<<" kills="<<ink.swarmKills<<" orbit="<<state.orbit<<" resonance="<<state.resonance;
            PostQuitMessage(pass?0:2);
        }
    }
