#pragma once

#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
	// Textura 3D — usada pelo sistema de Light Probes (grids de irradiância
	// SH interpolados trilinearmente de graça pelo hardware via sampler3D).
	// Segue o mesmo padrão abstrato+OpenGL de Texture2D/CubemapTexture: a
	// interface não conhece GL; a implementação vive em graphics/opengl/.
	class AXE_API Texture3D
	{
	public:
		virtual ~Texture3D() = default;

		virtual std::uint32_t GetWidth()  const = 0;
		virtual std::uint32_t GetHeight() const = 0;
		virtual std::uint32_t GetDepth()  const = 0;

		virtual std::uint32_t GetRendererID() const = 0;
		virtual bool          IsLoaded()      const = 0;

		virtual void Bind(std::uint32_t slot = 0) const = 0;

		// Cria uma textura 3D RGBA16F imutável a partir de dados float
		// (RGBA intercalado, width*height*depth*4 floats), filtro linear,
		// clamp to edge — exatamente o que um grid de probes precisa.
		static std::shared_ptr<Texture3D> CreateRGBA16F(
			std::uint32_t width, std::uint32_t height, std::uint32_t depth,
			const float* rgbaData);
	};
}