#pragma once
#include "axe/graphics/renderer/taa_pass.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class Shader;
    class VertexArray;
    class VertexBuffer;

    class OpenGLTAAPass final : public TAAPass
    {
    public:
        OpenGLTAAPass() = default;
        ~OpenGLTAAPass() override;

        void Initialize(uint32_t width, uint32_t height) override;
        void Resize(uint32_t width, uint32_t height)     override;
        bool IsInitialized() const override { return m_Shader != nullptr; }

        uint32_t Execute(
            uint32_t hdrColorID, uint32_t depthID,
            const glm::mat4& invViewProj, const glm::mat4& prevViewProj,
            const glm::vec2& jitter,
            const TAASettings& settings,
            uint32_t width, uint32_t height) override;

        void BeginFrame(const glm::mat4& viewProj) override;
        glm::vec2 GetCurrentJitter() const override { return m_Jitter; }
        glm::mat4 GetPrevViewProj()  const override { return m_PrevViewProj; }

    private:
        static float Halton(int index, int base);
        void CreateTextures(uint32_t w, uint32_t h);
        void DestroyTextures();

        std::shared_ptr<Shader>       m_Shader;
        std::shared_ptr<VertexArray>  m_QuadVAO;
        std::shared_ptr<VertexBuffer> m_QuadVBO;

        // Double-buffered history pra ping-pong
        uint32_t m_HistoryTex[2] = { 0, 0 };
        uint32_t m_HistoryFBO[2] = { 0, 0 };
        int      m_CurrentHistory = 0; // qual buffer foi escrito no último frame

        uint32_t m_Width = 0, m_Height = 0;

        glm::vec2 m_Jitter{ 0.f };
        glm::mat4 m_PrevViewProj{ 1.f };
        glm::mat4 m_CurrentViewProj{ 1.f };
        int       m_FrameIndex = 0;
    };

} // namespace axe