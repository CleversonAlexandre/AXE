#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>

namespace axe
{
    class Shader;

    class AXE_API GridRenderer
    {
    public:
        GridRenderer() = default;

        void Initialize(int halfSize = 50);

        // Renderiza o grid com depth test — não sobrepõe objetos
        void Render(const glm::mat4& view, const glm::mat4& projection);

    private:
        std::shared_ptr<Shader>      m_Shader;
        std::shared_ptr<VertexArray> m_VertexArray;
        uint32_t                     m_VertexCount = 0;
        bool                         m_Initialized = false;
    };

} // namespace axe