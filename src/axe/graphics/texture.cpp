#include "axe/graphics/texture.hpp"
#include "axe/graphics/opengl/opengl_texture.hpp"
#include <string>

namespace axe
{

	std::shared_ptr<Texture2D> Texture2D::Create(std::uint32_t width, std::uint32_t height)
	{
		return std::make_shared<OpenGLTexture2D>(width, height);
	}

	std::shared_ptr<Texture2D> Texture2D::Create(const std::string& filepath)
	{
		return std::make_shared<OpenGLTexture2D>(filepath);
	}

} // namespace axe