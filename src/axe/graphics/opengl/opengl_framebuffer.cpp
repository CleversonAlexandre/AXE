#include "opengl_framebuffer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{
	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		Invalidate();
	}

	OpenGLFramebuffer::~OpenGLFramebuffer()
	{
		glDeleteFramebuffers(1, &m_RendererID);
		glDeleteTextures(1, &m_ColorAttachment);
		glDeleteRenderbuffers(1, &m_DepthAttachment);
	}

	void OpenGLFramebuffer::Invalidate()
	{
		if (m_RendererID)
		{
			glDeleteFramebuffers(1, &m_RendererID);
			glDeleteTextures(1, &m_ColorAttachment);
			glDeleteRenderbuffers(1, &m_DepthAttachment);
		}

		glCreateFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
		glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			m_Specification.Width, m_Specification.Height,
			0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, m_ColorAttachment, 0);

		glCreateRenderbuffers(1, &m_DepthAttachment);
		glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
			m_Specification.Width, m_Specification.Height);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			GL_RENDERBUFFER, m_DepthAttachment);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
		glViewport(0, 0, m_Specification.Width, m_Specification.Height);
		//AXE_CORE_INFO("Framebuffer::Bind() - FB ID: {}, ColorAttachment ID: {}",
			//m_RendererID, m_ColorAttachment);
	}

	void OpenGLFramebuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Resize(std::uint32_t width, std::uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		m_Specification.Width = width;
		m_Specification.Height = height;
		Invalidate();
	}

	std::uint32_t OpenGLFramebuffer::ReadPixel(std::uint32_t x, std::uint32_t y) const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		// Lê 1 pixel na posição (x, y) como 4 bytes RGBA
		std::uint8_t pixel[4] = { 0, 0, 0, 0 };
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Reconstrói o ID a partir dos 4 canais
		// Mesmo encoding usado no shader de picking
		return  (std::uint32_t)(pixel[0]) |
			(std::uint32_t)(pixel[1]) << 8 |
			(std::uint32_t)(pixel[2]) << 16 |
			(std::uint32_t)(pixel[3]) << 24;
	}
}