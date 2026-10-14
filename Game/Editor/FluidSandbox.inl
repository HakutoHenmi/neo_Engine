// Included inside namespace Game in EditorUI.cpp.
static bool fluidSandboxOpen = false;
void EditorUI::OpenFluidSandbox() { fluidSandboxOpen = true; }

static bool IsFluidTank(SceneComponents& registry, entt::entity entity) {
    const auto* data = registry.try_get<VariableComponent>(entity);
    return data && data->GetValue("fluidTank") == 1;
}

static void FocusFluidTank(GameScene* scene, entt::entity tank) {
    const auto matrix = scene->GetWorldMatrix(static_cast<int>(tank));
    const auto& data = scene->GetComponents().get<VariableComponent>(tank);
    const float extent = (std::max)(data.GetValue("width", 6), data.GetValue("depth", 5));
    const float x=matrix.m[3][0], y=matrix.m[3][1], z=matrix.m[3][2];
    auto& camera = scene->GetCamera();
    camera.SetPosition(x+extent*1.3f, y+extent, z-extent*1.7f);
    const float dx=-extent*1.3f, dz=extent*1.7f;
    camera.SetRotation(std::atan2(extent-data.GetValue("height",5)*.4f,std::sqrt(dx*dx+dz*dz)),std::atan2(dx,dz),0);
}

static entt::entity CreateWaterEmitter(GameScene* scene, entt::entity parent, float height) {
    auto& registry = scene->GetComponents();
    const auto entity = scene->CreateEntity("Water Emitter");
    registry.get<TransformComponent>(entity).translate = {0, height+1, 0};
    registry.get_or_emplace<HierarchyComponent>(entity).parentId = parent;
    auto& emitter = registry.emplace<FluidEmitterComponent>(entity);
    emitter.emitCountPerFrame = 20;
    emitter.velocity = {0, -.8f, 0}; // Solver API uses velocity * 5.
    emitter.retainWater = true;
    emitter.particleLimit = 16000;
    emitter.enabled = false; // Start explicitly after placing the nozzle.
    scene->SelectEntity(entity);
    return entity;
}

static void UpdateFluidTank(GameScene* scene, entt::entity tank, const DirectX::XMFLOAT4& tint) {
    auto& registry = scene->GetComponents();
    auto& data = registry.get<VariableComponent>(tank);
    const auto panels = Engine::MakeFluidTankPanels(data.GetValue("width",6), data.GetValue("height",5),
                                                  data.GetValue("depth",5), data.GetValue("thickness",.25f));
    auto children = registry.view<HierarchyComponent, TransformComponent, VariableComponent, MeshRendererComponent>();
    for (auto child : children) {
        if (children.get<HierarchyComponent>(child).parentId != tank) continue;
        const auto& panelData = children.get<VariableComponent>(child);
        const int index = static_cast<int>(panelData.GetValue("fluidTankPanel", -1));
        if (index<0 || index>=static_cast<int>(panels.size())) continue;
        auto& transform = children.get<TransformComponent>(child);
        transform.translate = {panels[index].center.x,panels[index].center.y,panels[index].center.z};
        transform.scale = {panels[index].size.x*.5f,panels[index].size.y*.5f,panels[index].size.z*.5f};
        transform.rotate = {};
        children.get<MeshRendererComponent>(child).color = tint;
    }
}

