#include "scene_collector.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/material/light_material_evaluator.hpp"
#include "axe/core/time.hpp"
#include <algorithm>
#include <cmath>
#include "axe/log/log.hpp"
#include "axe/core/time.hpp"
#include <algorithm>

namespace axe
{
    // Uma instância só, reaproveitada frame a frame — Initialize() é
    // idempotente (retorna na hora se já inicializada).
    static LightMaterialEvaluator s_LightMaterialEvaluator;

    RenderQueue SceneCollector::Collect(const Scene& scene, uint32_t selectedEntityID,
        const glm::vec3& cameraPosition)
    {
        RenderQueue queue;
        auto& registry = const_cast<Scene&>(scene).GetRegistry();

        // --- Luz direcional ---
        DirectionalLight* mutableLight = nullptr; // não-const, só pra esta seção
        for (auto entity : registry.view<LightComponent>())
        {
            auto& lc = registry.get<LightComponent>(entity);
            if (lc.Data) { mutableLight = lc.Data.get(); queue.Light = mutableLight; break; }
        }

        if (mutableLight && mutableLight->LightMaterialShader)
        {
            s_LightMaterialEvaluator.Initialize();
            // Sobrescreve (não multiplica) — recalculado do zero a cada
            // frame. Ver comentário em LightMaterialResult: esta luz é
            // referenciada por ponteiro, não copiada, então multiplicar
            // Color direto aqui corromperia o valor a cada frame.
            mutableLight->LightMaterialResult = s_LightMaterialEvaluator.Evaluate(
                mutableLight->LightMaterialShader, mutableLight->LightMaterialSamplers,
                Time::Elapsed(), glm::vec3(0.0f));
        }
        else if (mutableLight)
        {
            mutableLight->LightMaterialResult = glm::vec3(1.0f);
        }

        // --- Point lights — posição sincronizada com TransformComponent ---
        for (auto entity : registry.view<PointLightComponent>())
        {
            auto& plc = registry.get<PointLightComponent>(entity);
            if (!plc.Data) continue;

            PointLight pl = *plc.Data;
            if (auto* tc = registry.try_get<TransformComponent>(entity))
            {
                // Extrai posição e direção do transform MUNDIAL
                glm::mat4 worldMat = scene.GetWorldTransform(entity);
                pl.Position = glm::vec3(worldMat[3]); // coluna 3 = translação

                if (pl.IsSpot)
                {
                    // Extrai eixo -Y do transform mundial = direção do spot
                    glm::vec3 worldUp = glm::normalize(glm::vec3(worldMat[1]));
                    pl.Direction = -worldUp;
                }
            }

            // (O antigo flicker/pulso por sinf(Time) foi removido — o Light
            // Material faz isso pelo grafo, de forma mais flexível. Os campos
            // Animated/AnimSpeed/AnimAmplitude continuam na struct apenas por
            // compatibilidade de cenas antigas; não têm mais efeito.)

            // Light Material — grafo real (Time, Sine, Noise, etc.),
            // avaliado uma vez por frame, multiplicado na Color. Um
            // Emissive em escala de cinza oscilando 0..1 funciona como
            // flicker/pulso; colorido tinge a luz.
            if (pl.LightMaterialShader)
            {
                s_LightMaterialEvaluator.Initialize();
                glm::vec3 emissive = s_LightMaterialEvaluator.Evaluate(
                    pl.LightMaterialShader, pl.LightMaterialSamplers,
                    Time::Elapsed(), pl.Position);
                pl.Color = pl.Color * emissive;
            }

            queue.PointLights.push_back(pl);
        }

        // --- Meshes — percorre a hierarquia de raiz ---
        auto roots = const_cast<Scene&>(scene).GetRootEntities();
        for (auto entity : roots)
            CollectEntity(scene, entity, queue, selectedEntityID);

        // --- Partículas — um lote por emissor habilitado ---
        for (auto entity : registry.view<ParticleSystemComponent>())
        {
            auto& ps = registry.get<ParticleSystemComponent>(entity);
            if (!ps.Data) continue;

            // Extrai posição e rotação do transform MUNDIAL — necessário para
            // beam e particle light com hierarquia pai→filho correta.
            glm::vec3 entityOrigin(0.f);
            glm::mat3 entityRot(1.f);
            if (auto* tc = registry.try_get<TransformComponent>(entity))
            {
                glm::mat4 m = scene.GetWorldTransform(entity);
                entityOrigin = glm::vec3(m[3]);
                entityRot = glm::mat3(
                    glm::normalize(glm::vec3(m[0])),
                    glm::normalize(glm::vec3(m[1])),
                    glm::normalize(glm::vec3(m[2])));
            }

            for (size_t ei = 0; ei < ps.Data->Emitters.size(); ++ei)
            {
                const ParticleEmitterDef& def = ps.Data->Emitters[ei];
                if (!def.Enabled) continue;
                if (ei >= ps.EmitterRuntimes.size()) continue;
                const ParticleEmitterRuntime& rt = ps.EmitterRuntimes[ei];

                // ── Beam (lightning) — ribbon procedural entre dois pontos ───
                if (def.IsBeam)
                {
                    glm::vec3 start = entityOrigin + entityRot * def.SpawnOffset;
                    glm::vec3 end = entityOrigin + entityRot * (def.SpawnOffset + def.BeamTargetOffset);
                    glm::vec3 dir = end - start;
                    float     len = glm::length(dir);
                    if (len > 0.001f)
                    {
                        glm::vec3 dirN = dir / len;
                        glm::vec3 up = (std::abs(glm::dot(dirN, glm::vec3(0, 1, 0))) > 0.99f)
                            ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                        glm::vec3 p1 = glm::normalize(glm::cross(dirN, up));
                        glm::vec3 p2 = glm::normalize(glm::cross(dirN, p1));

                        float t_now = axe::Time::Elapsed();
                        float seed = (float)ei * 17.3f; // seed único por emitter

                        RibbonBatch rb;
                        rb.BlendMode = def.BlendMode;
                        rb.OverrideShader = def.ParticleMaterialShader;
                        rb.OverrideSamplers = def.ParticleMaterialSamplers;
                        rb.Points.reserve(def.BeamPoints + 1);

                        for (int bi = 0; bi <= def.BeamPoints; ++bi)
                        {
                            float t = (float)bi / def.BeamPoints;
                            glm::vec3 base = glm::mix(start, end, t);

                            // sin(t*π) = 0 nos extremos, 1 no meio
                            float env = std::sin(t * glm::pi<float>());
                            float d1 = std::sin(t * 8.7f + t_now * def.BeamFlickerSpeed + seed)
                                * def.BeamDeviation * env;
                            float d2 = std::cos(t * 6.3f + t_now * def.BeamFlickerSpeed + seed + 1.5f)
                                * def.BeamDeviation * env;
                            // Segunda frequência para detalhe maior
                            d1 += std::sin(t * 19.f + t_now * def.BeamFlickerSpeed * 1.7f + seed)
                                * def.BeamDeviation * 0.35f * env;
                            d2 += std::cos(t * 14.f + t_now * def.BeamFlickerSpeed * 1.5f + seed + 3.f)
                                * def.BeamDeviation * 0.35f * env;

                            glm::vec3 pos = base + p1 * d1 + p2 * d2;
                            glm::vec4 col = glm::mix(def.ColorStart, def.ColorEnd, t);
                            rb.Points.push_back({ pos, col, def.BeamWidth, t });
                        }
                        queue.RibbonBatches.push_back(std::move(rb));
                    }
                    continue; // pula coleta normal de partículas
                }

                if (def.IsRibbon)
                {
                    // ── Ribbon: coleta pontos ordenados cauda→cabeça ──────
                    // Filtra vivos e ordena por idade decrescente (mais velho
                    // = cauda da fita, mais novo = cabeça).
                    std::vector<const Particle*> alive;
                    alive.reserve(rt.Particles.size());
                    for (const auto& p : rt.Particles)
                        if (p.Alive) alive.push_back(&p);

                    if (alive.size() >= 2)
                    {
                        std::sort(alive.begin(), alive.end(),
                            [](const Particle* a, const Particle* b)
                            { return a->Age > b->Age; }); // mais velho primeiro (cauda)

                        RibbonBatch rb;
                        rb.BlendMode = def.BlendMode;
                        rb.OverrideShader = def.ParticleMaterialShader;
                        rb.OverrideSamplers = def.ParticleMaterialSamplers;
                        rb.Points.reserve(alive.size());

                        for (const auto* p : alive)
                        {
                            float age01 = (p->Lifetime > 0.f)
                                ? glm::clamp(p->Age / p->Lifetime, 0.f, 1.f) : 0.f;
                            rb.Points.push_back({ p->Position, p->Color, p->Size, age01 });
                        }
                        queue.RibbonBatches.push_back(std::move(rb));
                    }
                }
                else
                {
                    ParticleBatch batch;
                    batch.BlendMode = def.BlendMode;
                    batch.StretchAmount = def.StretchAmount;
                    batch.FlipbookEnabled = def.FlipbookEnabled;
                    batch.FlipbookCols = def.FlipbookCols;
                    batch.FlipbookRows = def.FlipbookRows;
                    batch.FlipbookCycles = def.FlipbookCycles;
                    batch.OverrideShader = def.ParticleMaterialShader;
                    batch.OverrideSamplers = def.ParticleMaterialSamplers;

                    batch.Instances.reserve(rt.Particles.size());
                    for (const auto& p : rt.Particles)
                    {
                        if (!p.Alive) continue;
                        float age01 = (p.Lifetime > 0.0f)
                            ? glm::clamp(p.Age / p.Lifetime, 0.0f, 1.0f) : 0.0f;
                        batch.Instances.push_back({ p.Position, p.Color, p.Size, p.Rotation, age01, p.Velocity });
                    }

                    if (!batch.Instances.empty())
                        queue.ParticleBatches.push_back(std::move(batch));

                    // ── Particle Light ────────────────────────────────────────
                    // Adiciona um Point Light dinâmico na posição do emitter.
                    // Intensidade escala pelo número de partículas vivas (opcional).
                    if (def.LightEnabled && !rt.Particles.empty())
                    {
                        int alive = 0;
                        glm::vec3 lightPos(0.f);

                        // Calcula posição média das partículas vivas pra luz
                        // acompanhar o centro real do efeito
                        for (const auto& p : rt.Particles)
                            if (p.Alive) { lightPos += p.Position; ++alive; }

                        if (alive > 0)
                        {
                            lightPos /= (float)alive;

                            float intensity = def.LightIntensity;
                            if (def.LightScaleByParticles)
                            {
                                float ratio = glm::clamp((float)alive / (float)rt.Particles.size(), 0.f, 1.f);
                                intensity *= ratio;
                            }

                            PointLight pl;
                            pl.Position = lightPos;
                            pl.Color = def.LightColor;
                            pl.Intensity = intensity;
                            pl.Radius = def.LightRadius;
                            pl.Animated = def.LightFlicker;
                            pl.AnimSpeed = def.LightFlickerSpeed;
                            pl.AnimAmplitude = intensity * def.LightFlickerAmount;
                            queue.PointLights.push_back(pl);
                        }
                    }
                } // end else (billboard, não ribbon)
            }
        }

        // ── Depth Sorting — back-to-front pra BlendMode Alpha (0) ────────
        // Additive (1) não precisa: somar luz é comutativo, a ordem não
        // importa. Alpha precisa: o que está atrás deve ser desenhado primeiro
        // ou o resultado fica incorreto (artefatos de transparência).
        for (auto& b : queue.ParticleBatches)
        {
            if (b.BlendMode != 0) continue; // só Alpha
            std::sort(b.Instances.begin(), b.Instances.end(),
                [&cameraPosition](const ParticleInstance& a, const ParticleInstance& b)
                {
                    float da = glm::length(a.Position - cameraPosition);
                    float db = glm::length(b.Position - cameraPosition);
                    return da > db; // maior distância primeiro (back-to-front)
                });
        }

        return queue;
    }

