#pragma once
#include "axe/renderer/volumetric_fog_pass.hpp"
// VOLUME_SUN_V2 — AXE_SHADOW_CASCADES e CascadeData. Aqui pode: esta e a
// camada opengl/, e e ela que desempacota a cascata.
#include "axe/graphics/renderer/cascaded_shadow_pass.hpp"
#include <memory>
#include <map>
#include <array>
#include <string>
#include <cstdint>

namespace axe
{
    class Shader;
    class Texture2D;
    class VertexArray;
    class VertexBuffer;

    class OpenGLVolumetricFogPass final : public VolumetricFogPass
    {
    public:
        OpenGLVolumetricFogPass() = default;
        ~OpenGLVolumetricFogPass() = default;

        void Initialize() override;
        bool IsInitialized() const override { return m_Shader != nullptr; }

        // VOLUME_SUN_V2
        void SetSun(const glm::vec3& direction, const glm::vec3& color,
            float intensity) override;
        void SetCascadedShadow(const CascadedShadowPass* csm,
            const glm::mat4& view) override;

        void Execute(
            const GBuffer& gbuffer,
            const VolumetricFogSettings& settings,
            const glm::mat4& invViewProj,
            const glm::vec3& cameraPosition,
            const std::vector<PointLight>& pointLights,
            float                        time,
            uint32_t                     width,
            uint32_t                     height,
            const std::vector<InteriorVolumeData>& interiorVolumes,
            const std::vector<ProbeVolumeData>& probeVolumes) override;

    private:
        void EnsureSceneColorTex(uint32_t width, uint32_t height);

        // VOLUME_DOMAIN_V1 — resolve o material de fog para um shader. Devolve
        // nullptr quando nao ha material (ou quando ele nao resolve), e nesse
        // caso o passe usa o embutido. Mesmo desenho do
        // OpenGLPostProcessPass::ResolveUserShader, inclusive o cache por
        // (UUID, geracao) que faz o botao Compile ter efeito no frame seguinte.
        std::shared_ptr<Shader> ResolveVolumeShader(const std::string& uuid);

        std::shared_ptr<Shader>       m_Shader;
        std::shared_ptr<VertexArray>  m_QuadVAO;
        std::shared_ptr<VertexBuffer> m_QuadVBO;
        uint32_t m_SceneColorTex = 0;
        uint32_t m_LastW = 0, m_LastH = 0;

        // ── VOLUME_SUN_V2 ────────────────────────────────────────────────────
        //
        // Intensidade 0 e o default e significa "sem sol": o termo direcional
        // do ray march desaparece e o fog volta a ser exatamente o da V1.
        // Contagem de cascata 0 significa "sem sombra" — e o valor tem de ser
        // ENVIADO, nao omitido: uniform nao enviada vale zero em silencio, e
        // zero aqui significaria "cascata 0 valida", que amostraria a unidade
        // de textura com o que estivesse nela (a licao do MeshRenderer).
        glm::vec3 m_SunDirection{ 0.0f, -1.0f, 0.0f };
        glm::vec3 m_SunColor{ 1.0f };
        float     m_SunIntensity = 0.0f;

        uint32_t  m_CascadeArrayID = 0;
        int       m_CascadeCount = 0;
        glm::mat4 m_CascadeView{ 1.0f };
        std::array<glm::mat4, AXE_SHADOW_CASCADES> m_CascadeMatrices{};
        std::array<float, AXE_SHADOW_CASCADES>     m_CascadeSplits{};
        std::array<float, AXE_SHADOW_CASCADES>     m_CascadeTexels{};

        // VOLUME_DOMAIN_V1
        std::shared_ptr<Shader> m_VolumeShader;
        std::map<std::string, std::shared_ptr<Texture2D>> m_VolumeSamplers;
        std::string   m_VolumeUUID;
        std::uint64_t m_VolumeGen = 0;
    };

} // namespace axe