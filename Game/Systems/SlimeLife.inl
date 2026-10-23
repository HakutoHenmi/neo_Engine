// SlimeLife.inl
// スライムの生活モード (現在のゲームの本編。ChronoSystem の一部)。
// 吸収で成長し、武器 (ハンマー・剣・飛行機・タイヤ・消火栓) に変形して街を進む。調合室 (研究所) の画面もここにある。
namespace Game {
// このファイルの中だけで使う補助の関数
namespace {
// スライムの色を描画用の色にする / 数値を小数 1 桁の文字列にする
Engine::Vector4 LifeColor(const SlimeColor& color){auto rgb=color.ToRGB();return {rgb.x,rgb.y,rgb.z,1};}
std::string LifeNumber(float value){char text[32];snprintf(text,sizeof(text),"%.1f",value);return text;}
// 武器の形のメッシュ (1: ハンマー, 2: 剣, それ以外: 飛行機)
const Engine::FluidShape::Mesh& LifeWeaponMesh(int shape){
    // 武器のメッシュは一度だけ読み込んで使い回す
    static const Engine::FluidShape::Mesh hammer("Resources/Models/SlimeLife/hammer.obj"),sword("Resources/Models/SlimeLife/sword.obj"),plane("Resources/Models/SlimeLife/plane.obj");
    return shape==1?hammer:shape==2?sword:plane;
}
// 形ごとの粒子の配置 (0: 球、4: タイヤ、5: 消火栓、それ以外は武器のメッシュ) を作って使い回す
const std::vector<Engine::FluidShape::Point>& LifeCloud(int shape,uint32_t count){
    using namespace Engine::FluidShape;
    // 形と粒子数ごとの点の配置のキャッシュ
    static std::map<std::pair<int,uint32_t>,std::vector<Point>> cache;
    auto key=std::make_pair(shape,count);auto found=cache.find(key);if(found!=cache.end())return found->second;
    // 0: 楕円体の中に置く
    if(shape==0)return cache.emplace(key,Sample(nullptr,count)).first->second;
    // 4: タイヤの形の中に Halton 列で点を置き、Morton 順に並べる
    if(shape==4){
        std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*LifeTireTube,Halton(i,3)*LifeTireRadius*2,(Halton(i,5)*2-1)*LifeTireRadius};
            if(LifeInsideTire({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});
        return cache.emplace(key,std::move(points)).first->second;
    }
    // 5: 消火栓の形の中に点を置く
    if(shape==5){std::vector<Point> points;points.reserve(count);
        for(uint32_t i=1;points.size()<count;++i){Point p{(Halton(i,2)*2-1)*2.2f,Halton(i,3)*5.1f,(Halton(i,5)*2-1)*2.2f};
            if(LifeInsideHydrant({p.x,p.y,p.z}))points.push_back(p);}
        std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return cache.emplace(key,std::move(points)).first->second;}
    // それ以外: 武器のメッシュの中に点を置く
    return cache.emplace(key,Sample(&LifeWeaponMesh(shape),count)).first->second;
}
void LifeInterruptTopology(SlimeLife& life){
    // 持ち主が変わった後は正しい武器を作り直す。小さすぎる武器と、
    // 途中で止めた取り込みだけが液体に戻る。確定した成長は残す
    // 武器の形の体だけを調べる
    for(auto& b:life.bodies)if(b.mass>0&&b.action!=LifeAction::Idle){
        if(LifeCanMorph(b,b.action))LifeRetargetWeapon(b);
        else LifeReturnToSlime(b);
    }
}
// 現在の行動 (吸収と回復) にかかる時間
float LifeDuration(const LifeBody& body,const SlimeRecipe& recipe){return body.action==LifeAction::Engulf?recipe.AbsorbDuration():body.restoreDuration;}
}
// カメラが映す対象 (中心と半径)。液体のときは体の大きさ、武器のときは攻撃の動き全体を包む範囲
LifeCameraSubject ChronoSystem::GetLifeCameraSubject()const{
    // 本体の大きさの倍率
    const auto& body=life_.bodies[0];float scale=LifeScale(body.mass);
    if(!LifeWeaponForm(body.action)){
        // 液体のときは、動きで伸びた分も含めた体の半径
        auto radii=LifeRadii(body,life_.recipe);
        float radius=std::max({radii.x*(1+.55f*body.motion),radii.y,radii.z*(1+.55f*body.motion)})*1.25f+.8f*scale;
        return {body.at+body.fluidOffset+V{0,-1.25f+radii.y*.5f+.8f*body.air,0},radius};
    }
    // 攻撃の動き全体の範囲を使い、振りやカメラの回り込みでもズームを安定させる
    // タイヤと消火栓は形が決まっているので、その大きさを使う
    if(body.action==LifeAction::Tire)return {body.at+V{0,-1.25f+LifeTireRadius*scale,0},(LifeTireRadius+.9f)*scale};
    if(body.action==LifeAction::Hydrant)return {body.at+V{0,-1.25f+2.5f*scale,0},4.2f*scale};
    // 元の頂点はシミュレーションの点群と同じ正規化を使う
    // ハンマー・剣・飛行機それぞれの攻撃の全フレームの頂点から、包む球を一度だけ求める
    static const auto envelopes=[](){std::array<LifeCameraSubject,3> result{};
        for(int shape=1;shape<=3;++shape){const auto& mesh=LifeWeaponMesh(shape);auto form=LifeAction(int(LifeAction::Hammer)+shape-1);
            auto& subject=result[size_t(shape-1)];subject.center={0,(shape==1?2.7f:shape==2?.8f:.9f)*mesh.scale,0};subject.radius=0;
            for(int frame=0;frame<=96;++frame)for(const auto& vertex:mesh.vertices){V point{vertex.x*mesh.scale,vertex.y*mesh.scale,vertex.z*mesh.scale};
                auto posed=LifeWeaponPoseAt(form,point,true,LifeWeaponDuration(form)*float(frame)/96,float(frame)*.13f);
                subject.radius=std::max(subject.radius,Length(posed-subject.center));}
            subject.radius=subject.radius*1.05f+.9f;
        }return result;
    }();
    // 今の武器の範囲を、体の位置と大きさに合わせる
    auto subject=envelopes[size_t(int(body.action)-int(LifeAction::Hammer))];
    subject.center=body.at+V{0,-1.25f,0}+subject.center*scale;subject.radius*=scale;return subject;
}
// 調合室のレシピを JSON に保存する (一時ファイルに書いてから置き換える)
bool ChronoSystem::SaveSlimeLab(){
    // 自動テスト中は保存しない
    if(wcsstr(GetCommandLineW(),L"--slime-life-smoke"))return true;
    // 保存先 (記録と同じフォルダ) に、版・選んだスロット・材料・クリア数・レシピを書く
    try{auto path=RecordPath().parent_path()/L"slime-lab-v1.json";std::filesystem::create_directories(path.parent_path());
        nlohmann::json data{{"version",2},{"slot",lifeRecipeSlot_},{"materials",lifeMaterials_},{"clears",lifeClears_}};
        data["recipes"]=nlohmann::json::array();for(const auto& recipe:lifeRecipes_){auto rgb=recipe.color.ToRGB();data["recipes"].push_back({{"mix",recipe.mix},{"color",{rgb.x,rgb.y,rgb.z}},{"colorHSV",{recipe.color.h,recipe.color.s,recipe.color.v}}});}
        // 一時ファイルに書いてから置き換え、途中で壊れないようにする
        auto temp=path;temp+=L".tmp";{std::ofstream file(temp);if(!file)throw std::runtime_error("Recipe save failed");file<<data.dump(2);if(!file)throw std::runtime_error("Recipe save failed");}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Recipe replace failed");
        return true;
    // 保存に失敗したらメッセージを出す
    }catch(...){life_.message="SAVE FAILED / PLEASE CHECK STORAGE";life_.messageAge=4;return false;}
}
// スライムの生活モードを作る: 保存したレシピを読み込み、体・敵・取り込める物・門・音を初期化する
void ChronoSystem::BuildSlimeLife(entt::registry& r){
    // 最初の 1 回だけ保存を読み込む (自動テスト中は読まない)
    if(!lifeLoaded_){lifeLoaded_=true;
        if(!wcsstr(GetCommandLineW(),L"--slime-life-smoke"))try{
            std::ifstream file(RecordPath().parent_path()/L"slime-lab-v1.json");nlohmann::json data;if(file){file>>data;
                // スロット・材料の数・クリア数を読み、範囲に収める
                lifeRecipeSlot_=std::clamp(data.value("slot",0),0,2);lifeMaterials_=std::clamp(data.value("materials",3),3,4);lifeClears_=std::max(0,data.value("clears",0));
                // 各レシピの配合と色 (新しい HSV・古い RGB・さらに古い番号の形式に対応)
                const auto& recipes=data.at("recipes");for(size_t i=0;i<lifeRecipes_.size()&&i<recipes.size();++i){auto& out=lifeRecipes_[i];out.mix=recipes[i].at("mix").get<std::array<int,4>>();
                    for(int& n:out.mix)n=std::clamp(n,0,100);if(lifeMaterials_<4)out.mix[3]=0;if(out.mix[0]+out.mix[1]+out.mix[2]+out.mix[3]==0)out.mix[0]=40;
                    if(recipes[i].contains("colorHSV")){auto c=recipes[i].at("colorHSV").get<std::array<float,3>>();for(float& v:c)if(!std::isfinite(v))v=0;out.color={std::clamp(c[0],0.f,1.f),std::clamp(c[1],0.f,1.f),std::clamp(c[2],0.f,1.f)};}
                    else if(recipes[i].contains("color")&&recipes[i]["color"].is_array()){auto c=recipes[i]["color"].get<std::array<float,3>>();out.color=SlimeColor::FromRGB({c[0],c[1],c[2]});}
                    else out.color=SlimeColor::Legacy(recipes[i].value("color",0));}}
        // 保存が壊れていたら既定のレシピを使う
        }catch(...){OutputDebugStringA("Slime Lab: using default recipe after invalid save.\n");}
    }
    // 生活の状態を作り直し、選んでいるレシピを使う
    life_={};life_.revision=GetTickCount64();life_.recipe=lifeRecipes_[size_t(lifeRecipeSlot_)];lifeLastAction_.fill(-1);lifeLastCounts_={};
    // 消火栓の水の読み戻しを止める
    Engine::Renderer::GetInstance()->EnableHydrantWaterReadback(false);hydrantWaterSerial_=0;
    // 通常は調合室から始める (自動テストではすぐ始める)
    life_.lab=!wcsstr(GetCommandLineW(),L"--slime-life-smoke");
    lifeWasLab_=life_.lab;lifeReseed_=true;lifePreviewEmitted_=false;
    // 調合室の見本のスライム (地上から離れた高い所に置く)
    life_.preview.at={0,1201.25f,0};life_.preview.mass=LifeStartMass;life_.preview.particles=Engine::Renderer::kPlayerFluidParticles;life_.preview.forward={0,0,-1};
    // プレイヤーを街の開始位置に置く
    auto& p=r.get<Player>(player_);p.mass=LifeStartMass;p.grounded=true;p.velocity={};p.collisionScale=1;
    auto& t=r.get<TransformComponent>(player_);t.translate={CityStart().x,InkGround(CityStart())+1.25f,CityStart().z};t.scale={1,1,1};
    // 本体と子の体を初期化する
    LifeInitializeBodies(life_,Engine::Renderer::kPlayerFluidParticles);
    auto& body=life_.bodies[0];body.at=Read(t.translate);
    // 体力を 100 にする
    auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(100);health.SetHp(100);health.SetDead(false);
    // 強化メニューを止め、群れのモードで戦う
    auto& ink=r.get<InkPlayer>(player_);ink.rogue.enabled=false;ink.rogue.menu=false;ink.battlePhase=BattlePhase::Horde;
    ink.hordeEnabled=true;ink.swarmKills=0;
    // ムービーは使わない
    openingPending_=cinematicActive_=false;
    // 最初の段階の敵を出し、ボスは隠す
    swarm_.clear();swarmEffects_.clear();
    SpawnCityStep(0);ink.swarmSpawned=ink.swarmAlive=int(swarm_.size());
    SetSwarmBossVisible(r,false);
    // 取り込める物は街の景観から作る。鉄の門は騎士団が倒れた後に加わる
    // 街の小物を取り込める物として登録する
    for(const auto& seed:cityProps_){LifeCollectible prop;prop.at=seed.at;prop.required=seed.required;prop.reward=seed.reward;prop.visual=uint32_t(seed.entity);prop.morphItem=seed.morphItem;life_.props.push_back(prop);}
    // 鉄の門 (騎士団を倒すまでは無効)
    {LifeCollectible gate;gate.at=CityGateCenter()+V{0,1.2f,-8};gate.required=CityGateAbsorbMass();gate.reward=CityGateReward();gate.visual=uint32_t(cityGateEntity_);gate.active=false;
        cityGateProp_=int(life_.props.size());life_.props.push_back(gate);}
    // 門・バリケード・弾などの状態を初期化する
    cityGates_={};cityGateUnlocked_=false;cityBarricadeHealth_=CityBarricadeHealth();cityGateHealth_=CityGateHealth();cityAnnounced_=-1;cityHintClock_=0;
    if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=true;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();
    // 前回取り込んで隠した小物を表示し直す
    for(auto e:r.view<NameComponent,MeshRendererComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono City prop",0)==0)r.get<MeshRendererComponent>(e).enabled=true;
    // 流体の見本と変形を初期化する
    scene_->ResetFluidPreview();
    auto* render=Engine::Renderer::GetInstance();render->SetRogueWorldFrozen(false);render->SetFluidMorphTargets({},0);
    render->SetFluidTendrils({});
    // 分裂と飲み込みの音を一度だけ読み込む
    if(lifeEngulfSound_==0xffffffffu){auto* audio=Engine::Audio::GetInstance();
        for(int i=0;i<3;++i)lifeSplitSounds_[i]=audio->Load("Resources/Sound/slime-life-split-"+std::to_string(i)+".wav");
        lifeEngulfSound_=audio->Load("Resources/Sound/slime-life-engulf.wav");}
}
// スライムが被弾したとき: 体力を減らし、分裂した子を外へ飛ばす。武器の形なら攻撃を止める
void ChronoSystem::DamageSlimeLife(entt::registry& r,Player& p,float amount,GameContext& ctx,V source){
    // 無敵中・終了後は受けない
    if(amount<=0||p.invincible>0||finished_)return;
    // 本体の体力を減らし、分裂した子の番号を受け取る
    auto& body=life_.bodies[0];float previousHp=body.health;int childIndex=LifeTakeHit(life_,amount);
    // 無敵時間と被弾の記録
    p.invincible=.7f;p.damageAge=0;p.damageSource=source;++p.stats.hitsTaken;
    // 子が分かれたら、攻撃の反対側へ飛ばし、変形を整え直して音とメッセージを出す
    if(childIndex>0){
        auto& child=life_.bodies[size_t(childIndex)];V away=body.at-source;away.y=0;
        if(Length(away)<.01f)away={body.forward.z,0,-body.forward.x};else away=Unit(away);
        child.at=body.at+away*(2.55f*(LifeScale(body.mass)+LifeScale(child.mass))+.7f);
        child.at.y=InkGround(child.at)+1.25f;
        LifeInterruptTopology(life_);
        auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[life_.splits%3],false,.65f*audio->GetMasterSEVolume(),1+.05f*life_.recipe.Ratio(1));
        life_.message="被弾分裂：本体HP -"+LifeNumber(previousHp-body.health)+" / 子の大きさ "+LifeNumber(child.mass);life_.messageAge=2;
    // 武器の形なら攻撃を止め (大きさが足りなければ戻る)、それ以外の行動はスライムに戻る
    }else if(LifeWeaponForm(body.action)){LifeStopWeaponAttack(body);if(!LifeCanMorph(body,body.action))LifeReturnToSlime(body);}
    else if(body.action!=LifeAction::Idle)LifeReturnToSlime(body);
    ++life_.revision;
    // 泡の配合なら攻撃の反対側へ押される
    if(life_.recipe.Ratio(3)>0){V away=body.at-source;away.y=0;if(Length(away)>.01f)p.velocity=p.velocity+Unit(away)*(14*life_.recipe.Ratio(3));}
    // 体力の表示を更新し、画面を揺らす
    auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(body.maxHealth);health.SetHp(body.health);health.SetDead(body.health<=0);
    ctx.camera->StartImpactShake(.14f,.12f,{0,1,0},12);
    // 体力が尽きたら負け
    if(body.health<=0)Finish(p,false);
}
// 取り込みを始める: 口の前の取り込める物や敵を近い順に選び、触手でつかむ
bool ChronoSystem::StartLifeAbsorb(int bodyIndex){
    // 待機中の体だけが取り込みを始められる
    auto& b=life_.bodies[size_t(bodyIndex)];if(b.mass<=0||b.down||b.action!=LifeAction::Idle)return false;
    b.grabs.clear();
    // 取り込みの候補 (近く、正面に近いほど優先)
    struct Candidate {uint32_t id;V at;float required,hp,maxHp,reward,score;};std::vector<Candidate> candidates;
    // 取り込める大きさで口の範囲にあれば候補に加える
    auto candidate=[&](uint32_t id,V point,float required,float hp,float maxHp,float reward){
        if(!LifeCanAbsorb(b.mass,required,hp,maxHp)||!LifeInMouth(b.at,b.forward,point,b.mass,0,life_.recipe))return;
        V delta=point-b.at;delta.y=0;float score=LifeDistance(point,b.at)+3*(1-Dot(Unit(delta),b.forward));
        candidates.push_back({id,point,required,hp,maxHp,reward,score});};
    // 小物と敵から候補を集める
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(prop.active)candidate(0x80000000u|uint32_t(i),prop.at,prop.required,0,1,prop.reward);}
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto drop=CityEnemyDrop(enemy);candidate(enemy.id,enemy.at,drop.required,enemy.health,enemy.maxHealth,drop.reward);}
    // 近い順に触手でつかむ
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& c){return a.score==c.score?a.id<c.id:a.score<c.score;});
    for(const auto& c:candidates)LifeAddGrab(life_,b,c.id,c.at,c.required,c.hp,c.maxHp,c.reward);
    if(b.grabs.empty())return false;
    // 最初につかんだ対象を目標にする
    b.target=b.grabs.front().target;b.engulfPoint=b.grabs.front().at;
    ++life_.revision;return true;
}
// スライムの攻撃が敵に当たったとき (盾持ちと守護者の装甲は特定の武器でだけ崩せる)
void ChronoSystem::LifeHit(entt::registry& r,Player& p,GameContext& ctx,uint32_t id,float damage,LifeAction source,V from){
    for(auto& enemy:swarm_)if(enemy.id==id&&SwarmAlive(enemy)){
        // 触れた敵は気付く
        auto kind=CityKind(enemy);enemy.alerted=true;
        // 盾持ちは剣 (と消火栓) 以外の攻撃を正面で防ぐ。剣は構えを崩す
        if(CityShieldBlocks(enemy,from,source==LifeAction::Sword||source==LifeAction::Hydrant)){damage*=.25f;
            if(life_.messageAge<=0){life_.message="盾に防がれた！ 剣で体勢を崩そう";life_.messageAge=1.5f;}}
        else if(kind==CityEnemy::Shield&&(source==LifeAction::Sword||source==LifeAction::Hydrant)){enemy.stagger=source==LifeAction::Hydrant?.6f:2.5f;}
        // 守護者の足の装甲はハンマーでしか削れない
        if(kind==CityEnemy::Guardian&&enemy.armor>0){
            if(source==LifeAction::Hammer){enemy.armor-=damage;
                life_.message=enemy.armor>0?"足の装甲 残り "+std::to_string(int(std::ceil(enemy.armor)))+" / ハンマーで砕け":std::string("装甲が砕けた！ 本体を叩き込め");life_.messageAge=1.6f;
                damage*=.5f;}
            else{damage*=.15f;if(life_.messageAge<=0){life_.message="装甲が硬い！ ハンマーで足の装甲を砕こう";life_.messageAge=1.5f;}}}
        // 体力を減らして光らせる
        enemy.health-=damage;enemy.flash=.15f;
        // 倒したら落とし物を残し、エフェクトを出す
        if(enemy.health<=0){enemy.phase=SwarmPhase::Dead;++r.get<InkPlayer>(player_).swarmKills;
            life_.props.push_back(CityEnemyDrop(enemy));if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,3*std::sqrt(enemy.scale),2});}
        // 倒れていなければ怯ませる (大砲と守護者は怯まない)
        else if(kind!=CityEnemy::Cannon&&kind!=CityEnemy::Guardian){ChangeSwarmPhase(enemy,SwarmPhase::Recover);}
        (void)p;(void)ctx;return;
    }
}
// スライムの体 (本体と子) の形を GPU 流体の目標として渡す (液体・武器・触手)
void ChronoSystem::SyncSlimeBodies(GameContext& ctx){
    // 研究所では顔をカメラに向ける
    ctx.renderer->SetSlimeFaceCamera(life_.lab?1.f:0.f);
    // 体ごとの GPU 流体の目標の一覧
    using Target=Engine::Renderer::FluidMorphTarget;std::vector<Target> targets;targets.reserve(Engine::Renderer::kPlayerFluidParticles);
    // 研究所の開閉が変わったら流体を作り直す
    if(lifeWasLab_!=life_.lab){scene_->ResetFluidPreview();lifeWasLab_=life_.lab;lifeReseed_=true;lifePreviewEmitted_=false;lifeLastCounts_={};++life_.revision;}
    // 研究所では見本、それ以外では実際の体を描く
    auto renderedBody=[&](size_t i)->const LifeBody& {return life_.lab?(i==0?life_.preview:life_.previewChild):life_.bodies[i];};
    static_assert(LifeBodyCount<=Engine::Renderer::kFluidMorphBodyCount);
    const uint32_t allBodies=Engine::Renderer::kFluidMorphAllBodies;
    // 作り直すときは全部の体、そうでなければ数が変わった体だけ
    uint32_t mask=lifeReseed_?allBodies:0;for(size_t i=0;i<LifeBodyCount;++i){const auto& b=renderedBody(i);uint32_t count=life_.lab&&i>1?0:b.particles;if(lifeLastCounts_[i]!=count)mask=allBodies;else if(lifeLastAction_[i]!=int(b.action))mask|=1u<<i;lifeLastCounts_[i]=count;lifeLastAction_[i]=int(b.action);}
    const float frameDt=std::clamp(ctx.dt,0.f,.05f);
    // スライムの色
    const auto color=LifeColor(life_.recipe.color);
    // 各体: 形・粒子数・大きさ・行動の時間から点の目標を作る
    for(size_t i=0;i<(life_.lab?size_t(2):LifeBodyCount);++i){const auto& b=renderedBody(i);if(b.mass<=0)continue;
        uint32_t count=b.particles;
        // 形の番号 (1: ハンマー, 2: 剣, 3: 飛行機, 4: タイヤ, 5: 消火栓, 0: 液体)
        int shape=b.action==LifeAction::Hammer?1:b.action==LifeAction::Sword?2:b.action==LifeAction::Plane?3:b.action==LifeAction::Tire?4:b.action==LifeAction::Hydrant?5:0;
        bool liquid=!LifeWeaponForm(b.action),weapon=LifeWeaponForm(b.action);
        const auto& cloud=LifeCloud(shape,count);float scale=LifeScale(b.mass),age=b.actionAge;V radii=LifeRadii(b,life_.recipe);
        // 体全体の値 (特に地形へのレイ) を 6600 点それぞれで計算してはいけない。
        // 下で変わるのは点ごとの姿勢と速度だけ
        Target common{};common.blend=liquid?b.motion:LifeSmooth(age/.25f);
        common.origin={b.at.x+(liquid?b.fluidOffset.x:0),b.at.y-1.25f+(liquid?.8f*b.air:0),b.at.z+(liquid?b.fluidOffset.z:0)};
        common.density=LifeDensity(b.mass,count,Engine::Renderer::kPlayerFluidParticles);
        auto heading=liquid?b.liquidForward:b.forward;
        common.forward={heading.x,float(i),heading.z};common.omega=liquid?life_.recipe.Cohesion():b.action==LifeAction::Engulf?36.f:b.weapon.active?80.f:32.f;
        common.material={life_.recipe.Damping(),life_.recipe.Viscosity(),life_.recipe.SurfaceTension(),life_.lab?1200.f:InkGround(b.at)};
        V drive=liquid?b.coreVelocity:b.velocity;common.motion={drive.x,drive.y,drive.z,b.air};
        common.shape={radii.x,b.down?radii.y*.25f:radii.y,radii.z,liquid?1.f:weapon?-float(b.action):0.f};common.color=color;
        if(liquid)common.reserved={b.flowShape.x,b.flowShape.z,b.coreLead.x-b.fluidOffset.x,b.coreLead.z-b.fluidOffset.z};
        // 形の各点を、体の大きさと姿勢に合わせて目標の位置にする
        for(size_t pointIndex=0;pointIndex<cloud.size();++pointIndex){auto point=cloud[pointIndex];V local{point.x,point.y,point.z};
            if(liquid)local={local.x/2.55f*radii.x,local.y/1.8f*radii.y,local.z/2.55f*radii.z};
            if(weapon)local=LifeWeaponPose(b,local,b.weapon.age);
            if(b.down)local.y*=.25f;
            if(!liquid)local=local*scale;
            Target target=common;target.local={local.x,std::max(.18f,local.y),local.z};
            if(weapon&&b.weapon.ready){V velocity=LifeWeaponVelocity(b,{point.x,point.y,point.z},frameDt);target.reserved={velocity.x,velocity.y,velocity.z,-1};}
            targets.push_back(target);
        }
    }
    // 取り込み中の触手を管として渡す
    std::vector<Engine::Renderer::FluidTendril> arms;
    // 取り込み中の体ごとに、つかんだ各対象への触手を節に分けて作る
    for(size_t owner=0;owner<(life_.lab?size_t(2):LifeBodyCount);++owner){const auto& b=renderedBody(owner);
        if(b.mass<=0||b.down||b.action!=LifeAction::Engulf)continue;
        for(size_t i=0;i<b.grabs.size();++i){const auto& grab=b.grabs[i];
            V previous=LifeTentaclePoint(b,life_.recipe,grab,i,0);
            float previousRadius=LifeTentacleRadius(b.mass,0);
            for(int segment=1;segment<=LifeTentacleSegments;++segment){float u=float(segment)/LifeTentacleSegments;
                V next=LifeTentaclePoint(b,life_.recipe,grab,i,u);float radius=LifeTentacleRadius(b.mass,u);
                if(Length(next-previous)>.001f)arms.push_back({EV(previous),previousRadius,EV(next),radius});
                previous=next;previousRadius=radius;
            }
        }
    }
    // 触手・変形の目標・色をレンダラーへ渡す
    ctx.renderer->SetFluidTendrils(std::move(arms));
    ctx.renderer->SetFluidMorphTargets(std::move(targets),life_.revision,mask,lifeReseed_,frameDt);lifeReseed_=false;
    ctx.renderer->SetSlimeColor(color);
}
// 調合室を開く / 閉じる
void ChronoSystem::SetSlimeLabOpen(bool open){
    // 研究所を開くとカーソルを出し、カメラを作り直す
    life_.lab=open;Engine::WindowDX::SetCursorVisible(open);cameraReady_=false;
    // 調合室を出ても、TRY/Tab と DEPLOY のどちらでも編集した配合を残す
    lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;
}
// 調合室の試し (移動・取り込み・被弾) を始める
void ChronoSystem::StartSlimeTrial(SlimeTrial trial){
    // 見本を初期化して、選んだ試しを始める
    life_.preview={};auto& b=life_.preview;b.at={0,1201.25f,0};b.mass=100;b.health=b.maxHealth=100;b.particles=Engine::Renderer::kPlayerFluidParticles;b.forward=b.liquidForward={0,0,-1};
    life_.previewChild={};life_.previewAuto=false;life_.previewJumpClock=0;life_.previewImpact=0;life_.trial=trial;life_.trialAge=life_.trialDistance=0;life_.trialMessageAge=3;
    // 移動の試し: 同じ時間だけ往復する
    if(trial==SlimeTrial::Move)life_.trialMessage="同じ時間だけ往復します。速さ・切り返し・止まり方を比較";
    // 取り込みの試し: 9 本の触手で周りの物をつかむ
    if(trial==SlimeTrial::Absorb){b.action=LifeAction::Engulf;b.restoreDuration=.12f;b.engulfPoint={0,1200.8f,-life_.recipe.AbsorbRange()*.75f};
        for(int i=0;i<9;++i){float angle=(float(i)-4)*.14f,reach=life_.recipe.AbsorbRange()*(.55f+float(i%3)*.18f);
            b.grabs.push_back({uint32_t(i),{std::sin(angle)*reach,1200.8f,-std::cos(angle)*reach},0});}
        life_.trialMessage="9本の触手で高速回収 "+LifeNumber(life_.recipe.AbsorbDuration())+"秒 / 届く距離 "+LifeNumber(life_.recipe.AbsorbRange());}
    // 被弾の試し: 同じ 8 ダメージで分裂させる
    if(trial==SlimeTrial::Hit){SlimeLife sample;sample.recipe=life_.recipe;sample.bodies[0]=b;int index=LifeTakeHit(sample,8);b=sample.bodies[0];
        if(index>0){life_.previewChild=sample.bodies[size_t(index)];life_.previewChild.at={3.4f,1201.25f,0};}
        b.aspectVelocity=-6;life_.trialMessage="同じ8ダメージ：本体HP "+LifeNumber(b.health)+" / 子の大きさ "+LifeNumber(life_.previewChild.mass);}
    lifeReseed_=true;++life_.revision;
}
// 調合室の更新: 見本のスライムを動かし、体全体が画面の中央に収まるようカメラを合わせる
void ChronoSystem::UpdateSlimeLab(GameContext& ctx){
    // 研究所の時間と見本の演出の時間を進める
    float dt=std::clamp(ctx.dt,0.f,.05f);life_.labAge+=dt;life_.previewJumpClock+=dt;life_.previewMix=std::max(0.f,life_.previewMix-dt);
    life_.trialMessageAge=std::max(0.f,life_.trialMessageAge-dt);life_.previewImpact=std::max(0.f,life_.previewImpact-dt);life_.trialAge+=dt;
    // 見本の体と接地
    auto& b=life_.preview;bool grounded=b.at.y<=1201.251f,wasGrounded=grounded;
    // 移動の試しの入力 (右 → 左 → 止まる)
    V wish{};if(life_.trial==SlimeTrial::Move){if(life_.trialAge<.3f)wish={1,0,0};else if(life_.trialAge<.6f)wish={-1,0,0};}
    // 配合の性能で動かす
    b.velocity=LifeMoveVelocity(b.velocity,wish,life_.recipe,grounded,dt);b.at.x+=b.velocity.x*dt;b.at.z+=b.velocity.z*dt;
    if(Length(wish)>.01f)b.forward=Unit(wish);
    life_.trialDistance=std::max(life_.trialDistance,std::abs(b.at.x));
    if(life_.trial==SlimeTrial::Move&&life_.trialAge>1.6f){life_.trial=SlimeTrial::None;life_.trialMessage="移動テスト：到達距離 "+LifeNumber(life_.trialDistance)+" / 最高速 "+LifeNumber(life_.recipe.Speed());life_.trialMessageAge=4;}
    // 取り込みの試し: 時間が来たら回復へ、回復が終わったら待機へ
    if(life_.trial==SlimeTrial::Absorb){b.actionAge+=dt;if(b.action==LifeAction::Engulf&&b.actionAge>=life_.recipe.AbsorbDuration()){b.action=LifeAction::Restore;b.actionAge=0;}
        else if(b.action==LifeAction::Restore&&b.actionAge>b.restoreDuration){b.action=LifeAction::Idle;life_.trial=SlimeTrial::None;life_.trialMessageAge=3;}}
    // 被弾の試し: 分かれた子を飛ばし、時間が来たら終える
    if(life_.trial==SlimeTrial::Hit){auto& child=life_.previewChild;V velocity{0,0,-life_.recipe.ChildSpeed()*.12f};child.at=child.at+velocity*dt;LifeTickMotion(child,life_.recipe,velocity,true,false,dt);
        if(life_.trialAge>2.2f){auto message=life_.trialMessage;StartSlimeTrial(SlimeTrial::None);life_.trialMessage=message;life_.trialMessageAge=4;}}
    // つついたときの揺れ
    if(life_.previewPoke){b.aspectVelocity-=8*life_.recipe.Slosh();life_.previewPoke=false;}
    // ジャンプ (自動の試しでは一定間隔で跳ぶ)
    if(life_.previewJump||(life_.previewAuto&&life_.previewJumpClock>3.6f)){
        if(grounded){b.velocity.y=life_.recipe.Bounce();b.reboundAvailable=true;grounded=false;life_.previewJumpClock=0;}life_.previewJump=false;
    }
    // 重力と床
    b.velocity.y-=life_.recipe.Gravity()*dt;b.at.y+=b.velocity.y*dt;
    if(b.at.y<1201.25f){b.at.y=1201.25f;b.velocity.y=0;grounded=true;}
    // 体の動き (揺れ・伸び) を更新する
    LifeTickMotion(b,life_.recipe,b.velocity,grounded,!wasGrounded&&grounded,dt);
    if(!wasGrounded&&grounded)life_.previewImpact=.6f;
    // 反発の配合なら着地でもう一度跳ねる
    if(!wasGrounded&&grounded&&b.reboundAvailable){b.reboundAvailable=false;b.velocity.y=life_.recipe.Rebound();}
    b.age+=dt;
    // 研究所の間は世界を止め、カーソルを出す
    ctx.renderer->SetRogueWorldFrozen(true);Engine::WindowDX::SetCursorVisible(true);
    // 体全体を UI の空いた中央に収める。とても平たい配合や、
    // 既にあるシミュレーションの表面も含める (コントローラーだけではない)
    // 見本・子・触手・実際の流体の範囲を合わせた箱を求める
    auto radii=LifeRadii(b,life_.recipe);float originY=b.at.y-1.25f+.8f*b.air;
    V lo{b.at.x-radii.x,originY-radii.y*b.air,b.at.z-radii.z},hi{b.at.x+radii.x,originY+radii.y,b.at.z+radii.z};
    if(life_.previewChild.mass>0){auto childRadii=LifeRadii(life_.previewChild,life_.recipe);auto childAt=life_.previewChild.at;lo.x=std::min(lo.x,childAt.x-childRadii.x);lo.z=std::min(lo.z,childAt.z-childRadii.z);hi.x=std::max(hi.x,childAt.x+childRadii.x);hi.z=std::max(hi.z,childAt.z+childRadii.z);}
    if(life_.trial==SlimeTrial::Absorb)lo.z=std::min(lo.z,b.engulfPoint.z-.7f);
    if(life_.trial==SlimeTrial::Absorb){auto posed=b;posed.actionAge=life_.recipe.AbsorbDuration()*.45f;
        for(size_t i=0;i<b.grabs.size();++i)for(int sample=0;sample<=LifeTentacleSegments;++sample){
            V world=LifeTentaclePoint(posed,life_.recipe,b.grabs[i],i,float(sample)/LifeTentacleSegments);
            lo={std::min(lo.x,world.x),std::min(lo.y,world.y),std::min(lo.z,world.z)};hi={std::max(hi.x,world.x),std::max(hi.y,world.y),std::max(hi.z,world.z)};}}
    const auto& snapshot=ctx.renderer->GetFluidBodySnapshot();
    for(auto p:snapshot.offsets){auto world=p+snapshot.core;if(std::abs(world.x)<30&&world.y>1190&&world.y<1230&&std::abs(world.z)<30){lo={std::min(lo.x,world.x),std::min(lo.y,world.y),std::min(lo.z,world.z)};hi={std::max(hi.x,world.x),std::max(hi.y,world.y),std::max(hi.z,world.z)};}}
    // 箱の中心へカメラをなめらかに寄せる
    lo=lo-V{.7f,.7f,.7f};hi=hi+V{.7f,.7f,.7f};V centre=(lo+hi)*.5f;
    if(!cameraReady_){cameraFollow_=centre;cameraReady_=true;}else cameraFollow_=Lerp(cameraFollow_,centre,1-std::exp(-10*dt));
    // 箱が画面の空いた所に収まる距離を求める
    V view=Unit(V{0,-.28f,1}),up{0,view.z,-view.y};float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),tanHalf=std::tan(.72f*.5f),distance=10;
    for(int i=0;i<8;++i){V point{(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z};V d=point-cameraFollow_;float depth=Dot(d,view);
        distance=std::max({distance,std::abs(d.x)/(tanHalf*aspect*.43f)-depth,std::abs(Dot(d,up))/(tanHalf*.38f)-depth});}
    V eye=cameraFollow_-view*(distance*1.05f);
    // カメラを置いて射影を設定する
    ctx.camera->StopShake();ctx.camera->SetHandheld(0);ctx.camera->SetPosition(Write(eye));ctx.camera->LookAt(Write(cameraFollow_),{0,1,0});
    ctx.camera->SetProjection(.72f,aspect,.1f,2000);
    // 見本の体の形を GPU 流体へ渡す
    SyncSlimeBodies(ctx);
}
// 調合室の描画 (床・皿・触手・踏みつけの輪・混ぜる材料の滴・UI)
void ChronoSystem::DrawSlimeLab(entt::registry& r,GameContext& ctx){
    // 研究所の描画: カメラ・流体の設定・床と皿・触手・踏みつけの輪・材料の滴・UI
    auto* renderer=ctx.renderer;renderer->SetCamera(*ctx.camera);renderer->SetFluidAABBs({});renderer->SetFluidSplashSpheres({});
    auto at=life_.preview.at;
    renderer->SetGPUFluidCore({at.x,at.y,at.z},32,{life_.preview.aspect,life_.preview.air,life_.preview.motion},{0,0,-1},4,100);renderer->SetPlayerPos({at.x,at.y,at.z});
    renderer->SetSlimeGround({0,1200,0},life_.preview.air<.5f);renderer->SetSlimeFaceCamera(1);renderer->SetGPUFluidTether({},false);renderer->SetGPUFluidDecoy({0,-1000,0},0);
    renderer->SetChronoFluidPresentation(1,0,false);
    // 最初の 1 回だけ見本の流体を出す
    if(!lifePreviewEmitted_){renderer->EmitGPUFluid({0,1201.25f,0},{0,0,0},LifeColor(life_.recipe.color),Engine::Renderer::kPlayerFluidParticles);lifePreviewEmitted_=true;}
    Engine::Transform floor;floor.translate={0,1199.65f,0};floor.scale={120,.3f,120};renderer->DrawMesh(sphere_,white_,floor,{.14f,.22f,.19f,1});
    Engine::Transform tray;tray.translate={0,1199.8f,0};tray.scale={5.4f,.2f,5.4f};renderer->DrawMesh(sphere_,white_,tray,{.65f,.76f,.69f,1});
    DrawLifeTentacles(ctx,life_.preview);
    if(life_.previewImpact>0){float radius=life_.recipe.StompRadius()*(1-life_.previewImpact/.6f);for(int i=0;i<40;++i){float a=float(i)*6.2831853f/40,c=float(i+1)*6.2831853f/40;
        renderer->DrawLine3D({at.x+std::cos(a)*radius,1200.18f,at.z+std::sin(a)*radius},{at.x+std::cos(c)*radius,1200.18f,at.z+std::sin(c)*radius},UI::Teal);}}
    if(life_.previewMix>0)for(int i=0;i<7;++i){float t=std::fmod(life_.labAge*1.7f+float(i)*.17f,1.f);Engine::Transform drop;
        drop.translate={std::sin(float(i)*2.4f)*.65f,life_.preview.at.y+4*(1-t),std::cos(float(i)*2.4f)*.65f};drop.scale={.11f,.23f,.11f};renderer->DrawMeshInstanced(sphere_,white_,drop,LifeColor(life_.recipe.color));}
    DrawSlimeLifeUI(r,ctx);
}
// スライムの生活モードの更新: 入力・変形・移動・接触吸収・取り込み・踏みつけ・子の行動・武器の攻撃・街と敵
void ChronoSystem::UpdateSlimeLife(entt::registry& r,Player& p,GameContext& ctx){
    // 本体・入力の記録
    auto& ink=r.get<InkPlayer>(player_);auto& body=life_.bodies[0];auto* control=r.try_get<ControlFrame>(player_);
    float dt=std::clamp(ctx.dt,0.f,.05f);diagnostic_=control!=nullptr;
    body.previousForward=body.forward;
    // TAB / BACK で調合室を開く
    bool tab=control?control->lab:Controls::Key(VK_TAB)||Controls::Pad(XINPUT_GAMEPAD_BACK);
    if(tab&&!life_.previousLab)SetSlimeLabOpen(!life_.lab);life_.previousLab=tab;
    life_.messageAge=std::max(0.f,life_.messageAge-dt);
    // 研究所で「出発」を押したら、レシピを保存して最初からやり直す
    if(life_.deployRequested){life_.deployRequested=false;lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;if(!SaveSlimeLab()){life_.lab=true;return;}Reset(r);SetSlimeLabOpen(false);SyncSlimeBodies(ctx);return;}
    // 研究所を開いている間は研究所だけを更新する
    if(life_.lab){UpdateSlimeLab(ctx);return;}
    // 世界を動かし、カーソルを隠す
    ctx.renderer->SetRogueWorldFrozen(false);Engine::WindowDX::SetCursorVisible(false);
    life_.age+=dt;life_.morphAge+=dt;p.stats.seconds+=dt;
    // タイヤと消火栓をこのフレームで解放したか調べるために前の状態を記録する
    bool hadTire=body.tireUnlocked,hadHydrant=body.hydrantUnlocked;life_.tireNotice=std::max(0.f,life_.tireNotice-dt);life_.hydrantNotice=std::max(0.f,life_.hydrantNotice-dt);
    p.invincible=std::max(0.f,p.invincible-dt);p.damageAge+=dt;life_.stompCooldown=std::max(0.f,life_.stompCooldown-dt);life_.contactCooldown=std::max(0.f,life_.contactCooldown-dt);
    // カメラの向き
    if(control){yaw_=control->yaw;pitch_=control->pitch;}else if(ctx.input){float y,x;Controls::Look(dt,y,x);yaw_+=ctx.input->GetMouseDeltaX()*.0025f+y;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f+x,-.7f,.85f);}
    // 入力: 取り込み・変形攻撃・スライムに戻る。変形の種類は 1〜5 キーか十字キーで選ぶ
    bool absorb=control?control->absorb:Controls::Absorb();
    bool fire=control?control->attack:Controls::MorphAttack();
    bool returnToSlime=control?control->returnToSlime:Controls::ReturnToSlime();
    bool firePressed=fire&&!life_.previousFire,changedForm=false,returned=false;
    // メッセージを出す関数
    auto say=[&](const char* text){life_.message=text;life_.messageAge=2;};
    // スライムに戻る
    if(returnToSlime&&!life_.previousReturn&&LifeWeaponForm(body.action)){
        LifeReturnToSlime(body);life_.tackleTime=0;++life_.revision;returned=true;say("スライムに戻る / 再び取り込み可能");}
    // 武器の形では取り込めないことを知らせる
    if(absorb&&!life_.previousAbsorb&&LifeWeaponForm(body.action)&&body.action!=LifeAction::Hydrant)say("R / B でスライムに戻って取り込み");
    // 十字キーとキーボードで変形の種類を選ぶ
    if(!control&&UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT))life_.form=LifeAction(int(LifeAction::Hammer)+(int(life_.form)-int(LifeAction::Hammer)+1)%5);
    if(!control&&UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT))life_.form=LifeAction(int(LifeAction::Hammer)+(int(life_.form)-int(LifeAction::Hammer)+4)%5);
    if(control&&control->morphForm>=1&&control->morphForm<=5)life_.form=LifeAction(int(LifeAction::Hammer)+control->morphForm-1);
    if(!control){if(Controls::Key('1'))life_.form=LifeAction::Hammer;if(Controls::Key('2'))life_.form=LifeAction::Sword;if(Controls::Key('3'))life_.form=LifeAction::Plane;if(Controls::Key('4'))life_.form=LifeAction::Tire;if(Controls::Key('5'))life_.form=LifeAction::Hydrant;}
    // 攻撃を押したとき: 小さければタックル、条件を満たせば選んだ武器に変形する
    if(firePressed&&!returned&&body.cooldown<=0&&(body.action==LifeAction::Idle||(LifeWeaponForm(body.action)&&body.action!=life_.form))){
        if(life_.form==LifeAction::Tire&&!body.tireUnlocked){say("金色に光るタイヤを吸収すると、タイヤ変形が解放される");
        }else if(life_.form==LifeAction::Hydrant&&!body.hydrantUnlocked){say("大通りの金色に光る消火栓を吸収すると、放水攻撃と防御を解放");
        // 小さいうちはタックル
        }else if(body.mass<LifeHammerMass&&life_.form!=LifeAction::Tire&&life_.form!=LifeAction::Hydrant){
            life_.tackleTime=.24f;body.cooldown=.45f;
            say("タックル！ / 吸収して本体180でハンマー解放");
        // 大きさが足りないことを知らせる
        }else if(!LifeCanMorph(body,life_.form)){
            life_.message="変形には本体の大きさ "+LifeNumber(LifeRequiredMass(life_.form))+" が必要 / 今はタックル・踏みつけで攻撃";life_.messageAge=2;
        // 武器に変形する
        }else if(LifeEnterWeapon(body,life_.form,LifeFacesCamera(life_.form)?Forward(yaw_,0):body.forward)){changedForm=true;++life_.revision;
            say(body.action==LifeAction::Hydrant?"消火栓変形！ 左クリック・RTで放水 / 右クリック・Xで水の防御 / R・Bで戻る":body.action==LifeAction::Tire?"タイヤ変形！ 移動入力で加速 / 曲がるときは早めに / R・Bで戻る":"変形を維持 / 左クリック・RTで攻撃 / R・Bで戻る");}}
    // 前フレームの入力を記録する
    life_.previousFire=fire;life_.previousReturn=returnToSlime;
    // 移動とジャンプ (カメラ基準)
    V input{};if(control)input=Unit(control->move);else Controls::Move(input.x,input.z);
    V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    // 体の向きを更新する
    LifeUpdateFacing(body,wish,Forward(yaw_,0),dt);
    // ジャンプ (空中で押すと踏みつけ)
    bool jump=control?control->jump:Controls::Jump(),jumped=jump&&!prevSpace_&&p.grounded;
    if(jumped){p.velocity.y=life_.recipe.Bounce();body.reboundAvailable=true;}else if(jump&&!prevSpace_&&!p.grounded&&life_.stompCooldown<=0){life_.stomping=true;p.velocity.y=-32;}
    prevSpace_=jump;
    // 移動の速さ (配合と行動で変わる)
    float speed=life_.recipe.Speed()*LifeTravelScale(body);
    // タックル中は前へ速く進む
    if(life_.tackleTime>0){life_.tackleTime=std::max(0.f,life_.tackleTime-dt);wish=body.forward;speed=25;}
    // 飛行機の攻撃中は前へ進む
    if(body.action==LifeAction::Plane&&body.weapon.active&&body.weapon.age>.15f&&body.weapon.age<.72f){wish=body.forward;speed=25;}
    // タイヤは専用の速度、それ以外は配合の速度
    if(body.action==LifeAction::Tire&&body.weapon.ready)p.velocity=LifeTireMoveVelocity(body,p.velocity,wish,life_.recipe,p.grounded,dt);
    else p.velocity=LifeMoveVelocity(p.velocity,wish,life_.recipe,p.grounded,dt,speed/life_.recipe.Speed());
    // 重力
    p.velocity.y-=life_.recipe.Gravity()*dt;
    // 次の位置を街の通れる範囲に収め、接地を判定する
    V old=body.at,next=old+p.velocity*dt;
    {V radii=LifeRadii(body,life_.recipe);next=CityConstrain(old,next,std::max(radii.x,radii.z)*.7f,cityGates_);
        if(next.x==old.x)p.velocity.x=0;if(next.z==old.z)p.velocity.z=0;}
    // 段差が高すぎれば水平に動かない
    float floor=InkGround(old),nextFloor=InkGround(next);if(nextFloor>old.y+.1f){next.x=old.x;next.z=old.z;nextFloor=floor;}
    // 接地を判定し、地面へ合わせる
    bool grounded=p.grounded;p.grounded=SlimeGrounded(old.y,next.y,p.velocity.y,p.grounded,jumped,floor,nextFloor,LifeDistance(old,next));
    if(p.grounded){next.y=nextFloor+1.25f;p.velocity.y=0;p.recoveryPoint=next;}
    // 体の位置と動きを更新する (タイヤは転がった分だけ回す)
    body.at=next;LifeTickMotion(body,life_.recipe,p.velocity,p.grounded,!grounded&&p.grounded,dt,body.forward);
    if(body.action==LifeAction::Tire)body.tireAngle=std::remainder(body.tireAngle+Dot(body.at-old,body.forward)/(LifeTireRadius*LifeScale(body.mass)),6.2831853f);
    // 十分に大きければ、触れた物や敵をそのまま吸収する
    int touchCount=0;float touchGrowth=0;
    for(size_t i=0;i<life_.props.size();++i){auto& prop=life_.props[i];if(!prop.active)continue;
        if(LifeCollectTouch(life_,0x80000000u|uint32_t(i),prop.at,prop.required,0,1,prop.reward,old)){
            prop.active=false;++touchCount;touchGrowth+=prop.reward;
            if(prop.visual!=0xffffffffu){auto entity=entt::entity(prop.visual);if(r.valid(entity))r.get<MeshRendererComponent>(entity).enabled=false;}
        }}
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){auto drop=CityEnemyDrop(enemy);
        if(LifeCollectTouch(life_,enemy.id,enemy.at,drop.required,enemy.health,enemy.maxHealth,drop.reward,old)){
            enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;++touchCount;touchGrowth+=drop.reward;
        }}
    // 吸収したらメッセージ・音・エフェクトを出す
    if(touchCount){life_.message="接触吸収 "+std::to_string(touchCount)+"個 / 成長 +"+LifeNumber(touchGrowth);life_.messageAge=1.5f;
        auto* audio=Engine::Audio::GetInstance();audio->Play(lifeEngulfSound_,false,.35f*audio->GetMasterSEVolume(),1.2f);
        if(swarmEffects_.size()<128)swarmEffects_.push_back({body.at,0,2*LifeScale(body.mass),2});}
    // 取り込みを始める (対象が無ければ知らせる)
    if(LifeWantsAbsorb(absorb,body)&&!StartLifeAbsorb(0)&&!life_.previousAbsorb)say("NO ABSORBABLE TARGET / WEAKEN OR GROW FIRST");
    life_.previousAbsorb=absorb;
    // 空中から着地したら踏みつけ (範囲内の敵にダメージ)
    if(!grounded&&p.grounded&&life_.stomping){life_.stomping=false;life_.stompCooldown=.7f;++life_.stomps;
        float radius=life_.recipe.StompRadius()*LifeScale(body.mass),damage=life_.recipe.StompDamage()*LifeScale(body.mass);int hits=0;
        for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&LifeDistance(enemy.at,body.at)<radius+enemy.scale){LifeHit(r,p,ctx,enemy.id,damage,LifeAction::Idle,body.at);++hits;}
        swarmEffects_.push_back({body.at,0,radius,2});life_.message="踏みつけ "+LifeNumber(damage)+"ダメージ / "+std::to_string(hits)+"体に命中";life_.messageAge=2;
        ctx.camera->StartImpactShake(.18f,.08f+.12f*life_.recipe.Ratio(1),{0,1,0},13);}
    if(!grounded&&p.grounded&&body.reboundAvailable){body.reboundAvailable=false;p.velocity.y=life_.recipe.Rebound();if(p.velocity.y>0)p.grounded=false;}
    // プレイヤーの位置と大きさ、液体の見た目の値を反映する
    auto& transform=r.get<TransformComponent>(player_);transform.translate=Write(body.at);float size=LifeScale(body.mass);transform.scale={size,size,size};transform.rotate.y=std::atan2(body.forward.x,body.forward.z);
    ink.fluidDirection=body.liquidForward;ink.groundHeight=nextFloor;ink.groundSlope={};ink.airBlend=body.air;ink.fluidAspect=body.aspect;ink.fluidMotion=body.motion;ink.swimming=false;ink.phase=SlimePhase::Roaming;
    // 各体 (本体と子) の更新
    for(size_t index=0;index<life_.bodies.size();++index){auto& b=life_.bodies[index];if(b.mass<=0)continue;b.age+=dt;b.cooldown=std::max(0.f,b.cooldown-dt);b.actionAge+=dt;
        // 子: 本体から離れすぎれば戻り、近くの敵や物を自分で取り込む
        if(index>0){V previousAt=b.at;float distance=LifeDistance(b.at,body.at);
            if(b.down){ /* 倒れた子はここに残る。近づいても本体の体力は回復しない */ }
            else if(distance>90){
                // 壁や門の向こうではぐれた: 本体の横へ戻る
                float angle=float(index)*2.399963f;b.at=CityConstrain(body.at,body.at+V{std::sin(angle)*6,0,std::cos(angle)*6},2,cityGates_);}
            else if(distance>32){
                // 離れすぎなら本体へ向かう
                if(distance>1.8f){V step=Unit(body.at-b.at);b.at=b.at+step*std::min(distance,dt*life_.recipe.ChildSpeed()*1.4f);b.forward=step;b.forward.y=0;}
            }else if(b.action==LifeAction::Idle){
                // 近くの敵を探し、本体の周りの決まった位置へ向かう
                SwarmEnemy* selected=nullptr;float best=18;
                for(auto& enemy:swarm_)if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,b.at);if(d<best){best=d;selected=&enemy;}}
                float angle=float(index)*2.399963f;V goal=body.at+V{std::sin(angle)*7,0,std::cos(angle)*7};if(selected)goal=selected->at;
                V direction=goal-b.at;direction.y=0;if(Length(direction)>.5f){b.forward=Unit(direction);b.at=b.at+b.forward*std::min(Length(direction),dt*life_.recipe.ChildSpeed());}
                // 取り込めなければ近くの敵に体当たり
                if(!StartLifeAbsorb(int(index))&&selected&&best<3.1f*LifeScale(b.mass)+selected->scale&&b.cooldown<=0){LifeHit(r,p,ctx,selected->id,life_.recipe.ChildDamage()*LifeScale(b.mass),LifeAction::Idle,b.at);b.cooldown=life_.recipe.ChildCooldown();++life_.childHits;}
            }
            // 通れる範囲に収め、地面に合わせて動きを更新する
            if(!b.down)b.at=CityConstrain(previousAt,b.at,2.4f*LifeScale(b.mass)*.7f,cityGates_);
            b.at.y=InkGround(b.at)+1.25f;
            LifeTickMotion(b,life_.recipe,dt>0?(b.at-previousAt)*(1/dt):V{},true,false,dt);
        }
        // 取り込み中: つかんだ物が手放されないか調べ、時間が来たら取り込みを確定する
        if(b.action==LifeAction::Engulf){int captured=0;float gained=0;bool remaining=false;
            // 各触手: 取り消し・確定済みならとばす
            for(auto& grab:b.grabs){
                if(grab.cancelled||grab.committed)continue;
                // つかんだ対象の今の位置 (小物か敵)
                V targetAt{};bool valid=false;bool prop=(grab.target&0x80000000u)!=0;uint32_t id=grab.target&0x7fffffffu;
                if(prop&&id<life_.props.size()){const auto& object=life_.props[id];targetAt=object.at;valid=object.active&&LifeCanAbsorb(b.mass,object.required);}
                else if(!prop)for(const auto& enemy:swarm_)if(enemy.id==id){targetAt=enemy.at;valid=SwarmAlive(enemy)&&LifeCanAbsorb(b.mass,CityEnemyRequired(enemy),enemy.health,enemy.maxHealth);break;}
                // 対象が無くなったか手放したら取り消す
                if(!valid||!LifeHoldGrab(b,targetAt,life_.recipe)){grab.cancelled=true;grab.cancelAge=b.actionAge;continue;}
                grab.at=targetAt;remaining=true;
                // 確定の時点に来たら取り込みを確定し、対象を消す
                if(b.actionAge>=life_.recipe.AbsorbDuration()*LifeTentacleCapturePhase&&LifeCommitGrab(life_,b,grab)){
                    ++captured;gained+=grab.reward;
                    if(prop){auto& object=life_.props[id];object.active=false;if(object.visual!=0xffffffffu){auto e=entt::entity(object.visual);if(r.valid(e))r.get<MeshRendererComponent>(e).enabled=false;}}
                    else for(auto& enemy:swarm_)if(enemy.id==id){enemy.health=0;enemy.phase=SwarmPhase::Dead;++ink.swarmKills;break;}
                }
            }
            // 取り込んだら音とメッセージ
            if(captured){auto* a=Engine::Audio::GetInstance();a->Play(lifeEngulfSound_,false,.6f*a->GetMasterSEVolume(),1+.1f*life_.recipe.Ratio(0));
                if(index==0){life_.message=std::to_string(captured)+"個まとめて取り込み / 成長 +"+LifeNumber(gained);life_.messageAge=1.7f;}}
            // 残りが無ければスライムに戻る
            if(!remaining&&!b.committed){LifeReturnToSlime(b);++life_.revision;}
        }
        // 大きさが足りなくなった武器はスライムに戻る
        if(LifeWeaponForm(b.action)&&!LifeCanMorph(b,b.action)){LifeReturnToSlime(b);++life_.revision;}
        // 武器の形: 攻撃の当たりを 2 回に分けて調べ、敵や門にダメージを与える
        if(LifeWeaponForm(b.action)){
            // 形ができたら武器の攻撃を進める (押した瞬間・押し続け)
            bool formed=b.actionAge>=.3f&&ctx.renderer->FluidMorphReady(life_.revision,index,1.f*LifeScale(b.mass));
            if(LifeTickWeapon(life_,b,dt,index==0&&firePressed&&!changedForm,index==0&&fire&&!returned,formed)){
                auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[0],false,.16f*audio->GetMasterSEVolume(),b.action==LifeAction::Hammer?1.8f:1.2f);
            }
            for(int pulse=0;pulse<2;++pulse){bool hit=false;uint64_t hitKey=b.attackSerial*2+uint64_t(pulse);
                for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.lifeHitRevision!=hitKey&&LifeWeaponTouches(b,enemy.at,1.25f*enemy.scale,pulse)){
                    enemy.lifeHitRevision=hitKey;LifeHit(r,p,ctx,enemy.id,LifeWeaponDamage(b),b.action,b.at);++life_.hammerHits;hit=true;}
                if(index==0)CityWeaponHits(r,ctx,b,pulse,hitKey);
                if(hit){ctx.camera->StartImpactShake(.1f,b.action==LifeAction::Hammer?.16f:.08f,{0,1,0},18);
                    if(swarmEffects_.size()<128)swarmEffects_.push_back({b.at+b.forward*(3*LifeScale(b.mass)),0,3*LifeScale(b.mass),2});}
            }
        }
        // 取り込み → 回復 → 待機の順に進め、回復の終わりで成長を反映する
        if((b.action==LifeAction::Engulf||b.action==LifeAction::Restore)&&b.actionAge>=LifeDuration(b,life_.recipe)){
            // 回復の終わりに成長を反映し、メッセージを出す
            if(b.action==LifeAction::Restore){b.action=LifeAction::Idle;float gained=LifeCompleteGrowth(life_,b);b.cooldown=.25f;
                if(gained>0){
                    life_.message="吸収して成長！ 本体 "+LifeNumber(body.mass)+" / 子分 "+std::to_string(life_.ChildCount())+"体";life_.messageAge=2;}
            }
            else b.action=LifeAction::Restore;
            b.actionAge=0;b.target=0xffffffffu;b.grabs.clear();++life_.revision;
        }
    }
    // 転がるタイヤやタックルが触れた敵にダメージ
    bool rolling=body.action==LifeAction::Tire&&body.weapon.ready;
    if(life_.contactCooldown<=0&&LifeDistance(old,body.at)>dt*(rolling?10.f:5.f)&&(body.action==LifeAction::Idle||rolling)){
        for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(rolling?LifeSegmentDistance(enemy.at,old,body.at):LifeDistance(enemy.at,body.at))<2.7f*LifeScale(body.mass)+enemy.scale){
            LifeHit(r,p,ctx,enemy.id,(rolling?20.f:12.f)*LifeScale(body.mass),rolling?LifeAction::Tire:LifeAction::Idle,body.at);life_.contactCooldown=.55f;break;}}
    // 狙いと発射は、体の移動と向きの後のこのフレームのカメラを使う
    InkCamera(r,p,ctx);UpdateHydrantAim(ctx);
    UpdateHydrantWater(r,p,ctx,dt,fire,absorb);
    // 街のステージを更新する
    UpdateCityStage(r,p,ctx,dt);
    // タイヤや消火栓を初めて解放したら、その形を選んだ状態にして知らせる
    if(!hadTire&&body.tireUnlocked){life_.form=LifeAction::Tire;life_.tireNotice=6;}
    if(!hadHydrant&&body.hydrantUnlocked){life_.form=LifeAction::Hydrant;life_.hydrantNotice=6;}
    // 街の敵を更新する
    ink.swarmSpawned=int(swarm_.size());
    UpdateCityEnemies(r,p,ctx,dt);
    // エフェクトの時間を進めて古いものを消す
    for(auto& effect:swarmEffects_)effect.age+=dt;swarmEffects_.erase(std::remove_if(swarmEffects_.begin(),swarmEffects_.end(),[](const SwarmEffect& e){return e.age>.7f;}),swarmEffects_.end());
    // 体力・新しい材料の解放・演出。全段階を終えたらクリア
    p.mass=body.mass;auto& health=r.get<HealthComponent>(player_);health.SetMaxHp(body.maxHealth);health.SetHp(body.health);health.SetDead(body.health<=0);
    // 5 回取り込んだら泡の材料を解放する
    if(life_.absorbed>=5&&lifeMaterials_<4){lifeMaterials_=4;say("NEW MATERIAL / FOAM UNLOCKED IN SLIME LAB");SaveSlimeLab();}
    // 旧モードの値を戻し、演出と体の形を反映する
    p.worldScale=p.aimScale=1;p.aiming=false;p.action=Action::Free;
    Presentation(r,p,ctx);SyncSlimeBodies(ctx);
    // 体力が尽きたら負け、最後の段階の敵を全滅させたら勝ち
    if(body.health<=0)Finish(p,false);
    else if(life_.step==CityStepCount-1&&CityStepAlive()==0){++lifeClears_;SaveSlimeLab();Finish(p,true);}
}
// 取り込み中の触手の先に、つかんだ物 (または光る球) を描く
void ChronoSystem::DrawLifeTentacles(GameContext& ctx,const LifeBody& body){
    if(body.action!=LifeAction::Engulf)return;
    // 取り込みの進み具合
    float phase=body.actionAge/life_.recipe.AbsorbDuration();
    for(size_t i=0;i<body.grabs.size();++i){const auto& grab=body.grabs[i];
        // 本物の対象は取り込むまで残る。その後、見た目は先端と一緒に戻り、
        // 予約した報酬は回復を待つ
        // 対象を確定したか、研究所の見本なら、触手の先に描く
        if(!grab.cancelled&&(grab.committed||life_.lab)){
            // 確定後は触手の先と一緒に戻り、小さくなる
            V at=phase>=LifeTentacleCapturePhase?LifeTentaclePoint(body,life_.recipe,grab,i,1):grab.at;
            float shrink=1-.7f*LifeSmooth((phase-LifeTentacleCapturePhase)/(1-LifeTentacleCapturePhase));
            float radius=(life_.lab?.27f:LifeOrbRadius(grab.reward))*shrink;
            bool prop=(grab.target&0x80000000u)!=0;uint32_t index=grab.target&0x7fffffffu;
            // 街の小物なら実際のモデルで描く
            if(!life_.lab&&prop&&index<life_.props.size()&&!life_.props[index].orb&&DrawCityPropInTentacle(ctx,life_.props[index].visual,at,shrink))continue;
            // それ以外は報酬の大きさの色の球
            auto tint=life_.lab?V{.08f,.72f,1}:LifeOrbColor(grab.reward);Engine::Transform object;object.translate=EV(at);object.scale={radius,radius,radius};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,object,{tint.x,tint.y,tint.z,1});
        }
    }
}
// スライムの生活モードの描画: 群れ・街・変形アイテムの強調・触手・取り込める物
void ChronoSystem::DrawSlimeLife(entt::registry& r,GameContext& ctx){
    // 群れと街を描く
    DrawSwarm(r,ctx);DrawCityStage(ctx);
    // 変形アイテム (タイヤ・消火栓) を光らせ、足元の輪と上の矢印で示す
    for(const auto& prop:life_.props)if(prop.active&&prop.morphItem!=LifeMorphItem::None){
        auto entity=entt::entity(prop.visual);auto visual=cityVisuals_.find(prop.visual);
        if(r.valid(entity)&&r.all_of<TransformComponent,MeshRendererComponent>(entity)&&visual!=cityVisuals_.end()){
            const auto& v=visual->second;auto t=r.get<TransformComponent>(entity).GetTransform();
            // スキャンの中心 (取り込んだときの原点も含む) を基準に広げる
            V center=V{t.translate.x,t.translate.y,t.translate.z}+v.center;t.scale=EV(v.scale*1.035f);t.translate=EV(center-v.center*1.035f);
            ctx.renderer->DrawMeshInstanced(v.mesh,white_,t,{1,.63f,.08f,.8f},"MorphItemHighlight");
        }
        Engine::Transform ring;ring.translate={prop.at.x,InkGround(prop.at)+.18f,prop.at.z};ring.scale={3.2f,1,3.2f};
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,ring,{1,.65f,.08f,.25f},"HordeEffect");
        float bounce=.4f*std::sin(life_.age*3);
        V tip{prop.at.x,prop.at.y+4.8f+bounce,prop.at.z};
        ctx.renderer->DrawLine3D(EV(tip+V{-1,1,0}),EV(tip),{1,.72f,.15f,1});
        ctx.renderer->DrawLine3D(EV(tip+V{1,1,0}),EV(tip),{1,.72f,.15f,1});
    }
    // 触手を描く
    for(const auto& body:life_.bodies)DrawLifeTentacles(ctx,body);
    // モデルの無い取り込める物: 光る球 (報酬の段階の輪付き) か、鎧の破片
    for(const auto& prop:life_.props)if(prop.active&&prop.visual==0xffffffffu){Engine::Transform t;
        if(prop.orb){float radius=LifeOrbRadius(prop.reward);auto tint=LifeOrbColor(prop.reward);Engine::Vector4 color{tint.x,tint.y,tint.z,1};
            t.translate=EV(prop.at);t.translate.y+=.12f*std::sin(life_.age*2+prop.at.x);t.scale={radius,radius,radius};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,color);t.scale={radius*.45f,radius*.45f,radius*.45f};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.9f,1,1,1});
            for(int ring=0;ring<=LifeOrbTier(prop.reward);++ring){float reach=radius*(1.25f+float(ring)*.38f);
                for(int segment=0;segment<24;++segment){float a=float(segment)*6.2831853f/24,b=float(segment+1)*6.2831853f/24;
                    ctx.renderer->DrawLine3D({prop.at.x+std::cos(a)*reach,prop.at.y-.8f,prop.at.z+std::sin(a)*reach},
                        {prop.at.x+std::cos(b)*reach,prop.at.y-.8f,prop.at.z+std::sin(b)*reach},color);}}
            continue;
        }
        t.translate=EV(prop.at);float size=prop.required>100?1.35f:.55f;t.scale={size*swarmMeshes_[0].inverseExtent.x,size*.7f*swarmMeshes_[0].inverseExtent.y,size*swarmMeshes_[0].inverseExtent.z};
        ctx.renderer->DrawMeshInstanced(swarmMeshes_[0].handle,white_,t,prop.required>100?Engine::Vector4{.5f,.6f,.45f,1}:Engine::Vector4{.65f,.78f,.4f,1},"Default");}
}
// スライムの生活モードの UI: 結果・現在の段階・取り込みの大きさ・門の体力・体の状態・操作・変形・消火栓の照準・案内
void ChronoSystem::DrawSlimeLifeUI(entt::registry& r,GameContext& ctx){
    // UI の準備
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    auto& body=life_.bodies[0];auto& ink=r.get<InkPlayer>(player_);
    // 終わったら結果を表示する
    if(finished_){ui.Result(won_,won_?"CITY DEVOURED":"SLIME LOST COHESION","ABSORBED  "+std::to_string(life_.absorbed)+"   MAX GROUP MASS  "+std::to_string(int(life_.TotalMass())));return;}
    if(!life_.lab){
    // 段階の情報: エリア・段階・目標・残りの敵・配合の型
    {const auto& step=CityStepInfo(life_.step);auto area=CityAreaAt(body.at);
    ui.Rounded({22,20,420,151},{.025f,.045f,.035f,.8f},20);ui.Label("SLIME CITY  /  AREA "+std::to_string(int(step.area)+1),42,34,15,UI::Lime,2);
    ui.Text(std::string(CityAreaName(area))+"  段階 "+std::to_string(life_.step+1)+" / "+std::to_string(CityStepCount)+"  "+step.title,42,58,22,UI::Paper,UI::JapaneseFont);
    ui.Text(step.goal,42,94,15,UI::Gold,UI::JapaneseFont);
    ui.Text("この段階の敵 残り "+std::to_string(CityStepAlive())+"体 / 討伐 "+std::to_string(ink.swarmKills)+" / "+std::to_string(CityEnemyTotal()),42,120,15,UI::Muted,UI::JapaneseFont);
    ui.Text(life_.recipe.StyleName(),42,144,15,UI::Teal,UI::JapaneseFont);}
    // 取り込める大きさの説明 (小・中・大)
    ui.Rounded({22,198,340,88},{.025f,.045f,.035f,.8f},18);
    for(int tier=0;tier<3;++tier){float reward=12.f*float(tier+1),x=42+float(tier)*103;auto tint=LifeOrbColor(reward);
        ui.Text(std::string(tier==0?"小":tier==1?"中":"大")+" "+std::string(tier==0?"<18":tier==1?"18-29":"30+"),x,213,16,{tint.x,tint.y,tint.z,1},UI::JapaneseFont);}
    ui.Text(body.mass>=LifeContactAbsorbMass?"接触吸収 ON / 小さい物は通るだけ":"本体180から小さい物を接触吸収",42,248,14,body.mass>=LifeContactAbsorbMass?UI::Lime:UI::Muted,UI::JapaneseFont);
    // 閉じた門の体力
    if(!cityGates_.barricadeOpen||!cityGates_.gateOpen){
        bool barricade=!cityGates_.barricadeOpen;float health=barricade?cityBarricadeHealth_/CityBarricadeHealth():cityGateHealth_/CityGateHealth();
        ui.Rounded({22,300,340,64},{.025f,.045f,.035f,.8f},16);
        ui.Text(barricade?"工事バリケード：ハンマーで破壊":cityGateUnlocked_?"封鎖シャッター：消火栓の放水で破壊":"封鎖シャッター：騎士団が守っている",42,310,14,UI::Paper,UI::JapaneseFont);
        ui.Bar({42,340,290,7},health,barricade?UI::Gold:UI::Teal);}
    // 成長の段階・全体の量・本体の体力・子の数
    ui.Rounded({22,493,340,187},{.025f,.045f,.035f,.8f},20);
    float progression=LifeStartMass+life_.absorbedMass;
    const char* stage=progression>=LifePlaneMass?"LATE / SWARM":progression>=LifeHammerMass?"MID / MORPH":"EARLY / TACKLE";
    ui.Label(stage,42,505,14,UI::Gold);ui.Bar({42,530,290,7},std::min(1.f,life_.TotalMass()/LifeMaxMass),UI::Lime);
    ui.Label("BODY  "+std::to_string(int(body.mass))+"    GROUP  "+std::to_string(int(life_.TotalMass()))+" / "+std::to_string(int(LifeMaxMass)),42,553,16,UI::Lime);
    ui.Bar({42,584,270,10},body.health/std::max(1.f,body.maxHealth),UI::Teal);ui.Text("本体HP  "+std::to_string(int(std::ceil(body.health)))+" / "+std::to_string(int(body.maxHealth)),42,600,16,UI::Paper,UI::JapaneseFont);
    ui.Label("CHILDREN  "+std::to_string(life_.ChildCount())+" / "+std::to_string(life_.ChildLimit())+"    GROWTH +"+std::to_string(int(life_.absorbedMass)),42,643,13,UI::Muted);
    // 操作の説明 (パッドとキーボードで切り替える)
    ui.Rounded({385,588,865,100},{.025f,.045f,.035f,.8f},20);
    bool inWeaponForm=LifeWeaponForm(body.action);
    std::string attack=inWeaponForm?(body.action==LifeAction::Tire?"移動で転がる":body.action==LifeAction::Hammer?"連打（長押し可）":body.action==LifeAction::Sword?"横薙ぎ":"突進"):(body.mass<LifeHammerMass&&life_.form!=LifeAction::Tire)?"タックル":"変形";
    if(body.action==LifeAction::Hydrant)ui.Text(UI::UsingPad()?"RT 長押し 放水攻撃   X 長押し 水の防御   B 戻る":"左クリック長押し 放水攻撃   右クリック長押し 水の防御   R 戻る",405,600,14,UI::Paper,UI::JapaneseFont);
    else if(body.action==LifeAction::Tire)ui.Text(UI::UsingPad()?"左スティック 加速・旋回   B 戻る   A ジャンプ":"WASD 加速・旋回   R 戻る   Space ジャンプ   入力を離すと減速",405,600,14,UI::Paper,UI::JapaneseFont);
    else ui.Text(UI::UsingPad()?"RT "+attack+"   B 戻る   X 長押しでまとめ取り   A ジャンプ":"左クリック "+attack+"   R 戻る   右クリック長押しで吸収   Space ジャンプ",405,600,14,UI::Paper,UI::JapaneseFont);
    // 各変形の解放状況
    std::string gates;
    for(auto weapon:{LifeAction::Hammer,LifeAction::Sword,LifeAction::Plane,LifeAction::Tire,LifeAction::Hydrant}){const char* name=weapon==LifeAction::Hammer?"1 HAMMER":weapon==LifeAction::Sword?"2 SWORD":weapon==LifeAction::Plane?"3 PLANE":weapon==LifeAction::Tire?"4 TIRE":"5 HYDRANT";
        gates+=std::string(name)+(LifeCanMorph(body,weapon)?" OK  ":(weapon==LifeAction::Tire||weapon==LifeAction::Hydrant)?" LOCK: ITEM  ":" LOCK "+std::to_string(int(LifeRequiredMass(weapon)))+"  ");}
    ui.Label(gates,405,628,12,UI::Gold);
    // 今の形と選んでいる形
    const char* form=life_.form==LifeAction::Hydrant?"HYDRANT":life_.form==LifeAction::Tire?"TIRE":life_.form==LifeAction::Hammer?"HAMMER":life_.form==LifeAction::Sword?"SWORD":"PLANE";
    const char* current=body.action==LifeAction::Hydrant?"HYDRANT":body.action==LifeAction::Tire?"TIRE":body.action==LifeAction::Hammer?"HAMMER":body.action==LifeAction::Sword?"SWORD":body.action==LifeAction::Plane?"PLANE":"SLIME";
    std::string status=inWeaponForm?(!body.weapon.ready?" / FORMING":body.action==LifeAction::Tire?" / ROLL":body.weapon.active?" / ATTACK":" / READY"):"";
    ui.Label(std::string("FORM: ")+current+status+"   SELECT: "+form+"   1-5 / DPAD   TAB / BACK: LAB",405,657,12,UI::Lime);
    // 解放のお知らせかメッセージ
    if(life_.hydrantNotice>0)ui.Center(UI::UsingPad()?"消火栓変形 解放！ 十字キーで選択 → RTで変形 / Xで防御":"消火栓変形 解放！ 5で選択 → 左クリックで変形 / 右クリックで防御",640,185,18,UI::Gold,UI::JapaneseFont);
    else if(life_.tireNotice>0)ui.Center(UI::UsingPad()?"タイヤ変形 解放！ 十字キーで選択 → RTで変形 / Bで戻る":"タイヤ変形 解放！ 4で選択 → 左クリックで変形 / Rで戻る",640,185,18,UI::Gold,UI::JapaneseFont);
    else if(life_.messageAge>0)ui.Center(life_.message,640,185,16,UI::Gold,UI::JapaneseFont);
    // 右上の案内 (大通りでは消火栓、それ以外はタイヤ)
    ui.Rounded({902,26,350,70},{.025f,.045f,.035f,.85f},16);
    bool hydrantFocus=CityAreaAt(body.at)==CityArea::Street||body.action==LifeAction::Hydrant;
    if(hydrantFocus){ui.Text(body.hydrantUnlocked?"消火栓：水を飛ばして攻撃・防御":"金色の消火栓を吸収して変形を解放",918,39,16,UI::Gold,UI::JapaneseFont);
        ui.Text(body.hydrantUnlocked?(UI::UsingPad()?"RT 放水 / X 水の防御 / B 戻る":"5 変形選択 / 左 放水 / 右 水の防御"):"入口・歩道にある金色の消火栓を吸収",918,67,14,UI::Paper,UI::JapaneseFont);}
    else{ui.Text(body.tireUnlocked?"タイヤ変形 解放済み / 何度でも使える":"金色のハイライト ＝ 変形アイテム",918,39,16,UI::Gold,UI::JapaneseFont);
        ui.Text(body.tireUnlocked?(UI::UsingPad()?"十字キーでTIRE → RT / Bで戻る":"4 → 左クリックで変形 / Rで戻る"):(UI::UsingPad()?"タイヤを X 長押しで吸収して解放":"タイヤを右クリック長押しで吸収して解放"),918,67,14,UI::Paper,UI::JapaneseFont);}
    // 消火栓の照準: 届かない・遮られるときは金色にして着弾点に印を付ける
    if(hydrantAim_.active){
        bool obstructed=hydrantAim_.blocked||!hydrantAim_.reachable;
        Engine::Vector4 color=!body.weapon.ready?UI::Muted:obstructed?UI::Gold:hydrantAim_.enemy?Engine::Vector4{1,.36f,.24f,1}:Engine::Vector4{.65f,1,1,1};
        // 暗い縁で、空・ガラス・明るい水に対しても見やすくする
        for(auto bar:{UI::Rect{626,359,8,2},UI::Rect{646,359,8,2},UI::Rect{639,346,2,8},UI::Rect{639,366,2,8}}){
            ui.Fill({bar.x-1,bar.y-1,bar.w+2,bar.h+2},{.015f,.025f,.03f,.9f});ui.Fill(bar,color);
        }
        ui.Dot(640,360,2.8f,{.015f,.025f,.03f,.9f});ui.Dot(640,360,1.4f,color);
        if(obstructed){
            ui.Center(hydrantAim_.blocked?"射線が遮られています":"射程外",640,386,13,UI::Gold,UI::JapaneseFont);
            DirectX::XMFLOAT4 clip;const auto& point=hydrantAim_.impact;
            DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(point.x,point.y,point.z,1),ctx.camera->View()*ctx.camera->Proj()));
            if(clip.w>.1f){float x=(clip.x/clip.w*.5f+.5f)*1280,y=(.5f-clip.y/clip.w*.5f)*720;
                if(x>10&&x<1270&&y>110&&y<575){ui.Line(x-4,y-4,x+4,y+4,2,UI::Gold);ui.Line(x-4,y+4,x+4,y-4,2,UI::Gold);}}
        }
    }
    // 近くの変形アイテムの上に説明の札を出す
    const LifeCollectible* catalyst=nullptr;float nearest=70;
    for(const auto& prop:life_.props)if(prop.active&&prop.morphItem!=LifeMorphItem::None){float distance=LifeDistance(body.at,prop.at);if(distance<nearest){nearest=distance;catalyst=&prop;}}
    if(catalyst){DirectX::XMFLOAT4 clip;auto point=catalyst->at+V{0,6,0};
        DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(point.x,point.y,point.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>.1f){float x=(clip.x/clip.w*.5f+.5f)*1280,y=(.5f-clip.y/clip.w*.5f)*720;
            if(x>465&&x<1145&&y>120&&y<500){ui.Rounded({x-115,y-4,230,48},{.06f,.045f,.01f,.88f},12);
                bool hydrant=catalyst->morphItem==LifeMorphItem::Hydrant;bool unlocked=hydrant?body.hydrantUnlocked:body.tireUnlocked;
                ui.Center(hydrant?"消火栓 / 変形アイテム":"タイヤ / 変形アイテム",x,y+1,16,UI::Gold,UI::JapaneseFont);
                ui.Center(unlocked?"吸収してさらに成長":hydrant?"吸収すると放水攻撃・防御を解放":"吸収すると高速移動形態を解放",x,y+25,12,UI::Paper,UI::JapaneseFont);}}
    }
    // 正面の取り込み範囲に何かあれば、長押しのまとめ取りを説明する
    // 正面に取り込める物があれば長押しを案内する
    if(!life_.lab&&body.action==LifeAction::Idle){bool ready=false;for(const auto& prop:life_.props)if(prop.active&&LifeCanAbsorb(body.mass,prop.required)&&LifeInMouth(body.at,body.forward,prop.at,body.mass,0,life_.recipe))ready=true;
        for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&LifeCanAbsorb(body.mass,CityEnemyRequired(enemy),enemy.health,enemy.maxHealth)&&LifeInMouth(body.at,body.forward,enemy.at,body.mass,0,life_.recipe))ready=true;
        if(ready)ui.Center(std::string(UI::UsingPad()?"X 長押し":"右クリック長押し")+"で正面をまとめ取り / 移動中も回収",640,560,21,UI::Lime,UI::JapaneseFont);}
    // 研究所の UI はここから下
    return;
    }
    // ここから調合室の UI: レシピの選択・材料の配合・色の選択・性能・試しのボタン・保存と出発
    ui.Rounded({24,20,1232,104},{.025f,.045f,.035f,.92f},22);
    ui.Text("スライム調合室",48,34,32,UI::Paper,UI::JapaneseFont);
    ui.Text("混ぜる → 触る → 自分だけのスライムで出発",49,84,16,UI::Muted,UI::JapaneseFont);
    // ボタンを描いて押されたかを返す関数 / 配合を変えたら試しを止める関数
    auto button=[&](UI::Rect rect,const std::string& text,bool primary=false){ui.Button(rect,text,primary);return ui.Click(rect);};
    auto changed=[&](){if(life_.trial!=SlimeTrial::None||life_.previewChild.mass>0)StartSlimeTrial(SlimeTrial::None);life_.trialMessageAge=0;life_.previewMix=1.1f;life_.previewPoke=true;lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;};
    // レシピのスロットの切り替え
    for(int slot=0;slot<3;++slot)if(button({716+float(slot)*172,49,154,48},"RECIPE "+std::to_string(slot+1),slot==lifeRecipeSlot_)){lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;lifeRecipeSlot_=slot;life_.recipe=lifeRecipes_[size_t(slot)];changed();}
    // 材料を混ぜる: 4 つの材料の割合を +/- で変える (5% ずつ、合計は 100%)
    ui.Rounded({24,144,304,438},{.025f,.045f,.035f,.94f},22);ui.Text("材料を混ぜる",48,161,21,UI::Paper,UI::JapaneseFont);
    const char* names[]={"水","ホウ砂水","洗濯のり","泡"};const char* descriptions[]={"速い・広く包む / ちぎれやすい","高く跳ぶ・強い踏みつけ","取り込みが速い・本体を保つ","長い滞空・子が活発 / 本体は弱い"};const int order[]={2,0,1,3};
    for(int row=0;row<4;++row){int i=order[row];float y=206+float(row)*76;
        ui.Text(names[i],48,y,19,i<lifeMaterials_?UI::Paper:UI::Muted,UI::JapaneseFont);
        ui.Text(descriptions[i],48,y+27,12,UI::Muted,UI::JapaneseFont);
        if(i>=lifeMaterials_){ui.Text("5回取り込みで解放",48,y+49,13,UI::Muted,UI::JapaneseFont);continue;}
        ui.Label(std::to_string(int(life_.recipe.Ratio(i)*100))+"%",157,y+3,15,UI::Lime,0);
        ui.Bar({48,y+52,254,6},life_.recipe.Ratio(i),UI::Lime);
        if(button({222,y,35,35},"-")){auto& value=life_.recipe.mix[size_t(i)];if(value>=5){value-=5;int sum=0;for(int n:life_.recipe.mix)sum+=n;if(sum==0)value=5;changed();}}
        if(button({269,y,35,35},"+")){life_.recipe.mix[size_t(i)]=std::min(100,life_.recipe.mix[size_t(i)]+5);changed();}
    }
    // プリセットの配合
    if(button({48,529,116,35},"WATERY")){life_.recipe.mix={80,10,10,0};changed();}
    if(button({184,529,120,35},"BOUNCY")){life_.recipe.mix={10,65,25,0};changed();}
    if(button({48,497,116,26},"STICKY")){life_.recipe.mix={10,10,80,0};changed();}
    if(button({184,497,120,26},lifeMaterials_>=4?"FOAM":"FOAM: LOCK")&&lifeMaterials_>=4){life_.recipe.mix={20,10,20,50};changed();}
    // 色と手触り: 彩度と明るさの四角と色相の帯で色を選ぶ
    ui.Rounded({952,144,304,438},{.025f,.045f,.035f,.94f},22);ui.Text("色と手触り",976,161,21,UI::Paper,UI::JapaneseFont);
    // 色の選択 (彩度と明るさの四角・色相の帯)
    UI::Rect sv{976,203,252,128},hue{976,344,252,18};auto pointer=ui.Pointer();
    ui.Track(sv);ui.Track(hue);
    // マウスの押下 (ImGui の入力も見る)
    bool mouseDown=ctx.input&&ctx.input->IsMouseDown(0),mousePressed=ctx.input&&ctx.input->IsMouseTrigger(0);
