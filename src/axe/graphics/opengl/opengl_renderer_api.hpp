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
        void ClearColorDepth() override;

        void SetDepthTest(bool enabled) override;
        void SetDepthWrite(bool enabled) override;
        void SetBlend(bool enabled) override;
        void SetBlendFunc(uint32_t src, uint32_t dst) override;
        void SetDepthFunc(DepthFunc func) override;

        void SetCullFace(bool enabled) override;
        void SetCullMode(bool frontFace) override;
        void BindTextureUnit(uint32_t slot, uint32_t textureID) override;
        void SetColorWrite(bool enabled) override;
        void SetStencilTest(bool enabled) override;
        void SetStencilWrite(uint32_t mask) override;
        void SetStencilFunc(uint32_t func, int ref, uint32_t mask) override;
        void SetStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) override;

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