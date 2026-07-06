#include "cascaded_shadow_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_cascaded_shadow_pass.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<CascadedShadowPass> CascadedShadowPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLCascadedShadowPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada para CascadedShadowPass");
            return nullptr;
        }
    }
} // namespace axe