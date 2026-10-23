
    // ローグライト (スライムの強化) のテストの状態: メニューのフレーム数・段階・選んだカードとランクなど
    int rogueMenuFrames_=0,rogueStage_=0,rogueStartRank_=0,rogueChosen_=-1,rogueCombatFrames_=0;
    bool rogueRerolled_=false;
    // メニュー中に止まっているかを確かめるための値と、各確認の結果
    float rogueFrozenTime_=0,rogueFrozenBattle_=0,rogueFrozenIdle_=0;bool rogueFreezeValid_=true,rogueResume_=false,rogueKillXP_=false;
    // 毎フレーム呼ぶ: 強化の選択・引き直し・スキップ・戦闘を順に試す
    void UpdateRogueliteTest(){
        using namespace Game;using namespace Game::Chrono;
        // プレイヤーと、その状態と入力
        auto& registry=GetComponents();auto player=FindObjectByName("Player");auto& ink=registry.get<InkPlayer>(player);auto& p=registry.get<Player>(player);
        auto& state=ink.rogue;auto& input=registry.get<ControlFrame>(player);input={};input.pitch=.1f;p.invincible=1;
        // 経験値を明示的に与えてから始める (出現した敵から入る経験値に頼らない)
        if(frames_++==0){state.random=73192;state.Gain(state.Required());}
        // メニューが開いているとき
        if(state.menu){
            // 最初に、止まっているはずの時刻を覚える
            if(rogueMenuFrames_++==0){rogueFrozenTime_=p.stats.seconds;rogueFrozenBattle_=ink.battleAge;rogueFrozenIdle_=state.idle;}
            // メニュー中は時間が進まないこと
            rogueFreezeValid_&=p.stats.seconds==rogueFrozenTime_&&ink.battleAge==rogueFrozenBattle_;
            // 20 フレーム目の画面を保存する
            if(rogueMenuFrames_==20)Capture(L"tests/out/rogue-selection.png");
            // 段階 0: 引き直してから最初のカードを選ぶ
            if(rogueStage_==0){
                if(state.CanChoose()&&!rogueRerolled_){input.upgradeReroll=true;rogueRerolled_=true;}
                else if(state.CanChoose()&&rogueRerolled_){rogueChosen_=state.offers[0];rogueStartRank_=state.ranks[size_t(rogueChosen_)];input.upgradeChoice=0;}}
            // 段階 1: 質量を 650 にしてスキップする (+60)
            else if(rogueStage_==1&&state.CanChoose()){p.mass=650;input.upgradeSkip=true;}
        }
        // 段階 2: 戦闘中は強化を出さない
        if(rogueStage_==2){state.pending=0;state.xp=0;}
        if(rogueStage_==2)++rogueCombatFrames_;
        // 40 フレームごとに、全種類の強化を付けて輪の攻撃を撃つ
        if(rogueStage_==2&&rogueCombatFrames_%40==0){
            // 5 系統すべてを付けた状態で、実際の連射を試す
            for(auto& rank:state.ranks)rank=1;
            // プレイヤーの周りに半径 35 の輪を作る
            DomainLoop loop;Vec pos{registry.get<TransformComponent>(player).translate.x,.16f,registry.get<TransformComponent>(player).translate.z};
            for(int i=0;i<=64;++i){float angle=float(i)*6.283185f/64;loop.points.push_back(pos+Vec{std::cos(angle)*35,0,std::sin(angle)*35});}loop.Measure();
            ink.domainPath.candidate=loop;input.attack=true;input.upgradeChoice=-1;
        }
        // 150 フレーム目でポーズする
        if(rogueCombatFrames_==150)SetPaused(true);
        // 更新の前にメニューが開いていたか
        bool menuBefore=state.menu;Game::GameScene::Update();
        // 倒して経験値が入ったか
        if(rogueStage_==2&&(state.xp>0||state.pending>0))rogueKillXP_=true;
        // メニューが閉じたら結果を確かめて次の段階へ
        if(menuBefore&&!state.menu){
            if(rogueStage_==0){rogueFreezeValid_&=state.ranks[size_t(rogueChosen_)]==rogueStartRank_+1&&state.rerolls==2&&state.idle>rogueFrozenIdle_;
                rogueStage_=1;state.Gain(state.Required());rogueMenuFrames_=0;}
            else{rogueFreezeValid_&=p.mass==710;rogueStage_=2;}}
        // メニューを閉じた後に時間が進むこと
        if(rogueStage_==2&&p.stats.seconds>rogueFrozenTime_+.1f)rogueResume_=true;
        // 190 フレーム目の画面を保存する
        if(rogueCombatFrames_==190)Capture(L"tests/out/rogue-builds.png");
        // テスト開始からの経過時間
        float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start_).count();
        // 220 フレームか 60 秒で終わり、結果を書き出す
        if(rogueCombatFrames_>=220||elapsed>60){
            bool pass=rogueFreezeValid_&&rogueResume_&&rogueKillXP_&&ink.domainVolleys>=3&&state.orbit>0&&state.ranks[5]>0&&state.resonance>0;
            // DLSS と RT のテストも指定されていれば、その結果も確かめる
            auto* renderer=Engine::Renderer::GetInstance();bool dlssRequested=wcsstr(GetCommandLineW(),L"--dlss-smoke")!=nullptr;
            bool rtRequested=wcsstr(GetCommandLineW(),L"--rt-smoke")!=nullptr;
            if(rtRequested)pass&=renderer->RtShadowFrames()>30&&renderer->RtShadowPixels()>0&&renderer->RtLitPixels()>0;
            if(dlssRequested)pass&=renderer->DlssEvaluatedFrames()>30;
            // 結果のファイル
            std::ofstream("tests/out/ink-smoke.txt")<<(pass?"PASS":"FAIL")<<" rogue earned_xp="<<rogueKillXP_<<" freeze="<<rogueFreezeValid_<<" resume="<<rogueResume_<<" level="<<state.level<<" rerolls="<<state.rerolls<<" volleys="<<ink.domainVolleys<<" kills="<<ink.swarmKills<<" orbit="<<state.orbit<<" resonance="<<state.resonance;
            if(dlssRequested)std::ofstream("tests/out/ink-smoke.txt",std::ios::app)<<" dlss_frames="<<renderer->DlssEvaluatedFrames()<<" status="<<renderer->DlssStatus();
            if(rtRequested)std::ofstream("tests/out/ink-smoke.txt",std::ios::app)<<" rt_frames="<<renderer->RtShadowFrames()<<" shadow_pixels="<<renderer->RtShadowPixels()<<" lit_pixels="<<renderer->RtLitPixels();
            // 成功 0 / 失敗 2 で終了する
            PostQuitMessage(pass?0:2);
        }
    }
