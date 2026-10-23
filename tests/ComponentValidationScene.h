// ComponentValidationScene.h
// コンポーネントの追加・編集・削除と、スクリプトの差し替え・後片付けが正しく動くかを確かめる自動テストのシーン。
// --component-smoke で起動し、結果を tests/out/component-validation.txt に書く。
#pragma once
// ゲームシーン・エディタ・スクリプトの登録と変更
#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Game/Scripts/ScriptEngine.h"
#include "../Game/Scripts/ScriptEdits.h"
// 画面の保存 (DirectXTex)
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <functional>

// 診断は一時的なシーンで行い、Resources には保存しない
class ComponentValidationScene final : public Game::GameScene {
    // 開始と後片付けの回数を数えるだけのテスト用スクリプト
    class CleanupProbe final : public Game::IScript {
    public:
        // 開始・後片付けの回数 (シーン側の変数を参照する)
        int& starts;
        int& stops;
        // 後片付けのときに追加で行う処理 (テストごとに設定する)
        std::function<void(entt::entity, Game::GameScene*)> cleanup;
        CleanupProbe(int& startCount, int& stopCount) : starts(startCount), stops(stopCount) {}
        // 開始で回数を増やす
        void Start(entt::entity, Game::GameScene*) override { ++starts; }
        void OnDestroy(entt::entity entity, Game::GameScene* scene) override {
            // 後片付けで回数を増やし、追加の処理があれば呼ぶ
            ++stops;
            if (cleanup) cleanup(entity, scene);
        }
    };
    // エディタで開いたままにするか (true なら終了しない)
    bool interactive_;
    // すべて成功したか・経過フレーム数・開始と後片付けの回数・ウィンドウ・結果のファイル
    bool passed_ = true;
    unsigned frames_ = 0;
    int starts_ = 0, stops_ = 0;
    Engine::WindowDX* window_ = nullptr;
    std::ofstream report_;

