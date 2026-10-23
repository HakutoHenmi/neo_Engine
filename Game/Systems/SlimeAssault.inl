// SlimeAssault.inl
// 輪を描いて誘導弾を撃つ戦闘の本番の処理 (ChronoSystem の一部)。
namespace Game {
// インク (輪を描く戦闘) のモードの毎フレームの更新。スライムの生活モードなら SlimeLife.inl の処理へ任せる
void ChronoSystem::UpdateInk(entt::registry& r,Player& p,GameContext& ctx){
    // スライムの生活モードなら専用の更新へ
    if(slimeLife_){UpdateSlimeLife(r,p,ctx);return;}
    // インクのプレイヤーと、確認用の入力
    auto& ink=r.get<InkPlayer>(player_);auto* control=r.try_get<ControlFrame>(player_);
    // 経過時間 (上限 0.05 秒)。確認用の入力があれば診断モード
    const float dt=std::min(ctx.dt,.05f);diagnostic_=control!=nullptr;
    // ムービー中・撃破の演出中はそちらだけを更新する
    if(UpdateCinematic(r,p,ctx))return;
    if(bossDefeat_){UpdateBossDefeat(r,p,ctx);return;}
#ifndef NDEBUG
    // デバッグビルド: F9 でボス撃破の演出へすぐ進み、見た目を確認できる
    // ボス戦にして撃破の演出を始める
    if(!control&&Down(VK_F9)&&!finished_){
        if(ink.battlePhase!=BattlePhase::Boss){ink.battlePhase=BattlePhase::Boss;ink.battleAge=SwarmEmergenceSeconds;SetSwarmBossVisible(r,true,0);PoseCreature(r);}
        StartBossDefeat(r,p,ctx);return;
    }
#endif
    // 強化メニュー中は戦闘を進めない
    if(UpdateRogueMenu(r,p,ctx))return;
    // 描ける点の数 (持続の強化で増える)
    ink.domainPath.pointLimit=DomainPath::MaxPoints+size_t(384*ink.rogue.ranks[3]);
    // 強化の時間・回避からの時間・完璧な回避の演出の時間を進める
    ink.rogue.TickGameplay(dt);
    ink.dodgeAge+=dt;
    if(ink.perfectAge>=0){ink.perfectAge+=dt;if(ink.perfectAge>1.2f)ink.perfectAge=-1;}
    // 入力 (テスト用の ControlFrame があればそれを使う)。F4 は蛇だけのモード、F6 は画面の揺れの強さを切り替える
    bool attack=control?control->attack:Controls::Fire();
    if(!control&&Down(VK_F4)&&!prevSnake_){snakeOnly_=!snakeOnly_;prevSnake_=true;Reset(r);return;}prevSnake_=!control&&Down(VK_F4);
    bool shake=!control&&Down(VK_F6);if(shake&&!prevShake_)shakeSetting_=(shakeSetting_+1)%3;prevShake_=shake;ctx.camera->SetShakeStrength(shakeSetting_*.5f);
    // カメラの向き (マウスとパッド)
    if(control){if(!ink.lockedOn){yaw_=control->yaw;pitch_=control->pitch;}}
    else if(ctx.input&&ink.rogue.returnDelay<=0){float padYaw,padPitch;Controls::Look(dt,padYaw,padPitch);
        yaw_+=ctx.input->GetMouseDeltaX()*.0025f+padYaw;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f+padPitch,-1.15f,1.2f);}
    // ロックオン: ボス戦では弱点、それ以外では一番近い敵へカメラを向ける
    bool lockPressed=control?control->lockOn:Controls::LockOn();
    if(lockPressed&&!ink.previousLock)ink.lockedOn=!ink.lockedOn;
    ink.previousLock=lockPressed;
    // ロックオン中: 注目する位置の方へカメラを回す
    if(ink.lockedOn){
        V from=Read(r.get<TransformComponent>(player_).translate)+V{0,.25f,0};
        V attention=Center(r,weakpoint_);
        // 群れの戦闘中は一番近い敵を見る (いなければ解除)
        if(ink.battlePhase!=BattlePhase::Boss){float nearest=1e9f;
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&DomainDistance(enemy.at,from)<nearest){nearest=DomainDistance(enemy.at,from);attention=enemy.at;}
            if(nearest==1e9f)ink.lockedOn=false;}
        // その位置を向くヨーとピッチ
        V direction=Unit(attention-from);
        float aimYaw=std::atan2(direction.x,direction.z),aimPitch=-std::asin(std::clamp(direction.y,-1.f,1.f));
        const auto& boss=r.get<CreatureBoss>(boss_);
        // ボスの急降下や衝撃の直後は下を向きすぎない
        if(ink.battlePhase==BattlePhase::Boss&&((boss.stage==CreatureStage::Dive&&boss.sweepPhase>=2)||boss.impactAge<.4f))
            aimPitch=std::max(aimPitch,-.32f);
        // なめらかに向きを合わせる
        yaw_+=std::remainder(aimYaw-yaw_,6.283185f)*(1-std::exp(-12*dt));
        pitch_+=(aimPitch-pitch_)*(1-std::exp(-12*dt));
    }
    // カーソルを隠す
    Engine::WindowDX::SetCursorVisible(false);
    // プレイヤーの時間・無敵・ヒットストップなどを進める
    p.stats.seconds+=dt;p.worldScale=p.aimScale=1;p.aiming=p.automatic=false;p.target=p.preview=p.bufferedTarget=entt::null;p.buffer=0;
    // 完璧な回避の直後は世界を遅くする
    if(ink.perfectAge>=0&&ink.perfectAge<.4f)p.worldScale=.15f;
    p.invincible=std::max(0.f,p.invincible-dt);p.damageAge+=dt;p.hitStop=std::max(0.f,p.hitStop-dt);ink.hitFlash=std::max(0.f,ink.hitFlash-dt);bossFlash_=std::max(0.f,bossFlash_-dt);
    ink.shotClock=std::max(0.f,ink.shotClock-dt);ink.phaseAge+=dt;if(ink.monoAge>=0)ink.monoAge+=dt;
    // 地面の高さ・跡に残した量・体の量の回復
    auto& t=r.get<TransformComponent>(player_);V pos=Read(t.translate);int surface=-1;float floor=InkGround(pos,&surface);
    // 跡に残した量の合計と、体の量の回復
    ink.deployed=0;for(const auto& trail:slimeTrails_)ink.deployed+=trail.mass;
    ink.Recover(p.mass,p.damageAge,dt);
    // 移動中は溜められる量を更新する
    if(ink.phase==SlimePhase::Roaming){float total=std::min(SlimeMaximumMass,p.mass+ink.deployed+ink.spent);ink.capacity=SlimeCapacity(total);ink.reserve=SlimeReserve(total);}
    // 移動・ジャンプ・回避 (カメラ基準の向きと速さ)
    bool jump=control?control->jump:Controls::Jump(),dodge=control?control->dodge:Controls::Dodge();
    // 回避: 押した瞬間で、待ち時間が無く、撃っていなければ
    bool dodged=dodge&&!ink.previousDodge&&p.cooldown<=0&&ink.phase!=SlimePhase::Firing&&ink.phase!=SlimePhase::Returning;
    // 移動の入力をカメラ基準の向きにする
    V input{};if(control)input=Unit(control->move);else Controls::Move(input.x,input.z);
    V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    // 地上を移動中はインクの中を泳ぐ
    ink.swimming=p.grounded&&ink.phase==SlimePhase::Roaming;
    // 移動の速さ (溜め中は遅い)
    float speed=InkMoveSpeed(ink.swimming,false)*ink.rogue.Move();
    if(ink.phase==SlimePhase::Charging)speed*=.28f;
    // ジャンプ (地上で押した瞬間)
    bool jumped=jump&&!prevSpace_&&p.grounded&&ink.phase!=SlimePhase::Firing;
    if(jumped)p.velocity.y=12;prevSpace_=jump;
    // 回避を始める (向き・時間・待ち時間・無敵)
    if(dodged){ink.dodgeAge=0;ink.perfectUsed=false;ink.dodgeOrigin=pos;p.action=Action::Dodge;p.timer=.18f;p.cooldown=.65f;p.invincible=std::max(p.invincible,SlimeDodgeInvincibility);p.dodgeDirection=Length(wish)>.1f?wish:Forward(yaw_,0);}ink.previousDodge=dodge;
    p.cooldown=std::max(0.f,p.cooldown-dt);
    bool isDodge=p.action==Action::Dodge;
    // 回避中は回避の向きへ速く動き、他の行動中は遅くなる
    if(isDodge){wish=p.dodgeDirection;speed=30;p.timer-=dt;if(p.timer<=0)p.action=Action::Free;}
    else if(p.action!=Action::Free){p.timer-=dt;if(p.timer<=0)p.action=Action::Free;speed*=.4f;}
    // 回避中は液体の見た目にする
    ink.dodgeLiquid=p.action==Action::Dodge;
    // 速さを目標へ近づけ、重力をかけて次の位置を求める (溶かせる物にはぶつかる)
    p.velocity.x+=(wish.x*speed-p.velocity.x)*(1-std::exp(-18*dt));p.velocity.z+=(wish.z*speed-p.velocity.z)*(1-std::exp(-18*dt));p.velocity.y-=30*dt;
    // 次の位置 (アリーナの範囲に収める)
    V next=pos+p.velocity*dt;next.x=std::clamp(next.x,-328.f,328.f);next.z=std::clamp(next.z,-338.f,378.f);
    // 溶かせる物にはぶつかる
    for(auto e:r.view<SlimeDissolvable>()){
        const auto& prop=r.get<SlimeDissolvable>(e);float f;V n;
        if(Chrono::Sweep(pos,next-pos,{prop.center-prop.half,prop.center+prop.half},{.6f,1.1f,.6f},f,n)){
            next=pos+(next-pos)*std::max(0.f,f-.001f);p.velocity=p.velocity-n*std::min(0.f,Dot(p.velocity,n));}
    }
    // 段差が高すぎれば水平に動かない。接地の判定と地面への吸着
    int nextSurface=-1;float nextFloor=InkGround(next,&nextSurface);
    if(nextFloor>pos.y+.1f){next.x=pos.x;next.z=pos.z;nextFloor=floor;}
    // 接地の判定
    bool wasGrounded=p.grounded;
    p.grounded=SlimeGrounded(pos.y,next.y,p.velocity.y,wasGrounded,jumped,floor,nextFloor,Length(V{next.x-pos.x,0,next.z-pos.z}));
    // 接地したら地面の上に合わせ、復帰位置を記録する
    if(p.grounded){next.y=nextFloor+1.25f;p.velocity.y=0;p.recoveryPoint=next;}
    // 地面の傾き (液体の見た目に使う)
    ink.groundHeight=nextFloor;ink.groundSlope={};
    if(nextSurface>=0){V n=inkSurfaces_[nextSurface].Normal();if(n.y>.5f)ink.groundSlope={-n.x/n.y,0,-n.z/n.y};}
    // 体の足元の範囲で地面の傾きを混ぜ、液体の見た目に使う
    float sx=(InkGround(next+V{2,0,0})-InkGround(next-V{2,0,0}))/4;
    float sz=(InkGround(next+V{0,0,2})-InkGround(next-V{0,0,2}))/4;
    if(std::abs(sx)<.65f)ink.groundSlope.x=sx;
    if(std::abs(sz)<.65f)ink.groundSlope.z=sz;
    // 固定の円ではなく、GPU で最後に完成した体の形を読む。読み戻しの短い遅れの間も
    // ずれはコントローラーと一緒に動くので、変形は保たれる
    // GPU で最後に完成した体の形の大きさを使う
    const auto& body=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();
    if(!body.offsets.empty()){ink.footprintSerial=body.serial;ink.footprintRadius=2.5f;}
    // 地上だけを一定の間隔で記録する (回避の滑りも含む)。空中に出ると線が途切れる
    if(wasGrounded&&p.grounded){ink.domainPath.Append({next.x,nextFloor+.16f,next.z});}
    else if(!p.grounded)ink.domainPath.BreakTrail();
    // 描いている線と候補の輪の液体を進める
    trailLiquid_.Sync(ink.domainPath.points);trailLiquid_.Step(dt);
    candidateLiquid_.Sync(ink.domainPath.candidate.points);candidateLiquid_.Step(dt);
    // 描いている輪で捕獲できる敵の数を 0.1 秒ごとに数える
    captureAge_+=dt;
    if((captureScanClock_-=dt)<=0){
        captureScanClock_=.1f;candidateCapture_=0;
        if(ink.domainPath.candidate.Ready())
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.at.y-InkGround(enemy.at)<SwarmCaptureHeight&&DomainContains(ink.domainPath.candidate,enemy.at))++candidateCapture_;
    }
    // 攻撃: 閉じた輪があれば、その輪を領域として発動する (同時に 6 つまで)
    if(attack&&!ink.previousAttack&&ink.domainPath.candidate.Ready()&&homingDomains_.size()<6){
        // 候補の輪から発動する輪を作る (塗り・威力・弾数・強化・届く範囲・ボスを囲んだか)
        HomingDomain domain;domain.loop=ink.domainPath.candidate;
        domain.liquid=candidateLiquid_;
        domain.highlight=DomainHighlight(domain.loop);
        domain.bulletPower=DomainBulletPower(domain.loop.Enclosures());
        domain.total=DomainAmmo(domain.loop);domain.remaining=domain.loop.shape==DomainShape::Triangle?3:domain.total;
        ActivateRogue(r,p,ctx,domain);
        domain.reach=DomainReach(domain.loop);
        domain.enclosesBoss=DomainContains(domain.loop,Center(r,boss_));
        // 捕獲: 閉じた輪の中の地面近くにいる敵はすぐに溶ける。
        // 飛ぶ敵はプレイヤーへ急降下するときだけ届くので、攻撃を誘って
        // その周りを大きな輪で囲むのが一番よい
        // 輪の中の地面近くの敵をすぐ倒す (捕獲)
        int captured=0;
        for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.at.y-InkGround(enemy.at)<SwarmCaptureHeight&&DomainContains(domain.loop,enemy.at)){HitSwarm(r,p,ctx,enemy.id,1e6f);++captured;}
        // 捕獲したら数を表示して画面を揺らす
        if(captured>0){
            captureShown_=captured;captureAge_=0;candidateCapture_=0;
            ctx.camera->StartImpactShake(.16f+.004f*float(std::min(captured,60)),std::min(.32f,.05f+.006f*float(captured)),{0,1,0},14);
        }
        // HUD 用に最後の輪の情報を記録する
        ink.domainLastCount=domain.total;ink.domainLastArea=domain.loop.area;ink.domainLastLength=domain.loop.length;
        ink.domainLastEnclosures=domain.loop.Enclosures();
        ink.domainLastShape=domain.loop.shape;++ink.domainSkillUses[size_t(domain.loop.shape)];
        // 輪を加え、描いている線を消して今の位置から描き直す
        homingDomains_.push_back(std::move(domain));++ink.domainVolleys;++ink.shots;
        ink.domainPath.Clear();ink.domainPath.Append({next.x,nextFloor+.16f,next.z});
        // 発動の揺れと音
        ctx.camera->StartImpactShake(.10f,.08f,{0,1,0},12);
        auto* audio=Engine::Audio::GetInstance();audio->Play(slimePressureSound_,false,.25f*audio->GetMasterSEVolume(),1.1f);
    }
    // 前フレームの攻撃の入力を記録する
    ink.previousAttack=attack;
    // 減衰する体積を保った姿勢でメッシュの拡大ではなく力を動かす。着地で
    // ばねを揺らし、空中では支えていた平面を少しずつ外す
    // 着地したら縦のつぶれの揺れを起こす
    if(!wasGrounded&&p.grounded)ink.fluidAspectVelocity=-7.f;
    // 空中では縦長、地上では元の比率へばねで戻す
    float targetAspect=p.grounded?1.f:std::clamp(1.15f+p.velocity.y*.025f,.9f,1.5f);
    ink.fluidAspectVelocity+=(70.f*(targetAspect-ink.fluidAspect)-7.f*ink.fluidAspectVelocity)*dt;
    ink.fluidAspect=std::clamp(ink.fluidAspect+ink.fluidAspectVelocity*dt,.55f,1.6f);
    // 空中の割合
    ink.airBlend+=((p.grounded?0.f:1.f)-ink.airBlend)*(1-std::exp(-14*dt));
    // 移動の向きと勢いを液体の見た目へ反映する
    V planar{p.velocity.x,0,p.velocity.z};float travelSpeed=Length(planar);
    if(travelSpeed>.5f){
        float heading=std::atan2(ink.fluidDirection.x,ink.fluidDirection.z);
        heading+=std::remainder(std::atan2(planar.x,planar.z)-heading,6.283185f)*(1-std::exp(-12*dt));
        ink.fluidDirection={std::sin(heading),0,std::cos(heading)};
    }
    ink.fluidMotion+=(std::min(1.f,travelSpeed/18.f)-ink.fluidMotion)*(1-std::exp(-7*dt));
    // 位置・体の大きさ (量の立方根)・溜め中の膨らみを反映する
    t.translate=Write(next);float bodyScale=std::cbrt(std::max(1.f,p.mass)/SlimeMaximumMass);
    float pressure=ink.phase==SlimePhase::Charging?1+.15f*(ink.Power(ink.charge)):1;
    float pulse=ink.phase==SlimePhase::Charging?1+.025f*std::sin(ink.phaseAge*(25+ink.tier*12)):1;
    t.scale={bodyScale*pressure*pulse,bodyScale*pressure/pulse,bodyScale*pressure*pulse};t.rotate.y=ink.phase==SlimePhase::Firing?ink.beamYaw:yaw_;
    // カメラ・群れ・ボス・領域・強化スキルの更新
    InkCamera(r,p,ctx);
    // 群れを更新する
    UpdateSwarm(r,p,ctx);
    // ボスの出現が始まったらムービーへ
    if(!control&&ink.battlePhase==BattlePhase::Emerging){UpdateCinematic(r,p,ctx);return;}
    // ボス戦ならボスを更新する
    if(p.hitStop<=0&&ink.battlePhase==BattlePhase::Boss)UpdateInkBoss(r,p,ctx);
    // 終わったら演出だけ
    if(finished_){ink.monoAge=-1;ink.phase=SlimePhase::Roaming;Presentation(r,p,ctx);return;}
    // 輪と強化のスキルを更新する
    UpdateDomains(r,p,ctx);
    UpdateRogueSkills(r,p,ctx);
    // 発動とダメージで線が切れることがあるので、液体の更新の後で同期し直す
    trailLiquid_.Sync(ink.domainPath.points);candidateLiquid_.Sync(ink.domainPath.candidate.points);
    // 跡に残した量を数え直す
    ink.deployed=0;for(const auto& trail:slimeTrails_)ink.deployed+=trail.mass;ink.tank=ink.deployed;
    // インクのテクスチャを 0.1 秒ごとにアップロードする (必要なら塗りを作り直す)
    inkUploadClock_+=dt;if(inkDirty_&&inkUploadClock_>=.1f){
        if(inkNeedsRebuild_){
            // 必要なら跡から塗りを作り直す
            for(auto& s:inkSurfaces_)s.mask.fill(0);
            for(const auto& trail:slimeTrails_)inkSurfaces_[trail.surface].Stamp(trail.at,trail.radius,&trail.footprint,std::sqrt(trail.mass/std::max(trail.originalMass,.0001f)));
            inkNeedsRebuild_=false;
        }
        UploadInk();inkUploadClock_=0;
    }
    // インクの上にいるか・体力の反映・演出
    ink.onInk=surface>=0&&p.grounded&&inkSurfaces_[surface].Painted({next.x,nextFloor,next.z});
    // 体力の表示を体の量に合わせる
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(p.mass);hp.SetMaxHp(SlimeMaximumMass);hp.SetDead(p.mass<=0);
    // 演出を更新する
    Presentation(r,p,ctx);
    // 完璧な回避や被弾の直後は画面を白黒にする
    if(!finished_&&ink.perfectAge>=0&&ink.perfectAge<.4f){auto params=ctx.renderer->GetPostProcessParams();
        params.san=1;params.vignette=.3f;params.chromaShift=SlimePerfectPulse(ink.perfectAge);
        ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("Grayscale");}
    if(!finished_&&ink.monoAge>=0&&ink.monoAge<.2f){auto params=ctx.renderer->GetPostProcessParams();params.san=SlimeMono(ink.monoAge);params.vignette=.35f;
        ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("Grayscale");}
}

