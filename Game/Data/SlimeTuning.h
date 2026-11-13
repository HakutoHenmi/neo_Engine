// スライムの調整値。JSON・検証・ImGui は同じ項目定義を使う。
// 読み手には変更されないスナップショットを渡し、ロード中のスレッドとも共有する。
#pragma once
#include "GameData.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>

namespace Game::Data {

inline constexpr const char* SlimeTuningFile = "slime_tuning.json";
// 最小・最大は各材料100%のときの結果にも適用する (混合はその間になる)。
#define SLIME_RECIPE_PARAMETERS(X) \
    X(Speed, "移動速度", .01f, 200.f) \
    X(Acceleration, "加速", .01f, 200.f) \
    X(Braking, "減速", .01f, 200.f) \
    X(Gravity, "重力", .01f, 200.f) \
    X(Bounce, "ジャンプ初速", .01f, 200.f) \
    X(Rebound, "反発初速", 0.f, 200.f) \
    X(AbsorbDuration, "取り込み時間 (秒)", .01f, 30.f) \
    X(AbsorbRange, "取り込み距離", .01f, 200.f) \
    X(AbsorbDot, "取り込み正面の内積", -1.f, 1.f) \
    X(AbsorbHold, "取り込み保持時間 (秒)", .01f, 30.f) \
    X(StompDamage, "踏みつけダメージ", 0.f, 10000.f) \
    X(StompRadius, "踏みつけ範囲", .01f, 200.f) \
    X(DamageMultiplier, "被ダメージ倍率", .01f, 100.f) \
    X(SplitCap, "分裂量の上限", .01f, 10000.f) \
    X(SplitRatio, "分裂割合", 0.f, 1.f) \
    X(ChildSpeedMultiplier, "子の速度倍率", .01f, 100.f) \
    X(ChildCooldownDivisor, "子の攻撃頻度倍率", .01f, 100.f) \
    X(ChildDamage, "子のダメージ", 0.f, 10000.f) \
    X(Spread, "広がり", .01f, 100.f) \
    X(Viscosity, "粘性", 0.f, 1000.f) \
    X(Damping, "減衰", 0.f, 1000.f) \
    X(Cohesion, "まとまり", 0.f, 1000.f) \
    X(SurfaceTension, "表面張力", 0.f, 1000.f) \
    X(Slosh, "揺れ", 0.f, 100.f)

enum class RecipeParameter {
#define SLIME_ENUM(name, label, low, high) name,
    SLIME_RECIPE_PARAMETERS(SLIME_ENUM)
#undef SLIME_ENUM
    Count
};
struct RecipeParameterSpec { const char* key; const char* label; float min, max; };
inline constexpr RecipeParameterSpec RecipeParameters[] = {
#define SLIME_SPEC(name, label, low, high) {#name, label, low, high},
    SLIME_RECIPE_PARAMETERS(SLIME_SPEC)
#undef SLIME_SPEC
};
#undef SLIME_RECIPE_PARAMETERS

// 基本値 + 水・ホウ砂水・洗濯のり・泡の各割合 × 係数。
struct RecipeCurve {
    float base = 0;
    std::array<float, 4> materials{};
};

// スカラー項目を増やすときはここに1行とJSONの値を追加する。
#define SLIME_SCALAR_PARAMETERS(X) \
    X(speedScale, "調合", "移動速度の全体倍率", .01f, 10.f) \
    X(reboundThreshold, "調合", "反発に必要なホウ砂水の割合", 0.f, 1.f) \
    X(styleThreshold, "調合", "型が変わる材料の割合", .5f, 1.f) \
    X(childCooldown, "調合", "子の基本攻撃間隔 (秒)", .01f, 30.f) \
    X(restoreSeconds, "武器", "通常の復元時間 (秒)", .01f, 10.f) \
    X(engulfRestoreSeconds, "武器", "取り込みからの復元時間 (秒)", .01f, 10.f) \
    X(attackBufferSeconds, "武器", "攻撃の先行入力 (秒)", 0.f, 2.f) \
    X(turnResponse, "武器", "向きの追従速度", .01f, 100.f) \
    X(turnRadiansPerSecond, "武器", "旋回上限 (rad/秒)", .01f, 50.f) \
    X(tireSpeedScale, "タイヤ", "最高速度倍率", .01f, 20.f) \
    X(tireSizeSpeedMax, "タイヤ", "大きさによる速度倍率の加算上限", 0.f, 10.f) \
    X(tireSizeSpeedRate, "タイヤ", "大きさによる速度倍率の加算率", 0.f, 10.f) \
    X(tireAcceleration, "タイヤ", "加速", .01f, 100.f) \
    X(tireBraking, "タイヤ", "減速", .01f, 100.f) \
    X(tireAirControl, "タイヤ", "空中の操作倍率", 0.f, 1.f) \
    X(tireChargeSeconds, "タイヤ", "溜めきる時間 (秒)", .01f, 30.f) \
    X(tireMinCharge, "タイヤ", "突進に必要な溜め", .01f, 1.f) \
    X(tireSpinMax, "タイヤ", "最大回転速度", 0.f, 200.f) \
    X(tireDashSeconds, "タイヤ", "突進の基本時間 (秒)", .01f, 10.f) \
    X(tireDashChargeSeconds, "タイヤ", "溜めによる突進時間の加算", 0.f, 10.f) \
    X(tireDashSpeed, "タイヤ", "突進速度の基本倍率", .01f, 20.f) \
    X(tireDashChargeSpeed, "タイヤ", "溜めによる速度倍率の加算", 0.f, 20.f) \
    X(tireDashDamage, "タイヤ", "突進の基本ダメージ", 0.f, 10000.f) \
    X(tireDashChargeDamage, "タイヤ", "溜めによるダメージの加算", 0.f, 10000.f) \
    X(tireGateDamage, "タイヤ", "門への基本ダメージ", 0.f, 10000.f) \
    X(tireGateChargeDamage, "タイヤ", "溜めによる門ダメージの加算", 0.f, 10000.f) \
    X(cameraBlendSeconds, "カメラ", "構図の切り替え時間 (秒)", .01f, 10.f) \
    X(cameraFrameFraction, "カメラ", "画面内に収める割合", .01f, 1.f) \
    X(cameraMinDistance, "カメラ", "最短距離", .01f, 1000.f) \
    X(cameraPadding, "カメラ", "対象の半径に足す余裕", 0.f, 100.f)

struct WeaponTuning { float duration = 0, damage = 0, hitBegin = 0, hitEnd = 0; };
inline constexpr const char* WeaponKeys[] = {"Idle", "Engulf", "Hammer", "Sword", "Plane", "Tire", "Hydrant", "Restore", "Signal", "Vacuum", "Spring"};
inline constexpr const char* WeaponLabels[] = {"通常", "取り込み", "ハンマー", "剣", "飛行機", "タイヤ", "消火栓", "復元", "信号機", "掃除機", "バネ"};
struct SlimeTuning {
    std::array<RecipeCurve, size_t(RecipeParameter::Count)> recipe{};
    std::array<WeaponTuning, std::size(WeaponKeys)> weapons{};
#define SLIME_FIELD(name, group, label, low, high) float name = 0;
    SLIME_SCALAR_PARAMETERS(SLIME_FIELD)
#undef SLIME_FIELD
};
struct SlimeScalarSpec { const char* key; const char* group; const char* label; float SlimeTuning::*field; float min, max; };
inline constexpr SlimeScalarSpec SlimeScalars[] = {
#define SLIME_SPEC(name, group, label, low, high) {#name, group, label, &SlimeTuning::name, low, high},
    SLIME_SCALAR_PARAMETERS(SLIME_SPEC)
#undef SLIME_SPEC
};
#undef SLIME_SCALAR_PARAMETERS

inline void ValidateTuningNumber(float value, float min, float max, const std::string& key) {
    if (!std::isfinite(value) || value < min || value > max)
        throw std::runtime_error(std::string(SlimeTuningFile) + ": " + key + " must be in [" + std::to_string(min) + ", " + std::to_string(max) + "]");
}
inline void ValidateSlimeTuning(const SlimeTuning& settings) {
    for (const auto& spec : SlimeScalars) ValidateTuningNumber(settings.*spec.field, spec.min, spec.max, spec.key);
    for (size_t i = 0; i < settings.recipe.size(); ++i) {
        const auto& curve = settings.recipe[i];
        const auto& spec = RecipeParameters[i];
        ValidateTuningNumber(curve.base, spec.min, spec.max, std::string(spec.key) + ".base");
        for (float coefficient : curve.materials) {
            ValidateTuningNumber(coefficient, -10000, 10000, spec.key);
            ValidateTuningNumber(curve.base + coefficient, spec.min, spec.max, spec.key);
        }
    }
    for (size_t i = 0; i < settings.weapons.size(); ++i) {
        const auto& weapon = settings.weapons[i];
        const std::string key = std::string("weapons.") + WeaponKeys[i];
        ValidateTuningNumber(weapon.duration, .01f, 30, key + ".duration");
        ValidateTuningNumber(weapon.damage, 0, 10000, key + ".damage");
        ValidateTuningNumber(weapon.hitBegin, 0, weapon.duration, key + ".hitBegin");
        ValidateTuningNumber(weapon.hitEnd, weapon.hitBegin, weapon.duration, key + ".hitEnd");
    }
}

inline SlimeTuning ParseSlimeTuning(const nlohmann::json& json) {
    SlimeTuning out;
    try {
        if (json.at("version").get<int>() != 1) throw std::runtime_error("unsupported version");
        for (const auto& spec : SlimeScalars) out.*spec.field = json.at("scalars").at(spec.key).get<float>();
        for (size_t i = 0; i < out.recipe.size(); ++i) {
            const auto& curve = json.at("recipe").at(RecipeParameters[i].key);
            out.recipe[i].base = curve.at("base").get<float>();
            out.recipe[i].materials = ReadArray<float, 4>(curve.at("materials"), RecipeParameters[i].key);
        }
        for (size_t i = 0; i < out.weapons.size(); ++i) {
            const auto& weapon = json.at("weapons").at(WeaponKeys[i]);
            out.weapons[i] = {weapon.at("duration").get<float>(), weapon.at("damage").get<float>(), weapon.at("hitBegin").get<float>(), weapon.at("hitEnd").get<float>()};
        }
        ValidateSlimeTuning(out);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string(SlimeTuningFile) + ": " + e.what());
    }
    return out;
}
// 既存JSONに値を重ねるので、説明や将来の項目も保存時に失わない。
inline void WriteSlimeTuning(nlohmann::json& json, const SlimeTuning& settings) {
    ValidateSlimeTuning(settings);
    for (const auto& spec : SlimeScalars) json["scalars"][spec.key] = settings.*spec.field;
    for (size_t i = 0; i < settings.recipe.size(); ++i) {
        auto& curve = json["recipe"][RecipeParameters[i].key];
        curve["base"] = settings.recipe[i].base;
        curve["materials"] = settings.recipe[i].materials;
    }
    for (size_t i = 0; i < settings.weapons.size(); ++i) {
        const auto& weapon = settings.weapons[i];
        json["weapons"][WeaponKeys[i]].update({{"duration", weapon.duration}, {"damage", weapon.damage}, {"hitBegin", weapon.hitBegin}, {"hitEnd", weapon.hitEnd}});
    }
}

