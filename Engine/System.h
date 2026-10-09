#pragma once
#include "ECS.h"
#include <set>

namespace TDEngine {
namespace ECS {

    class System {
    public:
        const std::set<Entity>& Entities() const { return m_entities; }
        void AddEntity(Entity entity) { m_entities.insert(entity); }
        void RemoveEntity(Entity entity) { m_entities.erase(entity); }
        virtual ~System() = default;
        virtual void Update(float) {} // dt名を削除して警告回避
    private:
        std::set<Entity> m_entities;
    };

} // namespace ECS
} // namespace TDEngine
