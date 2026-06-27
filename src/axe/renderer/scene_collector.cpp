#include "scene_collector.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/material/light_material_evaluator.hpp"
#include "axe/log/log.hpp"
#include "axe/core/time.hpp"
#include <algorithm>

namespace axe
{
    // Uma instância só, reaproveitada frame a frame — Initialize() é
    // idempotente (retorna na hora se já inicializada).
    static LightMaterialEvaluator s_LightMaterialEvaluator;

    RenderQueue SceneCollector::Collect(const Scene& scene, uint32_t selectedEntityID)
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