// Snapshotだけはロードスレッドからも呼べる。それ以外はメインスレッドから呼ぶ。
class SlimeTuningStore {
public:
    explicit SlimeTuningStore(std::string path = std::string(Folder) + SlimeTuningFile) : path_(std::move(path)) {
        if (!Reload()) throw std::runtime_error(error_);
    }
    std::shared_ptr<const SlimeTuning> Snapshot() const {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        return current_;
    }
    bool Apply(const SlimeTuning& settings) {
        try {
            ValidateSlimeTuning(settings);
            Publish(settings);
            dirty_ = true;
            error_.clear();
            return true;
        } catch (const std::exception& e) { error_ = e.what(); return false; }
    }
    bool Reload() {
        try {
            auto json = ReadFile();
            auto settings = ParseSlimeTuning(json);
            const auto stamp = Stamp();
            Publish(settings);
            document_ = std::move(json);
            stamp_ = stamp;
            dirty_ = false;
            error_.clear();
            return true;
        } catch (const std::exception& e) { error_ = e.what(); return false; }
    }
    bool Save() {
        const auto temporary = std::filesystem::path(path_.wstring() + L".tmp");
        try {
            auto json = document_;
            WriteSlimeTuning(json, *Snapshot());
            { // 最後まで書けてから元のファイルを置き換える。
                std::ofstream file(temporary, std::ios::trunc);
                file.exceptions(std::ios::failbit | std::ios::badbit);
                file << json.dump(2) << '\n';
                file.close();
            }
            std::filesystem::rename(temporary, path_);
            document_ = std::move(json);
            stamp_ = Stamp();
            dirty_ = false;
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            error_ = std::string(SlimeTuningFile) + ": save failed: " + e.what();
            return false;
        }
    }
    void Poll(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        if (now < nextPoll_) return;
        nextPoll_ = now + PollInterval;
        try {
            const auto stamp = Stamp();
            if (stamp == stamp_) return;
            if (dirty_) { error_ = "外部ファイルも変更されています。保存で上書き、再読み込みでファイルの値を採用します。"; return; }
            // 不正なファイルは次の変更まで再試行せず、有効なスナップショットを保つ。
            if (!Reload()) stamp_ = stamp;
        } catch (const std::exception& e) { error_ = std::string(SlimeTuningFile) + ": " + e.what(); }
    }
    bool Dirty() const { return dirty_; }
    const std::string& Error() const { return error_; }
    uint64_t Revision() const { return revision_.load(std::memory_order_acquire); }
private:
    static constexpr auto PollInterval = std::chrono::milliseconds(500);
    using FileStamp = std::pair<std::filesystem::file_time_type, uintmax_t>;
    FileStamp Stamp() const { return {std::filesystem::last_write_time(path_), std::filesystem::file_size(path_)}; }
    nlohmann::json ReadFile() const {
        std::ifstream file(path_);
        if (!file) throw std::runtime_error("Game data file not found: " + path_.string());
        nlohmann::json json;
        try { file >> json; }
        catch (const std::exception& e) { throw std::runtime_error(path_.string() + ": " + e.what()); }
        return json;
    }
    void Publish(const SlimeTuning& settings) {
        std::shared_ptr<const SlimeTuning> next = std::make_shared<SlimeTuning>(settings);
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            current_ = std::move(next);
        }
        revision_.fetch_add(1, std::memory_order_release);
    }
    std::filesystem::path path_;
    std::shared_ptr<const SlimeTuning> current_;
    mutable std::mutex snapshotMutex_;
    nlohmann::json document_;
    FileStamp stamp_{};
    bool dirty_ = false;
    std::string error_;
    std::atomic<uint64_t> revision_{0};
    std::chrono::steady_clock::time_point nextPoll_{};
};
inline SlimeTuningStore& SlimeTuningData() { static SlimeTuningStore store; return store; }
inline std::shared_ptr<const SlimeTuning> SlimeSettings() {
    auto& store = SlimeTuningData();
    // 頂点の姿勢計算などから何度も呼ぶので、変更のないフレームでは
    // スナップショット取得の排他処理を繰り返さない。
    thread_local uint64_t revision = 0;
    thread_local std::shared_ptr<const SlimeTuning> settings;
    const auto currentRevision = store.Revision();
    if (revision != currentRevision) {
        settings = store.Snapshot();
        revision = currentRevision;
    }
    return settings;
}

} // namespace Game::Data
