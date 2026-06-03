#include "scene_collector.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/point_light.hpp"

namespace axe
{
    RenderQueue SceneCollector::Collect(const Scene& scene, uint32_t selectedEntityID)
    {
        RenderQueue queue;
        auto& registry = const_cast<Scene&>(scene).GetRegistry();

        // --- Luz direcional ---
        for (auto entity : registry.view<LightComponent>())
        {
            auto& lc = registry.get<LightComponent>(entity);
            if (lc.Data) { queue.Light = lc.Data.get(); break; }
        }

        // --- Point lights — posição sincronizada com TransformComponent ---
        for (auto entity : registry.view<PointLightComponent>())
        {
            auto& plc = registry.get<PointLightComponent>(entity);
            if (!plc.Data) continue;

            PointLight pl = *plc.Data;
            if (auto* tc = registry.try_get<TransformComponent>(entity))
                pl.Position = tc->Data.Position;

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