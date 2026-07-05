#include "axe/renderer/volumetric_fog_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_volumetric_fog_pass.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<VolumetricFogPass> VolumetricFogPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLVolumetricFogPass>();
        default:
            AXE_CORE_ASSERT(false, "RendererAPI não suportada para VolumetricFogPass");
            return nullptr;
        }
    }
} // namespace axe