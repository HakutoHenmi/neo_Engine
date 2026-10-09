#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Game/Scripts/ScriptEngine.h"
#include "../Game/Scripts/ScriptEdits.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <functional>

// Opt-in diagnostics use an isolated, temporary scene and never save Resources.
class ComponentValidationScene final : public Game::GameScene {
    class CleanupProbe final : public Game::IScript {
    public:
        int& starts;
        int& stops;
        std::function<void(entt::entity, Game::GameScene*)> cleanup;
        CleanupProbe(int& startCount, int& stopCount) : starts(startCount), stops(stopCount) {}
        void Start(entt::entity, Game::GameScene*) override { ++starts; }
        void OnDestroy(entt::entity entity, Game::GameScene* scene) override {
            ++stops;
            if (cleanup) cleanup(entity, scene);
        }
    };
    bool interactive_;
    bool passed_ = true;
    unsigned frames_ = 0;
    int starts_ = 0, stops_ = 0;
    Engine::WindowDX* window_ = nullptr;
    std::ofstream report_;

    void Check(bool condition, const char* name) {
        passed_ &= condition;
        report_ << (condition ? "PASS " : "FAIL ") << name << '\n';
        report_.flush();
    }
    std::shared_ptr<CleanupProbe> Attach(entt::entity entity, const char* name) {
        auto script = std::make_shared<CleanupProbe>(starts_, stops_);
        GetComponents().get_or_emplace<Game::ScriptComponent>(entity).scripts.push_back({name, "{}", script, false});
        Game::ScriptEngine::GetInstance()->Execute(entity, this, 1.f / 60.f);
        return script;
    }
public:
    explicit ComponentValidationScene(bool interactive = false) : interactive_(interactive) {}
    ~ComponentValidationScene() override {
        // Clean up probes while the counters referenced by their callbacks live.
        ClearScene();
    }
    void Initialize(Engine::WindowDX* window, const Engine::SceneParameters& params) override {
        window_ = window;
        std::filesystem::create_directories("tests/out");
        std::ofstream("tests/out/component-editor.json") << R"({"objects":[{"id":1,"name":"ComponentProbe","components":[]}]})";
        auto diagnostic = params;
        diagnostic.stagePath = "tests/out/component-editor.json";
        Game::GameScene::Initialize(window, diagnostic);
        SetPaused(true);
        report_.open("tests/out/component-validation.txt");
        auto entity = FindObjectByName("ComponentProbe");
        auto& components = GetComponents();
        auto& health = components.emplace<Game::HealthComponent>(entity);
        health.SetHp(75);
        health.enabled = false;
        Check(health.Hp() == 75 && !health.enabled, "health add/edit/disable");
        components.emplace<Game::BoxColliderComponent>(entity).size = {2, 3, 4};
        Check(components.get<Game::BoxColliderComponent>(entity).size.y == 3, "collider add/edit");
        components.remove<Game::BoxColliderComponent>(entity);
        Check(!components.all_of<Game::BoxColliderComponent>(entity), "collider remove");

        auto script = Attach(entity, "replacement-probe");
        Game::ScriptEdit replace{Game::ScriptEditKind::Replace, entity, 0, "replacement-probe", script, "BaseScript"};
        Check(Game::ApplyScriptEdit(components, replace, this) && starts_ == 1 && stops_ == 1, "started script replacement cleanup once");
        Check(!components.get<Game::ScriptComponent>(entity).scripts[0].isStarted, "replacement starts fresh");
        Check(!Game::ApplyScriptEdit(components, replace, this), "stale edit rejected");
        components.remove<Game::ScriptComponent>(entity);

        script = Attach(entity, "remove-own-component");
        script->cleanup = [](entt::entity e, Game::GameScene* scene) { scene->GetComponents().remove<Game::ScriptComponent>(e); };
        Check(Game::ApplyScriptEdit(components, {Game::ScriptEditKind::Replace, entity, 0, "remove-own-component", script, "BaseScript"}, this), "replacement with reentrant component removal");
        Check(stops_ == 2 && !components.all_of<Game::ScriptComponent>(entity), "removed component not resurrected");

        script = Attach(entity, "remove-probe");
        Check(Game::ApplyScriptEdit(components, {Game::ScriptEditKind::RemoveComponent, entity, 0, {}, {}, {}}, this) && stops_ == 3, "component removal cleanup once through scene signals");
        Check(!components.all_of<Game::ScriptComponent>(entity), "script component removed");
        script = Attach(entity, "clear-probe");
        components.get<Game::ScriptComponent>(entity).scripts[0].scriptPath = "BaseScript";
        const std::string snapshot = Game::EditorUI::SaveToMemory(this);
        Game::EditorUI::LoadFromMemory(this, snapshot);
        Check(stops_ == 4, "scene reload cleanup once");
        entity = FindObjectByName("ComponentProbe");
        Check(components.valid(entity) && components.get<Game::HealthComponent>(entity).Hp() == 75 &&
              !components.get<Game::HealthComponent>(entity).enabled, "component values survive memory roundtrip");
        components.remove<Game::ScriptComponent>(entity); // Restored probes have not started.
        Check(stops_ == 4, "unstarted restored instance requires no cleanup");
        components.emplace<Game::BoxColliderComponent>(entity).size = {2, 3, 4};
        auto& entry = components.emplace<Game::ScriptComponent>(entity).scripts.emplace_back();
        entry.scriptPath = "BaseScript";
        entry.parameterData = "{}";
        SelectEntity(entity);
    }
    void Update() override {
        Game::GameScene::Update();
        if (++frames_ != 30) return;
        DirectX::ScratchImage capture;
        auto hr = DirectX::CaptureTexture(window_->Queue(), window_->GetCurrentBackBufferResource(), false, capture,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
        if (SUCCEEDED(hr)) hr = DirectX::SaveToWICFile(*capture.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"tests/out/component-validation.png");
        Check(SUCCEEDED(hr), "GPU frame capture");
        report_ << (passed_ ? "PASS" : "FAIL") << " component runtime validation\n";
        report_.flush();
        if (!interactive_) PostQuitMessage(passed_ ? 0 : 2);
    }
};
