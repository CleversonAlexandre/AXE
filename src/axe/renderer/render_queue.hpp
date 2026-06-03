#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include <vector>
#include <cstdint>

namespace axe
{
    class Mesh;
    class Material;

    // Representa um draw call de mesh — sem saber de Scene ou entt.
    struct MeshDrawCall
    {
        const Mesh* Mesh = nullptr;
        const Material* Material = nullptr;
        glm::mat4       Transform{ 1.0f };
        bool            Selected = false;
    };

    // Fila de renderização — tudo que o SceneRenderer precisa saber
    // para produzir um frame, sem nenhuma dependência de ECS.
    struct RenderQueue
    {
        // Luzes
        const DirectionalLight* Light = nullptr;
        std::vector<PointLight> PointLights;

        // Meshes a renderizar
        std::vector<MeshDrawCall> Meshes;

        // Entity selecionada — ID opaco para o outline
        // SceneRenderer não precisa saber o que é uma "entity"
        uint32_t SelectedID = UINT32_MAX; // UINT32_MAX = nenhum
        glm::mat4 SelectedTransform{ 1.0f };
        const axe::Mesh* SelectedMesh = nullptr;

        void Clear()
        {
            Light = nullptr;
            PointLights.clear();
            Meshes.clear();
            SelectedID = UINT32_MAX;
            SelectedMesh = nullptr;
            SelectedTransform = glm::mat4(1.0f);
        }
    };

} // namespace axe