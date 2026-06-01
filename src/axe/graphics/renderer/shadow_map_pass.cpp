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
        const glm::vec3& direction, float distance, const glm::vec3& center)
    {
        glm::vec3 dir = glm::normalize(direction);

        // ✅ Usa só XZ da câmera — Y fixo em 0 para centrar no chão
        glm::vec3 sceneCenter = glm::vec3(center.x, 0.0f, center.z);

        glm::vec3 lightPos = sceneCenter + (-dir * 20.f);
        glm::mat4 lightView = glm::lookAt(
            lightPos, sceneCenter, glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 lightProj = glm::ortho(
            -distance, distance,
            -distance, distance,
            0.1f, distance * 3.f);
        return lightProj * lightView;
    }
}