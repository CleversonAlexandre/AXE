#include "lighting_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_lighting_pass.hpp"
#include "axe/log/log.hpp"
namespace axe
{
    std::shared_ptr<LightingPass> LightingPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLLightingPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada");
            return nullptr;
        }
    }
}