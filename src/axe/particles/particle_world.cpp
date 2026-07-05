#include "axe/particles/particle_world.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/asset/asset_database.hpp"

#include <random>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>

namespace axe
{
    // Sub-emitter spawn queue — fora da classe pra não exportar via AXE_API
    struct PendingSubEmitter { glm::vec3 Position; std::string AssetUUID; };
    static std::vector<PendingSubEmitter> s_PendingSubEmitters;

    ParticleWorld::ParticleWorld() = default;
    ParticleWorld::~ParticleWorld() = default;

    namespace
    {
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

        // Calcula offset local — todos os shapes exceto Helix (que é sequencial)
        glm::vec3 computeSpawnOffset(const ParticleEmitterDef& def)
        {
            switch (def.SpawnShape)
            {
            case 1: // Esfera
            {
                float z = rand11();
                float theta = rand01() * 6.2831853f;
                float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
                glm::vec3 dir(r * std::cos(theta), z, r * std::sin(theta));
                float radius = def.SpawnRadius;
                if (!def.SpawnOnSurface)
                    radius *= std::cbrt(std::max(0.0f, rand01()));
                return dir * radius;
            }
            case 2: // Anel
            {
                float theta = rand01() * 6.2831853f;
                return glm::vec3(std::cos(theta), 0.0f, std::sin(theta)) * def.SpawnRadius;
            }
            case 3: // Cilindro
            {
                float theta = rand01() * 6.2831853f;
                float height = rand11() * def.SpawnHeight * 0.5f;
                return glm::vec3(std::cos(theta), 0.0f, std::sin(theta)) * def.SpawnRadius
                    + glm::vec3(0.0f, height, 0.0f);
            }
            case 5: // Cone — posição na base do disco
            {
                float theta = rand01() * 6.2831853f;
                float r = std::sqrt(rand01()) * def.SpawnRadius; // distribuição uniforme
                return glm::vec3(std::cos(theta) * r, 0.0f, std::sin(theta) * r);
            }
            default: // Point
                return glm::vec3(0.0f);
            }
        }

        // Avalia Color Curve em t ∈ [0,1]. Fallback linear se curva vazia.
        glm::vec4 evalColorCurve(const std::vector<ColorKey>& curve,
            float t, glm::vec4 start, glm::vec4 end)
        {
            if (curve.empty()) return glm::mix(start, end, t);
            if (curve.size() == 1) return curve[0].Color;
            if (t <= curve.front().Time) return curve.front().Color;
            if (t >= curve.back().Time)  return curve.back().Color;
            for (size_t i = 0; i < curve.size() - 1; ++i)
            {
                if (t >= curve[i].Time && t <= curve[i + 1].Time)
                {
                    float lt = (curve[i + 1].Time - curve[i].Time) < 1e-6f ? 0.0f :
                        (t - curve[i].Time) / (curve[i + 1].Time - curve[i].Time);
                    return glm::mix(curve[i].Color, curve[i + 1].Color, lt);
                }
            }
            return glm::mix(start, end, t);
        }

        // Avalia Size Curve em t ∈ [0,1]. Fallback linear se curva vazia.
        float evalSizeCurve(const std::vector<SizeKey>& curve,
            float t, float start, float end)
        {
            if (curve.empty()) return glm::mix(start, end, t);
            if (curve.size() == 1) return curve[0].Size;
            if (t <= curve.front().Time) return curve.front().Size;
            if (t >= curve.back().Time)  return curve.back().Size;
            for (size_t i = 0; i < curve.size() - 1; ++i)
            {
                if (t >= curve[i].Time && t <= curve[i + 1].Time)
                {
                    float lt = (curve[i + 1].Time - curve[i].Time) < 1e-6f ? 0.0f :
                        (t - curve[i].Time) / (curve[i + 1].Time - curve[i].Time);
                    return glm::mix(curve[i].Size, curve[i + 1].Size, lt);
                }
            }
            return glm::mix(start, end, t);
        }

