#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "gbuffer.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace axe
{
    class AXE_API LightingPass
    {
    public:
        virtual ~LightingPass() = default;

        virtual void Initialize() = 0;
        virtual void Execute(const GBuffer& gbuffer,
            uint32_t ssaoTextureID,
            uint32_t shadowMapID,
            const glm::mat4& lightSpaceMatrix,
            const glm::vec3& cameraPosition,
            const DirectionalLight* light,
            const SceneEnvironment* environment,
            const std::vector<PointLight>& pointLights = {}) = 0;
        virtual bool IsInitialized() const = 0;
        virtual void SetSSAODebug(bool debug) {}

        static std::shared_ptr<LightingPass> Create();
    };
}