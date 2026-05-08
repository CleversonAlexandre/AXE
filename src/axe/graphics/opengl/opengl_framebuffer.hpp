#pragma once
#include "axe/core/types.hpp"

#include "axe/graphics/framebuffer.hpp"

namespace axe
{
	class AXE_API OpenGLFramebuffer final : public Framebuffer
	{
	public:
		OpenGLFramebuffer(const FramebufferSpecification& spec);
		~OpenGLFramebuffer() override;

		void Bind() override;
		void Unbind() override;
		void Resize(std::uint32_t width, std::uint32_t height) override;

		std::uint32_t GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
		std::uint32_t ReadPixel(std::uint32_t x, std::uint32_t y) const override;
		const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

	private:
		void Invalidate();

	private:
		FramebufferSpecification m_Specification;
		unsigned int m_RendererID = 0;
		unsigned int m_ColorAttachment = 0;
		unsigned int m_DepthAttachment = 0;
	};
}