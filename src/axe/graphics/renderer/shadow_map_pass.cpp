#include "shadow_map_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_shadow_map_pass.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    std::shared_ptr<ShadowMapPass> ShadowMapPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLShadowMapPass>();

        case RendererAPI::API::None:
        default:
            AXE_CORE_ASSERT(false, "RendererAPI::None not supported");
            return nullptr;
        }
    }

    glm::mat4 ShadowMapPass::CalcLightSpaceMatrix(
        const glm::vec3& direction, float distance)
    {
        glm::vec3 dir = glm::normalize(direction);
        glm::vec3 lightPos = -dir * 20.f;

        glm::mat4 lightView = glm::lookAt(
            lightPos, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

        glm::mat4 lightProj = glm::ortho(
            -distance, distance,
            -distance, distance,
            0.1f, distance * 3.f);
        //glm::mat4 lightProj = glm::ortho(
        //    -15.0f, 15.0f,
        //    -15.0f, 15.0f,
        //    0.1f, 60.0f);

        return lightProj * lightView;
    }
}