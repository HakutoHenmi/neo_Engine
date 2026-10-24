// AssignmentConfig.h
// 評価課題デモ (AssignmentScene) の設定: Resources/Data/assignment.json の素材・パーティクルの発生源・
// 開始時の値・戦闘の値を読み込む。
#pragma once
#include "../../Engine/GPUParticle.h"
#include "../../Engine/Matrix4x4.h"
#include "../Data/GameData.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace Game {
namespace AssignmentConfig {
// assignment.json の中身 (素材・パーティクルの発生源・開始時の値・戦闘の値)
struct AssignmentData {
    std::string sword, swordTexture, enemy, enemyTexture, particleModel, particleTexture, mutant;
    std::vector<std::string> mutantAnimations, mutantTextures;
    int materialCount = 0;
    uint32_t cpuParticles = 0, gpuParticles = 0;
    std::vector<Engine::GPUParticleEmitterData> emitters;
    int playerHp = 0, enemyHp = 0;
    Engine::Vector3 enemyPosition{}, enemyScale{};
    float cameraDistance = 0, cameraPitch = 0;
    std::string animation;
    float noticeRange = 0, loseRange = 0, attackRange = 0, attackFacing = 0;
    float enemyAttackRange = 0, enemyHitRange = 0, enemyWindup = 0, enemyCooldown = 0;
    int attackDamage = 0, enemyDamage = 0, enemyGuardedDamage = 0;
};
// assignment.json を読み込む (最初に使われたときに 1 回だけ)
inline const AssignmentData& Config() {
    static const AssignmentData data = [] {
        const char* file = "assignment.json";
        const auto& json = Data::Load(file);
        AssignmentData out;
        auto f3 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 3>(v, file); return DirectX::XMFLOAT3{a[0], a[1], a[2]}; };
        auto f4 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 4>(v, file); return DirectX::XMFLOAT4{a[0], a[1], a[2], a[3]}; };
        const auto& assets = Data::Require(json, "assets", file);
        out.sword = assets.at("sword").get<std::string>(); out.swordTexture = assets.at("swordTexture").get<std::string>();
        out.enemy = assets.at("enemy").get<std::string>(); out.enemyTexture = assets.at("enemyTexture").get<std::string>();
        out.particleModel = assets.at("particleModel").get<std::string>(); out.particleTexture = assets.at("particleTexture").get<std::string>();
        out.mutant = assets.at("mutant").get<std::string>();
        out.mutantAnimations = Data::ReadStrings(assets.at("mutantAnimations"));
        out.mutantTextures = Data::ReadStrings(assets.at("mutantTextures"));
        out.materialCount = assets.at("materialCount").get<int>();
        const auto& particles = Data::Require(json, "particles", file);
        out.cpuParticles = particles.at("cpuMax").get<uint32_t>(); out.gpuParticles = particles.at("gpuMax").get<uint32_t>();
        for (const auto& e : particles.at("emitters")) {
            Engine::GPUParticleEmitterData emitter = {};
            emitter.emitPos = f3(e.at("position")); emitter.emitRate = e.at("rate").get<float>();
            emitter.emitVel = f3(e.at("velocity")); emitter.emitLife = e.at("life").get<float>();
            emitter.emitterShape = e.at("shape").get<uint32_t>(); emitter.emitterExtents = f3(e.at("extents"));
            emitter.colorStart = f4(e.at("colorStart")); emitter.colorEnd = f4(e.at("colorEnd"));
            emitter.fieldType = e.at("field").get<uint32_t>(); emitter.fieldPos = emitter.emitPos;
            emitter.fieldParams = f4(e.at("fieldParams")); emitter.particleType = e.at("type").get<uint32_t>();
            out.emitters.push_back(emitter);
        }
        if (out.emitters.size() < 2) throw std::runtime_error("assignment.json: particles.emitters needs the tornado and the sword emitter");
        const auto& start = Data::Require(json, "start", file);
        out.playerHp = start.at("playerHp").get<int>(); out.enemyHp = start.at("enemyHp").get<int>();
        auto v3 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 3>(v, file); return Engine::Vector3{a[0], a[1], a[2]}; };
        out.enemyPosition = v3(start.at("enemyPosition")); out.enemyScale = v3(start.at("enemyScale"));
        out.cameraDistance = start.at("cameraDistance").get<float>(); out.cameraPitch = start.at("cameraPitch").get<float>();
        out.animation = start.at("animation").get<std::string>();
        const auto& combat = Data::Require(json, "combat", file);
        out.noticeRange = combat.at("noticeRange").get<float>(); out.loseRange = combat.at("loseRange").get<float>();
        out.attackDamage = combat.at("attackDamage").get<int>(); out.attackRange = combat.at("attackRange").get<float>();
        out.attackFacing = combat.at("attackFacing").get<float>();
        out.enemyAttackRange = combat.at("enemyAttackRange").get<float>(); out.enemyHitRange = combat.at("enemyHitRange").get<float>();
        out.enemyWindup = combat.at("enemyWindup").get<float>(); out.enemyCooldown = combat.at("enemyCooldown").get<float>();
        out.enemyDamage = combat.at("enemyDamage").get<int>(); out.enemyGuardedDamage = combat.at("enemyGuardedDamage").get<int>();
        return out;
    }();
    return data;
}
} // namespace AssignmentConfig
} // namespace Game