    // 結果を 1 行書き、失敗があれば全体を失敗にする
    void Check(bool condition, const char* name) {
        passed_ &= condition;
        report_ << (condition ? "PASS " : "FAIL ") << name << '\n';
        report_.flush();
    }
    // テスト用スクリプトを付けて 1 フレーム実行する (Start が呼ばれる)
    std::shared_ptr<CleanupProbe> Attach(entt::entity entity, const char* name) {
        auto script = std::make_shared<CleanupProbe>(starts_, stops_);
        GetComponents().get_or_emplace<Game::ScriptComponent>(entity).scripts.push_back({name, "{}", script, false});
        Game::ScriptEngine::GetInstance()->Execute(entity, this, 1.f / 60.f);
        return script;
    }
public:
    // interactive なら結果を出しても終了しない
    explicit ComponentValidationScene(bool interactive = false) : interactive_(interactive) {}
    ~ComponentValidationScene() override {
        // コールバックが参照する回数の変数が残っているうちに、テスト用スクリプトを片付ける
        ClearScene();
    }
    // 初期化: 一時的なシーンを作り、各テストを順に行う
    void Initialize(Engine::WindowDX* window, const Engine::SceneParameters& params) override {
        window_ = window;
        // 出力先のフォルダーと、物 1 つだけのシーンファイルを作る
        std::filesystem::create_directories("tests/out");
        std::ofstream("tests/out/component-editor.json") << R"({"objects":[{"id":1,"name":"ComponentProbe","components":[]}]})";
        // そのシーンファイルでゲームシーンを初期化し、止めておく
        auto diagnostic = params;
        diagnostic.stagePath = "tests/out/component-editor.json";
        Game::GameScene::Initialize(window, diagnostic);
        SetPaused(true);
        // 結果のファイル
        report_.open("tests/out/component-validation.txt");
        // テスト対象の物
        auto entity = FindObjectByName("ComponentProbe");
        auto& components = GetComponents();
        // 体力: 追加・編集・無効化
        auto& health = components.emplace<Game::HealthComponent>(entity);
        health.SetHp(75);
        health.enabled = false;
        Check(health.Hp() == 75 && !health.enabled, "health add/edit/disable");
        // 箱の当たり判定: 追加・編集・削除
        components.emplace<Game::BoxColliderComponent>(entity).size = {2, 3, 4};
        Check(components.get<Game::BoxColliderComponent>(entity).size.y == 3, "collider add/edit");
        components.remove<Game::BoxColliderComponent>(entity);
        Check(!components.all_of<Game::BoxColliderComponent>(entity), "collider remove");

        // スクリプトの差し替え: 開始済みのものは 1 回だけ後片付けされ、新しいものは未開始になる。古い予約は反映されない
        auto script = Attach(entity, "replacement-probe");
        Game::ScriptEdit replace{Game::ScriptEditKind::Replace, entity, 0, "replacement-probe", script, "BaseScript"};
        Check(Game::ApplyScriptEdit(components, replace, this) && starts_ == 1 && stops_ == 1, "started script replacement cleanup once");
        Check(!components.get<Game::ScriptComponent>(entity).scripts[0].isStarted, "replacement starts fresh");
        Check(!Game::ApplyScriptEdit(components, replace, this), "stale edit rejected");
        components.remove<Game::ScriptComponent>(entity);

        // 後片付けの中で自分のコンポーネントを消しても、差し替えで復活しない
        script = Attach(entity, "remove-own-component");
        script->cleanup = [](entt::entity e, Game::GameScene* scene) { scene->GetComponents().remove<Game::ScriptComponent>(e); };
        Check(Game::ApplyScriptEdit(components, {Game::ScriptEditKind::Replace, entity, 0, "remove-own-component", script, "BaseScript"}, this), "replacement with reentrant component removal");
        Check(stops_ == 2 && !components.all_of<Game::ScriptComponent>(entity), "removed component not resurrected");

        // コンポーネントごとの削除で 1 回だけ後片付けされる
        script = Attach(entity, "remove-probe");
        Check(Game::ApplyScriptEdit(components, {Game::ScriptEditKind::RemoveComponent, entity, 0, {}, {}, {}}, this) && stops_ == 3, "component removal cleanup once through scene signals");
        Check(!components.all_of<Game::ScriptComponent>(entity), "script component removed");
        // シーンの読み込み直しで 1 回だけ後片付けされ、値が保たれる
        script = Attach(entity, "clear-probe");
        components.get<Game::ScriptComponent>(entity).scripts[0].scriptPath = "BaseScript";
        const std::string snapshot = Game::EditorUI::SaveToMemory(this);
        Game::EditorUI::LoadFromMemory(this, snapshot);
        Check(stops_ == 4, "scene reload cleanup once");
        entity = FindObjectByName("ComponentProbe");
        Check(components.valid(entity) && components.get<Game::HealthComponent>(entity).Hp() == 75 &&
              !components.get<Game::HealthComponent>(entity).enabled, "component values survive memory roundtrip");
        components.remove<Game::ScriptComponent>(entity); // 復元したテスト用スクリプトはまだ開始していない
        Check(stops_ == 4, "unstarted restored instance requires no cleanup");
        // エディタで確認できるよう、当たり判定とスクリプトの欄を付けて選んでおく
        components.emplace<Game::BoxColliderComponent>(entity).size = {2, 3, 4};
        auto& entry = components.emplace<Game::ScriptComponent>(entity).scripts.emplace_back();
        entry.scriptPath = "BaseScript";
        entry.parameterData = "{}";
        SelectEntity(entity);
    }
    void Update() override {
        // 通常の更新
        Game::GameScene::Update();
        // 30 フレーム目だけ以下を行う
        if (++frames_ != 30) return;
        // 画面を画像に保存できるか
        DirectX::ScratchImage capture;
        auto hr = DirectX::CaptureTexture(window_->Queue(), window_->GetCurrentBackBufferResource(), false, capture,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
        if (SUCCEEDED(hr)) hr = DirectX::SaveToWICFile(*capture.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"tests/out/component-validation.png");
        Check(SUCCEEDED(hr), "GPU frame capture");
        // 全体の結果を書き、エディタで開いていなければ終了する (成功 0 / 失敗 2)
        report_ << (passed_ ? "PASS" : "FAIL") << " component runtime validation\n";
        report_.flush();
        if (!interactive_) PostQuitMessage(passed_ ? 0 : 2);
    }
};
