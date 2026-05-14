#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>

namespace axe
{

    class Shader;

    class AXE_API SkyboxRenderer
    {
    public:
        SkyboxRenderer() = default;
        ~SkyboxRenderer() = default; // VertexArray cuida da destruição

        void Initialize();

        void SetCubemap(std::shared_ptr<CubemapTexture> cubemap) { m_Cubemap = cubemap; }
        bool HasCubemap() const { return m_Cubemap && m_Cubemap->IsLoaded(); }

        void Render(const glm::mat4& view, const glm::mat4& projection);

    private:
        std::shared_ptr<Shader>         m_Shader;
        std::shared_ptr<CubemapTexture> m_Cubemap;
        std::shared_ptr<VertexArray>    m_VertexArray;
    };

} // namespace axe