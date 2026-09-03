#include "axe/graphics/cubemap_texture.hpp"
#include "axe/graphics/opengl/opengl_cubemap.hpp"

namespace axe
{

	std::shared_ptr<CubemapTexture> CubemapTexture::CreateFromHDRI(const std::string& filepath)
	{
		auto cubemap = std::make_shared<OpenGLCubemap>();
		if (!cubemap->LoadFromHDRI(filepath))
			return nullptr;
		return cubemap;
	}

	// SKY_IBL_V1 — ver a nota em CubemapTexture::FaceDrawFn.
	std::shared_ptr<CubemapTexture> CubemapTexture::CreateFromFaces(uint32_t faceSize,
		const FaceDrawFn& drawFace)
	{
		auto cubemap = std::make_shared<OpenGLCubemap>();
		if (!cubemap->CaptureFaces(faceSize, drawFace, /*generateBRDF*/ true))
			return nullptr;
		return cubemap;
	}

} // namespace axe