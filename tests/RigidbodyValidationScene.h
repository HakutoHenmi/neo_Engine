#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Game/Systems/PhysicsSystem.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

// Exercises the production physics system and actual editor Play in both scene modes.
// All fixture writes stay in ignored tests/out, with no user-scene saves.
class RigidbodyValidationScene final : public Game::GameScene {
    bool chrono_;
    bool passed_ = true;
    unsigned frame_ = 0;
    std::ofstream report_;
    void Check(bool condition, const char* message) {
        passed_ &= condition;
        report_ << (condition ? "PASS " : "FAIL ") << message << '\n';
        report_.flush();
    }
    void TestPhysics() {
        entt::registry registry;
        Game::PhysicsSystem physics;
        Game::GameContext ctx; ctx.isPlaying=true; ctx.dt=1.f/60;
        auto body=[&](float x,bool collider) {
            auto entity=registry.create();
            registry.emplace<Game::TransformComponent>(entity).translate={x,10,0};
            registry.emplace<Game::RigidbodyComponent>(entity);
            if(collider) registry.emplace<Game::BoxColliderComponent>(entity);
            return entity;
        };
        const auto bare=body(0,false), disabledCollider=body(4,true), triggerBody=body(8,true);
        const auto kinematic=body(12,false), disabledBody=body(16,true), noGravity=body(20,false);
        const auto disabledController=body(24,false), controller=body(28,false), grounded=body(32,true);
        registry.get<Game::BoxColliderComponent>(disabledCollider).enabled=false;
        registry.get<Game::BoxColliderComponent>(triggerBody).isTrigger=true;
        registry.get<Game::RigidbodyComponent>(kinematic).isKinematic=true;
        registry.get<Game::RigidbodyComponent>(disabledBody).enabled=false;
        registry.get<Game::RigidbodyComponent>(noGravity).useGravity=false;
        registry.emplace<Game::CharacterMovementComponent>(disabledController).enabled=false;
        registry.emplace<Game::CharacterMovementComponent>(controller);
        registry.get<Game::TransformComponent>(grounded).translate.y=3;
        const auto floor=registry.create();
        registry.emplace<Game::TransformComponent>(floor).translate={32,-.5f,0};
        registry.emplace<Game::BoxColliderComponent>(floor).size={4,1,4};
        // A trigger floor must not support another falling body.
        const auto throughTrigger=body(40,true), trigger=registry.create();
        registry.get<Game::TransformComponent>(throughTrigger).translate.y=3;
        registry.emplace<Game::TransformComponent>(trigger).translate={40,-.5f,0};
        auto& triggerShape=registry.emplace<Game::BoxColliderComponent>(trigger);
        triggerShape.size={4,1,4};triggerShape.isTrigger=true;
        for(int i=0;i<60;++i) physics.Update(registry,ctx);
        auto y=[&](entt::entity entity) {return registry.get<Game::TransformComponent>(entity).translate.y;};
        Check(std::abs(y(bare)-5.018333f)<.001f,"Rigidbody alone falls under gravity for one second");
        Check(std::abs(y(disabledCollider)-y(bare))<.001f && std::abs(y(triggerBody)-y(bare))<.001f,"disabled/trigger collider does not suppress motion");
        Check(y(kinematic)==10 && registry.get<Game::RigidbodyComponent>(kinematic).velocity.y==0,"kinematic body stays still without accumulating gravity");
        Check(y(disabledBody)==10 && y(noGravity)==10,"disabled body and Use Gravity off remain still");
        Check(std::abs(y(disabledController)-y(bare))<.001f && y(controller)==10,"enabled character controller owns vertical motion; disabled one does not");
        Check(std::abs(y(grounded)-.5f)<.001f && std::abs(registry.get<Game::RigidbodyComponent>(grounded).velocity.y)<.001f,"box rests on a BoxCollider-only static floor");
        Check(y(throughTrigger)<0,"trigger floor does not block a falling body");
        ctx.isPlaying=false;const auto stopped=y(bare);physics.Update(registry,ctx);
        Check(y(bare)==stopped,"physics does not advance outside Play");
    }
public:
    explicit RigidbodyValidationScene(bool chrono=true):chrono_(chrono){}
    void Initialize(Engine::WindowDX* window,const Engine::SceneParameters& params) override {
        std::filesystem::create_directories("tests/out");
        const std::string stem=chrono_ ? "rigidbody-chrono" : "rigidbody-legacy";
        report_.open("tests/out/"+stem+"-validation.txt");
        TestPhysics();
        const auto path="tests/out/"+stem+".json";
        std::ofstream(path) << R"({"objects":[{"id":1,"name":"Rigidbody Probe","translate":[0,8,0],"scale":[0.5,0.5,0.5],"components":[{"type":"MeshRenderer","modelPath":"Resources/Models/cube/cube.obj","shaderName":"Default","color":[0.2,0.7,1,1]}]}]})";
        auto diagnostic=params;diagnostic.stagePath=path;
        Game::GameScene::Initialize(window,diagnostic);
        SetIsPlaying(false);
        // Simulate the user's Inspector add-Rigidbody action, followed by Play.
        GetComponents().emplace<Game::RigidbodyComponent>(FindObjectByName("Rigidbody Probe"));
        SetIsPlaying(true);
    }
    void Update() override {
        auto& registry=GetComponents();
        const auto probe=FindObjectByName("Rigidbody Probe");
        // Prime the same cache the renderer uses before physics advances.
        (void)GetWorldMatrix(static_cast<int>(probe));
        const float before=registry.get<Game::TransformComponent>(probe).translate.y;
        Game::GameScene::Update();
        const float after=registry.get<Game::TransformComponent>(probe).translate.y;
        const auto world=GetWorldMatrix(static_cast<int>(probe));
        if(std::abs(world.m[3][1]-after)>.0001f) Check(false,"render world matrix reflects the current Rigidbody position");
        if(frame_>=20 && frame_<25 && after!=before) Check(false,"Pause prevents Rigidbody motion");
        ++frame_;
        if(frame_==20) SetPaused(true);
        if(frame_==25) SetPaused(false);
        if(frame_==60) {
            Check(after<7.9f && registry.get<Game::RigidbodyComponent>(probe).velocity.y<0,"Inspector-added Rigidbody falls in editor Play");
            Check(std::abs(world.m[3][1]-after)<.0001f,"render world matrix follows the falling object");
            SetIsPlaying(false);
            const auto restored=FindObjectByName("Rigidbody Probe");
            Check(std::abs(registry.get<Game::TransformComponent>(restored).translate.y-8)<.0001f,"Stop restores the original editor position");
            report_ << (passed_ ? "PASS" : "FAIL") << " rigidbody integration\n";
            report_.flush();PostQuitMessage(passed_ ? 0 : 2);
        }
    }
};