// インクのモードの UI: ムービー・撃破・強化メニュー・結果、それ以外は戦闘の HUD
void ChronoSystem::DrawInkUI(entt::registry& r,GameContext& ctx){
    // スライムの生活モードなら専用の UI へ
    if(slimeLife_){DrawSlimeLifeUI(r,ctx);return;}
    // プレイヤーとインクの状態
    const auto& p=r.get<Player>(player_);const auto& ink=r.get<InkPlayer>(player_);
    // UI の準備
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    // ムービー中・撃破の演出中・強化メニュー中はそれぞれの画面
    if(cinematicActive_){DrawCinematic(ctx);return;}
    if(bossDefeat_&&!finished_){DrawBossDefeat(ctx);return;}
    if(ink.rogue.menu){UI::Canvas selection(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y,ink.rogue.MenuEase());
        if(ctx.useOverrideMouse)selection.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
        UI::RogueSelection(selection,ink.rogue);return;}
    // 終了後は結果を表示する
    if(finished_){
        ui.Result(won_,"TIME  "+TimeText(p.stats.seconds),
            "KOs  "+std::to_string(ink.swarmKills)+"  |  BEST CHAIN  "+std::to_string(ink.swarmBestCombo)+"  |  CORES  "+std::to_string(p.stats.counters)+" / 3");
        return;
    }
    // ボスの攻撃の警告と現在の目標の文
    const bool open=r.get<Target>(weakpoint_).active;
    bool bossBattle=ink.battlePhase==BattlePhase::Boss;
    const auto& creature=r.get<CreatureBoss>(boss_);
    std::string warning=creature.stage==CreatureStage::Dive&&creature.sweepPhase<=2?"DIVE INCOMING / LEAVE THE MARK":
        creature.stage==CreatureStage::Dive&&creature.sweepPhase==3?"RADIAL FEATHERS / FIND A GAP":
        creature.stage==CreatureStage::Snake&&creature.attack==CreatureAttack::HeadTail&&creature.sweepPhase==1?"HEAD STRIKE / DODGE SIDEWAYS":
        creature.stage==CreatureStage::Snake&&creature.attack==CreatureAttack::HeadTail&&(creature.sweepPhase==5||creature.sweepPhase==6)?"TAIL SWEEP / JUMP OR BACKSTEP":"DRAW LOOPS / CHAIN BEFORE FIRING";
    std::string objective=ink.battlePhase==BattlePhase::Horde?"CLEAR THE SWARM / CHAIN YOUR LOOPS":ink.battlePhase==BattlePhase::Emerging?"SERPENT AWAKENING / KEEP MOVING":ink.downTimer>0?"ARMOR MELTED / CORE EXPOSED":open?"CORE OPEN / CLOSE YOUR LOOP":warning;
    // 任務の表示: 半透明のガラス板と黄緑の縁。チェインの数はその下に出す
    // 左上の任務の表示: 段階の名前・撃破数か反撃数・目標・進み具合のバー
    ui.Rounded({22,18,428,146},{.025f,.045f,.035f,.62f},22);
    ui.RoundedRing({22,18,428,146},UI::WithAlpha(UI::Paper,.08f),22);
    ui.Rounded({22,34,5,114},bossBattle?UI::Gold:UI::Lime,3);
    ui.Label(bossBattle?"SERPENT HUNT":"SLIME SWARM",42,28,13,UI::Lime,3.2f);
    const std::string count=bossBattle?std::to_string(p.stats.counters):std::to_string(ink.swarmKills);
    ui.Bold(count,40,40,50,UI::Paper);
    ui.Text(bossBattle?" / 3 CORES":" / "+std::to_string(SwarmBossKills)+" KO",44+ui.Width(count,50),66,19,UI::Muted);
    const auto objectiveColor=open?UI::Gold:UI::Paper;
    ui.Rounded({42,108,4,18},open?UI::Gold:UI::Lime,2);ui.Bold(objective,54,106,17,objectiveColor);
    ui.Bar({42,134,380,8},bossBattle?ink.coreHealth/100:ink.battlePhase==BattlePhase::Horde?float(ink.swarmKills)/SwarmBossKills:ink.battleAge/SwarmEmergenceSeconds,bossBattle?UI::Gold:UI::Lime);
    // ボス戦は装甲の削れ具合、それ以外はチェインの数
    if(bossBattle)ui.Label("ARMOR DISSOLVE  "+std::to_string(int(ink.armor))+"%",44,152,13,UI::Gold,2.4f);
    else if(ink.swarmCombo>=2&&ink.swarmComboAge>0){
        const bool hot=ink.swarmCombo>=20;const std::string chain=std::to_string(ink.swarmCombo);
        if(hot)ui.Brush({30,146,ui.Width(chain,34)+150,52},UI::WithAlpha(UI::Gold,.9f),-.03f);
        ui.Bold(chain,44,150,34,hot?UI::Ink:UI::Lime);ui.Label("CHAIN",50+ui.Width(chain,34),166,13,hot?UI::Ink:UI::Lime,2.6f);
    }
    // 画面中央の帯の見出しを描く
    auto banner=[&](const std::string& text,float y,float size,Engine::Vector4 color,float alpha){
        UI::Canvas b=ui;b.MultiplyAlpha(alpha);const float w=b.Width(text,size);
        b.Brush({640-w*.5f-56,y-size*.28f,w+112,size*1.55f},UI::WithAlpha(color,.94f),-.025f);
        b.Center(text,640,y,size,UI::Ink);
    };
    // ボスの出現と完璧な回避の見出し
    if(ink.battlePhase==BattlePhase::Emerging)banner("THE SERPENT RISES",40,30,UI::Gold,1);
    if(ink.perfectAge>=0)banner("PERFECT DODGE",462,28,UI::Gold,std::clamp(1.2f-ink.perfectAge,0.f,1.f));
    // 体と成長: 左下の影の中に 2 つの輪を描く
    // 左下: 体の量の輪と、レベルの輪
    ui.Shade({18,520,270,150},.6f);
    const auto bodyColor=p.mass<=SlimeMaximumMass*.25f?UI::Gold:UI::Lime;
    ui.Ring(88,598,44,p.mass/SlimeMaximumMass,bodyColor,7);
    ui.Center(std::to_string(int(p.mass)),88,574,32,bodyColor);
    ui.LabelCenter("BODY",88,611,11,UI::Muted,2.4f);
    ui.Ring(196,604,28,float(ink.rogue.xp)/float(ink.rogue.Required()),UI::Teal,4);
    ui.Center(std::to_string(ink.rogue.level),196,588,24);
    ui.LabelCenter("LV",196,616,10,UI::Muted,2);
    // 輪の候補があるか
    bool ready=ink.domainPath.candidate.Ready();
    // 輪の案内: 輪で何が起きるかを伝える光る札
    // 画面下の案内の札
    auto pill=[&](const std::string& text,Engine::Vector4 color,const char* padFire){
        const float textW=ui.SpacedWidth(text,17,1.6f)+(padFire?34.f:0.f),w=textW+56,x=640-w*.5f;
        ui.Halo(640,628,w*.62f,UI::WithAlpha(color,.14f));
        ui.Rounded({x,608,w,40},{.05f,.085f,.07f,.92f},20);ui.RoundedRing({x,608,w,40},UI::WithAlpha(color,.8f),20);
        float tx=ui.Spaced(text,x+28,618,17,1.6f,color);
        if(padFire)ui.PadGlyph(padFire,tx+6,613,28);
    };
    // 体が少ないときは回復を案内する
    if(ink.regenerating||p.mass<=SlimeMaximumMass*.15f)
        pill(ink.regenerating?"REGENERATING  /  KEEP EVADING":"LOW BODY  /  EVADE TO REGENERATE",UI::Gold,nullptr);
    // 候補の輪があれば、捕獲の数と弾数を案内する
    else if(ready){
        const int shots=DomainAmmo(ink.domainPath.candidate)+12*ink.rogue.ranks[0];
        const auto infoColor=candidateCapture_>=10?UI::Gold:UI::Lime;
        if(homingDomains_.size()>=6)pill("DOMAINS BUSY  /  KEEP EVADING",UI::Muted,nullptr);
        else{
            const std::string info="CAPTURE "+std::to_string(candidateCapture_)+"   "+std::to_string(shots)+" SHOTS   ";
            if(UI::UsingPad())pill(info+"FIRE",infoColor,"xbox_rt");
            else pill(info+"CLICK TO FIRE",infoColor,nullptr);
        }
    }
    // 捕獲した数を大きく表示する
    if(captureShown_>0&&captureAge_<1.4f){
        // 捕獲が多いほど大きく表示する
        const float pop=1+.3f*std::exp(-captureAge_*10),fade=std::clamp((1.4f-captureAge_)/.45f,0.f,1.f);
        const float size=(30+.5f*float(std::min(captureShown_,60)))*pop;
        banner("CAPTURE  x"+std::to_string(captureShown_),170-size*.5f,size,captureShown_>=20?UI::Gold:UI::Lime,fade);
    }
    // 画面中央の照準
    ui.Dot(640,360,2.5f,ink.hitFlash>0?UI::Gold:UI::Paper);
    ui.Line(626,360,633,360,2,UI::WithAlpha(UI::Lime,.85f));ui.Line(647,360,654,360,2,UI::WithAlpha(UI::Lime,.85f));
    ui.Line(640,346,640,353,2,UI::WithAlpha(UI::Lime,.85f));ui.Line(640,367,640,374,2,UI::WithAlpha(UI::Lime,.85f));
    // 操作の案内は帯ではなく柔らかいグラデーションの上に出す
    // 画面下の操作の案内
    ui.ImageRot(UI::Art::Fade,{-200,640,1680,100},3.14159265f,UI::WithAlpha(UI::Night,.72f));
    {UI::Canvas hints=ui;hints.MultiplyAlpha(.88f);
    hints.Prompt("keyboard_w","MOVE + DRAW","xbox_stick_l","MOVE + DRAW",40,682,24,16);
    hints.Prompt("mouse_right","DODGE","xbox_button_b","DODGE",230,682,24,16);
    hints.Prompt("mouse_left","FIRE LOOPS","xbox_rt","FIRE LOOPS",360,682,24,16);
    hints.Prompt("keyboard_space","JUMP","xbox_button_a","JUMP",530,682,24,16);
    hints.Prompt("mouse_scroll",ink.lockedOn?"LOCK OFF":"LOCK ON","xbox_stick_r_press",ink.lockedOn?"LOCK OFF":"LOCK ON",640,682,24,16);
    hints.Prompt("keyboard_escape","PAUSE","xbox_button_menu","PAUSE",1150,682,24,16);}
    // 北が上の地図: アリーナ全体・描いている線・発動中の輪
    // 右上の地図: アリーナ・格子・輪・線・敵・ボス・プレイヤーの矢印
    ui.Rounded({1074,18,186,196},{.04f,.07f,.055f,.9f},20);ui.RoundedRing({1074,18,186,196},UI::WithAlpha(UI::Paper,.12f),20);
    ui.Label("N",1240,26,11,UI::Lime,0);
    const float mapX=1101,mapY=44,mapScale=.2f;
    // ワールドの位置を地図の座標にする関数
    auto mapPoint=[&](V at){return V{mapX+(std::clamp(at.x,-330.f,330.f)+330)*mapScale,mapY+(380-std::clamp(at.z,-340.f,380.f))*mapScale,0};};
    ui.Fill({mapX,mapY,660*mapScale,720*mapScale},{.025f,.065f,.045f,1});
    for(int i=1;i<4;++i){ui.Line(mapX+i*165*mapScale,mapY,mapX+i*165*mapScale,mapY+720*mapScale,1,{.1f,.18f,.13f,1});
        ui.Line(mapX,mapY+i*180*mapScale,mapX+660*mapScale,mapY+i*180*mapScale,1,{.1f,.18f,.13f,1});}
    // 点の列を地図の線で描く関数
    auto mapLine=[&](const std::vector<V>& points,Engine::Vector4 color,float width){
        if(points.empty())return;V from=mapPoint(points.front());
        for(size_t i=1;i<points.size();++i){V to=mapPoint(points[i]);if(Length(to-from)<1.2f&&i+1<points.size())continue;
            ui.Line(from.x,from.y,to.x,to.y,width,color);from=to;}
    };
    // 発動中の輪・描いている線・候補の輪
    for(const auto& domain:homingDomains_)mapLine(domain.loop.points,{.22f,.46f,.27f,1},1.5f);
    mapLine(ink.domainPath.points,UI::Lime,1.5f);
    if(ready)mapLine(ink.domainPath.candidate.points,UI::Gold,2.3f);
    // 敵の点 (自爆の敵は金色)
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){V point=mapPoint(enemy.at);ui.Fill({point.x-1,point.y-1,2,2},enemy.bomber?UI::Gold:Engine::Vector4{.85f,.31f,.22f,1});}
    // ボスの点
    if(ink.battlePhase!=BattlePhase::Horde){V enemy=mapPoint(Center(r,boss_));ui.Fill({enemy.x-4,enemy.y-4,8,8},{1,.25f,.18f,1});}
    // プレイヤーの向きの三角
    V marker=mapPoint(Read(r.get<TransformComponent>(player_).translate));
    V direction{std::sin(yaw_),-std::cos(yaw_),0},right{-direction.y,direction.x,0};
    V tip=marker+direction*6,left=marker-direction*4+right*4,rgt=marker-direction*4-right*4;
    ui.Line(tip.x,tip.y,left.x,left.y,2,UI::Paper);ui.Line(left.x,left.y,rgt.x,rgt.y,2,UI::Paper);ui.Line(rgt.x,rgt.y,tip.x,tip.y,2,UI::Paper);
    // 弱点が出ているかロックオン中なら、目標の位置に印を付ける
    if(open||ink.lockedOn){
        // 注目する位置 (ボス戦以外は一番近い敵)
        V target=Center(r,weakpoint_);
        if(ink.battlePhase!=BattlePhase::Boss){V from=Read(r.get<TransformComponent>(player_).translate);float nearest=1e9f;
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&DomainDistance(enemy.at,from)<nearest){nearest=DomainDistance(enemy.at,from);target=enemy.at;}}
        // 画面の座標に変換して、LOCKED か CORE の文字と枠を描く
        using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(target.x,target.y,target.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=std::clamp((clip.x/clip.w*.5f+.5f)*1280,40.f,1240.f),y=std::clamp((-clip.y/clip.w*.5f+.5f)*720,180.f,490.f);
            ui.Bold(ink.lockedOn?"LOCKED":"CORE",x-ui.Width(ink.lockedOn?"LOCKED":"CORE",22)*.5f,y-32,22,UI::Gold);
            if(ink.lockedOn){ui.Fill({x-18,y-18,8,2},UI::Gold);ui.Fill({x-18,y-18,2,10},UI::Gold);
                ui.Fill({x+10,y+16,8,2},UI::Gold);ui.Fill({x+16,y+8,2,10},UI::Gold);}}
    }
}

// 群れ・誘導弾・ローグライト・ボス撃破の処理を読み込む
#include "HordeCombat.inl"
#include "HomingDomains.inl"
#include "RogueliteCombat.inl"
#include "BossDefeat.inl"
}
