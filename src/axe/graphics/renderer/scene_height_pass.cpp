#include "scene_height_pass.hpp"
#include "axe/graphics/renderer_api.hpp"
#include "axe/graphics/opengl/opengl_scene_height_pass.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/log/log.hpp"

#include <cmath>

namespace axe
{
    std::shared_ptr<SceneHeightPass> SceneHeightPass::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return std::make_shared<OpenGLSceneHeightPass>();

        case RendererAPI::API::None:
        default:
            AXE_CORE_ASSERT(false, "RendererAPI::None not supported");
            return nullptr;
        }
    }

    glm::mat4 SceneHeightPass::CalcTopDownMatrix(const glm::vec3& center,
        float extent,
        uint32_t resolution,
        float height)
    {
        extent = glm::max(extent, 1.0f);
        resolution = resolution > 0 ? resolution : 1;

        // ── O ENCAIXE NA GRADE DE TEXEL ──────────────────────────────────────
        //
        // Cada texel cobre (2*extent / resolution) metros. Arredondar o centro
        // para um multiplo disso faz o conteudo do mapa andar de texel INTEIRO
        // em texel inteiro quando a camera se move.
        //
        // Sem isso, a cada frame o mundo cai numa fracao de texel diferente, a
        // amostragem muda de vizinho, e a borda da espuma ferve. E o mesmo
        // motivo pelo qual a shadow map em cascata arredonda o centro dela — o
        // sintoma la e a borda da sombra tremendo, aqui seria a linha da praia.
        const float texelSize = (2.0f * extent) / (float)resolution;
        const float snappedX = std::floor(center.x / texelSize) * texelSize;
        const float snappedZ = std::floor(center.z / texelSize) * texelSize;

        // A camera fica ALTA e olha para baixo. `height` e a meia-altura do
        // volume: tudo entre center.y - height e center.y + height entra.
        // Generoso de proposito — geometria cortada pelo plano de corte
        // sumiria do mapa, e o material leria "nao ha nada aqui" bem no lugar
        // onde ha uma montanha.
        const glm::vec3 eye(snappedX, center.y + height, snappedZ);
        const glm::vec3 target(snappedX, center.y - height, snappedZ);

        // Olhando reto para baixo, o `up` NAO pode ser (0,1,0): seria paralelo
        // a direcao da vista e o lookAt degenera numa matriz de NaN. Usa-se o
        // eixo Z como referencia, que e a convencao das cameras de topo.
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 0.0f, 1.0f));

        // near/far cobrem os 2*height do volume, com folga.
        const glm::mat4 proj = glm::ortho(-extent, extent, -extent, extent,
            0.1f, 2.0f * height + 1.0f);

        return proj * view;
    }
}