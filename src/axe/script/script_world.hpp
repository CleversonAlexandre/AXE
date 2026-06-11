#pragma once
#include "axe/core/types.hpp"
#include <entt/entt.hpp>
#include <string>

namespace axe
{
    class Scene;

    class AXE_API ScriptWorld
    {
    public:
        // Chamado no EnterPlay — carrega DLLs e chama OnStart
        void OnSceneStart(Scene& scene);

        // Chamado a cada frame durante Play
        // Roda todos os scripts e depois consome os pending physics commands
        void OnSceneUpdate(Scene& scene, float deltaTime);

        // Chamado no Stop — chama OnEnd e descarrega DLLs
        void OnSceneStop(Scene& scene);

        // Despacha evento para uma entidade específica com ScriptComponent
        void DispatchEvent(Scene& scene, entt::entity target,
            const std::string& eventName, float value = 0.0f);

        // Notifica colisão entre duas entities (chama OnCollision em ambas)
        void DispatchCollision(Scene& scene,
            entt::entity a, entt::entity b);

        // Notifica entrada em trigger (chama OnTriggerEnter em ambas)
        void DispatchTriggerEnter(Scene& scene,
            entt::entity trigger, entt::entity other);

        // Notifica saída de trigger (chama OnTriggerExit em ambas)
        void DispatchTriggerExit(Scene& scene,
            entt::entity trigger, entt::entity other);
    };

} // namespace axe