#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include "axe/lighting/reflection_probe.hpp"
#include <vector>
#include <cstdint>
#include <memory>
#include <map>
#include <string>

namespace axe
{
    class Mesh;
    class Material;
    class Shader;
    class Texture2D;

    // Representa um draw call de mesh — sem saber de Scene ou entt.
    struct MeshDrawCall
    {
        const Mesh* Mesh = nullptr;
        const Material* Material = nullptr;
        glm::mat4       Transform{ 1.0f };
        bool            Selected = false;
    };

    // ── Ribbon / Trail ────────────────────────────────────────────────────────
    // Cada RibbonPoint é um nó da fita — a cauda (mais velho) vem primeiro,
    // a cabeça (mais novo) vem por último. O RibbonRenderer conecta
    // consecutivos como triangle strip, perpendicular ao eixo câmera→ponto.
    struct RibbonPoint
    {
        glm::vec3 Position{ 0.f };
        glm::vec4 Color{ 1.f };
        float     Width = 0.2f;  // largura da fita neste ponto
        float     Age01 = 0.f;   // 0=nasceu, 1=morre (pra material)
    };

    struct RibbonBatch
    {
        std::vector<RibbonPoint> Points;   // ordered tail → head
        int   BlendMode = 1;
        std::shared_ptr<Shader>   OverrideShader;
        std::map<std::string, std::shared_ptr<Texture2D>> OverrideSamplers;
    };
    struct ParticleInstance
    {
        glm::vec3 Position{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float     Size = 1.0f;
        float     Rotation = 0.0f;
        float     Age01 = 0.0f;
        glm::vec3 Velocity{ 0.0f }; // pra velocity-stretch no vertex shader
    };

    // Lote de partículas de um emissor (mesmo blend mode + mesmo material).
    struct ParticleBatch
    {
        int   BlendMode = 1;
        float StretchAmount = 0.0f;

        // Flipbook — passado como uniforms pro shader do material
        bool  FlipbookEnabled = false;
        int   FlipbookCols = 1;
        int   FlipbookRows = 1;
        float FlipbookCycles = 1.0f;

        std::vector<ParticleInstance> Instances;
        std::shared_ptr<Shader>   OverrideShader;
        std::map<std::string, std::shared_ptr<Texture2D>> OverrideSamplers;
    };

    // Fila de renderização — tudo que o SceneRenderer precisa saber
    // para produzir um frame, sem nenhuma dependência de ECS.
    struct RenderQueue
    {
        // Luzes
        const DirectionalLight* Light = nullptr;
        std::vector<PointLight> PointLights;

        // Interior Volumes — caixas que bloqueiam sol + ambient/IBL em
        // ambientes fechados (ver axe/lighting/interior_volume.hpp)
        std::vector<InteriorVolumeData> InteriorVolumes;

        // Probe Volumes (GI-lite) — até 2 volumes simultâneos (limite de
        // texture units do lighting pass: 4 sampler3D por volume, units
        // 16-23). Sobrepostos combinam por peso com feather.
        std::vector<ProbeVolumeData> ProbeVolumes;

        // Reflection Probes — até 4 (units 24-27), especular local.
        std::vector<ReflectionProbeData> ReflectionProbes;

        // Pedidos de bake enfileirados pelo SceneCollector (botão "Bake"
        // no Inspector ou load de cena) — executados pelo SceneRenderer,
        // que é quem tem o contexto gráfico.
        std::vector<ProbeBakeRequest>      ProbeBakes;
        std::vector<ReflectionBakeRequest> ReflectionBakes;

        // Meshes a renderizar
        std::vector<MeshDrawCall> Meshes;

        // Partículas (billboards) — um lote por emissor
        std::vector<ParticleBatch>  ParticleBatches;
        std::vector<RibbonBatch>    RibbonBatches;

        // Entity selecionada — ID opaco para o outline
        // SceneRenderer não precisa saber o que é uma "entity"
        uint32_t SelectedID = UINT32_MAX; // UINT32_MAX = nenhum
        glm::mat4 SelectedTransform{ 1.0f };
        const axe::Mesh* SelectedMesh = nullptr;

        void Clear()
        {
            Light = nullptr;
            PointLights.clear();
            InteriorVolumes.clear();
            ProbeVolumes.clear();
            ReflectionProbes.clear();
            ProbeBakes.clear();
            ReflectionBakes.clear();
            Meshes.clear();
            ParticleBatches.clear();
            RibbonBatches.clear();
            SelectedID = UINT32_MAX;
            SelectedMesh = nullptr;
            SelectedTransform = glm::mat4(1.0f);
        }
    };

} // namespace axe