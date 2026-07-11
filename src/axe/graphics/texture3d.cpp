#include "axe/graphics/texture3d.hpp"
#include "axe/graphics/opengl/opengl_texture3d.hpp"

namespace axe
{
	// Factory — mesmo padrão de Texture2D::Create: a camada abstrata só
	// conhece a implementação pelo include do factory; nenhum GL vaza pra
	// quem consome Texture3D.
	std::shared_ptr<Texture3D> Texture3D::CreateRGBA16F(
		std::uint32_t width, std::uint32_t height, std::uint32_t depth,
		const float* rgbaData)
	{
		return std::make_shared<OpenGLTexture3D>(width, height, depth, rgbaData);
	}
}