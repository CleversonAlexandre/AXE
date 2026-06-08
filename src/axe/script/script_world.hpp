#pragma once
#include "axe/core/types.hpp"
#include <entt/entt.hpp>

namespace axe
{
    class Scene;

    class AXE_API ScriptWorld
    {
    public:
        // Chamado no EnterPlay — carrega DLLs e chama OnStart
        void OnSceneStart(Scene& scene);

        // Chamado a cada frame durante Play
        void OnSceneUpdate(Scene& scene, float deltaTime);

        // Chamado no Stop — chama OnEnd e descarrega DLLs
        void OnSceneStop(Scene& scene);

        // Despacha evento para todos os objetos com ScriptComponent
        void DispatchEvent(Scene& scene, entt::entity target,
            const std::string& eventName, float value = 0.0f);

        // Notifica colisão entre duas entities
        void DispatchCollision(Scene& scene,
            entt::entity a, entt::entity b);
    };

} // namespace axe