    void SceneCollector::CollectEntity(const Scene& scene,
        entt::entity entity,
        RenderQueue& queue,
        uint32_t selectedEntityID)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;

        // Ignora entidades que não contribuem para o render de mesh
        if (registry.any_of<PostProcessComponent>(entity)) return;
        if (registry.any_of<LightComponent>(entity))       return;
        if (registry.any_of<PointLightComponent>(entity))  return;

        if (registry.any_of<FolderComponent>(entity))
        {
            // Folders são só organização — percorre filhos
            auto* rel = registry.try_get<RelationshipComponent>(entity);
            if (rel)
                for (auto child : rel->Children)
                    CollectEntity(scene, child, queue, selectedEntityID);
            return;
        }

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        if (mc && mc->Data && tc)
        {
            MeshDrawCall dc;
            dc.Mesh = mc->Data.get();
            dc.Material = mat ? mat->Data.get() : nullptr;
            // Usa o transform MUNDIAL (herda a hierarquia de pais)
            dc.Transform = scene.GetWorldTransform(entity);
            dc.Selected = ((uint32_t)entity == selectedEntityID);

            queue.Meshes.push_back(dc);

            // Registra selecionado para o outline
            if (dc.Selected)
            {
                queue.SelectedID = selectedEntityID;
                queue.SelectedMesh = dc.Mesh;
                queue.SelectedTransform = dc.Transform;
            }
        }

        // Percorre filhos
        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel)
            for (auto child : rel->Children)
                CollectEntity(scene, child, queue, selectedEntityID);
    }

} // namespace axe