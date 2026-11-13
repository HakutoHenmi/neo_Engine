#include "../Game/Chrono/SlimeWeaponRules.h"
#include <cstdio>
#include <limits>

namespace {
using namespace Game;
using namespace Game::Chrono;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Near(float a, float b) { return std::abs(a-b) < .0001f; }

// 以前の式を独立した期待値として残し、全材料100%・既定・混合で比較する。
void TestOriginalRecipe() {
    for (const auto& mix : std::array<std::array<int,4>,6>{{{40,35,25,0},{100,0,0,0},{0,100,0,0},{0,0,100,0},{0,0,0,100},{15,20,30,35}}}) {
        SlimeRecipe recipe;
        recipe.mix = mix;
        const float water=recipe.Ratio(0), borax=recipe.Ratio(1), glue=recipe.Ratio(2), foam=recipe.Ratio(3);
        Check(Near(recipe.Speed(),1.2f*(8+20*water-2*glue+6*foam)), "original speed");
        Check(Near(recipe.Acceleration(),7+13*water-2*glue+3*borax), "original acceleration");
        Check(Near(recipe.Braking(),10-6*water+5*glue), "original braking");
        Check(Near(recipe.Gravity(),30-16*foam), "original gravity");
        Check(Near(recipe.Bounce(),8+16*borax+7*foam), "original jump");
        Check(Near(recipe.Rebound(),borax>=.5f?3+4*borax:0), "original rebound");
        Check(Near(recipe.AbsorbDuration(),.48f-.14f*water-.30f*glue+.06f*borax), "original absorption time");
        Check(Near(recipe.AbsorbRange(),8*(1+.85f*water)), "original absorption range");
        Check(Near(recipe.AbsorbDot(),.35f-.23f*water) && Near(recipe.AbsorbHold(),1+2*glue), "original absorption cone/hold");
        Check(Near(recipe.StompDamage(),12+42*borax) && Near(recipe.StompRadius(),3.2f+4.8f*borax), "original stomp");
        Check(Near(recipe.DamageMultiplier(),1+.35f*(water-.4f)+.8f*foam-.4f*(glue-.25f)), "original incoming damage");
        Check(Near(recipe.SplitCap(),20+12*(water-.4f)+10*foam-8*(glue-.25f)), "original split cap");
        Check(Near(recipe.SplitRatio(),.25f+.18f*(water-.4f)+.12f*foam-.1f*(glue-.25f)), "original split ratio");
        Check(Near(recipe.ChildSpeed(),recipe.Speed()*(1+1.3f*foam)), "original child speed");
        Check(Near(recipe.ChildCooldown(),.95f/(1+2.5f*foam)) && Near(recipe.ChildDamage(),6+5*foam), "original child combat");
        Check(Near(recipe.Spread(),1+.6f*water-.25f*borax-.15f*glue+.3f*foam), "original spread");
        Check(Near(recipe.Viscosity(),3+105*glue+28*borax) && Near(recipe.Damping(),1.5f+7*glue+2*borax), "original viscosity/damping");
        Check(Near(recipe.Cohesion(),30+65*borax+25*glue), "original cohesion");
        Check(Near(recipe.SurfaceTension(),8+45*glue+18*borax) && Near(recipe.Slosh(),1.2f+1.6f*water-.65f*glue-.7f*borax), "original surface/slosh");
    }
}

void TestLiveRules() {
    auto& store=Data::SlimeTuningData();
    const auto before=store.Snapshot();
    auto settings=*before;
    SlimeRecipe recipe;
    const float speed=recipe.Speed();
    settings.speedScale*=2;
    settings.weapons[size_t(LifeAction::Sword)]={1.2f,32,.5f,.9f};
    settings.tireChargeSeconds=.2f;
    settings.tireGateDamage=90;
    settings.cameraMinDistance=100;
    Check(store.Apply(settings), "apply live settings");
    Check(Near(recipe.Speed(),speed*2), "existing recipe uses live speed");
    LifeBody sword;
    sword.action=LifeAction::Sword;sword.mass=LifeStartMass;
    Check(Near(LifeWeaponDuration(sword.action),1.2f) && Near(LifeWeaponDamage(sword),32), "live sword damage/duration");
    sword.weapon.active=true;sword.weapon.previousAge=.3f;sword.weapon.age=.6f;
    float from=0,to=0;
    Check(LifeWeaponHitInterval(sword,0,from,to) && Near(from,.5f) && Near(to,.6f), "live hit window");
    LifeBody tire;
    tire.action=LifeAction::Tire;tire.weapon.ready=true;
    LifeTickTireCharge(tire,true,.2f);
    Check(tire.weapon.charge==1 && Near(LifeTireDashGateDamage(0),90), "live tire values");
    Check(Near(LifeCameraDistance(1,1,1),100), "live camera distance");
    Check(Near(before->speedScale,1.2f), "old snapshot remains immutable");
    Check(store.Apply(*before), "restore original settings");
}

void TestPersistenceAndInvalidFiles() {
    const std::filesystem::path path="tests/out/slime_tuning_test.json";
    auto json=Data::Load(Data::SlimeTuningFile);
    json["_customComment"]="preserve this";
    auto write=[&](const nlohmann::json& document) {
        std::ofstream file(path);file<<document.dump(2);file.close();
        // ファイルシステムの時刻分解能に依存せず、変更を検出させる。
        static int change=0;
        std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(++change));
    };
    write(json);
    Data::SlimeTuningStore store(path.string());
    auto clock=std::chrono::steady_clock::now();
    auto poll=[&] { clock+=std::chrono::seconds(1);store.Poll(clock); };
    json["scalars"]["speedScale"]=2;
    write(json);poll();
    Check(Near(store.Snapshot()->speedScale,2) && !store.Dirty(), "hot reload external change");
    const auto good=store.Snapshot();
    json["scalars"]["tireChargeSeconds"]=0;
    write(json);poll();
    Check(store.Snapshot()==good && !store.Error().empty(), "invalid file keeps last valid snapshot");
    json["scalars"]["tireChargeSeconds"]=1.1;
    write(json);poll();
    Check(store.Error().empty(), "recover after invalid file is fixed");
    { std::ofstream file(path);file<<"{ broken"; }
    Check(!store.Reload() && Near(store.Snapshot()->speedScale,2), "malformed JSON preserves active values");
    write(json);
    auto edit=*store.Snapshot();edit.speedScale=3;
    Check(store.Apply(edit) && store.Dirty(), "unsaved edit");
    json["scalars"]["speedScale"]=4;
    write(json);poll();
    Check(Near(store.Snapshot()->speedScale,3) && !store.Error().empty(), "external change does not discard unsaved edits");
    Check(store.Save() && !store.Dirty(), "save over existing JSON");
    Data::SlimeTuningStore nextLaunch(path.string());
    Check(Near(nextLaunch.Snapshot()->speedScale,3), "saved settings survive next launch");
    nlohmann::json saved;
    { std::ifstream file(path);file>>saved; }
    Check(saved.at("_customComment")=="preserve this", "save preserves comments/unknown fields");
    edit.speedScale=5;Check(store.Apply(edit), "second edit");
    Check(store.Reload() && Near(store.Snapshot()->speedScale,3) && !store.Dirty(), "manual reload discards unsaved edits");

