#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
    class Scene;

    class AXE_API ParticleWorld
    {
    public:
        ParticleWorld();
        ~ParticleWorld();

        // allowDestroy: true apenas em Play mode (nunca em Edit).
        // cameraPosition: usado para LOD — distância câmera→emissor.
        void OnUpdate(Scene& scene, float deltaTime,
            bool allowDestroy = false,
            const glm::vec3& cameraPosition = glm::vec3(0.f, 0.f, 0.f));

        void OnSceneStop(Scene& scene);

    private:
        float m_Time = 0.0f;
        // m_PendingSubEmitters está no .cpp como static local pra evitar
        // exportar std::vector<std::string> via AXE_API (problema MSVC C++17)
    };

} // namespace axe