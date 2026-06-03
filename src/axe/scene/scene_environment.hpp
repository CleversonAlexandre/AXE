#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>

namespace axe
{

	struct AXE_API SceneEnvironment
	{
		std::shared_ptr<CubemapTexture> Skybox;
		std::string                     SkyboxPath;
		bool                            ShowSkybox = true;
		float                           SkyboxRotation = 0.0f; // graus em Y

		bool HasSkybox() const { return Skybox && Skybox->IsLoaded(); }

		void LoadHDRI(const std::string& filepath)
		{
			Skybox = CubemapTexture::CreateFromHDRI(filepath);
			SkyboxPath = filepath;
		}

		bool HasIBL() const
		{
			return Skybox && Skybox->HasIBL();
		}

		// Retorna a view do skybox com rotação aplicada
		glm::mat4 GetSkyboxView(const glm::mat4& view) const
		{
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f),
				glm::radians(SkyboxRotation), glm::vec3(0, 1, 0));
			return glm::mat4(glm::mat3(view)) * rotY;
		}
	};

} // namespace axe