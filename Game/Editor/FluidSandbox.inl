// FluidSandbox.inl
// エディタの Fluid Sandbox ウィンドウ: ガラスの水槽と水のエミッターを作って流体を試す。
// Fluid Sandbox ウィンドウを開く要求 (検証シーンなどから呼ばれる)
static bool fluidSandboxOpen = false;
void EditorUI::OpenFluidSandbox() { fluidSandboxOpen = true; }

// 水槽のエンティティか (VariableComponent の fluidTank が 1)
static bool IsFluidTank(SceneComponents& registry, entt::entity entity) {
    // 汎用の変数のコンポーネント (無ければ水槽ではない)
    const auto* data = registry.try_get<VariableComponent>(entity);
    return data && data->GetValue("fluidTank") == 1;
}

// 水槽全体が見える位置へカメラを動かす
static void FocusFluidTank(GameScene* scene, entt::entity tank) {
    // 水槽のワールド行列と、大きさの情報
    const auto matrix = scene->GetWorldMatrix(static_cast<int>(tank));
    const auto& data = scene->GetComponents().get<VariableComponent>(tank);
    // 幅と奥行きの大きい方
    const float extent = (std::max)(data.GetValue("width", 6), data.GetValue("depth", 5));
    // 水槽の位置
    const float x=matrix.m[3][0], y=matrix.m[3][1], z=matrix.m[3][2];
    // エディタで使っているカメラ
    auto& camera = scene->GetViewCamera();
    // 斜め前の上に置く
    camera.SetPosition(x+extent*1.3f, y+extent, z-extent*1.7f);
    // カメラから水槽への水平の向き
    const float dx=-extent*1.3f, dz=extent*1.7f;
    // 水槽の方へ見下ろすように向ける
    camera.SetRotation(std::atan2(extent-data.GetValue("height",5)*.4f,std::sqrt(dx*dx+dz*dz)),std::atan2(dx,dz),0);
}

// 水槽の上に水のエミッターを作る (最初は止めておく)
static entt::entity CreateWaterEmitter(GameScene* scene, entt::entity parent, float height) {
    // このシーンの ECS
    auto& registry = scene->GetComponents();
    // 名前と位置を付けて作る
    const auto entity = scene->CreateEntity("Water Emitter");
    // 水槽の上 1 の高さに置く
    registry.get<TransformComponent>(entity).translate = {0, height+1, 0};
    // 水槽の子にする
    registry.get_or_emplace<HierarchyComponent>(entity).parentId = parent;
    // 流体エミッター: 1 フレーム 20 粒
    auto& emitter = registry.emplace<FluidEmitterComponent>(entity);
    emitter.emitCountPerFrame = 20;
    emitter.velocity = {0, -.8f, 0}; // ソルバー側では速度を 5 倍して使う
    // 落ち着いた水を残す / 合計 16000 粒まで
    emitter.retainWater = true;
    emitter.particleLimit = 16000;
    emitter.enabled = false; // ノズルを置いてから明示的に開始する
    // 作ったものを選ぶ
    scene->SelectEntity(entity);
    return entity;
}

// 水槽の大きさ・壁の厚さに合わせて5枚のガラス板の位置と大きさを更新する
static void UpdateFluidTank(GameScene* scene, entt::entity tank, const DirectX::XMFLOAT4& tint) {
    // このシーンの ECS
    auto& registry = scene->GetComponents();
    // 水槽の大きさの情報
    auto& data = registry.get<VariableComponent>(tank);
    // 5 枚のガラス板の中心と大きさを求める
    const auto panels = Engine::MakeFluidTankPanels(data.GetValue("width",6), data.GetValue("height",5),
                                                  data.GetValue("depth",5), data.GetValue("thickness",.25f));
    // 水槽の子のうち、板の番号を持つものを順に更新する
    auto children = registry.view<HierarchyComponent, TransformComponent, VariableComponent, MeshRendererComponent>();
    for (auto child : children) {
        // この水槽の子でなければ飛ばす
        if (children.get<HierarchyComponent>(child).parentId != tank) continue;
        const auto& panelData = children.get<VariableComponent>(child);
        // 板の番号 (範囲外なら飛ばす)
        const int index = static_cast<int>(panelData.GetValue("fluidTankPanel", -1));
        if (index<0 || index>=static_cast<int>(panels.size())) continue;
        // 板の位置と大きさ (立方体は ±1 なので半分にする)
        auto& transform = children.get<TransformComponent>(child);
        transform.translate = {panels[index].center.x,panels[index].center.y,panels[index].center.z};
        transform.scale = {panels[index].size.x*.5f,panels[index].size.y*.5f,panels[index].size.z*.5f};
        transform.rotate = {};
        // ガラスの色
        children.get<MeshRendererComponent>(child).color = tint;
    }
}

