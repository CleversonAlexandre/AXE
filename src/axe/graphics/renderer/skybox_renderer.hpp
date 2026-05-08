#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>

namespace axe
{

	class Shader;

	class AXE_API SkyboxRenderer
	{
	public:
		SkyboxRenderer();
		~SkyboxRenderer();

		void SetCubemap(std::shared_ptr<CubemapTexture> cubemap) { m_Cubemap = cubemap; }
		bool HasCubemap() const { return m_Cubemap && m_Cubemap->IsLoaded(); }
		void Initialize();
		// Renderiza o skybox — deve ser chamado ANTES dos objetos da cena
		void Render(const glm::mat4& view, const glm::mat4& projection);

	private:
		std::shared_ptr<Shader>         m_Shader;
		std::shared_ptr<CubemapTexture> m_Cubemap;

		uint32_t m_VAO = 0;
		uint32_t m_VBO = 0;
		uint32_t m_EBO = 0;
	};

} // namespace axe