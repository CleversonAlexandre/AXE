#include "texture.hpp"
#include "axe/graphics/opengl/opengl_texture.hpp"

namespace axe
{
	std::shared_ptr<Texture2D> Texture2D::Create(std::uint32_t width, std::uint32_t height)
	{
		return std::make_unique<OpenGLTexture2D>(width, height);
	}

}