#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace axe
{

	class AXE_API CubemapTexture
	{
	public:
		virtual ~CubemapTexture() = default;

		virtual void Bind(uint32_t slot = 0) const = 0;
		virtual uint32_t GetRendererID() const = 0;
		virtual bool IsLoaded() const = 0;

		//Cria cubemap a partir de HDRI equiretangular
		static std::shared_ptr<CubemapTexture> CreateFromHDRI(const std::string& filepath);
	};
}//namespace axe