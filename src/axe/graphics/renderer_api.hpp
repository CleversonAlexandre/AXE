#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class VertexArray;

    class AXE_API RendererAPI
    {
    public:
        enum class API { None = 0, OpenGL = 1 };

        enum class PolygonMode { Fill = 0, Line };

        enum class DepthFunc { Less = 0, LessEqual, Always };

        virtual ~RendererAPI() = default;

        // Estado
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(float r, float g, float b, float a) = 0;
        virtual void Clear() = 0;               // color + depth + stencil
        virtual void ClearColorDepth() = 0;     // color + depth apenas (sem stencil)

        // Depth
        virtual void SetDepthTest(bool enabled) = 0;
        virtual void SetDepthWrite(bool enabled) = 0;
        virtual void SetDepthFunc(DepthFunc func) = 0;

        // Culling
        virtual void SetCullFace(bool enabled) = 0;
        virtual void SetCullMode(bool frontFace) = 0; // true = cull front, false = cull back

        // Textura
        virtual void BindTextureUnit(uint32_t slot, uint32_t textureID) = 0;

        // Color write
        virtual void SetColorWrite(bool enabled) = 0;

        // Stencil
        virtual void SetStencilTest(bool enabled) = 0;
        virtual void SetStencilWrite(uint32_t mask) = 0;
        virtual void SetStencilFunc(uint32_t func, int ref, uint32_t mask) = 0;
        virtual void SetStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) = 0;

        // Draw
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;
        virtual void DrawIndexedCount(uint32_t indexCount) = 0;
        virtual void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

        // Polygon
        virtual void SetPolygonMode(PolygonMode mode) = 0;

        static API GetAPI() { return s_API; }
        static std::unique_ptr<RendererAPI> Create();

        virtual void BindFramebuffer(uint32_t id) = 0;
        virtual void ResetState() = 0;

        virtual void BlitDepth(uint32_t srcFBO, uint32_t dstFBO,
            uint32_t width, uint32_t height) = 0;

    private:
        static API s_API;
    };

} // namespace axe