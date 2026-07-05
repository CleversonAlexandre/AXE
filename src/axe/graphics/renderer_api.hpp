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

        // Fatores de blend agnósticos de back-end. O back-end traduz para o
        // enum nativo (GL_SRC_ALPHA, etc.) — a camada de engine não fala mais
        // hexadecimal de OpenGL via SetBlendFunc.
        enum class BlendFactor
        {
            Zero = 0,
            One,
            SrcColor,
            OneMinusSrcColor,
            SrcAlpha,
            OneMinusSrcAlpha,
            DstAlpha,
            OneMinusDstAlpha,
        };

        // Estado de viewport consultável — usado para salvar/restaurar em
        // passes que desviam o viewport temporariamente (ex.: avaliação de
        // Light Material num framebuffer 1x1). Mantém esse save/restore
        // dentro da abstração, sem glGetIntegerv cru na camada de engine.
        struct Viewport { uint32_t x = 0, y = 0, width = 0, height = 0; };

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

        // Blending
        virtual void SetBlend(bool enabled) = 0;
        virtual void SetBlendFunc(BlendFactor src, BlendFactor dst) = 0;

        // Draw
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;
        virtual void DrawIndexedCount(uint32_t indexCount) = 0;
        virtual void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
        virtual void DrawArraysStrip(uint32_t vertexCount) = 0;

        // Polygon
        virtual void SetPolygonMode(PolygonMode mode) = 0;

        static API GetAPI() { return s_API; }
        static std::unique_ptr<RendererAPI> Create();

        virtual void BindFramebuffer(uint32_t id) = 0;
        virtual uint32_t GetBoundFramebuffer() = 0; // FBO atualmente ligado
        virtual Viewport GetViewport() = 0;          // viewport atual
        virtual void ResetState() = 0;

        virtual void BlitDepth(uint32_t srcFBO, uint32_t dstFBO,
            uint32_t width, uint32_t height) = 0;

    private:
        static API s_API;
    };

} // namespace axe