        // Offset do Helix — sequencial: cada partícula avança o ângulo acumulado
        // no runtime, criando a espiral ao longo do eixo Y.
        glm::vec3 computeHelixOffset(const ParticleEmitterDef& def, ParticleEmitterRuntime& rt)
        {
            float angle = rt.SpawnAngle;

            // Cada partícula avança o ângulo. O passo determina o
            // "pitch" da espiral — menor passo = hélice mais apertada.
            float totalAngle = def.SpawnHelixTurns * 6.2831853f;
            float step = totalAngle / std::max(1.0f, (float)def.MaxParticles * 0.5f);
            rt.SpawnAngle += step;

            // Mapeia o ângulo pra altura ao longo do eixo (0..Height)
            float t = std::fmod(angle / totalAngle, 1.0f);
            float height = (t - 0.5f) * def.SpawnHeight;

            return glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * def.SpawnRadius
                + glm::vec3(0.0f, height, 0.0f);
        }

        void spawnParticle(const ParticleEmitterDef& def,
            const glm::vec3& origin,
            const glm::mat3& rot,
            ParticleEmitterRuntime& rt,
            Particle& p)
        {
            // Helix é sequencial — usa rt para avançar ângulo
            glm::vec3 localOffset = (def.SpawnShape == 4)
                ? computeHelixOffset(def, rt)
                : computeSpawnOffset(def);

            // SpawnOffset: deslocamento local da origem de emissão
            // (ex: Y=1.5 move o Ring pra 1.5u acima da origem da entity)
            glm::vec3 worldOffset = rot * (localOffset + def.SpawnOffset);
            p.Position = origin + worldOffset;

            if (def.SpawnShape == 5) // Cone — velocidade dentro do ângulo do cone
            {
                float halfAngle = glm::radians(def.SpawnConeAngle);
                float phi = rand01() * halfAngle;
                float theta = rand01() * 6.2831853f;
                float sinPhi = std::sin(phi);
                glm::vec3 dir(sinPhi * std::cos(theta), std::cos(phi), sinPhi * std::sin(theta));
                float speed = glm::length(def.StartVelocity)
                    + rand01() * glm::length(def.VelocityVariation);
                p.Velocity = rot * dir * speed;
            }
            else if (def.SpawnShape == 4)
            {
                glm::vec3 axisVel = rot * glm::vec3(0.0f, glm::length(def.StartVelocity), 0.0f);
                p.Velocity = axisVel + glm::vec3(
                    rand11() * def.VelocityVariation.x,
                    rand11() * def.VelocityVariation.y,
                    rand11() * def.VelocityVariation.z);
            }
            else if (def.VelocityFollowsShape && def.SpawnShape != 0 && glm::length(worldOffset) > 1e-5f)
            {
                float speed = glm::length(def.StartVelocity)
                    + rand11() * glm::length(def.VelocityVariation);
                p.Velocity = glm::normalize(worldOffset) * speed;
            }
            else
            {
                glm::vec3 localVel = def.StartVelocity + glm::vec3(
                    rand11() * def.VelocityVariation.x,
                    rand11() * def.VelocityVariation.y,
                    rand11() * def.VelocityVariation.z);
                p.Velocity = rot * localVel;
            }

            p.Lifetime = glm::max(0.05f, def.Lifetime * (1.0f + rand11() * def.LifetimeVariation));
            p.Age = 0.0f;
            p.SizeScale = glm::max(0.0f, 1.0f + rand11() * def.SizeVariation);
            p.Size = def.SizeCurve.empty()
                ? def.SizeStart * p.SizeScale
                : evalSizeCurve(def.SizeCurve, 0.0f, def.SizeStart, def.SizeEnd) * p.SizeScale;
            p.Color = def.ColorCurve.empty()
                ? def.ColorStart
                : evalColorCurve(def.ColorCurve, 0.0f, def.ColorStart, def.ColorEnd);
            // Rotação inicial com faixa min/max controlada
            p.Rotation = glm::mix(def.RotationMin, def.RotationMax, rand01());
            p.RotationSpeed = glm::mix(def.RotationSpeedMin, def.RotationSpeedMax, rand01());
            p.Alive = true;
        }

        glm::mat3 extractRotation(const Transform& t)
        {
            glm::mat4 m = t.GetMatrix();
            return glm::mat3(
                glm::normalize(glm::vec3(m[0])),
                glm::normalize(glm::vec3(m[1])),
                glm::normalize(glm::vec3(m[2])));
        }
    }

