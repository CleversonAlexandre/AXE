#pragma once
#include "axe/graphics/renderer/lighting_pass.hpp"

namespace axe
{
    class Shader;

    class OpenGLLightingPass final : public LightingPass
    {
    public:
        void Initialize() override;
        void RecompileShader() override;
        void Execute(const GBuffer& gbuffer,
            uint32_t ssaoTextureID,
            uint32_t shadowMapID,
            const glm::mat4& lightSpaceMatrix,
            const CascadedShadowPass* csm,
            const glm::mat4& view,
            const glm::vec3& cameraPosition,
            const DirectionalLight* light,
            const SceneEnvironment* environment,
            const std::vector<PointLight>& pointLights = {},
            const std::vector<InteriorVolumeData>& interiorVolumes = {},
            const std::vector<ProbeVolumeData>& probeVolumes = {},
            const std::vector<ReflectionProbeData>& reflectionProbes = {},
            uint32_t pointShadowArrayID = 0) override;

        bool IsInitialized() const override { return m_Initialized; }
        // SKY_LIGHT_V1 — ver o comentario na interface.
        void SetSkyLight(const SkyLight& sky, bool present) override
        {
            m_SkyLight = sky;
            m_HasSkyLight = present;
        }

        void SetProjection(const glm::mat4& projection) override { m_Projection = projection; }

        void SetSSAODebug(bool debug) { m_SSAODebug = debug; }

    private:
        void SetupQuad();

        std::shared_ptr<Shader> m_Shader;
        uint32_t m_QuadVAO = 0;
        uint32_t m_QuadVBO = 0;
        bool     m_Initialized = false;
        bool     m_SSAODebug = false;

        // SKY_LIGHT_V1 — copia por VALOR (ver a interface). m_HasSkyLight
        // false faz o pass cair nos campos legados do DirectionalLight.
        SkyLight m_SkyLight;
        bool     m_HasSkyLight = false;

        // CONTACT_SHADOW_V1 — identidade e neutro: sem projecao publicada o
        // u_ViewProjection vira so a view, o raio erra a tela e a funcao nao
        // acha bloqueador. Escurecer errado seria pior que nao escurecer.
        glm::mat4 m_Projection{ 1.0f };
    };
}