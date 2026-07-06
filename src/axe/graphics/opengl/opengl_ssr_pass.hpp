#pragma once
#include "axe/graphics/renderer/ssr_pass.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class Shader;
    class VertexArray;
    class VertexBuffer;

    class OpenGLSSRPass final : public SSRPass
    {
    public:
        OpenGLSSRPass() = default;
        ~OpenGLSSRPass() override;

        void Initialize(uint32_t width, uint32_t height) override;
        void Resize(uint32_t width, uint32_t height)     override;
        bool IsInitialized() const override { return m_Shader != nullptr; }

        uint32_t Execute(
            const GBuffer& gbuffer, uint32_t sceneColorID,
            const glm::mat4& projection, const glm::mat4& view,
            const SSRSettings& settings,
            uint32_t width, uint32_t height) override;

    private:
        void CreateTargets(uint32_t w, uint32_t h);
        void DestroyTargets();

        std::shared_ptr<Shader>       m_Shader;
        std::shared_ptr<VertexArray>  m_QuadVAO;
        std::shared_ptr<VertexBuffer> m_QuadVBO;

        uint32_t m_OutputTex = 0;
        uint32_t m_OutputFBO = 0;
        uint32_t m_Width = 0, m_Height = 0;
    };

} // namespace axe