    void ParticleWorld::OnUpdate(Scene& scene, float deltaTime,
        bool allowDestroy, const glm::vec3& cameraPosition)
    {
        if (deltaTime <= 0.0f) return;
        m_Time += deltaTime;

        auto& reg = scene.GetRegistry();
        reg.view<ParticleSystemComponent, TransformComponent>().each(
            [&](entt::entity entt_entity, ParticleSystemComponent& ps, TransformComponent& tr)
            {
                if (!ps.Data) return;

                // Usa o transform MUNDIAL — partículas filhas seguem o pai
                glm::mat4 worldMat = scene.GetWorldTransform(entt_entity);
                const glm::vec3 origin = glm::vec3(worldMat[3]);
                const glm::mat3 rot = glm::mat3(
                    glm::normalize(glm::vec3(worldMat[0])),
                    glm::normalize(glm::vec3(worldMat[1])),
                    glm::normalize(glm::vec3(worldMat[2])));

                if (ps.EmitterRuntimes.size() != ps.Data->Emitters.size())
                    ps.EmitterRuntimes.resize(ps.Data->Emitters.size());

                bool anyAlive = false; // pra AutoDestroy
                bool anyStillEmitting = false;

                for (size_t ei = 0; ei < ps.Data->Emitters.size(); ++ei)
                {
                    const ParticleEmitterDef& def = ps.Data->Emitters[ei];
                    ParticleEmitterRuntime& rt = ps.EmitterRuntimes[ei];

                    if (!def.Enabled) continue;

                    // Beam emitters são gerados proceduralmente no SceneCollector
                    // — não precisam de simulação de partículas.
                    if (def.IsBeam) continue;

                    int cap = glm::clamp(def.MaxParticles, 1, 100000);
                    if ((int)rt.Particles.size() != cap)
                        rt.Particles.assign(cap, Particle{});

                    // ── Warmup — pré-simula no primeiro tick ──────────────
                    if (!rt.Warmed && def.WarmupTime > 0.0f)
                    {
                        rt.Warmed = true;
                        constexpr float kStep = 1.0f / 30.0f; // 30 FPS fictícios
                        float remaining = def.WarmupTime;
                        while (remaining > 0.0f)
                        {
                            float step = glm::min(remaining, kStep);
                            remaining -= step;
                            // Emissão contínua no warmup
                            if (def.EmissionRate > 0.0f)
                            {
                                rt.EmissionAccumulator += def.EmissionRate * step;
                                int toSpawn = (int)rt.EmissionAccumulator;
                                rt.EmissionAccumulator -= (float)toSpawn;
                                for (int i = 0; i < toSpawn; ++i)
                                {
                                    Particle* slot = nullptr;
                                    for (auto& cand : rt.Particles)
                                        if (!cand.Alive) { slot = &cand; break; }
                                    if (!slot) break;
                                    spawnParticle(def, origin, rot, rt, *slot);
                                }
                            }
                            // Simulação de cada step do warmup
                            for (auto& p : rt.Particles)
                            {
                                if (!p.Alive) continue;
                                p.Age += step;
                                if (p.Age >= p.Lifetime) { p.Alive = false; continue; }
                                p.Velocity += def.Gravity * step;
                                p.Position += p.Velocity * step;
                                p.Rotation += p.RotationSpeed * step;
                                const float t = p.Age / p.Lifetime;
                                p.Size = glm::mix(def.SizeStart, def.SizeEnd, t) * p.SizeScale;
                                p.Color = glm::mix(def.ColorStart, def.ColorEnd, t);
                            }
                        }
                    }
                    else
                    {
                        rt.Warmed = true; // marca mesmo sem warmup
                    }

                    // ── Duration — para de emitir quando atingir o limite ─
                    bool durationExpired = (def.Duration >= 0.0f &&
                        rt.EmitterAge >= def.Duration);

                    rt.EmitterAge += deltaTime;

                    // Sincroniza BurstStates
                    if (rt.BurstStates.size() != def.Bursts.size())
                    {
                        size_t old = rt.BurstStates.size();
                        rt.BurstStates.resize(def.Bursts.size());
                        for (size_t bi = old; bi < def.Bursts.size(); ++bi)
                            rt.BurstStates[bi].NextFireTime = def.Bursts[bi].Time;
                    }

                    // ── LOD — escala emission rate por distância ──────────
                    float lodScale = 1.0f;
                    if (def.LODEnabled && def.LODDistanceZero > def.LODDistanceFull)
                    {
                        float dist = glm::length(origin - cameraPosition);
                        lodScale = 1.0f - glm::clamp(
                            (dist - def.LODDistanceFull) / (def.LODDistanceZero - def.LODDistanceFull),
                            0.0f, 1.0f);
                    }

                    // ── Emissão contínua (só se dentro da Duration) ───────
                    if (ps.Playing && !durationExpired && def.EmissionRate > 0.0f && lodScale > 0.0f)
                    {
                        float effectiveRate = def.EmissionRate * lodScale;
                        rt.EmissionAccumulator += effectiveRate * deltaTime;
                        int toSpawn = (int)rt.EmissionAccumulator;
                        rt.EmissionAccumulator -= (float)toSpawn;
                        for (int i = 0; i < toSpawn; ++i)
                        {
                            Particle* slot = nullptr;
                            for (auto& cand : rt.Particles)
                                if (!cand.Alive) { slot = &cand; break; }
                            if (!slot) break;
                            spawnParticle(def, origin, rot, rt, *slot);
                        }
                    }

                    // ── Bursts ────────────────────────────────────────────
                    if (ps.Playing && !durationExpired)
                    {
                        for (size_t bi = 0; bi < def.Bursts.size(); ++bi)
                        {
                            const ParticleBurstDef& burst = def.Bursts[bi];
                            BurstState& bs = rt.BurstStates[bi];
                            bool infinite = (burst.Cycles == -1);
                            bool canFire = infinite || (bs.CyclesDone < burst.Cycles);
                            if (canFire && rt.EmitterAge >= bs.NextFireTime)
                            {
                                int spawned = 0;
                                for (auto& cand : rt.Particles)
                                {
                                    if (!cand.Alive && spawned < burst.Count)
                                    {
                                        spawnParticle(def, origin, rot, rt, cand);
                                        ++spawned;
                                    }
                                }
                                ++bs.CyclesDone;
                                bs.NextFireTime += glm::max(0.001f, burst.Interval);
                            }
                        }
                    }

                    if (!durationExpired) anyStillEmitting = true;

                    // ── Local Space — arrasta partículas com o emissor ────
                    if (def.LocalSpace && rt.LastOrigin.x < 1e37f)
                    {
                        glm::vec3 delta = origin - rt.LastOrigin;
                        if (glm::length(delta) > 0.0001f)
                            for (auto& p : rt.Particles)
                                if (p.Alive) p.Position += delta;
                    }
                    rt.LastOrigin = origin;

                    // ── Simulação ────────────────────────────────────────
                    bool thisEmitterAlive = false; // só partículas DESTE emitter

                    for (auto& p : rt.Particles)
                    {
                        if (!p.Alive) continue;
                        anyAlive = true;
                        thisEmitterAlive = true;
                        p.Age += deltaTime;
                        if (p.Age >= p.Lifetime)
                        {
                            p.Alive = false;
                            thisEmitterAlive = false; // recalcula no próximo check
                            continue;
                        }

                        // Gravidade
                        p.Velocity += def.Gravity * deltaTime;

                        // Velocity Limit
                        if (def.VelocityLimit > 0.0f)
                        {
                            float spd = glm::length(p.Velocity);
                            if (spd > def.VelocityLimit)
                                p.Velocity *= def.VelocityLimit / spd;
                        }

                        // Orbit Force
                        if (def.OrbitStrength != 0.0f)
                        {
                            glm::vec3 toP = p.Position - origin;
                            glm::vec3 axis = glm::normalize(rot * def.OrbitAxis);
                            glm::vec3 orbitForce = glm::cross(axis, toP) * def.OrbitStrength;
                            p.Velocity += orbitForce * deltaTime;
                        }

                        // Turbulência
                        if (def.TurbulenceStrength > 0.0f)
                        {
                            float f = def.TurbulenceFrequency;
                            float tt = m_Time * def.TurbulenceSpeed;
                            float px = p.Position.x * f, py = p.Position.y * f, pz = p.Position.z * f;
                            glm::vec3 turb(
                                std::sin(py + tt * 1.3f) + std::sin(pz + tt * 0.7f),
                                std::sin(px + tt * 0.9f) + std::sin(pz + tt * 1.7f),
                                std::sin(px + tt * 1.1f) + std::sin(py + tt * 0.5f));
                            p.Velocity += turb * (def.TurbulenceStrength * 0.5f * deltaTime);
                        }

                        p.Position += p.Velocity * deltaTime;
                        p.Rotation += p.RotationSpeed * deltaTime;

                        // Colisão com plano Y
                        if (def.CollisionEnabled && p.Position.y < def.CollisionY)
                        {
                            p.Position.y = def.CollisionY;
                            p.Velocity.y = std::abs(p.Velocity.y) * def.CollisionBounciness;
                            p.Velocity.x *= (1.0f - def.CollisionFriction);
                            p.Velocity.z *= (1.0f - def.CollisionFriction);
                            if (p.Velocity.y < 0.05f) p.Velocity.y = 0.0f;
                        }

                        const float tl = p.Age / p.Lifetime;
                        p.Size = evalSizeCurve(def.SizeCurve, tl, def.SizeStart, def.SizeEnd) * p.SizeScale;
                        p.Color = evalColorCurve(def.ColorCurve, tl, def.ColorStart, def.ColorEnd);
                    }

                    // Recalcula thisEmitterAlive pós-simulação (garante precisão)
                    thisEmitterAlive = false;
                    for (auto& p : rt.Particles)
                        if (p.Alive) { thisEmitterAlive = true; anyAlive = true; break; }

                    // Sub-emitter: spawna UMA VEZ quando ESTE emitter completa.
                    // Usa thisEmitterAlive (não anyAlive global) pra não esperar
                    // outros emitters terminarem.
                    if (allowDestroy
                        && !def.SubEmitterUUID.empty()
                        && durationExpired
                        && !thisEmitterAlive
                        && !rt.SubEmitterSpawned)
                    {
                        rt.SubEmitterSpawned = true;
                        s_PendingSubEmitters.push_back({ origin, def.SubEmitterUUID });
                    }
                }

                // ── AutoDestroy ───────────────────────────────────────────
                // Só em Play mode (allowDestroy = true). Em Edit nunca
                // destroi — o emitter expira mas a entity permanece.
                if (allowDestroy && !anyStillEmitting && !anyAlive)
                {
                    bool shouldDestroy = false;
                    for (auto& def : ps.Data->Emitters)
                        if (def.AutoDestroy) { shouldDestroy = true; break; }
                    if (shouldDestroy)
                        reg.destroy(entt_entity);
                }
            }); // fim de reg.view().each()

        // ── Spawn de sub-emissores ─────────────────────────────────────────────
        // Processados APÓS o loop principal pra não invalidar o view iterator.
        if (!s_PendingSubEmitters.empty())
        {
            for (auto& pending : s_PendingSubEmitters)
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(pending.AssetUUID);
                if (!record || !std::filesystem::exists(record->FilePath)) continue;

                auto asset = ParticleSystemAsset::LoadFromFile(record->FilePath);
                if (!asset) continue;

                // Força AutoDestroy e limpa SubEmitterUUID pra evitar
                // cascade infinito (sub-emitter spawna sub-sub-emitter etc.)
                for (auto& emDef : asset->Emitters)
                {
                    emDef.AutoDestroy = true;
                    emDef.SubEmitterUUID = ""; // sem cascade
                }

                auto entity = scene.CreateEntity("SubEmitter");
                auto& tc = scene.GetRegistry().emplace_or_replace<TransformComponent>(entity);
                tc.Data.Position = pending.Position;

                auto& ps = scene.GetRegistry().emplace<ParticleSystemComponent>(entity);
                ps.Data = asset;
                ps.ParticleAssetUUID = pending.AssetUUID;
                ps.Playing = true;
                ps.EmitterRuntimes.resize(asset->Emitters.size());
            }
            s_PendingSubEmitters.clear();
        }
    } // fim de OnUpdate

    void ParticleWorld::OnSceneStop(Scene& scene)
    {
        auto& reg = scene.GetRegistry();
        reg.view<ParticleSystemComponent>().each(
            [&](entt::entity, ParticleSystemComponent& ps)
            {
                for (auto& rt : ps.EmitterRuntimes)
                {
                    for (auto& p : rt.Particles) p.Alive = false;
                    rt.EmissionAccumulator = 0.0f;
                    rt.SpawnAngle = 0.0f;
                    rt.EmitterAge = 0.0f;
                    rt.Warmed = false;
                    rt.SubEmitterSpawned = false;
                    rt.LastOrigin = glm::vec3(1e38f);
                    rt.BurstStates.clear();
                }
            });
    }

} // namespace axe