#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/texture.hpp"
#include <string>

namespace axe
{
	class AXE_API OpenGLTexture2D final : public Texture2D
	{
	public:
		OpenGLTexture2D(std::uint32_t width, std::uint32_t height);
		OpenGLTexture2D(const std::string& filepath); 
		~OpenGLTexture2D() override;

		std::uint32_t GetWidth()      const override { return m_Width; }
		std::uint32_t GetHeight()     const override { return m_Height; }
		std::uint32_t GetRendererID() const override { return m_RendererID; }
		bool          IsLoaded()      const override { return m_Loaded; }

		void Bind(std::uint32_t slot = 0) const override;
		void Unbind() const override;

	private:
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		std::uint32_t m_RendererID = 0;
		bool          m_Loaded = false;
	};
}