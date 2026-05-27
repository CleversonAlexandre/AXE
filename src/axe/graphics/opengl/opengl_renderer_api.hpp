#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/renderer_api.hpp"

namespace axe
{

    class AXE_API OpenGLRendererAPI final : public RendererAPI
    {
    public:
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetClearColor(float r, float g, float b, float a) override;
        void Clear() override;

        void SetDepthTest(bool enabled) override;
        void SetDepthWrite(bool enabled) override;
        void SetDepthFunc(DepthFunc func) override;

        void SetCullFace(bool enabled) override;

        void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
        void DrawIndexedCount(uint32_t indexCount) override;
        void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) override;

        void SetPolygonMode(PolygonMode mode) override;

        void BindFramebuffer(uint32_t id) override;
        void ResetState() override;
        void BlitDepth(uint32_t srcFBO, uint32_t dstFBO,
            uint32_t width, uint32_t height) override;
    };

} // namespace axe