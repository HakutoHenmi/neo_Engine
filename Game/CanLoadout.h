// CanLoadout.h
// 旧アクションゲームの「缶」の装備スロット (最大4つ) を管理する。
// セレクト画面で選んだ缶をゲームシーンへ渡すため、静的な配列に保持する。
// 最初の装備と缶の表示名は Resources/Data/loadout.json に置く。
#pragma once

// 缶の種類 (CanType) の定義
#include "ObjectTypes.h"
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include "Data/GameData.h"

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// 装備の管理 (静的関数だけのクラス)
class CanLoadout {
public:
    // 同時に装備できる缶の数
    static constexpr int kMaxEquippedCans = 4;

    // 現在の装備を取得する
    static const std::array<CanType, kMaxEquippedCans>& GetEquipped() {
        return Slots();
    }

    // 装備を設定する。空 (None) を詰めて先頭から並べ、上限を超えた分は捨てる
    static void SetEquipped(const std::array<CanType, kMaxEquippedCans>& cans) {
        // いったん全部空にする
        auto& slots = Slots();
        slots.fill(CanType::None);

        // 書き込む位置
        int writeIndex = 0;
        // 空の缶は飛ばし、上限に達したらやめる
        for (CanType can : cans) {
            if (can == CanType::None) continue;
            if (writeIndex >= kMaxEquippedCans) break;
            slots[writeIndex++] = can;
        }
    }

    // 可変長の一覧から装備を設定する (処理は配列版と同じ)
    static void SetEquipped(const std::vector<CanType>& cans) {
        auto& slots = Slots();
        slots.fill(CanType::None);

        int writeIndex = 0;
        for (CanType can : cans) {
            if (can == CanType::None) continue;
            if (writeIndex >= kMaxEquippedCans) break;
            slots[writeIndex++] = can;
        }
    }

    // 空でないスロットの数
    static int Count() {
        // None 以外を数える
        const auto& slots = Slots();
        return static_cast<int>(std::count_if(slots.begin(), slots.end(), [](CanType can) {
            return can != CanType::None;
        }));
    }

    // 缶の種類の表示名 (Resources/Data/loadout.json の names)
    static const char* Name(CanType can) {
        const auto& names = Data().names;
        const size_t index = size_t(can);
        return index < names.size() ? names[index].c_str() : names[0].c_str();
    }

private:
    // 缶の種類の名前 (JSON の名前と列挙型の対応。並びは CanType と同じ)
    static constexpr const char* kIds[] = {"None", "Fire", "Water", "Thunder", "Soda", "Ice", "Magnet", "Acid", "Bubble"};

    // loadout.json の中身 (最初の装備と表示名)
    struct LoadoutData {
        std::array<CanType, kMaxEquippedCans> defaults{};
        std::vector<std::string> names;
    };
    static const LoadoutData& Data() {
        static const LoadoutData data = [] {
            const auto& json = Game::Data::Load("loadout.json");
            LoadoutData out;
            out.defaults.fill(CanType::None);
            auto typeOf = [](const std::string& id) {
                for (size_t i = 0; i < std::size(kIds); ++i) if (id == kIds[i]) return CanType(i);
                throw std::runtime_error("loadout.json: unknown can " + id);
            };
            const auto& defaults = Game::Data::Require(json, "defaultCans", "loadout.json");
            for (size_t i = 0; i < defaults.size() && i < out.defaults.size(); ++i) out.defaults[i] = typeOf(defaults[i].get<std::string>());
            const auto& names = Game::Data::Require(json, "names", "loadout.json");
            for (const char* id : kIds) out.names.push_back(Game::Data::Require(names, id, "loadout.json names").get<std::string>());
            return out;
        }();
        return data;
    }

    // 装備スロットの実体 (最初は loadout.json の defaultCans)
    static std::array<CanType, kMaxEquippedCans>& Slots() {
        static std::array<CanType, kMaxEquippedCans> slots = Data().defaults;
        return slots;
    }
};

} // namespace Game
