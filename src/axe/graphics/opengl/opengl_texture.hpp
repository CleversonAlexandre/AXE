#pragma once

#include "axe/core/types.hpp"
#include "axe/graphics/texture.hpp"

namespace axe
{
	class AXE_API OpenGLTexture2D final : public Texture2D
	{
	public:
		OpenGLTexture2D(std::uint32_t width, std::uint32_t height);
		~OpenGLTexture2D() override;

		std::uint32_t GetWidth() const override { return m_Width; }
		std::uint32_t GetHeight() const override { return m_Height; }

		void Bind(std::uint32_t slot = 0) const override;
		void Unbind() const override;

	private:
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		std::uint32_t m_RendererID = 0;

	};
}