// Fluid Sandbox ウィンドウの中身
void EditorUI::ShowFluidSandbox(Engine::Renderer* renderer, GameScene* scene) {
    // 描画の窓口かシーンが無ければ何もしない
    if (!renderer || !scene) return;
    // このシーンの ECS
    auto& registry = scene->GetComponents();
    // 入力欄の幅をウィンドウの半分くらいにする
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .48f);
    // 説明 (エディタが停止中のときに使える)
    ImGui::TextWrapped("Glass tank and water preview. Works while the editor is stopped.");
    // PLAY 中は操作できないようにする
    ImGui::BeginDisabled(scene->GetIsPlaying());
    // 水槽の入ったシーン (fluid_sandbox.json) を開く (Undo で元のシーンに戻せる)
    if (ImGui::Button("Open standalone sandbox scene")) {
        // 今のシーンとパスを覚えておく
        const auto before = SaveToMemory(scene);
        const auto oldPath = currentScenePath;
        // サンドボックスのシーンを読み込み、流体を空にする
        LoadScene(scene, "Resources/Scenes/fluid_sandbox.json");
        scene->ResetFluidPreview();
        // Undo / Redo を登録する
        const auto after = SaveToMemory(scene);
        PushUndo({"Open fluid sandbox", [=]() { LoadFromMemory(scene,before); currentScenePath=oldPath; },
                  [=]() { LoadFromMemory(scene,after); currentScenePath="Resources/Scenes/fluid_sandbox.json"; }});
        // 水槽を探して選び、カメラを向ける
        auto tanks = registry.view<VariableComponent>();
        for (auto tank : tanks) if (IsFluidTank(registry,tank)) {
            scene->SelectEntity(tank); FocusFluidTank(scene,tank); break;
        }
    }
    // 注意: 今のシーンを置き換える
    ImGui::TextWrapped("Replaces the editor scene. Undo restores it; files are saved only with Save.");
    // 新しく作る水槽の大きさ・壁の厚さ・位置
    static float dimensions[3] = {6,5,5};
    static float thickness = .25f;
    static DirectX::XMFLOAT3 position = {0,.6f,0};
    ImGui::SeparatorText("Create");
    ImGui::DragFloat3("Inner size (W/H/D)", dimensions, .1f, 2, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Wall thickness", &thickness, .01f, .2f, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat3("Tank origin", &position.x, .1f);
    // 水槽 (底と4枚の壁) と水のエミッターを作る
    if (ImGui::Button("Create tank + emitter")) {
        // 作る前の状態を覚えておく
        const auto before = SaveToMemory(scene);
        // 水槽の親を作り、位置と大きさの情報を付ける
        const auto tank = scene->CreateEntity("Glass Tank");
        registry.get<TransformComponent>(tank).translate = position;
        auto& data = registry.emplace<VariableComponent>(tank);
        data.values = {{"fluidTank",1.f},{"width",dimensions[0]},{"height",dimensions[1]},
                       {"depth",dimensions[2]},{"thickness",thickness}};
        // 板の名前 (底・左・右・奥・手前)
        const char* names[] = {"Glass Bottom","Glass Left","Glass Right","Glass Back","Glass Front"};
        for (int i=0; i<5; ++i) {
            // 板を作って水槽の子にし、板の番号を付ける
            const auto child = scene->CreateEntity(names[i]);
            registry.emplace<HierarchyComponent>(child).parentId = tank;
            registry.emplace<VariableComponent>(child).values["fluidTankPanel"] = static_cast<float>(i);
            // 立方体のモデルをガラスのシェーダーで描く
            auto& mesh = registry.emplace<MeshRendererComponent>(child);
            mesh.modelPath = "Resources/Models/cube/cube.obj";
            mesh.modelHandle = renderer->LoadObjMesh(mesh.modelPath);
            mesh.shaderName = "Glass";
            mesh.reflectivity = .65f;
            // 箱の当たり判定と壁のタグ
            registry.emplace<BoxColliderComponent>(child).size = {2,2,2};
            scene->SetTag(child, TagType::Wall);
        }
        // 板を配置し、エミッターを作ってカメラを向ける
        UpdateFluidTank(scene,tank,{.72f,.9f,1,.08f});
        CreateWaterEmitter(scene,tank,dimensions[1]);
        FocusFluidTank(scene,tank);
        // Undo / Redo を登録する
        const auto after = SaveToMemory(scene);
        PushUndo({"Create fluid tank", [=]() { LoadFromMemory(scene,before); }, [=]() { LoadFromMemory(scene,after); }});
    }
    // 選択中の物、またはその親が水槽なら、その水槽を対象にする
    auto selected = scene->GetSelectedEntity();
    entt::entity selectedTank = entt::null;
    if (registry.valid(selected)) {
        if (IsFluidTank(registry,selected)) selectedTank=selected;
        else if (auto* hierarchy=registry.try_get<HierarchyComponent>(selected);
                 hierarchy && registry.valid(hierarchy->parentId) && IsFluidTank(registry,hierarchy->parentId)) selectedTank=hierarchy->parentId;
    }
    // 水のエミッターだけを作る (水槽を選んでいればその上に)
    if (ImGui::Button("Create water emitter")) {
        // 作る前の状態を覚えておく
        const auto before = SaveToMemory(scene);
        // 水槽があればその高さ、無ければ入力した高さ
        const float height = registry.valid(selectedTank) ? registry.get<VariableComponent>(selectedTank).GetValue("height",5) : dimensions[1];
        // エミッターを作る
        const auto emitter = CreateWaterEmitter(scene,selectedTank,height);
        // 水槽が無ければ入力した位置の上に置く
        if (!registry.valid(selectedTank)) registry.get<TransformComponent>(emitter).translate = {position.x,position.y+height+1,position.z};
        // Undo / Redo を登録する
        const auto after = SaveToMemory(scene);
        PushUndo({"Create water emitter", [=]() { LoadFromMemory(scene,before); }, [=]() { LoadFromMemory(scene,after); }});
    }
    // シーン内の水のエミッターから選ぶ
    if (ImGui::BeginCombo("Select emitter", registry.valid(selected) && registry.all_of<FluidEmitterComponent>(selected) ?
                          registry.get<NameComponent>(selected).name.c_str() : "Choose water emitter")) {
        // 流体エミッターと名前を持つものを並べ、選んだら選択する
        auto emitters=registry.view<FluidEmitterComponent,NameComponent>();
        for (auto emitter : emitters) {
            ImGui::PushID(static_cast<int>(entt::to_integral(emitter)));
            if (ImGui::Selectable(emitters.get<NameComponent>(emitter).name.c_str(),emitter==selected)) scene->SelectEntity(emitter);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    // 選び直した結果を読み直す
    selected=scene->GetSelectedEntity();
    // 選択中の水のエミッターの設定 (放出・ノズル位置・量・上限・速度・色・水を残すか)
    if (registry.valid(selected)) if (auto* emitter=registry.try_get<FluidEmitterComponent>(selected)) {
        ImGui::SeparatorText("Water emitter");
        // 放出するか
        ImGui::Checkbox("Emit water", &emitter->enabled);
        // ノズルの位置 (親から見た位置)
        auto& transform=registry.get<TransformComponent>(selected);
        ImGui::DragFloat3("Nozzle position (local)", &transform.translate.x, .1f);
        // 1 秒あたりの粒の数 (1 フレームの数 × 60)
        int rate = emitter->emitCountPerFrame * 60;
        if (ImGui::DragInt("Particles / second", &rate,60,0,6000,"%d",ImGuiSliderFlags_AlwaysClamp)) emitter->emitCountPerFrame = (rate+30)/60;
        // 合計の上限と、今まで出した数
        ImGui::DragInt("Total particle limit", &emitter->particleLimit,100,1,25000,"%d",ImGuiSliderFlags_AlwaysClamp);
        ImGui::Text("Emitted: %d / %d",emitter->emittedParticles,emitter->particleLimit);
        // 速度 (内部の値は 5 倍して使うので、表示は 5 倍にする)
        Engine::Vector3 velocity={emitter->velocity.x*5,emitter->velocity.y*5,emitter->velocity.z*5};
        if (ImGui::DragFloat3("Velocity (units/s)",&velocity.x,.1f,-25,25,"%.2f",ImGuiSliderFlags_AlwaysClamp)) emitter->velocity={velocity.x/5,velocity.y/5,velocity.z/5};
        // 水の色と、落ち着いた水を残すか
        ImGui::ColorEdit4("Water color",&emitter->color.x);
        ImGui::Checkbox("Keep settled water",&emitter->retainWater);
        ImGui::TextWrapped("Retention applies to newly emitted water. At the limit emission stops; reset to refill.");
    }
    // 選択中の水槽の設定 (内寸・壁の厚さ・ガラスの色・位置)
    if (registry.valid(selectedTank)) {
        ImGui::SeparatorText("Selected glass tank");
        // 水槽の大きさの情報
        auto& data=registry.get<VariableComponent>(selectedTank);
        // 内寸と壁の厚さ
        float size[]={data.GetValue("width",6),data.GetValue("height",5),data.GetValue("depth",5)};
        float wall=data.GetValue("thickness",.25f);
        // 変更されたか
        bool changed=ImGui::DragFloat3("Tank inner size",size,.1f,2,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        changed|=ImGui::DragFloat("Tank wall thickness",&wall,.01f,.2f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        // ガラスの色 (子の板があればその色)
        DirectX::XMFLOAT4 tint={.72f,.9f,1,.08f};
        auto meshes=registry.view<HierarchyComponent,MeshRendererComponent>();
        for (auto child:meshes) if(meshes.get<HierarchyComponent>(child).parentId==selectedTank) {tint=meshes.get<MeshRendererComponent>(child).color;break;}
        changed|=ImGui::ColorEdit4("Glass tint / opacity",&tint.x);
        // 変更されたら情報を書き換え、板を配置し直す
        if(changed) {
            data.values["width"]=size[0];data.values["height"]=size[1];data.values["depth"]=size[2];data.values["thickness"]=wall;
            UpdateFluidTank(scene,selectedTank,tint);
        }
        // 水槽の位置
        ImGui::DragFloat3("Tank position",&registry.get<TransformComponent>(selectedTank).translate.x,.1f);
        ImGui::TextWrapped("Keep the tank axis aligned: fluid collisions use world AABBs. Reset after changing its geometry.");
        // 水槽へカメラを向ける
        if(ImGui::Button("Focus tank")) FocusFluidTank(scene,selectedTank);
    }
    ImGui::Separator();
    // 全エミッターの停止と、流体と放出数のリセット
    if(ImGui::Button("Stop all emitters")) {
        // 流体エミッターをすべて止める
        auto emitters=registry.view<FluidEmitterComponent>();
        for(auto emitter:emitters) emitters.get<FluidEmitterComponent>(emitter).enabled=false;
    }
    // 流体と放出数をリセットする
    if(ImGui::Button("Reset fluid and emission counts")) scene->ResetFluidPreview();
    ImGui::TextWrapped("Reset clears all simulated fluid, including the player preview. Enabled emitters refill immediately. Water shares the finite 25,000-particle effect pool.");
    // PLAY 中の無効化と入力欄の幅を元に戻す
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
}
