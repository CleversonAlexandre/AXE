#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace axe
{
    class Mesh;
    class Shader;

    // Renderiza um outline ao redor de um mesh usando two-pass scale:
    //   Pass 1 — mesh levemente maior, cor sólida, depth write on
    //   Pass 2 — mesh normal, apenas limpa o depth (não escreve cor)
    // O que sobra visível nas bordas é o outline.
    //
    // Deve ser chamado APÓS o lighting pass, no forward pass do deferred,
    // com depth já copiado do G-Buffer (BlitDepth já feito).
    class AXE_API OutlineRenderer
    {
    public:
        OutlineRenderer();

        void Begin(const glm::mat4& viewProjection);
        void DrawOutline(const Mesh& mesh, const glm::mat4& model,
            const glm::vec4& color = { 1.0f, 0.5f, 0.0f, 1.0f },
            float scale = 1.03f);
        void End();

    private:
        std::shared_ptr<Shader> m_Shader;
        glm::mat4 m_ViewProjection{ 1.0f };
    };

} // namespace axe