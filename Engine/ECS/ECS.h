#pragma once
#include <cstdint>
#include <bitset>

namespace TDEngine {
namespace ECS {

// エンティティ
// ただの整数の ID。
using Entity = std::uint32_t;
const Entity MAX_ENTITIES = 10000;

// コンポーネントの種類
// コンポーネントの種類を区別するための整数の ID。
using ComponentType = std::uint8_t;
const ComponentType MAX_COMPONENTS = 32;

// シグネチャ
// エンティティがどのコンポーネントを持っているかを記録するビット列。
// システムが必要とするコンポーネントを表すのにも使う。
using Signature = std::bitset<MAX_COMPONENTS>;

} // namespace ECS
} // namespace TDEngine
