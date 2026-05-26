#pragma once
#include "axe/graphics/renderer/post_process_pass.hpp"

namespace axe
{
    class Shader;

    class OpenGLPostProcessPass final : public PostProcessPass
    {
    public:
        ~OpenGLPostProcessPass() override;

        void Initialize(uint32_t width, uint32_t height) override;
        void Resize(uint32_t width, uint32_t height)     override;
        void Execute(uint32_t hdrColorID,
            const PostProcessSettings& settings) override;

        bool IsInitialized() const override { return m_Initialized; }

    private:
        void SetupQuad();
        void SetupBloomBuffers(uint32_t width, uint32_t height);

        // Quad fullscreen
        uint32_t m_QuadVAO = 0;
        uint32_t m_QuadVBO = 0;

        // Bloom — dois ping-pong FBOs para blur gaussiano
        uint32_t m_BloomFBO[2] = {};
        uint32_t m_BloomColorTex[2] = {};

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool     m_Initialized = false;

        std::shared_ptr<Shader> m_TonemapShader;
        std::shared_ptr<Shader> m_BloomExtractShader;
        std::shared_ptr<Shader> m_BlurShader;
        std::shared_ptr<Shader> m_BloomCompositeShader;
    };
}