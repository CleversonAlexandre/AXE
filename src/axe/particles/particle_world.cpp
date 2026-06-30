#include "axe/particles/particle_world.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/utils/glm_config.hpp"

#include <random>

namespace axe
{
    namespace
    {
        // RNG simples e barato, compartilhado. -1..1 e 0..1.
        std::mt19937& rng()
        {
            static std::mt19937 gen{ std::random_device{}() };
            return gen;
        }
        float rand11()
        {
            static std::uniform_real_distribution<float> d(-1.0f, 1.0f);
            return d(rng());
        }
        float rand01()
        {
            static std::uniform_real_distribution<float> d(0.0f, 1.0f);
            return d(rng());
        }

        // Inicializa uma partícula a partir do emissor (com variações).
        void spawnParticle(ParticleSystemComponent& ps, const glm::vec3& origin, Particle& p)
        {
            p.Position = origin;

            p.Velocity = ps.StartVelocity + glm::vec3(
                rand11() * ps.VelocityVariation.x,
                rand11() * ps.VelocityVariation.y,
                rand11() * ps.VelocityVariation.z);

            p.Lifetime = glm::max(0.05f, ps.Lifetime * (1.0f + rand11() * ps.LifetimeVariation));
            p.Age = 0.0f;

            p.SizeScale = glm::max(0.0f, 1.0f + rand11() * ps.SizeVariation);
            p.Size = ps.SizeStart * p.SizeScale;
            p.Color = ps.ColorStart;

            p.Rotation = rand01() * 6.2831853f;          // ângulo inicial aleatório
            p.RotationSpeed = rand11() * 1.5f;

            p.Alive = true;
        }
    }

    void ParticleWorld::OnUpdate(Scene& scene, float deltaTime)
    {
        if (deltaTime <= 0.0f) return;

        auto& reg = scene.GetRegistry();
        reg.view<ParticleSystemComponent, TransformComponent>().each(
            [&](entt::entity, ParticleSystemComponent& ps, TransformComponent& tr)
            {
                const glm::vec3 origin = tr.Data.Position;

                // Garante o tamanho do pool (clamp defensivo)
                if (ps.MaxParticles < 1)    ps.MaxParticles = 1;
                if (ps.MaxParticles > 100000) ps.MaxParticles = 100000;
                if ((int)ps.Particles.size() != ps.MaxParticles)
                    ps.Particles.assign(ps.MaxParticles, Particle{});

                // ── 1) Emissão ──────────────────────────────────────────
                if (ps.Playing && ps.EmissionRate > 0.0f)
                {
                    ps.EmissionAccumulator += ps.EmissionRate * deltaTime;
                    int toSpawn = (int)ps.EmissionAccumulator;
                    ps.EmissionAccumulator -= (float)toSpawn;

                    for (int i = 0; i < toSpawn; ++i)
                    {
                        // Acha um slot morto (pool fixo — se cheio, ignora)
                        Particle* slot = nullptr;
                        for (auto& cand : ps.Particles)
                            if (!cand.Alive) { slot = &cand; break; }
                        if (!slot) break;
                        spawnParticle(ps, origin, *slot);
                    }
                }

                // ── 2) Simulação ────────────────────────────────────────
                for (auto& p : ps.Particles)
                {
                    if (!p.Alive) continue;

                    p.Age += deltaTime;
                    if (p.Age >= p.Lifetime) { p.Alive = false; continue; }

                    p.Velocity += ps.Gravity * deltaTime;
                    p.Position += p.Velocity * deltaTime;
                    p.Rotation += p.RotationSpeed * deltaTime;

                    const float t = p.Age / p.Lifetime;            // 0..1
                    p.Size = glm::mix(ps.SizeStart, ps.SizeEnd, t) * p.SizeScale;
                    p.Color = glm::mix(ps.ColorStart, ps.ColorEnd, t);
                }
            });
    }

    void ParticleWorld::OnSceneStop(Scene& scene)
    {
        auto& reg = scene.GetRegistry();
        reg.view<ParticleSystemComponent>().each(
            [&](entt::entity, ParticleSystemComponent& ps)
            {
                for (auto& p : ps.Particles) p.Alive = false;
                ps.EmissionAccumulator = 0.0f;
            });
    }

} // namespace axe