void EditorUI::ShowFluidSandbox(Engine::Renderer* renderer, GameScene* scene) {
    if (!renderer || !scene) return;
    auto& registry = scene->GetComponents();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .48f);
    ImGui::TextWrapped("Glass tank and water preview. Works while the editor is stopped.");
    ImGui::BeginDisabled(scene->GetIsPlaying());
    if (ImGui::Button("Open standalone sandbox scene")) {
        const auto before = SaveToMemory(scene);
        const auto oldPath = currentScenePath;
        LoadScene(scene, "Resources/Scenes/fluid_sandbox.json");
        scene->ResetFluidPreview();
        const auto after = SaveToMemory(scene);
        PushUndo({"Open fluid sandbox", [=]() { LoadFromMemory(scene,before); currentScenePath=oldPath; },
                  [=]() { LoadFromMemory(scene,after); currentScenePath="Resources/Scenes/fluid_sandbox.json"; }});
        auto tanks = registry.view<VariableComponent>();
        for (auto tank : tanks) if (IsFluidTank(registry,tank)) {
            scene->SelectEntity(tank); FocusFluidTank(scene,tank); break;
        }
    }
    ImGui::TextWrapped("Replaces the editor scene. Undo restores it; files are saved only with Save.");
    static float dimensions[3] = {6,5,5};
    static float thickness = .25f;
    static DirectX::XMFLOAT3 position = {0,.6f,0};
    ImGui::SeparatorText("Create");
    ImGui::DragFloat3("Inner size (W/H/D)", dimensions, .1f, 2, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Wall thickness", &thickness, .01f, .2f, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat3("Tank origin", &position.x, .1f);
    if (ImGui::Button("Create tank + emitter")) {
        const auto before = SaveToMemory(scene);
        const auto tank = scene->CreateEntity("Glass Tank");
        registry.get<TransformComponent>(tank).translate = position;
        auto& data = registry.emplace<VariableComponent>(tank);
        data.values = {{"fluidTank",1.f},{"width",dimensions[0]},{"height",dimensions[1]},
                       {"depth",dimensions[2]},{"thickness",thickness}};
        const char* names[] = {"Glass Bottom","Glass Left","Glass Right","Glass Back","Glass Front"};
        for (int i=0; i<5; ++i) {
            const auto child = scene->CreateEntity(names[i]);
            registry.emplace<HierarchyComponent>(child).parentId = tank;
            registry.emplace<VariableComponent>(child).values["fluidTankPanel"] = static_cast<float>(i);
            auto& mesh = registry.emplace<MeshRendererComponent>(child);
            mesh.modelPath = "Resources/Models/cube/cube.obj";
            mesh.modelHandle = renderer->LoadObjMesh(mesh.modelPath);
            mesh.shaderName = "Glass";
            mesh.reflectivity = .65f;
            registry.emplace<BoxColliderComponent>(child).size = {2,2,2};
            scene->SetTag(child, TagType::Wall);
        }
        UpdateFluidTank(scene,tank,{.72f,.9f,1,.08f});
        CreateWaterEmitter(scene,tank,dimensions[1]);
        FocusFluidTank(scene,tank);
        const auto after = SaveToMemory(scene);
        PushUndo({"Create fluid tank", [=]() { LoadFromMemory(scene,before); }, [=]() { LoadFromMemory(scene,after); }});
    }
    auto selected = scene->GetSelectedEntity();
    entt::entity selectedTank = entt::null;
    if (registry.valid(selected)) {
        if (IsFluidTank(registry,selected)) selectedTank=selected;
        else if (auto* hierarchy=registry.try_get<HierarchyComponent>(selected);
                 hierarchy && registry.valid(hierarchy->parentId) && IsFluidTank(registry,hierarchy->parentId)) selectedTank=hierarchy->parentId;
    }
    if (ImGui::Button("Create water emitter")) {
        const auto before = SaveToMemory(scene);
        const float height = registry.valid(selectedTank) ? registry.get<VariableComponent>(selectedTank).GetValue("height",5) : dimensions[1];
        const auto emitter = CreateWaterEmitter(scene,selectedTank,height);
        if (!registry.valid(selectedTank)) registry.get<TransformComponent>(emitter).translate = {position.x,position.y+height+1,position.z};
        const auto after = SaveToMemory(scene);
        PushUndo({"Create water emitter", [=]() { LoadFromMemory(scene,before); }, [=]() { LoadFromMemory(scene,after); }});
    }
    if (ImGui::BeginCombo("Select emitter", registry.valid(selected) && registry.all_of<FluidEmitterComponent>(selected) ?
                          registry.get<NameComponent>(selected).name.c_str() : "Choose water emitter")) {
        auto emitters=registry.view<FluidEmitterComponent,NameComponent>();
        for (auto emitter : emitters) {
            ImGui::PushID(static_cast<int>(entt::to_integral(emitter)));
            if (ImGui::Selectable(emitters.get<NameComponent>(emitter).name.c_str(),emitter==selected)) scene->SelectEntity(emitter);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    selected=scene->GetSelectedEntity();
    if (registry.valid(selected)) if (auto* emitter=registry.try_get<FluidEmitterComponent>(selected)) {
        ImGui::SeparatorText("Water emitter");
        ImGui::Checkbox("Emit water", &emitter->enabled);
        auto& transform=registry.get<TransformComponent>(selected);
        ImGui::DragFloat3("Nozzle position (local)", &transform.translate.x, .1f);
        int rate = emitter->emitCountPerFrame * 60;
        if (ImGui::DragInt("Particles / second", &rate,60,0,6000,"%d",ImGuiSliderFlags_AlwaysClamp)) emitter->emitCountPerFrame = (rate+30)/60;
        ImGui::DragInt("Total particle limit", &emitter->particleLimit,100,1,25000,"%d",ImGuiSliderFlags_AlwaysClamp);
        ImGui::Text("Emitted: %d / %d",emitter->emittedParticles,emitter->particleLimit);
        Engine::Vector3 velocity={emitter->velocity.x*5,emitter->velocity.y*5,emitter->velocity.z*5};
        if (ImGui::DragFloat3("Velocity (units/s)",&velocity.x,.1f,-25,25,"%.2f",ImGuiSliderFlags_AlwaysClamp)) emitter->velocity={velocity.x/5,velocity.y/5,velocity.z/5};
        ImGui::ColorEdit4("Water color",&emitter->color.x);
        ImGui::Checkbox("Keep settled water",&emitter->retainWater);
        ImGui::TextWrapped("Retention applies to newly emitted water. At the limit emission stops; reset to refill.");
    }
    if (registry.valid(selectedTank)) {
        ImGui::SeparatorText("Selected glass tank");
        auto& data=registry.get<VariableComponent>(selectedTank);
        float size[]={data.GetValue("width",6),data.GetValue("height",5),data.GetValue("depth",5)};
        float wall=data.GetValue("thickness",.25f);
        bool changed=ImGui::DragFloat3("Tank inner size",size,.1f,2,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        changed|=ImGui::DragFloat("Tank wall thickness",&wall,.01f,.2f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        DirectX::XMFLOAT4 tint={.72f,.9f,1,.08f};
        auto meshes=registry.view<HierarchyComponent,MeshRendererComponent>();
        for (auto child:meshes) if(meshes.get<HierarchyComponent>(child).parentId==selectedTank) {tint=meshes.get<MeshRendererComponent>(child).color;break;}
        changed|=ImGui::ColorEdit4("Glass tint / opacity",&tint.x);
        if(changed) {
            data.values["width"]=size[0];data.values["height"]=size[1];data.values["depth"]=size[2];data.values["thickness"]=wall;
            UpdateFluidTank(scene,selectedTank,tint);
        }
        ImGui::DragFloat3("Tank position",&registry.get<TransformComponent>(selectedTank).translate.x,.1f);
        ImGui::TextWrapped("Keep the tank axis aligned: fluid collisions use world AABBs. Reset after changing its geometry.");
        if(ImGui::Button("Focus tank")) FocusFluidTank(scene,selectedTank);
    }
    ImGui::Separator();
    if(ImGui::Button("Stop all emitters")) {
        auto emitters=registry.view<FluidEmitterComponent>();
        for(auto emitter:emitters) emitters.get<FluidEmitterComponent>(emitter).enabled=false;
    }
    if(ImGui::Button("Reset fluid and emission counts")) scene->ResetFluidPreview();
    ImGui::TextWrapped("Reset clears all simulated fluid, including the player preview. Enabled emitters refill immediately. Water shares the finite 25,000-particle effect pool.");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
}
