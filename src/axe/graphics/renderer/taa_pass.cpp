#include "taa_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_taa_pass.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<TAAPass> TAAPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLTAAPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada para TAAPass");
            return nullptr;
        }
    }
} // namespace axe