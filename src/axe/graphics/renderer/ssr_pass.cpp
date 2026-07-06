#include "ssr_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_ssr_pass.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<SSRPass> SSRPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLSSRPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada para SSRPass");
            return nullptr;
        }
    }
} // namespace axe