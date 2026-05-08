#pragma once

#include "axe/core/types.hpp"

#include <memory>
#include <cstdint>

namespace axe
{
	struct AXE_API  FramebufferSpecification
	{
		std::uint32_t Width = 1;
		std::uint32_t Height = 1;
	};

	class AXE_API Framebuffer
	{
	public:
		virtual ~Framebuffer() = default;

		virtual void Bind() = 0;
		virtual void Unbind() = 0;
		virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

		virtual std::uint32_t GetColorAttachmentRendererID() const = 0;
		virtual const FramebufferSpecification& GetSpecification() const = 0;

		static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);

		virtual std::uint32_t ReadPixel(std::uint32_t x, std::uint32_t y) const = 0;
	};
}