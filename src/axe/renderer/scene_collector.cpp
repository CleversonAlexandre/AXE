#include "scene_collector.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/material/light_material_evaluator.hpp"
#include <algorithm>
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
                pl.Position = tc->Data.Position;
                // Direção do cone vem da rotação do objeto, não de um
                // valor digitado — rotacionar o objeto aponta a luz.
                pl.Direction = ComputeSpotDirection(tc->Data.Rotation);
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

            for (size_t ei = 0; ei < ps.Data->Emitters.size(); ++ei)
            {
                const ParticleEmitterDef& def = ps.Data->Emitters[ei];
                if (!def.Enabled) continue;
                if (ei >= ps.EmitterRuntimes.size()) continue;
                const ParticleEmitterRuntime& rt = ps.EmitterRuntimes[ei];

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
            dc.Transform = tc->Data.GetMatrix();
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