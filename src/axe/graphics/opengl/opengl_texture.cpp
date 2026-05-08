#include "opengl_texture.hpp"
#include "glad/glad.h"
#include <stb_image.h>
#include "axe/log/log.hpp"
namespace axe
{
	OpenGLTexture2D::OpenGLTexture2D(std::uint32_t width, std::uint32_t height)
		: m_Width(width), m_Height(height)
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);

		glBindTexture(GL_TEXTURE_2D, m_RendererID);

	    glTexImage2D(
	    GL_TEXTURE_2D,
			0,
			GL_RGBA8,
			static_cast<GLsizei>(width),
			static_cast<GLsizei>(height),
			0,
			GL_RGBA,
			GL_UNSIGNED_INT,
			nullptr
		);

		glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	OpenGLTexture2D::OpenGLTexture2D(const std::string& filepath)
	{
		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		stbi_uc* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

		if (!data)
		{
			AXE_CORE_ERROR("Texture2D: falha ao carregar '{}'", filepath);
			return;
		}

		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;

		GLenum internalFormat = GL_RGB8;
		GLenum dataFormat = GL_RGB;

		if (channels == 4) { internalFormat = GL_RGBA8; dataFormat = GL_RGBA; }
		else if (channels == 3) { internalFormat = GL_RGB8;  dataFormat = GL_RGB; }
		else if (channels == 1) { internalFormat = GL_R8;    dataFormat = GL_RED; }

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
			dataFormat, GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
		m_Loaded = true;

		AXE_CORE_INFO("Texture2D: '{}' carregada ({}x{}, {} canais)",
			filepath, m_Width, m_Height, channels);
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture2D::Bind(std::uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}

	void OpenGLTexture2D::Unbind() const
	{
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}


