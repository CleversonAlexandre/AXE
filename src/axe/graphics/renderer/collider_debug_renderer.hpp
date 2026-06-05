#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/scene/components.hpp"
#include <memory>
#include <entt/entt.hpp>

namespace axe
{
    class Shader;
    class Scene;

    // Renderiza wireframes dos colliders no viewport — apenas no modo editor
    class AXE_API ColliderDebugRenderer
    {
    public:
        void Initialize();

        // Renderiza todos os colliders da cena
        void Render(const Scene& scene,
            const glm::mat4& view,
            const glm::mat4& projection);

    private:
        // Gera vértices de linha para cada shape
        void PushBox(std::vector<float>& verts, const glm::mat4& model, const glm::vec3& halfExt);
        void PushSphere(std::vector<float>& verts, const glm::mat4& model, float radius);
        void PushCapsule(std::vector<float>& verts, const glm::mat4& model, float radius, float height);

        void UploadAndDraw(const std::vector<float>& verts,
            const glm::mat4& viewProjection);

        std::shared_ptr<Shader> m_Shader;
        bool m_Initialized = false;

        // Cor do wireframe de collider (verde lima semitransparente)
        glm::vec4 m_Color = { 0.1f, 0.9f, 0.2f, 0.8f };
    };

} // namespace axe