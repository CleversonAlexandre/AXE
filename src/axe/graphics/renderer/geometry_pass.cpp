#include "geometry_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_geometry_pass.hpp"
#include "axe/log/log.hpp"
namespace axe
{
    std::shared_ptr<GeometryPass> GeometryPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLGeometryPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada");
            return nullptr;
        }
    }
}