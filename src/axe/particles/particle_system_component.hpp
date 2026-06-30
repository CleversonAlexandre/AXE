#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <vector>
#include <string>

namespace axe
{
    // Uma partícula individual em runtime. Os campos Color/Size são
    // "atuais" (recalculados a cada frame pela ParticleWorld a partir do
    // emissor + idade), pra o renderer só ler e desenhar.
    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        glm::vec4 Color{ 1.0f };       // atual (interpolada start->end)
        float Size = 1.0f;             // atual
        float SizeScale = 1.0f;        // fator aleatório fixo no spawn
        float Rotation = 0.0f;
        float RotationSpeed = 0.0f;
        float Age = 0.0f;
        float Lifetime = 1.0f;
        bool  Alive = false;
    };

    // Emissor de partículas anexado a uma entity (estilo PointLight/Material).
    // Fase 1 (MVP): parâmetros inline + pool CPU. Depois isso vira um asset.
    struct AXE_API ParticleSystemComponent
    {
        // ── Controle ──────────────────────────────────────────────
        bool Playing = true;
        bool Looping = true;   // (reservado p/ bursts/duração na fase 2)

        // ── Emissão ───────────────────────────────────────────────
        float EmissionRate = 30.0f;   // partículas por segundo
        int   MaxParticles = 1000;    // tamanho do pool

        // ── Vida ──────────────────────────────────────────────────
        float Lifetime = 2.0f;
        float LifetimeVariation = 0.3f;   // fração +/- (0.3 = ±30%)

        // ── Movimento ─────────────────────────────────────────────
        glm::vec3 StartVelocity{ 0.0f, 2.5f, 0.0f };
        glm::vec3 VelocityVariation{ 1.0f, 0.5f, 1.0f };
        glm::vec3 Gravity{ 0.0f, -1.0f, 0.0f };

        // ── Aparência (interpola start->end ao longo da vida) ─────
        glm::vec4 ColorStart{ 1.0f, 0.6f, 0.15f, 1.0f }; // laranja (fogo)
        glm::vec4 ColorEnd{ 0.6f, 0.0f, 0.0f, 0.0f };    // some em vermelho
        float SizeStart = 0.4f;
        float SizeEnd = 0.0f;
        float SizeVariation = 0.2f;   // fração +/-

        // ── Render ────────────────────────────────────────────────
        std::string TextureUUID;      // textura do billboard ("" = quad sólido)
        int BlendMode = 1;            // 0 = alpha, 1 = additive

        // ── Runtime (NÃO serializado) ─────────────────────────────
        std::vector<Particle> Particles;
        float EmissionAccumulator = 0.0f;
    };

} // namespace axe