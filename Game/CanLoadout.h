#pragma once

#include "ObjectTypes.h"
#include <array>
#include <algorithm>
#include <vector>

namespace Game {

class CanLoadout {
public:
    static constexpr int kMaxEquippedCans = 4;

    static const std::array<CanType, kMaxEquippedCans>& GetEquipped() {
        return Slots();
    }

    static void SetEquipped(const std::array<CanType, kMaxEquippedCans>& cans) {
        auto& slots = Slots();
        slots.fill(CanType::None);

        int writeIndex = 0;
        for (CanType can : cans) {
            if (can == CanType::None) continue;
            if (writeIndex >= kMaxEquippedCans) break;
            slots[writeIndex++] = can;
        }
    }

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

    static int Count() {
        const auto& slots = Slots();
        return static_cast<int>(std::count_if(slots.begin(), slots.end(), [](CanType can) {
            return can != CanType::None;
        }));
    }

    static const char* Name(CanType can) {
        switch (can) {
        case CanType::Fire: return "Fire";
        case CanType::Water: return "Water";
        case CanType::Thunder: return "Thunder";
        case CanType::Soda: return "Soda";
        case CanType::Ice: return "Ice";
        case CanType::Magnet: return "Magnet";
        case CanType::Acid: return "Acid";
        case CanType::Bubble: return "Bubble";
        default: return "Empty";
        }
    }

private:
    static std::array<CanType, kMaxEquippedCans>& Slots() {
        static std::array<CanType, kMaxEquippedCans> slots = {
            CanType::Fire,
            CanType::Water,
            CanType::Thunder,
            CanType::Soda,
        };
        return slots;
    }
};

} // namespace Game
