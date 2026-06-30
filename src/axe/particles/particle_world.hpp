#pragma once
#include "axe/core/types.hpp"

namespace axe
{
    class Scene;

    // Sistema-mundo das partículas — irmão de ScriptWorld/PhysicsWorld.
    // Tickado por frame: emite, envelhece, integra e mata partículas de
    // todas as entities com ParticleSystemComponent.
    class AXE_API ParticleWorld
    {
    public:
        // Chamado a cada frame (em Play; pode rodar no editor p/ preview).
        void OnUpdate(Scene& scene, float deltaTime);

        // Limpa as partículas vivas de todos os emissores (no Stop).
        void OnSceneStop(Scene& scene);
    };

} // namespace axe