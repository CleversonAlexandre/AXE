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

        static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
        

        static void DrawIndexedCount(uint32_t indexCount);
        

        static void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);
        
        

        static void SetPolygonMode(RendererAPI::PolygonMode mode);
        

    private:
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };

} // namespace axe