    const auto stable=store.Snapshot();
    edit=*stable;edit.speedScale=std::numeric_limits<float>::infinity();
    Check(!store.Apply(edit) && store.Snapshot()==stable, "reject infinity");
    edit=*stable;edit.weapons[2].hitEnd=edit.weapons[2].duration+1;
    Check(!store.Apply(edit) && store.Snapshot()==stable, "reject hit window outside attack");
    edit=*stable;edit.recipe[size_t(Data::RecipeParameter::Gravity)].materials[3]=-100;
    Check(!store.Apply(edit), "reject negative gravity at pure foam");
    auto missing=json;missing["scalars"].erase("speedScale");write(missing);
    Check(!store.Reload() && store.Error().find("speedScale")!=std::string::npos, "missing key is identified");
    write(saved);Check(store.Reload(), "restore valid test file");
    const auto temporary=std::filesystem::path(path.wstring()+L".tmp");
    std::filesystem::create_directory(temporary); // 一時ファイルが書けない状況
    Check(!store.Save(), "report failed save");
    std::filesystem::remove(temporary);
    Data::SlimeTuningStore afterFailure(path.string());
    Check(Near(afterFailure.Snapshot()->speedScale,3), "failed save preserves original file");
    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        TestOriginalRecipe();
        TestLiveRules();
        TestPersistenceAndInvalidFiles();
        std::puts("PASS slime tuning: original formulas, live rules, hot reload, persistence, validation, failed writes");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
