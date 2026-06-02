#pragma once
#include "axe/graphics/renderer/lighting_pass.hpp"

namespace axe
{
    class Shader;

    class OpenGLLightingPass final : public LightingPass
    {
    public:
        void Initialize() override;
        void Execute(const GBuffer& gbuffer,
            uint32_t ssaoTextureID,
            uint32_t shadowMapID,
            const glm::mat4& lightSpaceMatrix,
            const glm::vec3& cameraPosition,
            const DirectionalLight* light,
            const SceneEnvironment* environment,
            const std::vector<PointLight>& pointLights = {}) override;

        bool IsInitialized() const override { return m_Initialized; }
        void SetSSAODebug(bool debug) { m_SSAODebug = debug; }

    private:
        void SetupQuad();

        std::shared_ptr<Shader> m_Shader;
        uint32_t m_QuadVAO = 0;
        uint32_t m_QuadVBO = 0;
        bool     m_Initialized = false;
        bool     m_SSAODebug = false;
    };
}