#ifdef USE_IMGUI
    if(ImGui::GetCurrentContext()){mouseDown|=ImGui::GetIO().MouseDown[0];mousePressed|=ImGui::IsMouseClicked(ImGuiMouseButton_Left);}
#endif
    bool tintEdited=false;
    // マウスでドラッグして色を選ぶ
    if(!UI::UsingPad()){
        if(mousePressed){life_.colorDrag=0;if(ui.Click(sv))life_.colorDrag=1;else if(ui.Click(hue))life_.colorDrag=2;}
        if(mouseDown&&life_.colorDrag){auto& color=life_.recipe.color;
            if(life_.colorDrag==1){color.s=std::clamp((pointer[0]-sv.x)/sv.w,0.f,1.f);color.v=1-std::clamp((pointer[1]-sv.y)/sv.h,0.f,1.f);}
            else color.h=std::clamp((pointer[0]-hue.x)/hue.w,0.f,1.f);tintEdited=true;
        }else if(!mouseDown)life_.colorDrag=0;
    // パッドの右スティックで色を調整する
    }else if(ctx.input){float x=ctx.input->PadRX(),y=ctx.input->PadRY();
        if(ui.Focused(sv)){life_.recipe.color.s=std::clamp(life_.recipe.color.s+x*ctx.dt*.5f,0.f,1.f);life_.recipe.color.v=std::clamp(life_.recipe.color.v+y*ctx.dt*.5f,0.f,1.f);tintEdited=x!=0||y!=0;}
        if(ui.Focused(hue)){life_.recipe.color.h=std::clamp(life_.recipe.color.h+x*ctx.dt*.3f,0.f,1.f);tintEdited=x!=0;}}
    // 色を変えたらすぐ反映する
    if(tintEdited){lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;ctx.renderer->SetSlimeColor(LifeColor(life_.recipe.color));}
    // 色の四角と色相の帯を細かい矩形で描き、選んでいる位置に印を付ける
    for(int y=0;y<24;++y)for(int x=0;x<40;++x){SlimeColor sample{life_.recipe.color.h,(x+.5f)/40,1-(y+.5f)/24};ui.Fill({sv.x+sv.w*x/40,sv.y+sv.h*y/24,sv.w/40+.1f,sv.h/24+.1f},LifeColor(sample));}
    for(int x=0;x<64;++x)ui.Fill({hue.x+hue.w*x/64,hue.y,hue.w/64+.1f,hue.h},LifeColor(SlimeColor{(x+.5f)/64,1,1}));
    float markerX=sv.x+life_.recipe.color.s*sv.w,markerY=sv.y+(1-life_.recipe.color.v)*sv.h;
    ui.RoundedRing({markerX-5,markerY-5,10,10},UI::Paper,5);ui.RoundedRing({markerX-7,markerY-7,14,14},UI::Ink,7);
    ui.RoundedRing({hue.x+life_.recipe.color.h*hue.w-4,hue.y-3,8,hue.h+6},UI::Paper,3);
    // 色の 16 進表記
    auto rgb=life_.recipe.color.ToRGB();char hex[16];snprintf(hex,sizeof(hex),"#%02X%02X%02X",int(rgb.x*255+.5f),int(rgb.y*255+.5f),int(rgb.z*255+.5f));
    ui.Rounded({976,375,40,24},LifeColor(life_.recipe.color),6);ui.Label(hex,1030,379,17,UI::Paper,1);
    // 性能の表示 (文字と棒)
    auto stat=[&](const std::string& text,float y,float value){ui.Text(text,976,y,13,UI::Muted,UI::JapaneseFont);ui.Bar({976,y+22,252,6},value,UI::Teal);};
    const auto& recipe=life_.recipe;
    stat("移動速度 "+LifeNumber(recipe.Speed()),410,(recipe.Speed()-7.2f)/26.4f);
    stat("取り込み "+LifeNumber(recipe.AbsorbDuration())+"秒 / 距離 "+LifeNumber(recipe.AbsorbRange()),446,recipe.AbsorbRange()/14.8f);
    stat("踏みつけ "+LifeNumber(recipe.StompDamage())+" / 範囲 "+LifeNumber(recipe.StompRadius()),482,recipe.StompDamage()/54);
    stat("被ダメージ "+std::to_string(int(recipe.DamageMultiplier()*100))+"% / 子 "+LifeNumber(1/recipe.ChildCooldown())+"回/秒",518,recipe.DamageMultiplier()/1.8f);
    ui.Text(UI::UsingPad()?"右スティックで色を調整":"ドラッグで自由に色を選べます",976,551,12,UI::Paper,UI::JapaneseFont);
    // 型の名前と、試しのメッセージか得意なこと
    ui.Rounded({354,142,570,66},{.025f,.045f,.035f,.88f},18);ui.Center(recipe.StyleName(),640,149,20,UI::Paper,UI::JapaneseFont);
    ui.Center(life_.trialMessageAge>0?life_.trialMessage:std::string(recipe.Strength()),640,183,13,UI::Muted,UI::JapaneseFont);
    // 試しのボタン (つつく・跳ぶ・自動・移動・取り込み・被弾)
    if(button({354,521,170,32},"POKE"))life_.previewPoke=true;
    if(button({554,521,170,32},"JUMP")){life_.previewJump=true;life_.trialMessage="踏みつけ "+LifeNumber(recipe.StompDamage())+" / 空中時間 "+LifeNumber(2*recipe.Bounce()/recipe.Gravity())+"秒";life_.trialMessageAge=3;}
    if(button({754,521,170,32},life_.previewAuto?"AUTO: ON":"AUTO: OFF"))life_.previewAuto=!life_.previewAuto;
    if(button({354,560,170,32},"MOVE"))StartSlimeTrial(SlimeTrial::Move);
    if(button({554,560,170,32},"ABSORB"))StartSlimeTrial(SlimeTrial::Absorb);
    if(button({754,560,170,32},"HIT: 8"))StartSlimeTrial(SlimeTrial::Hit);
    ui.Rounded({24,604,1232,96},{.025f,.045f,.035f,.96f},22);
    // 保存・試遊に戻る・保存して出発
    if(button({48,624,238,53},"SAVE RECIPE",true)||UI::PadPressed(XINPUT_GAMEPAD_X)){lifeRecipes_[size_t(lifeRecipeSlot_)]=life_.recipe;if(SaveSlimeLab()){life_.message="RECIPE SAVED";life_.messageAge=2;}}
    if(button({690,624,238,53},"TRY / RESUME")||UI::PadPressed(XINPUT_GAMEPAD_Y))SetSlimeLabOpen(false);
    if(button({954,624,278,53},"SAVE & DEPLOY",true))life_.deployRequested=true;
    // 注意と説明
    ui.Text(recipe.Weakness(),309,629,12,UI::Gold,UI::JapaneseFont);
    ui.Text("試遊のHP・大きさは本番に影響しません",309,653,12,UI::Muted,UI::JapaneseFont);
    if(life_.messageAge>0)ui.LabelCenter(life_.message,640,596,13,UI::Gold,1);
}
}
