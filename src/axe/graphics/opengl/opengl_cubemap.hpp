#pragma once
#include "axe/graphics/cubemap_texture.hpp"

namespace axe
{
	class OpenGLCubemap : public CubemapTexture
	{
	public:
		OpenGLCubemap() = default;
		~OpenGLCubemap();

		bool LoadFromHDRI(const std::string& filepath);

		void Bind(uint32_t slot = 0) const override;
		uint32_t GetRendererID() const override { return m_RendererID; }
		bool IsLoaded() const override { return m_Loaded; }

	private:
		uint32_t m_RendererID = 0;
		bool m_Loaded = false;
	};
}//namespace axe
