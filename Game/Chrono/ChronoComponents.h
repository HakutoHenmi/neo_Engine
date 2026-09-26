#pragma once
#include "ChronoRules.h"
#include "../../externals/entt/entt.hpp"
namespace Game::Chrono {
enum class Kind { Enemy, Projectile, Anchor, Weakpoint, Boss };
enum class Action { Free, Extending, Pulling, Retracting, Recovery, Dodge, Collapsed };
struct Target {
    Kind kind=Kind::Enemy;
    float radius=1, hp=1, reward=12;
    bool active=true;
    int wave=-1;
    Vec offset{};
};
struct Hopper { Vec velocity{}; float timer=0; bool grounded=false; };
struct Projectile { Vec velocity{}; float lifetime=18; bool breakable=true; };
struct Anchor { bool reusable=false; float cooldown=0; };
struct Solid {};
struct CameraOccluder { Vec center{},size{};float opacity=1; };
enum class ChainFailure { None, NoTarget, OutOfRange, Obstructed, Recovering, Unstable, Missed, Fell };
// Optional input source used by deterministic playback / validation scenes.
struct ControlFrame {
    Vec move{};
    bool attack=false, aim=false, toggle=false, jump=false, dodge=false;
    bool arena=false;
    bool cameraInput=false; // Scripted view changes have the same priority as mouse motion.
    float yaw=0, pitch=0;
};
struct Boss {
    float clock=0; int phase=0, wave=0, emitted=0, waveBreaks=0;
    bool engaged=false, countered=false;
    Vec attackDirection{0,0,-1};
    bool attackHit=false;
};
struct Player {
    Action action=Action::Free;
    Vec velocity{}, hand{}, shotDirection{}, shotStart{}, endpoint{}, dodgeDirection{};
    float mass=100, instability=0, timer=0, buffer=0, invincible=0, cooldown=0, coyote=0, jumpBuffer=0;
    float shotDistance=0, shotLimit=0, pullCost=0;
    float collisionScale=1;
    float airHang=0;
    float aimScale=1, worldScale=1;
    float hitStop=0, landingAge=1, landingStrength=0, damageAge=1, failureTime=0;
    float cameraOpacity=1;
    Vec damageSource{}, recoveryPoint{0,3,-27};
    ChainFailure failure=ChainFailure::None;
    bool automatic=false, aiming=false, grounded=false, shotManual=false;
    entt::entity target=entt::null, preview=entt::null, blockedPreview=entt::null, bufferedTarget=entt::null;
    bool bufferedManual=false;
    float dodgeBuffer=0;
    Vec bufferedDodge{};
    Vec pullWaypoint{};
    bool pullStep=false;
    const char* damageReason="HIT";
    Flow flow;
    Stats stats;
};
}
