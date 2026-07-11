#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace axe
{
    class GBuffer;

    // Configurações do fog — editáveis via PostProcessComponent no Inspector.
    // Sem GL aqui — só dados puros.
    struct AXE_API VolumetricFogSettings
    {
        bool      Enabled = false;
        glm::vec3 FogColor{ 0.6f, 0.7f, 0.8f };
        float     Density = 0.04f;
        float     HeightBase = 0.0f;
        float     HeightFalloff = 0.15f;
        float     ScatterStrength = 0.6f;
        float     AmbientStrength = 0.15f;
        float     FogStart = 2.0f;
        float     FogEnd = 80.0f;
        int       Steps = 12;
        float     StepJitter = 0.5f;
    };

    // Interface abstrata — sem nenhum include de GL/GLFW.
    // A implementação OpenGL fica em opengl/opengl_volumetric_fog_pass.
    class AXE_API VolumetricFogPass
    {
    public:
        virtual ~VolumetricFogPass() = default;

        virtual void Initialize() = 0;
        virtual bool IsInitialized() const = 0;

        virtual void Execute(
            const GBuffer& gbuffer,
            const VolumetricFogSettings& settings,
            const glm::mat4& invViewProj,
            const glm::vec3& cameraPosition,
            const std::vector<PointLight>& pointLights,
            float                        time,
            uint32_t                     width,
            uint32_t                     height,
            const std::vector<InteriorVolumeData>& interiorVolumes = {},
            const std::vector<ProbeVolumeData>& probeVolumes = {}) = 0;

        // Factory — retorna a implementação correta para a API ativa
        static std::shared_ptr<VolumetricFogPass> Create();
    };

} // namespace axe