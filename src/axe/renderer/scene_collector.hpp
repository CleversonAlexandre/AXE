#pragma once
#include "axe/renderer/render_queue.hpp"
#include "axe/scene/scene.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace axe
{
    // Itera a Scene e monta uma RenderQueue.
    // Todo o conhecimento de ECS fica aqui — SceneRenderer não precisa
    // saber nada sobre Scene, entt, components ou hierarquia.
    class AXE_API SceneCollector
    {
    public:
        // Coleta todos os dados de renderização da cena.
        // selectedEntityID: entt::entity cast para uint32_t, UINT32_MAX = nenhum
        static RenderQueue Collect(const Scene& scene,
            uint32_t selectedEntityID = UINT32_MAX,
            const glm::vec3& cameraPosition = glm::vec3(0.0f));

    private:
        static void CollectEntity(const Scene& scene,
            entt::entity entity,
            RenderQueue& queue,
            uint32_t selectedEntityID);
    };

} // namespace axe