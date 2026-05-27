#pragma once
#include "axe/graphics/renderer/ssao_pass.hpp"
#include <vector>

namespace axe
{
    class Shader;

    class OpenGLSSAOPass final : public SSAOPass
    {
    public:
        ~OpenGLSSAOPass() override;

        void Initialize(uint32_t width, uint32_t height) override;
        void Resize(uint32_t width, uint32_t height)     override;
        void Execute(const GBuffer& gbuffer,
            const glm::mat4& projection,
            const glm::mat4& view,
            const SSAOSettings& settings) override;

        uint32_t GetOcclusionTextureID() const override { return m_OcclusionTex; }
        bool     IsInitialized()         const override { return m_Initialized; }

    private:
        void SetupKernel();
        void SetupNoiseTex();
        void SetupFBOs(uint32_t width, uint32_t height);
        void SetupQuad();
        float Lerp(float a, float b, float t) { return a + t * (b - a); }

        // FBOs
        uint32_t m_SSAOFBO = 0;  // oclusão raw
        uint32_t m_BlurFBO = 0;  // oclusão suavizada

        // Texturas
        uint32_t m_OcclusionTex = 0;  // resultado do blur
        uint32_t m_OcclusionRawTex = 0;  // saída do SSAO antes do blur
        uint32_t m_NoiseTex = 0;  // ruído 4x4

        // Quad
        uint32_t m_QuadVAO = 0;
        uint32_t m_QuadVBO = 0;

        // Kernel de amostras hemisféricas
        std::vector<glm::vec3> m_Kernel;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool     m_Initialized = false;

        std::shared_ptr<Shader> m_SSAOShader;
        std::shared_ptr<Shader> m_BlurShader;
    };
}