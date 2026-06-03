#pragma once
#include "axe/core/types.hpp"
#include "renderer_api.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class VertexArray;

    class AXE_API RenderCommand
    {
    public:
        static void Init();

        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        static void SetClearColor(float r, float g, float b, float a)
        {
            s_RendererAPI->SetClearColor(r, g, b, a);
        }

        static void Clear()
        {
            s_RendererAPI->Clear();
        }

        static void ClearColorDepth()
        {
            s_RendererAPI->ClearColorDepth();
        }

        static void SetDepthTest(bool enabled)
        {
            s_RendererAPI->SetDepthTest(enabled);
        }

        static void SetDepthWrite(bool enabled)
        {
            s_RendererAPI->SetDepthWrite(enabled);
        }

        static void SetDepthFunc(RendererAPI::DepthFunc func)
        {
            s_RendererAPI->SetDepthFunc(func);
        }

        static void SetCullFace(bool enabled)
        {
            s_RendererAPI->SetCullFace(enabled);
        }

        static void SetCullMode(bool frontFace)
        {
            s_RendererAPI->SetCullMode(frontFace);
        }

        static void SetColorWrite(bool enabled)
        {
            s_RendererAPI->SetColorWrite(enabled);
        }

        static void SetStencilTest(bool enabled)
        {
            s_RendererAPI->SetStencilTest(enabled);
        }

        static void SetStencilWrite(uint32_t mask)
        {
            s_RendererAPI->SetStencilWrite(mask);
        }

        // func: GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, etc.
        static void SetStencilFunc(uint32_t func, int ref, uint32_t mask)
        {
            s_RendererAPI->SetStencilFunc(func, ref, mask);
        }

        // fail/zfail/zpass: GL_KEEP, GL_REPLACE, GL_ZERO, etc.
        static void SetStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass)
        {
            s_RendererAPI->SetStencilOp(fail, zfail, zpass);
        }

        static void BindTextureUnit(uint32_t slot, uint32_t textureID)
        {
            s_RendererAPI->BindTextureUnit(slot, textureID);
        }

        static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);


        static void DrawIndexedCount(uint32_t indexCount);


        static void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);



        static void SetPolygonMode(RendererAPI::PolygonMode mode);

        static void BindFramebuffer(uint32_t id)
        {
            s_RendererAPI->BindFramebuffer(id);
        }

        static void ResetState() { s_RendererAPI->ResetState(); }

        static void BlitDepth(uint32_t srcFBO, uint32_t dstFBO,
            uint32_t width, uint32_t height)
        {
            s_RendererAPI->BlitDepth(srcFBO, dstFBO, width, height);
        }

    private:
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };

} // namespace axe