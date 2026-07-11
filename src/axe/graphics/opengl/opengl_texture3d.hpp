#pragma once
#include "axe/graphics/texture3d.hpp"

namespace axe
{
	class OpenGLTexture3D final : public Texture3D
	{
	public:
		OpenGLTexture3D(std::uint32_t width, std::uint32_t height,
			std::uint32_t depth, const float* rgbaData);
		~OpenGLTexture3D() override;

		std::uint32_t GetWidth()  const override { return m_Width; }
		std::uint32_t GetHeight() const override { return m_Height; }
		std::uint32_t GetDepth()  const override { return m_Depth; }

		std::uint32_t GetRendererID() const override { return m_RendererID; }
		bool          IsLoaded()      const override { return m_RendererID != 0; }

		void Bind(std::uint32_t slot = 0) const override;

	private:
		std::uint32_t m_RendererID = 0;
		std::uint32_t m_Width = 0, m_Height = 0, m_Depth = 0;
	};
}