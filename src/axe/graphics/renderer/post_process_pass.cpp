#include "post_process_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_post_process_pass.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<PostProcessPass> PostProcessPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLPostProcessPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada");
            return nullptr;
        }
    }
}