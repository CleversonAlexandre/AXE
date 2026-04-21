#pragma once

#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
	class AXE_API Texture2D
	{
	public:
		virtual ~Texture2D() = default;

		virtual std::uint32_t GetWidth() const = 0;
		virtual std::uint32_t GetHeight() const = 0;

		virtual void Bind(std::uint32_t slot = 0) const = 0;
		virtual void Unbind() const = 0;
		static std::shared_ptr<Texture2D> Create(std::uint32_t width, std::uint32_t height);
	};
}
