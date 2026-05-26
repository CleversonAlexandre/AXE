#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include <memory>
#include <string>

namespace axe
{

	struct AXE_API SceneEnvironment
	{
		std::shared_ptr<CubemapTexture> Skybox;
		std::string                     SkyboxPath; // caminho do .hdr para serialização
		bool                            ShowSkybox = true;

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
	};

} // namespace axe