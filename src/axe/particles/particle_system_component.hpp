#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include <vector>
#include <string>
#include <memory>

namespace axe
{
    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float Size = 1.0f;
        float SizeScale = 1.0f;
        float Rotation = 0.0f;
        float RotationSpeed = 0.0f;
        float Age = 0.0f;
        float Lifetime = 1.0f;
        bool  Alive = false;
    };

    // Estado de simulação de UM emitter em runtime — não serializado.
    // Um por emitter por entity: duas entities usando o mesmo asset
    // têm pools independentes, mas compartilham os parâmetros do asset.
    // Estado de runtime de um burst individual
    struct BurstState
    {
        float NextFireTime = 0.0f;
        int   CyclesDone = 0;
    };

    struct ParticleEmitterRuntime
    {
        std::vector<Particle>   Particles;
        float                   EmissionAccumulator = 0.0f;
        float                   SpawnAngle = 0.0f;
        float                   EmitterAge = 0.0f;
        std::vector<BurstState> BurstStates;
        bool                    Warmed = false;
        bool                    SubEmitterSpawned = false; // spawna só uma vez ao completar
        glm::vec3               LastOrigin{ 1e38f }; // sentinela: ainda não inicializado
    };

    // Componente na entity: referencia o asset por UUID + guarda um
    // ParticleEmitterRuntime pra cada ParticleEmitterDef do asset.
    struct AXE_API ParticleSystemComponent
    {
        std::string ParticleAssetUUID;
        std::shared_ptr<ParticleSystemAsset> Data;
        bool Playing = true;

        // Runtime — tamanho sempre igual a Data->Emitters.size()
        std::vector<ParticleEmitterRuntime> EmitterRuntimes;
    };

} // namespace axe