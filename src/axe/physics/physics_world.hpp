#pragma once
#include "axe/core/types.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/scene/scene.hpp"
#include "axe/utils/glm_config.hpp"
#include <entt/entt.hpp>

namespace axe
{
    // Sincroniza a cena entt com o mundo Jolt
    class AXE_API PhysicsWorld
    {
    public:
        void OnSceneStart(Scene& scene);
        void OnSceneStop(Scene& scene);
        void OnUpdate(Scene& scene, float deltaTime);

        void CreateBody(entt::entity entity, Scene& scene);
        void DestroyBody(entt::entity entity, Scene& scene);

        void AddForce(entt::entity entity, Scene& scene, const glm::vec3& force);
        void AddImpulse(entt::entity entity, Scene& scene, const glm::vec3& impulse);

        RaycastHit Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist = 1000.0f);
    };

} // namespace axe